/* SPDX-License-Identifier: GPL-2.0-or-later */
.text
.auto
.global _start
_start:
    ssm 0x2000
    srlz.d
    movl r8=0x3000000
    st8 [r8]=r0
    mov ar.unat=r0
    mov r4=0
    mov r5=2
phase:
    movl r20=2000000
loop:
    /* A fill after TB entry can introduce a NaT before the native region. */
    ld8.fill r14=[r8]
.explicit
region:
.rept 8
{ .mii
    add r15=1,r14
    add r16=1,r16
    add r17=1,r17 ;;
}
.endr
.auto
    tnat.nz p6,p7=r15
(p6) add r4=1,r4
    mov r14=r0
    mov r15=r0
    add r20=-1,r20
    cmp.ne p8,p9=r20,r0
(p8) br.cond.sptk loop
    mov r9=1
    mov ar.unat=r9
    add r5=-1,r5
    cmp.ne p8,p9=r5,r0
(p8) br.cond.sptk phase
done:
    br.sptk done
