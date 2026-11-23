/* SPDX-License-Identifier: GPL-2.0-or-later */
.text
.explicit
.global _start
_start:
{ .mii
    alloc r2=ar.pfs,0,8,0,0
    nop.i 0
    nop.i 0 ;;
}
{ .mii
    ssm 0x2008
    nop.i 0
    nop.i 0 ;;
}
{ .mii
    srlz.d
    nop.i 0
    nop.i 0 ;;
}
{ .mlx
    nop.m 0
    movl r20=2000000 ;;
}
{ .mlx
    nop.m 0
    movl r23=0x3000000 ;;
}
{ .mii
    mov r34=0
    add r24=8,r23
    mov r35=0 ;;
}
{ .mii
    st8 [r23]=r0
    mov r7=r23
    nop.i 0 ;;
}
region:
.rept 5
{ .mii
    add r35=1,r35
    add r34=1,r34
    or r36=r23,r0 ;;
}
.endr
{ .mii
    ld8 r18=[r23]
    add r35=1,r35
    add r34=1,r34 ;;
}
{ .mii
    nop.m 0
    add r18=1,r18
    nop.i 0 ;;
}
{ .mii
    st8 [r23]=r18
    nop.i 0
    nop.i 0 ;;
}
fault:
{ .mmi
    nop.m 0
    ld8 r19=[r24]
    add r34=8,r34 ;;
}
.rept 5
{ .mii
    add r35=1,r35
    add r34=1,r34
    or r36=r23,r0 ;;
}
.endr
{ .mii
    add r20=-1,r20
    nop.i 0
    nop.i 0 ;;
}
{ .mii
    nop.m 0
    cmp.ne p6,p7=r20,r0
    nop.i 0 ;;
}
{ .mib
    nop.m 0
    nop.i 0
(p6) br.cond.sptk region ;;
}
{ .mii
    add r24=1,r24
    nop.i 0
    nop.i 0 ;;
}
{ .mib
    nop.m 0
    nop.i 0
    br.sptk region ;;
}
