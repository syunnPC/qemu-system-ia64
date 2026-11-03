/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * IA-64 Linux user-mode address windows.
 *
 * An IA-64 virtual address keeps its region number in bits 63:61, and a
 * Linux process uses several regions at once: shared objects and anonymous
 * mappings in region 1, program text in region 2, data and the stacks in
 * region 3.  linux-user keeps one contiguous guest address space, so each
 * region gets a window of IA64_USER_WINDOW_BITS bits placed at
 * region << IA64_USER_WINDOW_BITS.  The architectural address (what the
 * program sees in its registers and in memory) folds into that space only
 * where QEMU touches memory: translated loads and stores, instruction
 * fetch, and the pointers system calls hand to the host.
 *
 * Region 0 maps to itself, which makes the fold idempotent: a folded
 * address is a region 0 address, so folding it again changes nothing.  The
 * price is that region 0 addresses at or above the first window alias the
 * other regions' windows.  Linux gives a program nothing in region 0 unless
 * it asks for a fixed address there.
 *
 * A region 1-7 offset that does not fit in its window would alias another
 * window, so it folds into the guard window instead, which never holds a
 * mapping: the access faults as it would on an unmapped page.
 */

#ifndef TARGET_IA64_USER_VADDR_H
#define TARGET_IA64_USER_VADDR_H

#define IA64_USER_WINDOW_BITS   40
#define IA64_USER_WINDOW_SIZE   (1ULL << IA64_USER_WINDOW_BITS)
#define IA64_USER_WINDOW_MASK   (IA64_USER_WINDOW_SIZE - 1)
#define IA64_USER_REGION_COUNT  8
#define IA64_USER_SPACE_BITS    (IA64_USER_WINDOW_BITS + 3)
/* Regions 5-7 belong to the kernel; region 7 never holds a user mapping. */
#define IA64_USER_GUARD_REGION  7
/* Linux's TASK_SIZE: user mappings live in regions 0-4. */
#define IA64_USER_TASK_SIZE     0xa000000000000000ULL

#define IA64_USER_REGION_BASE(region) \
    ((uint64_t)(region) << IA64_USER_WINDOW_BITS)

static inline uint64_t ia64_user_fold(uint64_t va)
{
    uint64_t region = va >> IA64_REGION_SHIFT;
    uint64_t offset = va & IA64_REGION7_PHYS_MASK;

    if (region == 0) {
        return va;
    }
    if (offset & ~IA64_USER_WINDOW_MASK) {
        return IA64_USER_REGION_BASE(IA64_USER_GUARD_REGION) |
               (offset & IA64_USER_WINDOW_MASK);
    }
    return IA64_USER_REGION_BASE(region) | offset;
}

static inline uint64_t ia64_user_unfold(uint64_t addr)
{
    uint64_t region = addr >> IA64_USER_WINDOW_BITS;

    if (region == 0 || region >= IA64_USER_REGION_COUNT) {
        return addr;
    }
    return (region << IA64_REGION_SHIFT) | (addr & IA64_USER_WINDOW_MASK);
}

/* True when [va, va + len) lies in one window, as one host range. */
static inline bool ia64_user_range_in_window(uint64_t va, uint64_t len)
{
    uint64_t offset = va & IA64_REGION7_PHYS_MASK;

    return len == 0 ||
           (offset < IA64_USER_WINDOW_SIZE &&
            len <= IA64_USER_WINDOW_SIZE - offset);
}

#endif /* TARGET_IA64_USER_VADDR_H */
