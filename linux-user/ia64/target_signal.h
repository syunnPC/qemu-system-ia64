/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef IA64_TARGET_SIGNAL_H
#define IA64_TARGET_SIGNAL_H

#include "../generic/signal.h"

/* arch/ia64/include/uapi/asm/signal.h */
#undef TARGET_MINSIGSTKSZ
#define TARGET_MINSIGSTKSZ 131027

/*
 * The handler returns through the trampoline in the gate page, which
 * init_main_thread() maps at the gate address Linux uses.
 */
#define TARGET_ARCH_HAS_SIGTRAMP_PAGE 0
void ia64_setup_gate_page(void);

#endif
