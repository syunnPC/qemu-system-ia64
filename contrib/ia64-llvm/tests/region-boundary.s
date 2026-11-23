/* SPDX-License-Identifier: GPL-2.0-or-later */
.text
.explicit
.equ bundles, 8
.equ testdev_bar2, 0x7ff0000000 + (0x1d << 15) + 0x18
.global _start
_start:
    /* Map the test device's RAM BAR into the last implemented physical page. */
    movl r16=testdev_bar2
    movl r17=0xc1000000 ;;
    st4 [r16]=r17
    movl r14=(1 << 50) - bundles * 16
    movl r15=region ;;
    mov r18=r14
    mov r19=bundles * 2 - 1 ;;
    mov ar.lc=r19 ;;
copy:
    ld8 r26=[r15],8 ;;
    st8 [r18]=r26 ;;
    fc.i r18 ;;
    adds r18=8,r18
    br.cloop.sptk copy ;;
    sync.i ;;
    srlz.i ;;

    ssm 0x2000 ;;
    srlz.d ;;
    movl r20=2000000 ;;
    mov ar.lc=r20
    mov b7=r14 ;;
region:
.rept bundles
{ .mii
    adds r21=1,r21
    adds r22=1,r22
    adds r23=1,r23 ;;
}
.endr
    br.cloop.sptk region ;;
    br.sptk b7 ;;
