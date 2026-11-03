/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef IA64_TARGET_MMAN_H
#define IA64_TARGET_MMAN_H

/* arch/ia64/include/uapi/asm/mman.h */
#define TARGET_MAP_GROWSUP      0x0200

/*
 * arch/ia64/include/asm/processor.h: mmap starts in region 1, and
 * arch/ia64/include/asm/elf.h puts ET_DYN images 0x800000000 above that.
 * Both are folded guest addresses (target/ia64/user-vaddr.h).
 */
#define TASK_UNMAPPED_BASE      IA64_USER_REGION_BASE(1)
#define ELF_ET_DYN_BASE         (TASK_UNMAPPED_BASE + 0x800000000ull)

#include "../generic/target_mman.h"

#endif
