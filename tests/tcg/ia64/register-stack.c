/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * The register stack engine against the backing store Linux gives a
 * process: deep recursion spills and refills stacked registers, longjmp
 * unwinds the register stack with loadrs, and alloca'd frames survive.
 */

#include <alloca.h>
#include <setjmp.h>
#include <string.h>
#include "ia64-test.h"

#define DEPTH 20000

static jmp_buf escape;
static volatile int jump_enabled = 1;

/* Many live values across the call keep the caller's frame large. */
static __attribute__((noinline)) uint64_t
deep(uint64_t n, uint64_t a, uint64_t b, uint64_t c, uint64_t d)
{
    uint64_t e = a * 3 + n, f = b ^ (n << 7), g = c + d, h = a - b;
    uint64_t r;

    if (n == 0) {
        return a + b + c + d;
    }
    r = deep(n - 1, b + 1, c ^ 5, d + n, a);
    return r + e + f + g + h - (e ^ f) - (g & h);
}

static uint64_t deep_reference(uint64_t n, uint64_t a, uint64_t b,
                               uint64_t c, uint64_t d)
{
    uint64_t total = 0;

    /* The same recurrence, iterated without the register stack. */
    for (;;) {
        uint64_t e = a * 3 + n, f = b ^ (n << 7), g = c + d, h = a - b;
        uint64_t na, nb, nc, nd;

        if (n == 0) {
            return total + a + b + c + d;
        }
        total += e + f + g + h - (e ^ f) - (g & h);
        na = b + 1;
        nb = c ^ 5;
        nc = d + n;
        nd = a;
        a = na;
        b = nb;
        c = nc;
        d = nd;
        n--;
    }
}

static __attribute__((noinline)) void dive(int n, volatile int *marker)
{
    volatile char pad[64];

    pad[0] = (char)n;
    if (n == 0) {
        *marker = pad[0] + 1;
        if (jump_enabled) {
            longjmp(escape, 42);
        }
        return;
    }
    dive(n - 1, marker);
    pad[1] = 0;
}

static __attribute__((noinline)) int alloca_chain(int n)
{
    char *buf = alloca(256 + n);
    int sum;

    memset(buf, n & 0x7f, 256 + n);
    if (n == 0) {
        return buf[0];
    }
    sum = alloca_chain(n - 1);
    return sum + buf[255 + n];
}

int main(void)
{
    volatile int marker = 0;
    int ret;

    CHECK_EQ(deep(DEPTH, 1, 2, 3, 4), deep_reference(DEPTH, 1, 2, 3, 4));

    ret = setjmp(escape);
    if (ret == 0) {
        dive(5000, &marker);
        CHECK(!"longjmp returned to its caller");
    }
    CHECK_EQ(ret, 42);
    CHECK_EQ(marker, 1);
    /* The register stack still works after the longjmp. */
    CHECK_EQ(deep(1000, 9, 8, 7, 6), deep_reference(1000, 9, 8, 7, 6));

    {
        int expected = 0;

        for (int i = 0; i <= 300; i++) {
            expected += i & 0x7f;
        }
        CHECK_EQ(alloca_chain(300), expected);
    }
    return test_finish("register-stack");
}
