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
    ssm 0x2000
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
    movl r21=replacement ;;
}
{ .mlx
    nop.m 0
    movl r22=changed ;;
}
{ .mii
    add r23=8,r21
    add r24=8,r22
    mov r25=0 ;;
}
{ .mii
    mov r34=0
    mov r35=0
    mov r36=0 ;;
}
{ .mii
    mov r37=0
    mov r38=0
    nop.i 0 ;;
}
region:
.rept 4
{ .mii
    add r35=1,r35
    add r36=2,r36
    add r37=3,r37 ;;
}
.endr
changed:
{ .mii
    add r34=1,r34
    add r36=2,r36
    add r37=3,r37 ;;
}
.rept 42
{ .mii
    add r35=1,r35
    add r36=2,r36
    add r37=3,r37 ;;
}
.endr
successor:
{ .mii
    mf
    add r38=1,r38
    nop.i 0 ;;
}
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
    nop.m 0
    cmp.ne p8,p9=r25,r0
    nop.i 0 ;;
}
{ .mib
    nop.m 0
    nop.i 0
(p8) br.cond.sptk done ;;
}
{ .mmi
    ld8 r26=[r21]
    ld8 r27=[r23]
    nop.i 0 ;;
}
{ .mmi
    st8 [r22]=r26
    st8 [r24]=r27
    mov r25=1 ;;
}
{ .mii
    fc.i r22
    nop.i 0
    nop.i 0 ;;
}
{ .mlx
    nop.m 0
    movl r21=next_replacement ;;
}
{ .mlx
    nop.m 0
    movl r22=successor ;;
}
{ .mii
    add r23=8,r21
    add r24=8,r22
    nop.i 0 ;;
}
{ .mmi
    ld8 r26=[r21]
    ld8 r27=[r23]
    nop.i 0 ;;
}
{ .mmi
    st8 [r22]=r26
    st8 [r24]=r27
    nop.i 0 ;;
}
{ .mii
    fc.i r22
    nop.i 0
    nop.i 0 ;;
}
{ .mii
    sync.i
    nop.i 0
    nop.i 0 ;;
}
{ .mii
    srlz.i
    nop.i 0
    nop.i 0 ;;
}
{ .mlx
    nop.m 0
    movl r20=2000000 ;;
}
{ .mib
    nop.m 0
    nop.i 0
    br.sptk region ;;
}
done:
{ .mib
    nop.m 0
    nop.i 0
    br.sptk done ;;
}
replacement:
{ .mii
    add r34=2,r34
    add r36=2,r36
    add r37=3,r37 ;;
}
next_replacement:
{ .mii
    mf
    add r38=2,r38
    nop.i 0 ;;
}
