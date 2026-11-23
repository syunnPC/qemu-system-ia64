/* SPDX-License-Identifier: GPL-2.0-or-later */
.text
.auto
.global _start
_start:
    ssm 0x2000
    srlz.d
    movl r8=ivt
    mov cr.iva=r8
    srlz.i
    movl r20=2000000
    mov r14=r0
    mov r5=r0
    mov r9=r0
    mov r10=r0
    mov r11=r0
    mov r12=r0
    br.sptk region

.explicit
region:
.rept 8
{ .mii
    add r10=1,r10
    add r11=2,r11
    add r12=3,r12 ;;
}
.endr
.auto
    add r9=1,r9
    add r20=-1,r20
    cmp.ne p6,p7=r20,r0
(p6) br.cond.sptk region
    cmp.ne p6,p7=r5,r0
(p6) br.cond.spnt done
    cmp.ne p6,p7=r14,r0
(p6) br.cond.sptk again

    /* Arm the timer after warming the same region. */
    mov r14=1
    mov r2=0xef
    mov cr.itv=r2
    movl r4=2000000
    mov r2=ar.itc
    add r2=r2,r4
    mov cr.itm=r2
    ssm 0x4000
    srlz.d
again:
    movl r20=50000
    br.sptk region
done:
    br.sptk done

.balign 32768
ivt:
.skip 0x3000
    mov r6=cr.ivr
    add r5=1,r5
    mov cr.eoi=r0
    rfi
