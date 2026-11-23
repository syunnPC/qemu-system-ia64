/* SPDX-License-Identifier: GPL-2.0-or-later */
.text
.auto
.global _start
_start:
    rsm 0x2000
    srlz.d
    movl r18=0x4009661
    mov r7=0x38
    mov cr.itir=r7
    movl r10=0x40201000
    mov r11=33
    movl r12=0x4000
    br.call.sptk.many b0=insert_pages
    ssm 0x22000
    srlz.d
    movl r8=0x40205000
    mov r26=90
    st8 [r8]=r26
    movl r13=2000000
warm:
    br.call.sptk.many b0=region
    add r13=-1,r13
    cmp.ne p6,p7=r13,r0
(p6) br.cond.sptk warm

    /* Fill all 32 DTLB1 slots, leaving page zero oldest. */
    movl r8=0x40201000
    mov r13=32
fill:
    ld8 r14=[r8],r12
    add r13=-1,r13
    cmp.ne p6,p7=r13,r0
(p6) br.cond.sptk fill

    /* Touch page zero natively. Page 32 must now evict page one. */
    movl r8=0x40201000
    br.call.sptk.many b0=region
    movl r8=0x40281000
    ld8 r14=[r8]
    rsm 0x2000
    srlz.d
    mov r11=65
    br.call.sptk.many b0=insert_pages
    ssm 0x2000
    srlz.d

    /* The first two of 98 pages have left the 96-entry DTLB2. */
    movl r8=0x40201000
    ld8 r31=[r8]

    br.call.sptk.many b0=arithmetic
    rsm 0x20000
    srlz.d
    br.call.sptk.many b0=arithmetic
done:
    br.sptk done

insert_pages:
    mov cr.ifa=r10
    itc.d r18
    add r10=r12,r10
    add r11=-1,r11
    cmp.ne p6,p7=r11,r0
(p6) br.cond.sptk insert_pages
    br.ret.sptk.many b0

.explicit
region:
{ .mii
    ld8 r14=[r8]
    nop.i 0
    nop.i 0 ;;
}
arithmetic:
.rept 10
{ .mii
    nop.m 0
    add r16=1,r16
    add r17=1,r17 ;;
}
.endr
{ .mib
    nop.m 0
    nop.i 0
    br.ret.sptk.many b0 ;;
}
