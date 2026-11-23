/* SPDX-License-Identifier: GPL-2.0-or-later */
.text
.explicit
{ .mii
    ld8 r32=[r10],r12
    add r36=1,r36
    nop.i 0 ;;
}
{ .mii
    st4 [r35]=r32,4
    add r33=1,r33
    cmp.eq p6,p7=r8,r0 ;;
}
{ .mii
    ld4 r18=[r11],4
    xor r20=r32,r33
    nop.i 0 ;;
}
{ .mii
    st8 [r9]=r18
    nop.i 0
    nop.i 0 ;;
}
{ .mii
    ld4 r22=[r13]
    nop.i 0
    nop.i 0 ;;
}
