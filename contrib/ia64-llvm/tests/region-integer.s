/* SPDX-License-Identifier: GPL-2.0-or-later */
.text
.explicit
{ .mii
    addp4 r16=r8,r9
    shladdp4 r17=r8,3,r9
    adds r18=-33,r8 ;;
}
{ .mii
    andcm r19=-8,r8
    dep.z r20=-3,57,7
    dep r21=-1,r9,48,9 ;;
}
{ .mii
    sub r22=r8,r9,1
    dep r23=r8,r9,61,3
    extr r24=r8,57,7 ;;
}
{ .mii
    cmp4.ltu p0,p62=-1,r8
    sxt2 r25=r8
    zxt4 r26=r9 ;;
}
{ .mii
    getf.sig r27=f1
    cmp.lt p60,p0=-17,r9
    shrp r8=r9,r8,0 ;;
}
