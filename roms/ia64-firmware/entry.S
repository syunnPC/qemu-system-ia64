/* SPDX-License-Identifier: GPL-2.0-or-later */
/* IA-64 EFI firmware entry point (GCC assembler syntax).
 *
 * The linker script links at VMA=0x100000, so all symbols (`__gp`,
 * firmware_main, etc.) are already at correct runtime addresses.
 * No GP manual offset computation is needed.
 */

    .section .text._start, "ax"
    .global _start
    .type _start, @function
    .proc _start
    .explicit
_start:
    /* Secondary processors resume here when the platform wake IPI arrives. */
    mov r14 = cr.lid
    ;;
    cmp.ne p6, p7 = r14, r0
    ;;
(p6) br.cond.sptk.few start_secondary
    ;;

    br.cond.sptk.few start_after_pal
    ;;
    .org 0x60
    .endp _start
    .size _start, . - _start

    .section .text.pal_proc_entry, "ax"
    .global pal_proc_entry
    .type pal_proc_entry, @function
    .proc pal_proc_entry
    .explicit
pal_proc_entry:
    break.m 0x100000
    ;;
    br.many b0
    ;;
    .endp pal_proc_entry
    .size pal_proc_entry, . - pal_proc_entry

    /*
     * Derive SAL code relocation from IP, then enter C code with physical GP
     * and instruction, data, and RSE translation disabled.
     *
     * Flush the caller's RSE state before switching to a private physical
     * backing store.  Each CPU uses 16 KiB for RSE state and 16 KiB for its
     * memory stack while interrupts and interruption collection are disabled.
     */
    .equ SAL_PSR_I,                         (1 << 14)
    .equ SAL_PSR_IC,                        (1 << 13)
    .equ SAL_PSR_DT,                        (1 << 17)
    .equ SAL_PSR_RT,                        (1 << 27)
    .equ SAL_PSR_IT,                        (1 << 36)
    .equ SAL_PSR_BN,                        (1 << 44)
    .equ SAL_PSR_TRANSLATION_MASK, SAL_PSR_DT | SAL_PSR_RT | SAL_PSR_IT
    .equ SAL_PSR_ENTRY_CLEAR, SAL_PSR_I | SAL_PSR_IC | SAL_PSR_TRANSLATION_MASK
    .equ SAL_RUNTIME_AREA_SHIFT,            15
    .equ SAL_RUNTIME_BACKING_SIZE,          0x4000
    .equ SAL_RUNTIME_STACK_FRAME_SIZE,      0x100
    .equ SAL_RUNTIME_RSC_EAGER_PL0,         3

    .section .text.sal_proc_entry, "ax"
    .align 16
    .global sal_proc_gp_anchor
    .type sal_proc_gp_anchor, @object
sal_proc_gp_anchor:
    .quad 0
    .quad 0
    .size sal_proc_gp_anchor, . - sal_proc_gp_anchor

    .align 8
    .global sal_runtime_area_top
    .type sal_runtime_area_top, @object
sal_runtime_area_top:
    .quad 0
    .size sal_runtime_area_top, . - sal_runtime_area_top

    .align 16
    .global sal_proc_entry
    .type sal_proc_entry, @function
    .proc sal_proc_entry
    .explicit
sal_proc_entry:
    /*
     * Preserve every value needed across the RSE switch in static GRs.
     * r4-r7 are left untouched so the C ABI preserves their values and NaT
     * bits without an unsafe ordinary store.
     */
    mov r14 = r32
    mov r15 = r33
    mov r16 = r34
    mov r17 = r35
    ;;
    mov r18 = r36
    mov r19 = r37
    mov r20 = r38
    mov r21 = r39
    ;;
    mov r22 = b0
    mov r23 = r1
    mov r24 = r12
    mov r25 = psr
    ;;

    /*
     * mov-from-PSR exposes only PSR{36:35} and PSR{31:0}; PSR.bn reads as
     * zero.  The SAL calling convention requires bank 1 on entry and exit,
     * so restore that architecturally implicit bit before either RFI.
     */
    movl r31 = SAL_PSR_BN
    ;;
    or r25 = r25, r31
    ;;
sal_proc_capture_ip:
    /*
     * Do not use br.call to obtain IP here.  A call overwrites ar.pfs before
     * the physical-context bridge has saved the caller's frame state.
     */
    mov r26 = ip
    mov r27 = ar.rsc
    ;;
    movl r31 = sal_proc_capture_ip
    ;;
    sub r26 = r26, r31

    /*
     * SAL permits fully physical or fully virtual calls.  Do not attempt a
     * privileged mode transition from a mixed translation context.
     */
    movl r31 = SAL_PSR_TRANSLATION_MASK
    ;;
    and r30 = r25, r31
    ;;
    cmp.eq p6, p7 = r30, r0
    cmp.eq p8, p9 = r30, r31
    ;;
(p6) br.cond.sptk.few sal_proc_mode_valid
(p8) br.cond.sptk.few sal_proc_mode_valid
    ;;
    mov r8 = -3
    mov r9 = r0
    mov r10 = r0
    mov r11 = r0
    ;;
    br.ret.sptk.many b0
    ;;

sal_proc_mode_valid:
    /*
     * Explicit flushrs may fault on the caller's backing store.  Perform it
     * before changing translation or interruption-collection state.
     */
    rsm psr.i
    ;;
    mov ar.rsc = r0
    ;;
    flushrs
    ;;
    mov r28 = ar.bsp
    mov r29 = ar.rnat
    mov r31 = cr.lid
    ;;
    extr.u r31 = r31, 24, 8
    ;;
    shl r31 = r31, SAL_RUNTIME_AREA_SHIFT
    movl r2 = sal_runtime_area_top
    ;;
    add r2 = r2, r26
    ;;
    ld8 r2 = [r2]
    ;;
    sub r2 = r2, r31
    movl r3 = (SAL_RUNTIME_BACKING_SIZE * 2)
    ;;
    sub r30 = r2, r3
    ;;

    /*
     * Start an empty physical RSE context.  mov-to-BSPSTORE makes RNAT
     * undefined, so initialize it before enabling eager RSE operation.
     * Interruption collection is disabled before BSPSTORE changes; until
     * that point both the caller's SP and RSE context remain intact.
     */
    rsm psr.ic
    ;;
    srlz.d
    ;;
    srlz.i
    ;;
    invala
    mov ar.bspstore = r30
    ;;
    mov ar.rnat = r0
    movl r31 = SAL_PSR_ENTRY_CLEAR
    ;;
    andcm r31 = r25, r31
    movl r3 = sal_proc_physical
    ;;
    mov cr.ipsr = r31
    mov cr.iip = r3
    mov cr.ifs = r0
    ;;
    rfi
    ;;

sal_proc_physical:
    adds r12 = -(SAL_RUNTIME_STACK_FRAME_SIZE * 2), r2
    mov ar.rsc = SAL_RUNTIME_RSC_EAGER_PL0
    movl r1 = __gp
    ;;

    /*
     * The bridge owns the fixed frame above the C stack.  All stored values
     * originate in non-NaT-capable special/static state; SAL arguments stay
     * in registers until they are copied into the new output frame.
     */
    adds r2 = SAL_RUNTIME_STACK_FRAME_SIZE, r12
    ;;
    st8 [r2] = r22, 8
    ;;
    st8 [r2] = r23, 8
    ;;
    st8 [r2] = r24, 8
    ;;
    st8 [r2] = r25, 8
    ;;
    st8 [r2] = r26, 8
    ;;
    st8 [r2] = r27, 8
    ;;
    st8 [r2] = r28, 8
    ;;
    st8 [r2] = r29, 8
    ;;

    alloc r31 = ar.pfs, 0, 0, 8, 0
    ;;
    st8 [r2] = r31
    mov r32 = r14
    mov r33 = r15
    ;;
    mov r34 = r16
    mov r35 = r17
    mov r36 = r18
    mov r37 = r19
    ;;
    mov r38 = r20
    mov r39 = r21
    ;;
    br.call.sptk.many b0 = sal_proc_dispatch
    ;;

    mov r23 = r8
    mov r24 = r9
    mov r25 = r10
    mov r26 = r11
    ;;
    adds r2 = SAL_RUNTIME_STACK_FRAME_SIZE, r12
    ;;
    ld8 r14 = [r2], 8
    ;;
    ld8 r15 = [r2], 8
    ;;
    ld8 r16 = [r2], 8
    ;;
    ld8 r17 = [r2], 8
    ;;
    ld8 r18 = [r2], 8
    ;;
    ld8 r19 = [r2], 8
    ;;
    ld8 r20 = [r2], 8
    ;;
    ld8 r21 = [r2], 8
    ;;
    ld8 r22 = [r2]
    ;;

    /*
     * Flush the private context before rebasing BSPSTORE.  No stacked
     * register is used after the switch until the caller's RT and RSC have
     * been restored.
     */
    mov ar.rsc = r0
    ;;
    flushrs
    ;;
    invala
    movl r31 = sal_proc_return
    ;;
    add r31 = r31, r18
    mov ar.bspstore = r20
    ;;
    mov ar.rnat = r21
    mov ar.pfs = r22
    mov cr.ipsr = r17
    ;;
    mov cr.iip = r31
    mov cr.ifs = r0
    mov ar.rsc = r19
    ;;
    rfi
    ;;

sal_proc_return:
    mov b0 = r14
    mov r1 = r15
    mov r12 = r16
    ;;
    mov r8 = r23
    mov r9 = r24
    mov r10 = r25
    mov r11 = r26
    ;;
    br.ret.sptk.many b0
    ;;
    .endp sal_proc_entry
    .size sal_proc_entry, . - sal_proc_entry

    /*
     * The machine implements the arithmetic behind this architected
     * firmware ABI.  Keeping the bridge as a leaf preserves all eight input
     * registers and returns the four-word result directly in r8-r11.
     */
    .section .text.fpswa_emulation_entry, "ax"
    .global fpswa_emulation_entry
    .type fpswa_emulation_entry, @function
    .proc fpswa_emulation_entry
    .explicit
fpswa_emulation_entry:
    break.m 0x100001
    ;;
    br.ret.sptk.many b0
    ;;
    .endp fpswa_emulation_entry
    .size fpswa_emulation_entry, . - fpswa_emulation_entry

    /* Copied into an IVT slot while an EFI debug callback is registered. */
    .section .text.fw_debug_ivt_stub, "ax"
    .align 16
    .global fw_debug_ivt_stub_start
    .global fw_debug_ivt_stub_end
    .type fw_debug_ivt_stub_start, @function
    .proc fw_debug_ivt_stub_start
    .explicit
fw_debug_ivt_stub_start:
    movl r16 = fw_debug_exception_entry
    ;;
    break.m 0x100002
    ;;
fw_debug_ivt_stub_end:
    .endp fw_debug_ivt_stub_start
    .size fw_debug_ivt_stub_start, . - fw_debug_ivt_stub_start

    /*
     * The target bridge supplies the exception type, context record, a
     * private stack, and the processor index in r16-r18.  cover preserves
     * the interrupted frame in cr.ifs before calling ordinary C code.
     */
    .section .text.fw_debug_exception_entry, "ax"
    .align 16
    .global fw_debug_exception_entry
    .type fw_debug_exception_entry, @function
    .proc fw_debug_exception_entry
    .explicit
fw_debug_exception_entry:
    cover
    ;;
    break.m 0x100003
    ;;
    .align 16
    mov r19 = r18
    movl r1 = __gp
    ;;
    alloc r18 = ar.pfs, 0, 3, 3, 0
    ;;
    mov r35 = r16
    mov r36 = r17
    mov r37 = r19
    ;;
    br.call.sptk.many b0 = fw_debug_support_dispatch
    ;;
    break.m 0x100004
    ;;
    break.m 0
    ;;
    .endp fw_debug_exception_entry
    .size fw_debug_exception_entry, . - fw_debug_exception_entry

    .section .text.start_after_pal, "ax"
    .type start_after_pal, @function
    .proc start_after_pal
    .explicit
start_after_pal:

    /*
     * HP profiles enter with descriptor GPA, size, and platform
     * ID in r8-r10.  Preserve them in static registers before the first C
     * call reuses the return-value registers.  VPC enters with all three
     * values zero and continues to use its fixed legacy handoff page.
     */
    mov r5 = r8
    mov r6 = r9
    mov r7 = r10
    ;;

    /*
     * QEMU reloads the firmware image on reset, but the raw binary does not
     * contain the trailing NOBITS .bss.  Clear it here so guest-requested and
     * monitor-requested resets start with the same firmware state as a fresh
     * process.
     */
    movl r14 = _bss_start
    ;;
    movl r15 = _bss_end
    ;;
1:
    cmp.ltu p6, p7 = r14, r15
    ;;
(p7) br.cond.dpnt.few 2f
    ;;
    st8 [r14] = r0, 8
    ;;
    br.cond.sptk.few 1b
    ;;
2:
    ;;

    /* GP = linker-defined __gp (already at runtime VMA=0x100000 address) */
    movl r1 = __gp
    ;;

    /*
     * Preserve the reset return pointer across the ABI call, then move the
     * firmware stack to the top of installed low RAM.  HP machine boot-info
     * already supplied a platform-specific stack in r12, so recover its
     * region top directly.  Only an all-zero transport selects VPC; malformed
     * partial transport must not touch the VPC fixed handoff page before the
     * descriptor validator rejects it.
     */
    mov r4 = b0
    or r8 = r5, r6
    ;;
    or r8 = r8, r7
    ;;
    cmp.ne p6, p7 = r8, r0
    ;;
(p6) adds r8 = 16, r12
(p7) br.call.sptk.many b0 = fw_boot_stack_top
    ;;
    /*
     * The region top may be the exact end of installed RAM, and the psABI
     * lets callees use the 16-byte scratch area [sp, sp+16).  Start sp 16
     * bytes below the region top so that scratch area stays inside RAM.
     */
    adds r12 = -16, r8
    mov r3 = r4
    ;;

    /* Alloc: 0 ins, 13 locals, 6 outs → output regs r45-r50
     * After br.call these become callee's input r32-r37. */
    alloc r2 = ar.pfs, 0, 13, 6, 0
    ;;

    /* Copy the descriptor before clearing its entry registers. */
    mov r45 = r5               /* arg0 → descriptor GPA */
    mov r46 = r6               /* arg1 → descriptor size */
    mov r47 = r7               /* arg2 → platform ID */
    ;;

    br.call.sptk.many b0 = fw_platform_descriptor_init
    ;;

    /* The descriptor has been copied; do not leak its transport registers. */
    mov r5 = r0
    mov r6 = r0
    mov r7 = r0
    mov r8 = r0
    mov r9 = r0
    mov r10 = r0
    ;;

    /* Pass args: gp, stack region top, boot_b0. */
    mov r45 = r1               /* arg0 → callee r32 = gp */
    adds r46 = 16, r12         /* arg1 → stack region top */
    mov r47 = r4               /* arg2 → callee r34 = boot_b0 */
    mov r48 = r0
    mov r49 = r0
    mov r50 = r0
    ;;

    br.call.sptk.many b0 = firmware_main
    ;;

    /* firmware_main should not return */
    break 0
    ;;
    .endp start_after_pal
    .size start_after_pal, . - start_after_pal

    .section .text.start_secondary, "ax"
    .type start_secondary, @function
    .proc start_secondary
    .explicit
start_secondary:
    movl r1 = __gp
    ;;
    mov r14 = cr.lid
    ;;
    extr.u r14 = r14, 24, 8
    ;;
    alloc r2 = ar.pfs, 0, 3, 1, 0
    ;;
    mov r32 = r14
    mov r35 = r14
    ;;
    br.call.sptk.many b0 = fw_ap_stack_top
    ;;
    adds r12 = -16, r8
    mov r35 = r32
    ;;
    br.call.sptk.many b0 = firmware_ap_main
    ;;
    break 0
    ;;
    .endp start_secondary
    .size start_secondary, . - start_secondary

    .section .text.fw_pal_halt_light, "ax"
    .global fw_pal_halt_light
    .type fw_pal_halt_light, @function
    .proc fw_pal_halt_light
    .explicit
fw_pal_halt_light:
    alloc r33 = ar.pfs, 0, 2, 0, 0
    ;;
    mov r32 = b0
    movl r28 = 0x1d
    ;;
    movl r14 = pal_proc_entry
    ;;
    mov b6 = r14
    movl r14 = 1f
    ;;
    mov b0 = r14
    ;;
    br.sptk.many b6
    ;;
1:
    mov b0 = r32
    mov ar.pfs = r33
    ;;
    br.ret.sptk.many b0
    ;;
    .endp fw_pal_halt_light
    .size fw_pal_halt_light, . - fw_pal_halt_light

    .section .text.fw_pal_platform_addr, "ax"
    .global fw_pal_platform_addr
    .type fw_pal_platform_addr, @function
    .proc fw_pal_platform_addr
    .explicit
fw_pal_platform_addr:
    alloc r34 = ar.pfs, 1, 2, 0, 0
    ;;
    mov r33 = b0
    mov r30 = r32
    movl r28 = 0x10
    ;;
    adds r29 = 1, r0
    mov r31 = r0
    movl r14 = pal_proc_entry
    ;;
    mov b6 = r14
    movl r14 = 1f
    ;;
    mov b0 = r14
    ;;
    br.sptk.many b6
    ;;
1:
    cmp.eq p6, p7 = r8, r0
    ;;
(p6) mov ar.k0 = r32
    ;;
    mov b0 = r33
    mov ar.pfs = r34
    ;;
    br.ret.sptk.many b0
    ;;
    .endp fw_pal_platform_addr
    .size fw_pal_platform_addr, . - fw_pal_platform_addr

    .section .text.fw_pal_itc_ratio, "ax"
    .global fw_pal_itc_ratio
    .type fw_pal_itc_ratio, @function
    .proc fw_pal_itc_ratio
    .explicit
fw_pal_itc_ratio:
    alloc r33 = ar.pfs, 0, 2, 0, 0
    ;;
    mov r32 = b0
    movl r28 = 0xe
    ;;
    mov r29 = r0
    mov r30 = r0
    mov r31 = r0
    movl r14 = pal_proc_entry
    ;;
    mov b6 = r14
    movl r14 = 1f
    ;;
    mov b0 = r14
    ;;
    br.sptk.many b6
    ;;
1:
    cmp.eq p6, p7 = r8, r0
    ;;
(p6) mov r8 = r11
(p7) mov r8 = r0
    ;;
    mov b0 = r32
    mov ar.pfs = r33
    ;;
    br.ret.sptk.many b0
    ;;
    .endp fw_pal_itc_ratio
    .size fw_pal_itc_ratio, . - fw_pal_itc_ratio

    .section .text.fw_pal_mc_drain, "ax"
    .global fw_pal_mc_drain
    .type fw_pal_mc_drain, @function
    .proc fw_pal_mc_drain
    .explicit
fw_pal_mc_drain:
    alloc r33 = ar.pfs, 0, 2, 0, 0
    ;;
    mov r32 = b0
    movl r28 = 0x16
    ;;
    mov r29 = r0
    mov r30 = r0
    mov r31 = r0
    movl r14 = pal_proc_entry
    ;;
    mov b6 = r14
    movl r14 = 1f
    ;;
    mov b0 = r14
    ;;
    br.sptk.many b6
    ;;
1:
    mov b0 = r32
    mov ar.pfs = r33
    ;;
    br.ret.sptk.many b0
    ;;
    .endp fw_pal_mc_drain
    .size fw_pal_mc_drain, . - fw_pal_mc_drain

    .section .text.fw_pal_mc_clear_log, "ax"
    .global fw_pal_mc_clear_log
    .type fw_pal_mc_clear_log, @function
    .proc fw_pal_mc_clear_log
    .explicit
fw_pal_mc_clear_log:
    alloc r33 = ar.pfs, 0, 2, 0, 0
    ;;
    mov r32 = b0
    movl r28 = 0x15
    ;;
    mov r29 = r0
    mov r30 = r0
    mov r31 = r0
    movl r14 = pal_proc_entry
    ;;
    mov b6 = r14
    movl r14 = 1f
    ;;
    mov b0 = r14
    ;;
    br.sptk.many b6
    ;;
1:
    mov b0 = r32
    mov ar.pfs = r33
    ;;
    br.ret.sptk.many b0
    ;;
    .endp fw_pal_mc_clear_log
    .size fw_pal_mc_clear_log, . - fw_pal_mc_clear_log

    .section .text.fw_pal_mc_register_mem, "ax"
    .global fw_pal_mc_register_mem
    .type fw_pal_mc_register_mem, @function
    .proc fw_pal_mc_register_mem
    .explicit
fw_pal_mc_register_mem:
    alloc r34 = ar.pfs, 1, 2, 0, 0
    ;;
    mov r33 = b0
    mov r29 = r32
    movl r28 = 0x1b
    ;;
    mov r30 = r0
    mov r31 = r0
    movl r14 = pal_proc_entry
    ;;
    mov b6 = r14
    movl r14 = 1f
    ;;
    mov b0 = r14
    ;;
    br.sptk.many b6
    ;;
1:
    mov b0 = r33
    mov ar.pfs = r34
    ;;
    br.ret.sptk.many b0
    ;;
    .endp fw_pal_mc_register_mem
    .size fw_pal_mc_register_mem, . - fw_pal_mc_register_mem

    /*
     * PAL saves the static/banked GRs, predicates, b0/b1 and RSE context.
     * SAL must preserve the remaining state before entering C or the OS.
     * Use the SAL-owned tail of the registered 4 KiB min-state region;
     * this also keeps the extra state in guest RAM across migration.
     */
    .equ FW_RAS_EXTRA_STATE_OFFSET, 1024
    .equ FW_RAS_EXTRA_STATE_SIZE, (12 * 8 + 126 * 16)
    .if FW_RAS_EXTRA_STATE_OFFSET + FW_RAS_EXTRA_STATE_SIZE > 4096
    .error "RAS extra state exceeds the registered min-state region"
    .endif

    .macro fw_ras_fp_state restore, regs:vararg
    .irp reg, \regs
    .if \restore
    ldf.fill \reg = [r14], 16
    .else
    stf.spill [r14] = \reg, 16
    .endif
    ;;
    .endr
    .endm

    .macro fw_ras_extra_state base, restore=0
    adds r14 = FW_RAS_EXTRA_STATE_OFFSET, \base
    ;;
    .irp reg, b2, b3, b4, b5, b6, b7, ar.pfs, ar.lc, ar.ec, ar.ccv, ar.unat, ar.fpsr
    .if \restore
    ld8 r15 = [r14], 8
    ;;
    mov \reg = r15
    .else
    mov r15 = \reg
    ;;
    st8 [r14] = r15, 8
    .endif
    ;;
    .endr
    fw_ras_fp_state \restore, f2, f3, f4, f5, f6, f7
    fw_ras_fp_state \restore, f8, f9, f10, f11, f12, f13, f14, f15
    fw_ras_fp_state \restore, f16, f17, f18, f19, f20, f21, f22, f23
    fw_ras_fp_state \restore, f24, f25, f26, f27, f28, f29, f30, f31
    fw_ras_fp_state \restore, f32, f33, f34, f35, f36, f37, f38, f39
    fw_ras_fp_state \restore, f40, f41, f42, f43, f44, f45, f46, f47
    fw_ras_fp_state \restore, f48, f49, f50, f51, f52, f53, f54, f55
    fw_ras_fp_state \restore, f56, f57, f58, f59, f60, f61, f62, f63
    fw_ras_fp_state \restore, f64, f65, f66, f67, f68, f69, f70, f71
    fw_ras_fp_state \restore, f72, f73, f74, f75, f76, f77, f78, f79
    fw_ras_fp_state \restore, f80, f81, f82, f83, f84, f85, f86, f87
    fw_ras_fp_state \restore, f88, f89, f90, f91, f92, f93, f94, f95
    fw_ras_fp_state \restore, f96, f97, f98, f99, f100, f101, f102, f103
    fw_ras_fp_state \restore, f104, f105, f106, f107, f108, f109, f110, f111
    fw_ras_fp_state \restore, f112, f113, f114, f115, f116, f117, f118, f119
    fw_ras_fp_state \restore, f120, f121, f122, f123, f124, f125, f126, f127
    .endm

    .section .text.fw_mca_entry, "ax"
    .align 16
    .global fw_mca_entry
    .type fw_mca_entry, @function
    .proc fw_mca_entry
    .explicit
fw_mca_entry:
    fw_ras_extra_state r10
    movl r1 = __gp
    ;;
    alloc r14 = ar.pfs, 0, 0, 3, 0
    ;;
    mov r32 = r8
    mov r33 = r9
    mov r34 = r10
    ;;
    br.call.sptk.many b0 = firmware_mca_prepare
    ;;
    mov r14 = r8
    mov r15 = r9
    mov r16 = r10
    mov r17 = r11
    ;;
    cmp.ne p6, p7 = r14, r0
    cmp.eq p8, p9 = 1, r17
    ;;
(p6) br.cond.sptk.few fw_mca_invoke_os
    ;;
(p8) mov r17 = r0
(p8) br.cond.sptk.few fw_mca_resume_saved
    ;;
    br.cond.sptk.few fw_mca_halt
    ;;

fw_mca_invoke_os:
    mov ar.rsc = r0
    ;;
    flushrs
    ;;
    cover
    ;;
    mov r1 = r15
    movl r8 = pal_proc_entry
    movl r9 = sal_proc_entry
    movl r10 = __gp
    ;;
    mov r11 = r17
    movl r12 = fw_mca_os_return
    mov r4 = r16
    ;;
    mov b6 = r14
    ;;
    br.sptk.many b6
    ;;

fw_mca_os_return:
    cmp.eq p6, p7 = r8, r0
    ;;
(p6) br.cond.sptk.few fw_mca_os_resume
    ;;
    cmp.eq p8, p9 = -1, r8
    ;;
(p8) br.cond.sptk.few fw_reset_warm_entry
    ;;
    cmp.eq p10, p11 = -2, r8
    ;;
(p10) br.cond.sptk.few fw_reset_cold_entry
    ;;
    br.cond.sptk.few fw_mca_halt
    ;;
fw_mca_os_resume:
    mov r1 = r9
    mov r16 = r4
    mov r17 = r10
    ;;

fw_mca_resume_saved:
    /* A new context supplies its own non-minimal state. */
    cmp.ne p6, p7 = r17, r0
    ;;
(p6) br.cond.sptk.few fw_mca_resume_pal
    ;;
    /* Address the saved physical FRs independently of the OS's rotation. */
    cover
    ;;
    fw_ras_extra_state r16, 1

fw_mca_resume_pal:
    movl r28 = 0x1a
    mov r29 = r0
    mov r30 = r16
    mov r31 = r17
    ;;
    movl r15 = fw_mca_halt
    ;;
    mov b0 = r15
    ;;
    /* A direct branch leaves the restored b2-b7 and ar.pfs untouched. */
    br.sptk.many pal_proc_entry
    ;;

fw_mca_halt:
    movl r28 = 0x1d
    mov r29 = r0
    mov r30 = r0
    mov r31 = r0
    movl r14 = pal_proc_entry
    ;;
    mov b6 = r14
    movl r15 = fw_mca_halt
    ;;
    mov b0 = r15
    ;;
    br.sptk.many b6
    ;;

fw_reset_warm_entry:
    movl r1 = __gp
    ;;
    br.call.sptk.many b0 = fw_reset_warm
    ;;
    br.cond.sptk.few fw_mca_halt
    ;;

fw_reset_cold_entry:
    movl r1 = __gp
    ;;
    br.call.sptk.many b0 = fw_reset_cold
    ;;
    br.cond.sptk.few fw_mca_halt
    ;;
    .endp fw_mca_entry
    .size fw_mca_entry, . - fw_mca_entry

    .section .text.fw_init_entry, "ax"
    .align 16
    .global fw_init_entry
    .type fw_init_entry, @function
    .proc fw_init_entry
    .explicit
fw_init_entry:
    fw_ras_extra_state r9
    movl r1 = __gp
    ;;
    alloc r14 = ar.pfs, 0, 0, 2, 0
    ;;
    mov r32 = r8
    mov r33 = r9
    ;;
    br.call.sptk.many b0 = firmware_init_prepare
    ;;
    mov r14 = r8
    mov r15 = r9
    mov r16 = r10
    mov r17 = r11
    ;;
    cmp.eq p6, p7 = r14, r0
    ;;
(p6) br.cond.sptk.few fw_reset_warm_entry
    ;;
    mov ar.rsc = r0
    ;;
    flushrs
    ;;
    cover
    ;;
    mov r1 = r15
    movl r8 = pal_proc_entry
    movl r9 = sal_proc_entry
    movl r10 = __gp
    ;;
    mov r11 = r17
    movl r12 = fw_mca_os_return
    mov r4 = r16
    ;;
    mov b6 = r14
    ;;
    br.sptk.many b6
    ;;
    .endp fw_init_entry
    .size fw_init_entry, . - fw_init_entry

    .section .text.fw_ebc_bridge, "ax"

    /* A generated thunk passes its firmware-owned context in r8. */
    .global fw_ebc_capture_context
    .type fw_ebc_capture_context, @function
    .proc fw_ebc_capture_context
    .explicit
fw_ebc_capture_context:
    br.ret.sptk.many b0
    ;;
    .endp fw_ebc_capture_context
    .size fw_ebc_capture_context, . - fw_ebc_capture_context

    /*
     * Invoke an IA-64 function descriptor from an EBC argument frame.
     * The first eight natural-width arguments are loaded into the register
     * stack.  The complete frame is copied to a temporary native stack so
     * that additional arguments remain at their ABI-defined locations.
     */
    .global fw_ebc_call_native
    .type fw_ebc_call_native, @function
    .proc fw_ebc_call_native
    .explicit
fw_ebc_call_native:
    alloc loc1 = ar.pfs, 3, 9, 8, 0
    ;;
    mov loc0 = b0
    mov loc2 = r12
    mov loc3 = r1
    sub loc4 = in2, in1
    ;;
    sub loc5 = r12, loc4
    mov loc6 = in1
    mov loc8 = loc4
    ;;
    and loc5 = -16, loc5
    ;;
    mov loc7 = loc5
    ;;
1:
    cmp.eq p6, p7 = loc8, r0
    ;;
(p6) br.cond.dpnt.few 2f
    ;;
    ld1 r14 = [loc6], 1
    ;;
    st1 [loc7] = r14, 1
    add loc8 = -1, loc8
    ;;
    br.cond.sptk.few 1b
    ;;
2:
    mov r14 = loc5
    ;;
    ld8 out0 = [r14], 8
    ;;
    ld8 out1 = [r14], 8
    ;;
    ld8 out2 = [r14], 8
    ;;
    ld8 out3 = [r14], 8
    ;;
    ld8 out4 = [r14], 8
    ;;
    ld8 out5 = [r14], 8
    ;;
    ld8 out6 = [r14], 8
    ;;
    ld8 out7 = [r14], 8
    ;;
    add r12 = 48, loc5
    mov r14 = in0
    ;;
    ld8 r15 = [r14], 8
    ;;
    ld8 r16 = [r14]
    ;;
    mov b6 = r15
    mov r1 = r16
    ;;
    br.call.sptk.many b0 = b6
    ;;
    mov r12 = loc2
    mov r1 = loc3
    mov b0 = loc0
    mov ar.pfs = loc1
    ;;
    br.ret.sptk.many b0
    ;;
    .endp fw_ebc_call_native
    .size fw_ebc_call_native, . - fw_ebc_call_native
