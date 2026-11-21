/* SPDX-License-Identifier: GPL-2.0-or-later */
.text
.explicit
{ .mmi
    mov r26=cr.ipsr ;;
    mov r27=cr.iip
    nop.i 0 ;;
}
{ .mmi
    mov r28=cr.isr ;;
    mov r29=cr.ifa
    nop.i 0 ;;
}
spin:
{ .mib
    nop.m 0
    nop.i 0
    br.sptk spin ;;
}
