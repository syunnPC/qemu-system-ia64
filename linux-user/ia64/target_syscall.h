/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef IA64_TARGET_SYSCALL_H
#define IA64_TARGET_SYSCALL_H

#define UNAME_MACHINE "ia64"
#define UNAME_MINIMUM_RELEASE "4.4.0"

/* arch/ia64/include/uapi/asm/break.h */
#define TARGET_IA64_BREAK_SYSCALL 0x100000

#define TARGET_MCL_CURRENT 1
#define TARGET_MCL_FUTURE  2
#define TARGET_MCL_ONFAULT 4

#endif
