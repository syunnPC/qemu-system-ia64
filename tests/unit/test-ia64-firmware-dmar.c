/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "fw-dmar.h"

static void check_layout(uint64_t base)
{
    uint8_t buf[FW_DMAR_SIZE + 8];
    unsigned offset, all = 0, count = 0, sum = 0;

    memset(buf, 0xa5, sizeof(buf));
    g_assert_true(fw_build_dmar(buf, sizeof(buf), base));
    g_assert_cmpmem(buf, 4, "DMAR", 4);
    g_assert_cmpuint(ldl_le_p(buf + 4), ==, FW_DMAR_SIZE);
    g_assert_cmpuint(buf[8], ==, 1);
    g_assert_cmpuint(buf[36] + 1, ==, 51);
    g_assert_cmpuint(buf[37], ==, 0);
    for (offset = 0; offset < FW_DMAR_SIZE; offset++) {
        sum += buf[offset];
    }
    g_assert_cmpuint(sum & 255, ==, 0);
    for (offset = 48; offset < FW_DMAR_SIZE; ) {
        unsigned length = lduw_le_p(buf + offset + 2);
        g_assert_cmpuint(lduw_le_p(buf + offset), ==, 0);
        g_assert_cmpuint(length, >=, 16);
        g_assert_cmpuint(offset + length, <=, FW_DMAR_SIZE);
        g_assert_cmpuint(lduw_le_p(buf + offset + 6), ==, 0);
        if (buf[offset + 4] & 1) {
            all++;
            g_assert_cmpuint(offset + length, ==, FW_DMAR_SIZE);
            g_assert_cmpuint(ldq_le_p(buf + offset + 8), ==, base);
        } else {
            g_assert_cmpuint(length, ==, 24);
            g_assert_cmpuint(ldq_le_p(buf + offset + 8), ==, base + 0x1000);
            g_assert_cmpuint(buf[offset + 16], ==, 2);
            g_assert_cmpuint(buf[offset + 17], ==, 8);
            g_assert_cmpuint(buf[offset + 21], ==, 0);
            g_assert_cmpuint(buf[offset + 22], ==, 7);
            g_assert_cmpuint(buf[offset + 23], ==, 0);
        }
        count++;
        offset += length;
    }
    g_assert_cmpuint(count, ==, 2);
    g_assert_cmpuint(all, ==, 1);
    for (offset = FW_DMAR_SIZE; offset < sizeof(buf); offset++) {
        g_assert_cmpuint(buf[offset], ==, 0xa5);
    }
}

static void test_layout(void)
{
    check_layout(INTEL_7500_VTBAR);
    check_layout(INTEL_7500_VTBAR | (1ULL << 48));
}

static void test_invalid(void)
{
    uint8_t buf[FW_DMAR_SIZE];
    memset(buf, 0xa5, sizeof(buf));
    g_assert_false(fw_build_dmar(buf, sizeof(buf) - 1, INTEL_7500_VTBAR));
    g_assert_false(fw_build_dmar(buf, sizeof(buf), INTEL_7500_VTBAR + 1));
    g_assert_false(fw_build_dmar(buf, sizeof(buf), 1ULL << 51));
    g_assert_false(fw_build_dmar(buf, sizeof(buf), (1ULL << 51) - 0x1000));
    g_assert_cmpuint(buf[0], ==, 0xa5);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/ia64/dmar/layout", test_layout);
    g_test_add_func("/ia64/dmar/invalid", test_invalid);
    return g_test_run();
}
