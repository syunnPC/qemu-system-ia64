/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Control and data speculation as Linux sets them up for a process: a
 * speculative load from an unmapped page is deferred to a NaT instead of
 * faulting, chk.s branches to recovery code for it, an lfetch.fault of an
 * unmapped page is cancelled, and chk.a recovers an advanced load.
 */

#include <sys/mman.h>
#include <unistd.h>
#include "ia64-test.h"

/* Return 1 if a speculative load from addr left a NaT, with its value. */
static int load_speculative(const void *addr, uint64_t *value)
{
    uint64_t v, nat;

    asm volatile("ld8.s %0 = [%2] ;;\n\t"
                 "tnat.nz p6, p7 = %0 ;;\n\t"
                 "(p6) mov %1 = 1\n\t"
                 "(p7) mov %1 = 0 ;;\n\t"
                 "(p6) mov %0 = 0 ;;"
                 : "=&r"(v), "=&r"(nat) : "r"(addr) : "p6", "p7");
    *value = v;
    return nat;
}

/* chk.s on a deferred load jumps to the recovery code, which reloads. */
static uint64_t load_checked(const volatile uint64_t *addr, int *recovered)
{
    uint64_t v;
    int r = 0;

    asm volatile("ld8.s %0 = [%2] ;;\n\t"
                 "chk.s %0, 1f ;;\n\t"
                 "br.sptk.few 2f ;;\n"
                 "1:\n\t"
                 "ld8 %0 = [%2]\n\t"
                 "mov %1 = 1 ;;\n"
                 "2:"
                 : "=&r"(v), "+r"(r) : "r"(addr) : "memory");
    *recovered = r;
    return v;
}

int main(void)
{
    long page = sysconf(_SC_PAGESIZE);
    volatile uint64_t mapped = 0x1122334455667788ULL;
    char *hole;
    uint64_t value;
    int recovered;

    hole = mmap(NULL, page, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK(hole != MAP_FAILED);
    munmap(hole, page);

    CHECK_EQ(load_speculative((const void *)&mapped, &value), 0);
    CHECK_EQ(value, 0x1122334455667788ULL);
    CHECK_EQ(load_speculative(hole, &value), 1);
    /* No access rights defer too (DCR.dr). */
    hole = mmap(NULL, page, PROT_NONE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    CHECK_EQ(load_speculative(hole + 8, &value), 1);
    munmap(hole, page);

    value = load_checked(&mapped, &recovered);
    CHECK_EQ(value, 0x1122334455667788ULL);
    CHECK_EQ(recovered, 0);

    /* The kernel cancels a faulting lfetch.fault with PSR.ed. */
    asm volatile("lfetch.fault [%0] ;;" : : "r"(hole) : "memory");

    /* ld8.a, a conflicting store, then chk.a must reload. */
    {
        volatile uint64_t cell = 5;
        uint64_t v;
        int reloaded = 0;

        asm volatile("ld8.a %0 = [%2] ;;\n\t"
                     "st8 [%2] = %3 ;;\n\t"
                     "chk.a.clr %0, 1f ;;\n\t"
                     "br.sptk.few 2f ;;\n"
                     "1:\n\t"
                     "ld8 %0 = [%2]\n\t"
                     "mov %1 = 1 ;;\n"
                     "2:"
                     : "=&r"(v), "+r"(reloaded)
                     : "r"(&cell), "r"((uint64_t)9) : "memory");
        CHECK_EQ(v, 9);
        CHECK_EQ(reloaded, 1);
    }
    return test_finish("speculation");
}
