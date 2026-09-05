/*
 * HP zx1 MIO CSR tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/pci-host/hp-zx1-mio-regs.h"

static uint64_t read64(const HPZX1MIORegs *regs, uint64_t offset)
{
    uint64_t value = UINT64_MAX;

    g_assert_true(hp_zx1_mio_regs_read(regs, offset, 8, &value));
    return value;
}

static void test_identification_and_reset(void)
{
    HPZX1MIORegs regs;
    unsigned int i;

    memset(&regs, 0xa5, sizeof(regs));
    hp_zx1_mio_regs_reset(&regs);

    g_assert_cmphex(read64(&regs, HP_ZX1_MIO_F0_ID), ==,
                    UINT64_C(0x1229103c));
    g_assert_cmphex(read64(&regs, HP_ZX1_MIO_F0_CLASS), ==,
                    UINT64_C(0x0000002006800023));
    g_assert_cmphex(read64(&regs, HP_ZX1_MIO_MODULE_INFO), ==,
                    UINT64_C(0x0703000a));
    g_assert_cmphex(read64(&regs, HP_ZX1_MIO_F1_ID), ==,
                    UINT64_C(0x122a103c));
    g_assert_cmphex(read64(&regs, HP_ZX1_MIO_F1_CLASS), ==,
                    UINT64_C(0x0000002006800023));

    g_assert_cmphex(read64(&regs, HP_ZX1_MIO_LMMIO_DIST_BASE), ==,
                    UINT64_C(0x80000000));
    g_assert_cmphex(read64(&regs, HP_ZX1_MIO_LMMIO_DIR_MASK(1)), ==,
                    UINT64_C(0x80000000));
    g_assert_cmphex(read64(&regs, HP_ZX1_MIO_ROPE_CONFIG_BASE), ==,
                    UINT64_C(0x80000000));
    g_assert_cmphex(read64(&regs, HP_ZX1_MIO_IOS_DIR_MASK), ==,
                    UINT64_C(0xffff0000));
    g_assert_cmphex(read64(&regs, HP_ZX1_MIO_IOS_DIST_MASK), ==,
                    UINT64_C(0xffff0000));
    g_assert_cmphex(read64(&regs, HP_ZX1_MIO_IOS_DIST_ROUTE), ==,
                    UINT64_C(12) << 58);
    g_assert_cmphex(read64(&regs, HP_ZX1_MIO_ROPE_CONFIG), ==,
                    UINT64_C(0x00ff));
    for (i = 0; i < HP_ZX1_MIO_LBA_PORT_COUNT; i++) {
        g_assert_cmphex(read64(&regs, HP_ZX1_MIO_LBA_PORT_CONTROL(i)),
                        ==, 0);
    }
}

static void test_f0_masks_and_read_only(void)
{
    static const struct {
        uint16_t offset;
        uint64_t expected;
    } masked[] = {
        { HP_ZX1_MIO_LMMIO_DIR_BASE(0),
          UINT64_C(0x80000000) | (UINT64_C(0x7ff) << 20) | 1 },
        { HP_ZX1_MIO_LMMIO_DIR_MASK(0),
          UINT64_C(0x80000000) | (UINT64_C(0x7ff) << 20) },
        { HP_ZX1_MIO_LMMIO_DIR_ROUTE(0), UINT64_C(0x7) },
        { HP_ZX1_MIO_LMMIO_DIR_BASE(1),
          UINT64_C(0x80000000) | (UINT64_C(0x7ff) << 20) | 1 },
        { HP_ZX1_MIO_LMMIO_DIR_MASK(1),
          UINT64_C(0x80000000) | (UINT64_C(0x7ff) << 20) },
        { HP_ZX1_MIO_LMMIO_DIR_ROUTE(1), UINT64_C(0x7) },
        { HP_ZX1_MIO_LMMIO_DIST_BASE,
          UINT64_C(0x80000000) | (UINT64_C(0x7ff) << 20) | 1 },
        { HP_ZX1_MIO_LMMIO_DIST_MASK,
          UINT64_C(0x80000000) | (UINT64_C(0x7ff) << 20) },
        { HP_ZX1_MIO_LMMIO_DIST_ROUTE, UINT64_C(0x3f) << 58 },
        { HP_ZX1_MIO_GMMIO_DIST_BASE,
          (UINT64_C(0xfff) << 32) | UINT64_C(0x7) },
        { HP_ZX1_MIO_GMMIO_DIST_MASK, UINT64_C(0xfff) << 32 },
        { HP_ZX1_MIO_GMMIO_DIST_ROUTE, UINT64_C(0x3f) << 58 },
        { HP_ZX1_MIO_IOS_DIST_BASE, UINT64_C(0x1) },
        { HP_ZX1_MIO_IOS_DIST_MASK, UINT64_C(0xffff0000) },
        { HP_ZX1_MIO_IOS_DIST_ROUTE, UINT64_C(0x3f) << 58 },
        { HP_ZX1_MIO_ROPE_CONFIG_BASE,
          UINT64_C(0x80000000) | (UINT64_C(0x3fff) << 17) | 1 },
        { HP_ZX1_MIO_VGA_ROUTE,
          (UINT64_C(0x3) << 62) | UINT64_C(0xf) },
        { HP_ZX1_MIO_IOS_DIR_BASE, UINT64_C(0xff01) },
        { HP_ZX1_MIO_IOS_DIR_MASK, UINT64_C(0xffffff00) },
        { HP_ZX1_MIO_IOS_DIR_ROUTE, UINT64_C(0x7) },
    };
    HPZX1MIORegs regs;
    unsigned int i;

    hp_zx1_mio_regs_reset(&regs);
    for (i = 0; i < G_N_ELEMENTS(masked); i++) {
        g_assert_true(hp_zx1_mio_regs_write(
                          &regs, masked[i].offset, 8, UINT64_MAX));
        g_assert_cmphex(read64(&regs, masked[i].offset), ==,
                        masked[i].expected);
    }

    g_assert_true(hp_zx1_mio_regs_write(&regs, HP_ZX1_MIO_IOS_DIST_MASK,
                                        8, 0));
    g_assert_cmphex(read64(&regs, HP_ZX1_MIO_IOS_DIST_MASK), ==,
                    UINT64_C(0xfff00000));

    g_assert_true(hp_zx1_mio_regs_write(&regs, HP_ZX1_MIO_F0_ID,
                                        8, UINT64_MAX));
    g_assert_true(hp_zx1_mio_regs_write(&regs, HP_ZX1_MIO_MODULE_INFO,
                                        8, UINT64_MAX));
    g_assert_cmphex(read64(&regs, HP_ZX1_MIO_F0_ID), ==,
                    UINT64_C(0x1229103c));
    g_assert_cmphex(read64(&regs, HP_ZX1_MIO_MODULE_INFO), ==,
                    UINT64_C(0x0703000a));
}

static void test_f1_byte_lane_writes(void)
{
    HPZX1MIORegs regs;

    hp_zx1_mio_regs_reset(&regs);

    g_assert_true(hp_zx1_mio_regs_write(
                      &regs, HP_ZX1_MIO_ROPE_CONFIG + 1, 1, 0xa5));
    g_assert_cmphex(read64(&regs, HP_ZX1_MIO_ROPE_CONFIG), ==,
                    UINT64_C(0xa5ff));

    g_assert_true(hp_zx1_mio_regs_write_be(
                      &regs, HP_ZX1_MIO_ROPE_CONFIG,
                      UINT64_C(0x5a0000000000003c), 0x81));
    g_assert_cmphex(read64(&regs, HP_ZX1_MIO_ROPE_CONFIG), ==,
                    UINT64_C(0xa53c));
    g_assert_true(hp_zx1_mio_regs_write(
                      &regs, HP_ZX1_MIO_ROPE_CONFIG + 2, 2, 0xffff));
    g_assert_cmphex(read64(&regs, HP_ZX1_MIO_ROPE_CONFIG), ==,
                    UINT64_C(0xa53c));
    g_assert_true(hp_zx1_mio_regs_write(
                      &regs, HP_ZX1_MIO_ROPE_CONFIG + 4, 4,
                      UINT32_MAX));
    g_assert_cmphex(read64(&regs, HP_ZX1_MIO_ROPE_CONFIG), ==,
                    UINT64_C(0xa53c));

    /* F1 byte enables also apply to read-only registers. */
    g_assert_true(hp_zx1_mio_regs_write(&regs, HP_ZX1_MIO_F1_ID + 2,
                                        2, 0xffff));
    g_assert_cmphex(read64(&regs, HP_ZX1_MIO_F1_ID), ==,
                    UINT64_C(0x122a103c));

    /* RF is write-only and does not change active topology. */
    g_assert_true(hp_zx1_mio_regs_write(
                      &regs, HP_ZX1_MIO_LBA_PORT_CONTROL(0), 1, 0xf1));
    g_assert_cmphex(read64(&regs, HP_ZX1_MIO_LBA_PORT_CONTROL(0)), ==,
                    UINT64_C(0x70));
    g_assert_cmphex(read64(&regs, HP_ZX1_MIO_LBA_PORT_CONTROL(1)), ==, 0);
    g_assert_true(hp_zx1_mio_regs_write(
                      &regs, HP_ZX1_MIO_LBA_PORT_CONTROL(0) + 1, 1, 0xff));
    g_assert_cmphex(read64(&regs, HP_ZX1_MIO_LBA_PORT_CONTROL(0)), ==,
                    UINT64_C(0x70));

    hp_zx1_mio_regs_report_fault(&regs, HP_ZX1_MIO_ERROR_IOMMU,
                                 UINT64_C(0x12345000), UINT64_C(0x55));
    g_assert_true(hp_zx1_mio_regs_write_be(
                      &regs, HP_ZX1_MIO_ERROR_STATUS,
                      HP_ZX1_MIO_ERROR_VALID, 0x80));
    g_assert_cmphex(read64(&regs, HP_ZX1_MIO_ERROR_STATUS), ==, 0);
    g_assert_cmphex(read64(&regs, HP_ZX1_MIO_ERROR_ADDRESS), ==, 0);
    g_assert_cmphex(read64(&regs, HP_ZX1_MIO_ERROR_INFORMATION), ==, 0);
    g_assert_true(hp_zx1_mio_regs_write_be(
                      &regs, HP_ZX1_MIO_ERROR_ADDRESS, UINT64_MAX,
                      UINT8_MAX));
    g_assert_true(hp_zx1_mio_regs_write_be(
                      &regs, HP_ZX1_MIO_ERROR_INFORMATION, UINT64_MAX,
                      UINT8_MAX));
}

static void test_invalid_accesses(void)
{
    HPZX1MIORegs regs;
    uint64_t value = UINT64_MAX;

    hp_zx1_mio_regs_reset(&regs);

    g_assert_false(hp_zx1_mio_regs_read(
                       &regs, HP_ZX1_MIO_F0_ID, 4, &value));
    g_assert_false(hp_zx1_mio_regs_write(
                       &regs, HP_ZX1_MIO_F0_ID, 4, 0));
    g_assert_false(hp_zx1_mio_regs_read(
                       &regs, HP_ZX1_MIO_ROPE_CONFIG, 4, &value));
    g_assert_false(hp_zx1_mio_regs_write(
                       &regs, HP_ZX1_MIO_ROPE_CONFIG + 1, 2, 0));
    g_assert_false(hp_zx1_mio_regs_write(
                       &regs, HP_ZX1_MIO_ROPE_CONFIG, 3, 0));
    g_assert_false(hp_zx1_mio_regs_write_be(
                       &regs, HP_ZX1_MIO_F0_ID, 0, UINT8_MAX));
    g_assert_false(hp_zx1_mio_regs_write_be(
                       &regs, HP_ZX1_MIO_ROPE_CONFIG + 1, 0, UINT8_MAX));

    g_assert_false(hp_zx1_mio_regs_read(&regs, 0x0010, 8, &value));
    g_assert_false(hp_zx1_mio_regs_write(&regs, 0x1010, 8, 0));
    g_assert_false(hp_zx1_mio_regs_read(&regs, 0x8000, 8, &value));
    g_assert_false(hp_zx1_mio_regs_write(&regs, 0xa000, 8, 0));
    g_assert_false(hp_zx1_mio_regs_read(
                       &regs, HP_ZX1_MIO_CSR_SIZE, 8, &value));

    g_assert_false(hp_zx1_mio_regs_read(NULL, 0, 8, &value));
    g_assert_false(hp_zx1_mio_regs_read(&regs, 0, 8, NULL));
    g_assert_false(hp_zx1_mio_regs_write(NULL, 0, 8, 0));
}

static void test_reset_restores_latches(void)
{
    HPZX1MIORegs regs;

    hp_zx1_mio_regs_reset(&regs);
    g_assert_true(hp_zx1_mio_regs_write(
                      &regs, HP_ZX1_MIO_LMMIO_DIST_BASE, 8,
                      UINT64_MAX));
    g_assert_true(hp_zx1_mio_regs_write(
                      &regs, HP_ZX1_MIO_ROPE_CONFIG, 8, 0));
    g_assert_true(hp_zx1_mio_regs_write(
                      &regs, HP_ZX1_MIO_LBA_PORT_CONTROL(7), 1, 0x70));

    hp_zx1_mio_regs_reset(&regs);
    g_assert_cmphex(read64(&regs, HP_ZX1_MIO_LMMIO_DIST_BASE), ==,
                    UINT64_C(0x80000000));
    g_assert_cmphex(read64(&regs, HP_ZX1_MIO_ROPE_CONFIG), ==,
                    UINT64_C(0x00ff));
    g_assert_cmphex(read64(&regs, HP_ZX1_MIO_LBA_PORT_CONTROL(7)), ==, 0);
}

static void test_migration_state_validation(void)
{
    HPZX1MIORegs regs;

    hp_zx1_mio_regs_reset(&regs);
    g_assert_true(hp_zx1_mio_regs_state_valid(&regs));

    g_assert_true(hp_zx1_mio_regs_write(
                      &regs, HP_ZX1_MIO_LMMIO_DIST_BASE, 8,
                      UINT64_MAX));
    g_assert_true(hp_zx1_mio_regs_write(
                      &regs, HP_ZX1_MIO_ROPE_CONFIG, 8,
                      UINT64_MAX));
    g_assert_true(hp_zx1_mio_regs_state_valid(&regs));

    regs.lmmio_dir_base[0] |= UINT64_C(1) << 40;
    g_assert_false(hp_zx1_mio_regs_state_valid(&regs));

    hp_zx1_mio_regs_reset(&regs);
    regs.ios_dist_mask &= ~UINT64_C(0x10000000);
    g_assert_false(hp_zx1_mio_regs_state_valid(&regs));

    hp_zx1_mio_regs_reset(&regs);
    regs.rope_config |= UINT64_C(1) << 20;
    g_assert_false(hp_zx1_mio_regs_state_valid(&regs));

    hp_zx1_mio_regs_reset(&regs);
    regs.lba_port_control[HP_ZX1_MIO_LBA_PORT_COUNT - 1] |= 1;
    g_assert_false(hp_zx1_mio_regs_state_valid(&regs));
    g_assert_false(hp_zx1_mio_regs_state_valid(NULL));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/hp-zx1-mio/identification-and-reset",
                    test_identification_and_reset);
    g_test_add_func("/hp-zx1-mio/f0-masks-and-read-only",
                    test_f0_masks_and_read_only);
    g_test_add_func("/hp-zx1-mio/f1-byte-lane-writes",
                    test_f1_byte_lane_writes);
    g_test_add_func("/hp-zx1-mio/invalid-accesses",
                    test_invalid_accesses);
    g_test_add_func("/hp-zx1-mio/reset-restores-latches",
                    test_reset_restores_latches);
    g_test_add_func("/hp-zx1-mio/migration-state-validation",
                    test_migration_state_validation);
    return g_test_run();
}
