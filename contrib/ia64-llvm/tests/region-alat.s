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
    mov r4=0
    mov r15=0 ;;
}
{ .mlx
    nop.m 0
    movl r8=0x3000000 ;;
}
{ .mlx
    nop.m 0
    movl r20=1000000 ;;
}
{ .mii
    st8 [r8]=r0
    mov r16=0
    mov r17=0 ;;
}
loop:
{ .mii
    ld8.a r14=[r8]
    add r15=1,r15
    nop.i 0 ;;
}
{ .mii
    chk.a.nc r14,miss
    nop.i 0
    nop.i 0 ;;
}
region:
.rept 8
{ .mii
    add r16=1,r16
    add r17=1,r17
    nop.i 0 ;;
}
.endr
{ .mii
    st8 [r8]=r15
    add r16=1,r16
    add r17=1,r17 ;;
}
{ .mii
    add r16=1,r16
    add r17=1,r17
    add r20=-1,r20 ;;
}
{ .mii
    ld8.c.clr r14=[r8]
    cmp.ne p8,p9=r20,r0
    nop.i 0 ;;
}
{ .mii
    nop.m 0
    cmp.ne p6,p7=r14,r15
    nop.i 0 ;;
}
{ .mbb
    nop.m 0
(p6) br.cond.spnt done
(p8) br.cond.sptk loop ;;
}
done:
{ .mib
    nop.m 0
    nop.i 0
    br.sptk done ;;
}
miss:
{ .mib
    add r4=1,r4
    nop.i 0
    br.sptk region ;;
}
