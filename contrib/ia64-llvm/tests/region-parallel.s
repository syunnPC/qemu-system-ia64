/* SPDX-License-Identifier: GPL-2.0-or-later */
.text
.explicit
.global _start
_start:
{ .mii
    ssm 0x2000
    nop.i 0
    nop.i 0 ;;
}
{ .mii
    srlz.d
    mov r16=0
    mov r17=0 ;;
}
{ .mlx
    nop.m 0
    movl r20=130000 ;;
}
{ .mlx
    nop.m 0
    movl r19=0x3000000 ;;
}

/* Each region has a distinct immediate and can compile independently. */
.macro arithmetic step
.rept 12
{ .mii
    add r16=\step,r16
    add r17=2,r17
    add r18=3,r18 ;;
}
.endr
.endm
.balign 256
region:
.set step, 1
.rept 95
    arithmetic step
{ .mib
    nop.m 0
    nop.i 0
    br.sptk 1f ;;
}
.balign 256
1:
.set step, step + 1
.endr
    arithmetic 96
{ .mii
    add r20=-1,r20
    nop.i 0
    nop.i 0 ;;
}
{ .mii
    st8 [r19]=r20
    cmp.ne p6,p7=r20,r0
    nop.i 0 ;;
}
{ .mib
    nop.m 0
    nop.i 0
(p6) br.cond.sptk region ;;
}
{ .mmi
    add r19=8,r19 ;;
    st8 [r19]=r16
    nop.i 0 ;;
}
done:
{ .mib
    nop.m 0
    nop.i 0
    br.sptk done ;;
}
