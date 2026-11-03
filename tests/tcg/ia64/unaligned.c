/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Misaligned integer loads and stores.  IA-64 faults on a reference that
 * crosses its alignment window, and Linux completes it for the process
 * (ia64_handle_unaligned) unless asked to send SIGBUS.
 */

#include <string.h>
#include "ia64-test.h"

struct __attribute__((packed)) record {
    char tag;
    uint64_t wide;
    uint32_t mid;
    uint16_t narrow;
};

static volatile struct record records[4];

/* Out of line, so the compiler cannot see the misalignment. */
static __attribute__((noipa)) uint64_t load64(const uint64_t *p)
{
    return *(const volatile uint64_t *)p;
}

static __attribute__((noipa)) void store64(uint64_t *p, uint64_t v)
{
    *(volatile uint64_t *)p = v;
}

static __attribute__((noipa)) uint32_t load32(const uint32_t *p)
{
    return *(const volatile uint32_t *)p;
}

static __attribute__((noipa)) void store16(uint16_t *p, uint16_t v)
{
    *(volatile uint16_t *)p = v;
}

int main(void)
{
    unsigned char raw[32] __attribute__((aligned(16)));

    for (int i = 0; i < 4; i++) {
        records[i].tag = i;
        records[i].wide = 0x0102030405060708ULL * (i + 1);
        records[i].mid = 0xa1b2c3d4U + i;
        records[i].narrow = 0x1234 + i;
    }
    for (int i = 0; i < 4; i++) {
        CHECK_EQ(records[i].tag, i);
        CHECK_EQ(records[i].wide, 0x0102030405060708ULL * (i + 1));
        CHECK_EQ(records[i].mid, 0xa1b2c3d4U + i);
        CHECK_EQ(records[i].narrow, 0x1234 + i);
    }

    for (int i = 0; i < 32; i++) {
        raw[i] = i;
    }
    CHECK_EQ(load64((uint64_t *)(raw + 5)), 0x0c0b0a0908070605ULL);
    CHECK_EQ(load32((uint32_t *)(raw + 7)), 0x0a090807);
    store64((uint64_t *)(raw + 5), 0x1122334455667788ULL);
    CHECK_EQ(raw[5], 0x88);
    CHECK_EQ(raw[12], 0x11);
    CHECK_EQ(raw[13], 13);
    store16((uint16_t *)(raw + 15), 0xbeef);
    CHECK_EQ(raw[15], 0xef);
    CHECK_EQ(raw[16], 0xbe);
    CHECK_EQ(raw[14], 14);
    CHECK_EQ(raw[17], 17);
    return test_finish("unaligned");
}
