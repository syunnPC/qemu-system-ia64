/*
 * HP zx2 MIOC register layer tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/pci-host/hp-zx1-mio-regs.h"
#include "hw/pci-host/hp-zx2-mio-regs.h"

static uint64_t read64(const HPZX2MIORegs *regs, uint64_t offset)
{
    uint64_t value = UINT64_MAX;

    g_assert_true(hp_zx2_mio_regs_read(regs, offset, 8, &value));
    return value;
}

static void test_reset_and_groups(void)
{
    HPZX2MIORegs regs;
    unsigned int group;
    unsigned int context;
    unsigned int index;

    memset(&regs, 0xa5, sizeof(regs));
    hp_zx2_mio_regs_reset(&regs);

    g_assert_cmphex(read64(&regs, HP_ZX2_MIO_CAPABILITIES), ==,
                    UINT64_C(0x0000003208040001));
    for (index = 0; index < HP_ZX2_MIO_GROUP_COUNT; index++) {
        g_assert_cmphex(read64(&regs, HP_ZX2_MIO_GROUP_ROPES(index)), ==,
                        (UINT64_C(0x0303) << (index * 2)) &
                        HP_ZX2_MIO_ROPE_MASK);
        g_assert_cmphex(read64(&regs, HP_ZX2_MIO_GROUP_CONTROL(index)), ==,
                        HP_ZX2_MIO_GROUP_ENABLE |
                        ((uint64_t)index <<
                         HP_ZX2_MIO_GROUP_CONTEXT_SHIFT));
    }
    g_assert_true(hp_zx2_mio_regs_state_valid(&regs));
    g_assert_true(hp_zx2_mio_regs_group_for_ropes(&regs, 0x03,
                                                  &group, &context));
    g_assert_cmpuint(group, ==, 0);
    g_assert_cmpuint(context, ==, 0);
    g_assert_true(hp_zx2_mio_regs_group_for_ropes(&regs, 0xc0,
                                                  &group, &context));
    g_assert_cmpuint(group, ==, 3);
    g_assert_cmpuint(context, ==, 3);
    g_assert_true(hp_zx2_mio_regs_group_for_ropes(&regs, 0x8000,
                                                  &group, &context));
    g_assert_cmpuint(group, ==, 3);
    g_assert_cmpuint(context, ==, 3);
    g_assert_false(hp_zx2_mio_regs_group_for_ropes(&regs, 0x05,
                                                   &group, &context));
}

static void test_programming_and_validation(void)
{
    HPZX2MIORegs regs;
    unsigned int group;
    unsigned int context;

    hp_zx2_mio_regs_reset(&regs);
    g_assert_true(hp_zx2_mio_regs_write(
                      &regs, HP_ZX2_MIO_GROUP_ROPES(0), 8, 0x0f));
    g_assert_true(hp_zx2_mio_regs_write(
                      &regs, HP_ZX2_MIO_GROUP_ROPES(1), 8, 0x00f0));
    g_assert_true(hp_zx2_mio_regs_write(
                      &regs, HP_ZX2_MIO_GROUP_ROPES(2), 8, 0x0f00));
    g_assert_true(hp_zx2_mio_regs_write(
                      &regs, HP_ZX2_MIO_GROUP_ROPES(3), 8, 0xf000));
    g_assert_true(hp_zx2_mio_regs_write(
                      &regs, HP_ZX2_MIO_GROUP_CONTROL(0), 8,
                      HP_ZX2_MIO_GROUP_ENABLE |
                      (UINT64_C(2) << HP_ZX2_MIO_GROUP_CONTEXT_SHIFT)));
    g_assert_true(hp_zx2_mio_regs_state_valid(&regs));
    g_assert_true(hp_zx2_mio_regs_group_for_ropes(&regs, 0x05,
                                                  &group, &context));
    g_assert_cmpuint(group, ==, 0);
    g_assert_cmpuint(context, ==, 2);
    g_assert_true(hp_zx2_mio_regs_group_for_ropes(&regs, 0x8000,
                                                  &group, &context));
    g_assert_cmpuint(group, ==, 3);

    regs.group_ropes[1] = 0x0f;
    g_assert_false(hp_zx2_mio_regs_state_valid(&regs));
    hp_zx2_mio_regs_reset(&regs);
    regs.group_control[0] |= UINT64_C(1) << 63;
    g_assert_false(hp_zx2_mio_regs_state_valid(&regs));
    hp_zx2_mio_regs_reset(&regs);
    regs.group_ropes[3] = 0;
    g_assert_false(hp_zx2_mio_regs_state_valid(&regs));
}

static void test_byte_lanes_and_masks(void)
{
    HPZX2MIORegs regs;
    uint64_t value;

    hp_zx2_mio_regs_reset(&regs);
    g_assert_true(hp_zx2_mio_regs_write(
                      &regs, HP_ZX2_MIO_IOMMU_SELECT, 1, UINT8_MAX));
    g_assert_cmphex(read64(&regs, HP_ZX2_MIO_IOMMU_SELECT), ==, 3);
    g_assert_true(hp_zx2_mio_regs_write(
                      &regs, HP_ZX2_MIO_ERROR_INTERRUPT, 8, UINT64_MAX));
    g_assert_cmphex(read64(&regs, HP_ZX2_MIO_ERROR_INTERRUPT), ==,
                    HP_ZX2_MIO_ERROR_INTERRUPT_ENABLE |
                    HP_ZX2_MIO_ERROR_INTERRUPT_VECTOR |
                    HP_ZX2_MIO_ERROR_INTERRUPT_ID |
                    HP_ZX2_MIO_ERROR_INTERRUPT_EID);
    g_assert_true(hp_zx2_mio_regs_write(
                      &regs, HP_ZX2_MIO_GROUP_ROPES(0), 8, UINT64_MAX));
    g_assert_cmphex(read64(&regs, HP_ZX2_MIO_GROUP_ROPES(0)), ==,
                    HP_ZX2_MIO_ROPE_MASK);

    value = UINT64_MAX;
    g_assert_false(hp_zx2_mio_regs_read(
                       &regs, HP_ZX2_MIO_CAPABILITIES + 1, 2, &value));
    g_assert_false(hp_zx2_mio_regs_read(
                       &regs, HP_ZX2_MIO_CAPABILITIES, 3, &value));
    g_assert_false(hp_zx2_mio_regs_read(&regs, 0x2ff8, 8, &value));
    g_assert_false(hp_zx2_mio_regs_write(&regs, 0x2ff8, 8, 0));
}

static void test_fault_latches(void)
{
    HPZX2MIORegs regs;

    hp_zx2_mio_regs_reset(&regs);
    hp_zx2_mio_regs_report_fault(&regs, HP_ZX1_MIO_ERROR_IOMMU,
                                 0x12345000, 0x55);
    g_assert_cmphex(read64(&regs, HP_ZX2_MIO_ERROR_STATUS), ==,
                    HP_ZX1_MIO_ERROR_VALID | HP_ZX1_MIO_ERROR_IOMMU);
    g_assert_cmphex(read64(&regs, HP_ZX2_MIO_ERROR_ADDRESS), ==,
                    UINT64_C(0x12345000));
    g_assert_cmphex(read64(&regs, HP_ZX2_MIO_ERROR_INFORMATION), ==, 0x55);

    hp_zx2_mio_regs_report_fault(&regs, HP_ZX1_MIO_ERROR_CSR_DECODE,
                                 0x6789, 0xaa);
    g_assert_cmphex(read64(&regs, HP_ZX2_MIO_ERROR_STATUS), ==,
                    HP_ZX1_MIO_ERROR_VALID | HP_ZX1_MIO_ERROR_MULTIPLE |
                    HP_ZX1_MIO_ERROR_IOMMU |
                    HP_ZX1_MIO_ERROR_CSR_DECODE);
    g_assert_cmphex(read64(&regs, HP_ZX2_MIO_ERROR_ADDRESS), ==,
                    UINT64_C(0x12345000));

    g_assert_true(hp_zx2_mio_regs_write(
                      &regs, HP_ZX2_MIO_ERROR_STATUS, 8,
                      HP_ZX1_MIO_ERROR_STATUS_W1C));
    g_assert_cmphex(read64(&regs, HP_ZX2_MIO_ERROR_STATUS), ==, 0);
    g_assert_cmphex(read64(&regs, HP_ZX2_MIO_ERROR_ADDRESS), ==, 0);
    g_assert_cmphex(read64(&regs, HP_ZX2_MIO_ERROR_INFORMATION), ==, 0);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/hp-zx2-mio/reset-and-groups", test_reset_and_groups);
    g_test_add_func("/hp-zx2-mio/programming-and-validation",
                    test_programming_and_validation);
    g_test_add_func("/hp-zx2-mio/byte-lanes-and-masks",
                    test_byte_lanes_and_masks);
    g_test_add_func("/hp-zx2-mio/fault-latches", test_fault_latches);
    return g_test_run();
}
