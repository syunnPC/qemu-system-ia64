/* SPDX-License-Identifier: GPL-2.0-or-later */
.text
.explicit
{ .mii
    nop.m 0
    mix4.r r16=r8,r9
    mix4.l r17=r8,r9 ;;
}
{ .mii
    ld8 r18=[r10]
    xor r19=r16,r17
    shrp r20=r8,r9,11 ;;
}
{ .mii
    ld4 r21=[r11]
    shr.u r22=r8,r13
    shr r23=r8,r13 ;;
}
{ .mii
    getf.sig r24=f38
    shl r25=r9,r13
    cmp4.lt p6,p7=r8,r9 ;;
}
{ .mii
    nop.m 0
    add r32=r19,r18
    xor r33=r21,r24 ;;
}
