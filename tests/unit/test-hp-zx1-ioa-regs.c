/*
 * HP zx1 Mercury I/O adapter register core tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/pci-host/hp-zx1-ioa-regs.h"

typedef struct ConfigLog {
    uint32_t read_value;
    uint32_t address;
    uint32_t value;
    unsigned int size;
    unsigned int reads;
    unsigned int writes;
    bool respond;
} ConfigLog;

typedef struct DeliveryLog {
    HPZX1IOARegs *ioa;
    unsigned int watch_entry;
    HPIOSAPICMessage messages[16];
    bool status_seen[16];
    unsigned int count;
} DeliveryLog;

typedef struct FaultLog {
    IA64ChipsetFault last;
    unsigned int count;
} FaultLog;

static uint64_t read_size(HPZX1IOARegs *ioa, uint64_t offset,
                          unsigned int size)
{
    uint64_t value = UINT64_MAX;

    g_assert_true(hp_zx1_ioa_regs_read(ioa, offset, size, &value));
    return value;
}

static uint64_t readq(HPZX1IOARegs *ioa, uint64_t offset)
{
    return read_size(ioa, offset, 8);
}

static void write_size(HPZX1IOARegs *ioa, uint64_t offset,
                       unsigned int size, uint64_t value)
{
    g_assert_true(hp_zx1_ioa_regs_write(ioa, offset, size, value));
}

static void writeq(HPZX1IOARegs *ioa, uint64_t offset, uint64_t value)
{
    write_size(ioa, offset, 8, value);
}

static void clear_error_log(HPZX1IOARegs *ioa)
{
    uint64_t control = readq(ioa, HP_ZX1_IOA_STATUS_CONTROL) &
        (HP_ZX1_IOA_SIC_FORWARD_VGA | HP_ZX1_IOA_SIC_HARD_FAIL);

    writeq(ioa, HP_ZX1_IOA_STATUS_CONTROL,
           control | HP_ZX1_IOA_SIC_CLEAR_ENABLE);
    writeq(ioa, HP_ZX1_IOA_STATUS_CONTROL,
           control | HP_ZX1_IOA_SIC_CLEAR_LOG);
}

static HPZX1IOARegsConfig test_config(HPZX1IOAMode mode)
{
    HPZX1IOARegsConfig config = {
        .mode = mode,
        .rope_mask = 1U << 2,
        .secondary_bus = 0x20,
        .subordinate_bus = 0x2f,
        .pci_reset_asserted = true,
        .bus_mode_reset = (UINT64_C(1) << 63) |
                          HP_ZX1_IOA_BUS_MODE_ROPE_2X_L,
        .slave_control_reset_straps = UINT64_C(1) << 31,
        .error_configuration_reset_straps = UINT32_C(1) << 31,
    };

    if (mode == HP_ZX1_IOA_MODE_PCIX) {
        config.bus_mode_reset |= UINT64_C(1) <<
                                 HP_ZX1_IOA_BUS_MODE_BUS_SHIFT;
    } else if (mode == HP_ZX1_IOA_MODE_AGP) {
        /* Rope mask 0x0c selects two ropes. */
        config.rope_mask = (1U << 2) | (1U << 5);
        config.bus_mode_reset &= ~HP_ZX1_IOA_BUS_MODE_ROPE_2X_L;
        config.bus_mode_reset |= HP_ZX1_IOA_BUS_MODE_AGP;
    }
    return config;
}

static bool config_read(void *opaque, uint32_t address, unsigned int size,
                        uint32_t *value)
{
    ConfigLog *log = opaque;

    log->address = address;
    log->size = size;
    log->reads++;
    *value = log->read_value;
    return log->respond;
}

static bool config_write(void *opaque, uint32_t address, unsigned int size,
                         uint32_t value)
{
    ConfigLog *log = opaque;

    log->address = address;
    log->size = size;
    log->value = value;
    log->writes++;
    return log->respond;
}

static bool record_fault(void *opaque, const IA64ChipsetFault *fault)
{
    FaultLog *log = opaque;

    log->last = *fault;
    log->count++;
    return true;
}

static unsigned int rte_low(unsigned int entry)
{
    return HP_IO_SAPIC_RTE_BASE + 2 * entry;
}

static void sapic_select(HPZX1IOARegs *ioa, unsigned int selector)
{
    write_size(ioa, HP_ZX1_IOA_IOREGSEL, 4, selector);
}

static uint32_t sapic_window_read(HPZX1IOARegs *ioa,
                                  unsigned int selector)
{
    sapic_select(ioa, selector);
    return read_size(ioa, HP_ZX1_IOA_IOWIN, 4);
}

static void sapic_window_write(HPZX1IOARegs *ioa, unsigned int selector,
                               uint32_t value)
{
    sapic_select(ioa, selector);
    write_size(ioa, HP_ZX1_IOA_IOWIN, 4, value);
}

static void sapic_program(HPZX1IOARegs *ioa, unsigned int entry,
                          uint32_t low, uint32_t high)
{
    sapic_window_write(ioa, rte_low(entry), low);
    sapic_window_write(ioa, rte_low(entry) + 1, high);
}

static void record_delivery(void *opaque, const HPIOSAPICMessage *message)
{
    DeliveryLog *log = opaque;
    uint64_t low;

    g_assert_cmpuint(log->count, <, G_N_ELEMENTS(log->messages));
    sapic_select(log->ioa, rte_low(log->watch_entry));
    low = read_size(log->ioa, HP_ZX1_IOA_IOWIN, 4);
    log->messages[log->count] = *message;
    log->status_seen[log->count] = low & HP_IO_SAPIC_RTE_STATUS;
    log->count++;
}

static void test_identity_reset_and_masks(void)
{
    HPZX1IOARegs ioa;
    HPZX1IOARegsConfig config = test_config(HP_ZX1_IOA_MODE_PCI);

    g_assert_true(hp_zx1_ioa_regs_init(&ioa, &config));
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_FUNCTION_ID), ==,
                    UINT64_C(0x02b00000122e103c));
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_FUNCTION_CLASS), ==,
                    UINT64_C(0x0000000006000032));
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_CAPABILITIES_POINTER), ==,
                    UINT64_C(0x000000a000000000));
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_PCIX_CAPABILITY), ==,
                    UINT64_C(0x0013ff0000000007));
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_BUS_NUMBER), ==, 0x2f20);
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_STATUS_CONTROL), ==,
                    HP_ZX1_IOA_SIC_RESET_COMPLETE);

    writeq(&ioa, HP_ZX1_IOA_FUNCTION_ID, UINT64_MAX);
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_FUNCTION_ID), ==,
                    UINT64_C(0x02b00146122e103c));
    hp_zx1_ioa_regs_set_pci_status(&ioa, HP_ZX1_IOA_PCI_STATUS_W1C);
    g_assert_cmphex(read_size(&ioa, HP_ZX1_IOA_FUNCTION_ID + 6, 2), ==,
                    HP_ZX1_IOA_PCI_STATUS_RESET |
                    HP_ZX1_IOA_PCI_STATUS_W1C);
    write_size(&ioa, HP_ZX1_IOA_FUNCTION_ID + 6, 2,
               HP_ZX1_IOA_PCI_STATUS_W1C);
    g_assert_cmphex(read_size(&ioa, HP_ZX1_IOA_FUNCTION_ID + 6, 2), ==,
                    HP_ZX1_IOA_PCI_STATUS_RESET);

    write_size(&ioa, HP_ZX1_IOA_FUNCTION_CLASS + 4, 1, 0x10);
    write_size(&ioa, HP_ZX1_IOA_FUNCTION_CLASS + 5, 1, 0x5a);
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_FUNCTION_CLASS), ==,
                    UINT64_C(0x00005a1006000032));

    writeq(&ioa, HP_ZX1_IOA_BUS_NUMBER, UINT64_MAX);
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_BUS_NUMBER), ==, 0xffff);
    hp_zx1_ioa_regs_set_pcix_status(&ioa, UINT32_MAX);
    g_assert_cmphex(ioa.pcix_status & UINT32_C(0x60080000), ==,
                    UINT32_C(0x40080000));
    writeq(&ioa, HP_ZX1_IOA_PCIX_CAPABILITY, UINT64_MAX);
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_PCIX_CAPABILITY), ==,
                    UINT64_C(0x0013ff0000010007));

    g_assert_cmphex(readq(&ioa, 0x0010), ==, 0);
    writeq(&ioa, 0x0010, UINT64_MAX);
    g_assert_cmphex(readq(&ioa, 0x0010), ==, 0);
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_IMPLEMENTED_SIZE), ==, 0);
    g_assert_false(hp_zx1_ioa_regs_read(
                       &ioa, HP_ZX1_IOA_CONFIG_APERTURE_SIZE, 1,
                       &(uint64_t){ 0 }));
    g_assert_false(hp_zx1_ioa_regs_write(&ioa, 0, 3, 0));

    hp_zx1_ioa_regs_reset(&ioa);
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_FUNCTION_ID), ==,
                    UINT64_C(0x02b00000122e103c));
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_BUS_NUMBER), ==, 0x2f20);
}

static void test_modes_ropes_and_reset_straps(void)
{
    HPZX1IOARegs ioa;
    HPZX1IOARegsConfig config = test_config(HP_ZX1_IOA_MODE_PCI);

    g_assert_true(hp_zx1_ioa_regs_init(&ioa, &config));
    g_assert_cmpuint(hp_zx1_ioa_regs_root_count(&ioa), ==, 1);
    g_assert_cmphex(hp_zx1_ioa_regs_rope_mask(&ioa), ==, 1U << 2);
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_BUS_MODE), ==,
                    HP_ZX1_IOA_BUS_MODE_ROPE_2X_L);
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_SLAVE_CONTROL), ==,
                    HP_ZX1_IOA_SLAVE_CONTROL_RESET);
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_ERROR_CONFIGURATION), ==, 0);

    writeq(&ioa, HP_ZX1_IOA_ARBITRATION_MASK, UINT64_MAX);
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_ARBITRATION_MASK), ==, 0x3f);

    writeq(&ioa, HP_ZX1_IOA_BUS_MODE, UINT64_MAX);
    writeq(&ioa, HP_ZX1_IOA_SLAVE_CONTROL, UINT64_MAX);
    writeq(&ioa, HP_ZX1_IOA_ERROR_CONFIGURATION, UINT64_MAX);
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_BUS_MODE), ==,
                    HP_ZX1_IOA_BUS_MODE_ROPE_2X_L |
                    HP_ZX1_IOA_BUS_MODE_SAFE_WRITE);
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_SLAVE_CONTROL), ==,
                    HP_ZX1_IOA_SLAVE_CONTROL_VISIBLE);
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_ERROR_CONFIGURATION), ==, 0x20);
    g_assert_cmphex(ioa.bus_mode & (UINT64_C(1) << 63), !=, 0);
    g_assert_cmphex(ioa.slave_control & (UINT64_C(1) << 31), !=, 0);
    g_assert_cmphex(ioa.error_configuration & (UINT32_C(1) << 31), !=, 0);

    ioa.reset_config = config;
    g_assert_true(hp_zx1_ioa_regs_init(&ioa, &ioa.reset_config));
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_BUS_NUMBER), ==, 0x2f20);

    config.rope_mask = 0;
    g_assert_false(hp_zx1_ioa_regs_init(&ioa, &config));
    config = test_config(HP_ZX1_IOA_MODE_PCI);
    config.rope_mask = 0x07;
    g_assert_false(hp_zx1_ioa_regs_init(&ioa, &config));
    config.rope_mask = 0x05;
    config.bus_mode_reset &= ~HP_ZX1_IOA_BUS_MODE_ROPE_2X_L;
    g_assert_true(hp_zx1_ioa_regs_init(&ioa, &config));
    g_assert_cmpuint(hp_zx1_ioa_regs_root_count(&ioa), ==, 1);
    g_assert_cmphex(hp_zx1_ioa_regs_rope_mask(&ioa), ==, 0x05);

    config = test_config(HP_ZX1_IOA_MODE_PCIX);
    g_assert_true(hp_zx1_ioa_regs_init(&ioa, &config));
    config.bus_mode_reset &= ~HP_ZX1_IOA_BUS_MODE_BUS_MASK;
    g_assert_false(hp_zx1_ioa_regs_init(&ioa, &config));

    config = test_config(HP_ZX1_IOA_MODE_PCI);
    config.bus_mode_reset |= HP_ZX1_IOA_BUS_MODE_SIX_MASTERS;
    g_assert_true(hp_zx1_ioa_regs_init(&ioa, &config));
    writeq(&ioa, HP_ZX1_IOA_ARBITRATION_MASK, UINT64_MAX);
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_ARBITRATION_MASK), ==, 0x7f);

    config = test_config(HP_ZX1_IOA_MODE_AGP);
    g_assert_true(hp_zx1_ioa_regs_init(&ioa, &config));
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_CAPABILITIES_POINTER), ==,
                    UINT64_C(0x0000006000000000));
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_AGP_CAPABILITY), ==,
                    UINT64_C(0x0f00023700200002));
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_PCIX_CAPABILITY), ==, 0);
    g_assert_false(hp_zx1_ioa_regs_set_input(&ioa, 7, true));

    config.rope_mask = 1U << 2;
    config.bus_mode_reset |= HP_ZX1_IOA_BUS_MODE_ROPE_2X_L;
    g_assert_false(hp_zx1_ioa_regs_init(&ioa, &config));
}

static void test_msi_ranges_and_reset_control(void)
{
    HPZX1IOARegs ioa;
    HPZX1IOARegsConfig config = test_config(HP_ZX1_IOA_MODE_AGP);
    uint64_t msi_base = UINT64_MAX;
    uint64_t msi_size = UINT64_MAX;

    g_assert_true(hp_zx1_ioa_regs_init(&ioa, &config));
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_LMMIO_BASE), ==,
                    HP_ZX1_IOA_LMMIO_BASE_RESET);
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_LMMIO_MASK), ==,
                    HP_ZX1_IOA_LMMIO_MASK_RESET);
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_GMMIO_BASE), ==, 0);
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_WLMMIO_BASE), ==,
                    HP_ZX1_IOA_WLMMIO_BASE_RESET);
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_ELMMIO_BASE), ==,
                    HP_ZX1_IOA_ELMMIO_BASE_RESET);
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_MSI_BASE), ==,
                    HP_ZX1_IOA_MSI_BASE_RESET);
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_MSI_MASK), ==,
                    HP_ZX1_IOA_MSI_MASK_RESET);
    g_assert_true(hp_zx1_ioa_regs_msi_range(&ioa, &msi_base, &msi_size));
    g_assert_cmphex(msi_base, ==, UINT64_C(0xfee00000));
    g_assert_cmphex(msi_size, ==, UINT64_C(0x00100000));

    writeq(&ioa, HP_ZX1_IOA_LMMIO_BASE, UINT64_MAX);
    writeq(&ioa, HP_ZX1_IOA_LMMIO_MASK, UINT64_MAX);
    writeq(&ioa, HP_ZX1_IOA_GMMIO_BASE, UINT64_MAX);
    writeq(&ioa, HP_ZX1_IOA_GMMIO_MASK, UINT64_MAX);
    writeq(&ioa, HP_ZX1_IOA_WLMMIO_BASE, UINT64_MAX);
    writeq(&ioa, HP_ZX1_IOA_WLMMIO_MASK, UINT64_MAX);
    writeq(&ioa, HP_ZX1_IOA_WGMMIO_BASE, UINT64_MAX);
    writeq(&ioa, HP_ZX1_IOA_WGMMIO_MASK, UINT64_MAX);
    writeq(&ioa, HP_ZX1_IOA_ELMMIO_BASE, UINT64_MAX);
    writeq(&ioa, HP_ZX1_IOA_ELMMIO_MASK, UINT64_MAX);
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_LMMIO_BASE), ==,
                    HP_ZX1_IOA_LMMIO_BASE_RESET |
                    HP_ZX1_IOA_LMMIO_BASE_WRITABLE);
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_LMMIO_MASK), ==,
                    HP_ZX1_IOA_LMMIO_MASK_RESET |
                    HP_ZX1_IOA_LMMIO_MASK_WRITABLE);
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_GMMIO_BASE), ==,
                    HP_ZX1_IOA_GMMIO_BASE_WRITABLE);
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_GMMIO_MASK), ==,
                    HP_ZX1_IOA_GMMIO_MASK_WRITABLE);
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_WLMMIO_BASE), ==,
                    HP_ZX1_IOA_WLMMIO_BASE_RESET |
                    HP_ZX1_IOA_WLMMIO_BASE_WRITABLE);
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_WLMMIO_MASK), ==,
                    HP_ZX1_IOA_WLMMIO_MASK_RESET |
                    HP_ZX1_IOA_WLMMIO_MASK_WRITABLE);
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_WGMMIO_BASE), ==,
                    HP_ZX1_IOA_WGMMIO_BASE_WRITABLE);
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_WGMMIO_MASK), ==,
                    HP_ZX1_IOA_WGMMIO_MASK_WRITABLE);
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_ELMMIO_BASE), ==,
                    HP_ZX1_IOA_ELMMIO_BASE_RESET |
                    HP_ZX1_IOA_ELMMIO_BASE_WRITABLE);
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_ELMMIO_MASK), ==,
                    HP_ZX1_IOA_ELMMIO_MASK_RESET |
                    HP_ZX1_IOA_ELMMIO_MASK_WRITABLE);

    g_assert_true(hp_zx1_ioa_regs_msi_contains(&ioa, 0xfee01234));
    g_assert_true(hp_zx1_ioa_regs_msi_contains(&ioa, 0xfee10000));
    g_assert_true(hp_zx1_ioa_regs_msi_contains(&ioa, 0xfeefffff));
    g_assert_false(hp_zx1_ioa_regs_msi_contains(&ioa, 0xfef00000));
    g_assert_false(hp_zx1_ioa_regs_msi_contains(
                       &ioa, (UINT64_C(1) << 44) | 0xfee00000));
    writeq(&ioa, HP_ZX1_IOA_MSI_BASE, 0);
    g_assert_false(hp_zx1_ioa_regs_msi_contains(&ioa, 0xfee00000));
    msi_base = UINT64_MAX;
    msi_size = UINT64_MAX;
    g_assert_false(hp_zx1_ioa_regs_msi_range(&ioa, &msi_base, &msi_size));
    g_assert_cmphex(msi_base, ==, UINT64_MAX);
    g_assert_cmphex(msi_size, ==, UINT64_MAX);
    writeq(&ioa, HP_ZX1_IOA_MSI_BASE, UINT64_MAX);
    writeq(&ioa, HP_ZX1_IOA_MSI_MASK, UINT64_MAX);
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_MSI_BASE), ==,
                    HP_ZX1_IOA_MSI_BASE_WRITABLE);
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_MSI_MASK), ==,
                    HP_ZX1_IOA_MSI_MASK_WRITABLE);
    g_assert_true(hp_zx1_ioa_regs_msi_range(&ioa, &msi_base, &msi_size));
    g_assert_cmphex(msi_base, ==, UINT64_C(0x00000fffffff0000));
    g_assert_cmphex(msi_size, ==, UINT64_C(0x10000));

    /* Noncontiguous masks and an enabled range below 2 GiB decode nothing. */
    writeq(&ioa, HP_ZX1_IOA_MSI_BASE, UINT64_C(0xfee00001));
    writeq(&ioa, HP_ZX1_IOA_MSI_MASK, UINT64_C(0x00000fffffef0000));
    g_assert_false(hp_zx1_ioa_regs_msi_range(&ioa, &msi_base, &msi_size));
    g_assert_false(hp_zx1_ioa_regs_msi_contains(&ioa, 0xfee00000));
    writeq(&ioa, HP_ZX1_IOA_MSI_BASE, UINT64_C(0x40000001));
    writeq(&ioa, HP_ZX1_IOA_MSI_MASK, UINT64_C(0x00000ffffff00000));
    g_assert_false(hp_zx1_ioa_regs_msi_range(&ioa, &msi_base, &msi_size));

    writeq(&ioa, HP_ZX1_IOA_AGP_COMMAND, UINT64_MAX);
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_AGP_COMMAND), ==, 0x337);
    write_size(&ioa, HP_ZX1_IOA_STATUS_CONTROL, 1, 0);
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_STATUS_CONTROL), ==, 0);
    writeq(&ioa, HP_ZX1_IOA_AGP_COMMAND, UINT64_MAX);
    write_size(&ioa, HP_ZX1_IOA_STATUS_CONTROL, 1,
               HP_ZX1_IOA_SIC_RESET_FUNCTION |
               HP_ZX1_IOA_SIC_FORWARD_VGA |
               HP_ZX1_IOA_SIC_CLEAR_ENABLE |
               HP_ZX1_IOA_SIC_HARD_FAIL);
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_AGP_COMMAND), ==, 0);
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_STATUS_CONTROL), ==,
                    HP_ZX1_IOA_SIC_FORWARD_VGA |
                    HP_ZX1_IOA_SIC_CLEAR_ENABLE |
                    HP_ZX1_IOA_SIC_HARD_FAIL |
                    HP_ZX1_IOA_SIC_RESET_COMPLETE);

    /* CL uses the previous CE; CE=CL=1 is deterministic no-clear policy. */
    write_size(&ioa, HP_ZX1_IOA_STATUS_CONTROL, 1,
               HP_ZX1_IOA_SIC_CLEAR_ENABLE);
    write_size(&ioa, HP_ZX1_IOA_STATUS_CONTROL, 1,
               HP_ZX1_IOA_SIC_CLEAR_LOG);
    g_assert_cmphex(read_size(&ioa, HP_ZX1_IOA_STATUS_CONTROL, 1), ==,
                    HP_ZX1_IOA_SIC_CLEAR_LOG);
    write_size(&ioa, HP_ZX1_IOA_STATUS_CONTROL, 1,
               HP_ZX1_IOA_SIC_CLEAR_LOG);
    g_assert_cmphex(read_size(&ioa, HP_ZX1_IOA_STATUS_CONTROL, 1), ==, 0);
    write_size(&ioa, HP_ZX1_IOA_STATUS_CONTROL, 1,
               HP_ZX1_IOA_SIC_CLEAR_ENABLE |
               HP_ZX1_IOA_SIC_CLEAR_LOG);
    g_assert_cmphex(read_size(&ioa, HP_ZX1_IOA_STATUS_CONTROL, 1), ==,
                    HP_ZX1_IOA_SIC_CLEAR_ENABLE);
}

static void test_configuration_cycles(void)
{
    HPZX1IOARegs ioa;
    ConfigLog log = {
        .read_value = UINT32_C(0x44332211),
        .respond = true,
    };
    HPZX1IOARegsConfig config = test_config(HP_ZX1_IOA_MODE_PCI);
    uint32_t selector = (2U << 16) | (3U << 11) | (4U << 8) | 0xfc;

    config.config_read = config_read;
    config.config_write = config_write;
    config.config_opaque = &log;
    g_assert_true(hp_zx1_ioa_regs_init(&ioa, &config));

    writeq(&ioa, HP_ZX1_IOA_CONFIG_ADDRESS,
           UINT64_C(0xff000000) | selector | 3);
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_CONFIG_ADDRESS), ==, selector);
    g_assert_cmphex(read_size(&ioa, HP_ZX1_IOA_CONFIG_DATA, 4), ==,
                    UINT32_C(0x44332211));
    g_assert_cmphex(log.address, ==, selector);
    g_assert_cmpuint(log.size, ==, 4);
    g_assert_cmpuint(log.reads, ==, 1);

    g_assert_cmphex(read_size(&ioa, HP_ZX1_IOA_CONFIG_DATA + 1, 1), ==,
                    0x11);
    g_assert_cmphex(log.address, ==, selector + 1);
    g_assert_cmpuint(log.size, ==, 1);
    g_assert_cmphex(read_size(&ioa, HP_ZX1_IOA_CONFIG_DATA + 2, 2), ==,
                    0x2211);
    g_assert_cmphex(log.address, ==, selector + 2);
    g_assert_cmpuint(log.size, ==, 2);

    write_size(&ioa, HP_ZX1_IOA_CONFIG_DATA + 3, 1, 0x7e);
    g_assert_cmphex(log.address, ==, selector + 3);
    g_assert_cmpuint(log.size, ==, 1);
    g_assert_cmphex(log.value, ==, 0x7e);

    /* Bus zero/device sixteen has no type-0 IDSEL and never calls out. */
    writeq(&ioa, HP_ZX1_IOA_CONFIG_ADDRESS, 16U << 11);
    g_assert_cmphex(read_size(&ioa, HP_ZX1_IOA_CONFIG_DATA, 2), ==, 0xffff);
    g_assert_cmpuint(log.reads, ==, 3);
    write_size(&ioa, HP_ZX1_IOA_CONFIG_DATA, 4, UINT32_MAX);
    g_assert_cmpuint(log.writes, ==, 1);

    /* The same device number is legal on a type-1 (nonzero bus) cycle. */
    writeq(&ioa, HP_ZX1_IOA_CONFIG_ADDRESS, (1U << 16) | (16U << 11));
    g_assert_cmphex(read_size(&ioa, HP_ZX1_IOA_CONFIG_DATA, 1), ==, 0x11);
    g_assert_cmpuint(log.reads, ==, 4);

    log.respond = false;
    g_assert_cmphex(read_size(&ioa, HP_ZX1_IOA_CONFIG_DATA, 4), ==,
                    UINT32_MAX);
    g_assert_cmphex(read_size(&ioa, HP_ZX1_IOA_CONFIG_DATA + 4, 4), ==, 0);
    g_assert_cmpuint(log.reads, ==, 5);
}

static void test_configuration_abort_no_mca(void)
{
    const struct {
        uint32_t selector;
        uint32_t bus_address;
    } probes[] = {
        { 0, 0x10000 },                        /* Empty type-0 slot. */
        { (1U << 11) | (3U << 8), 0x20300 },    /* Absent function. */
        { 16U << 11, 0 },                      /* No type-0 IDSEL. */
        { (2U << 16) | (16U << 11), 0x28001 },  /* Empty type-1 slot. */
    };
    HPZX1IOARegs ioa;
    HPZX1IOARegsConfig config = test_config(HP_ZX1_IOA_MODE_PCI);
    ConfigLog config_log = { .respond = false };
    FaultLog fault_log = { 0 };
    unsigned int i;

    config.config_read = config_read;
    config.config_write = config_write;
    config.config_opaque = &config_log;
    config.fault_notify = record_fault;
    config.fault_opaque = &fault_log;
    g_assert_true(hp_zx1_ioa_regs_init(&ioa, &config));

    for (i = 0; i < G_N_ELEMENTS(probes); i++) {
        clear_error_log(&ioa);
        writeq(&ioa, HP_ZX1_IOA_CONFIG_ADDRESS, probes[i].selector);
        g_assert_cmphex(read_size(&ioa, HP_ZX1_IOA_CONFIG_DATA, 4), ==,
                        UINT32_MAX);
        g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_ERROR_STATUS), ==,
                        0x40c); /* UNC, master-abort log code. */
        g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_OUTBOUND_ERROR_ADDRESS), ==,
                        UINT64_C(0x4000000000000000) | probes[i].bus_address);
        g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_ERROR_MASTER_ID), ==, 0);
        g_assert_cmphex(read_size(&ioa, HP_ZX1_IOA_FUNCTION_ID + 6, 2) & 0x2000,
                        ==, 0x2000);
        g_assert_cmpuint(fault_log.count, ==, 0);

        write_size(&ioa, HP_ZX1_IOA_CONFIG_DATA, 4, UINT32_MAX);
        g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_ERROR_STATUS), ==,
                        UINT64_C(0x4000000042c)); /* UNC_OV, OV, UNC, code. */
        g_assert_cmpuint(fault_log.count, ==, 0);
    }

    /* A prior configuration abort must not mask a later real fault. */
    hp_zx1_ioa_regs_report_fault(&ioa, HP_ZX1_IOA_FAULT_MSI_DECODE,
                                 UINT64_C(0xfee00000), 0x55);
    g_assert_cmpuint(fault_log.count, ==, 1);
    g_assert_cmpuint(fault_log.last.reason, ==, IA64_CHIPSET_FAULT_DECODE);
    g_assert_cmphex(fault_log.last.address, ==, UINT64_C(0xfee00000));
    g_assert_cmphex(fault_log.last.information, ==, 0x55);
    g_assert_cmphex(fault_log.last.status, ==, 0);
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_ERROR_STATUS), ==,
                    UINT64_C(0x4000000042c));

    clear_error_log(&ioa);
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_ERROR_STATUS), ==, 0);
    hp_zx1_ioa_regs_report_fault(&ioa, HP_ZX1_IOA_FAULT_CSR_DECODE,
                                 UINT64_C(0xfff8), 0);
    g_assert_cmpuint(fault_log.count, ==, 2);
    g_assert_cmpuint(fault_log.last.reason, ==, IA64_CHIPSET_FAULT_DECODE);
    g_assert_cmphex(fault_log.last.address, ==, UINT64_C(0xfff8));
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_ERROR_STATUS), ==, 0);
}

static void test_error_log_clear(void)
{
    HPZX1IOARegs ioa;
    HPZX1IOARegsConfig config = test_config(HP_ZX1_IOA_MODE_PCI);
    uint64_t status;
    uint64_t address;

    g_assert_true(hp_zx1_ioa_regs_init(&ioa, &config));
    writeq(&ioa, HP_ZX1_IOA_CONFIG_ADDRESS, 0x800);
    read_size(&ioa, HP_ZX1_IOA_CONFIG_DATA + 3, 1);
    status = readq(&ioa, HP_ZX1_IOA_ERROR_STATUS);
    address = readq(&ioa, HP_ZX1_IOA_OUTBOUND_ERROR_ADDRESS);
    g_assert_cmphex(status, ==, 0x40c);
    g_assert_cmphex(address, ==, UINT64_C(0x4000000000020000));

    /* Status and address are read-only, including subword writes. */
    writeq(&ioa, HP_ZX1_IOA_ERROR_STATUS, UINT64_MAX);
    write_size(&ioa, HP_ZX1_IOA_ERROR_STATUS + 1, 1, 0xff);
    writeq(&ioa, HP_ZX1_IOA_OUTBOUND_ERROR_ADDRESS, UINT64_MAX);
    writeq(&ioa, HP_ZX1_IOA_ERROR_MASTER_ID, UINT64_MAX);
    writeq(&ioa, 0x698, UINT64_MAX); /* Reserved, not an error-data register. */
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_ERROR_STATUS), ==, status);
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_OUTBOUND_ERROR_ADDRESS), ==,
                    address);
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_ERROR_MASTER_ID), ==, 0);
    g_assert_cmphex(readq(&ioa, 0x698), ==, 0);

    /* CE=0 prevents CL from discarding an unread error. */
    writeq(&ioa, HP_ZX1_IOA_STATUS_CONTROL, HP_ZX1_IOA_SIC_CLEAR_LOG);
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_ERROR_STATUS), ==, status);
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_STATUS_CONTROL), ==, 0);

    /* A new error between arming CE and writing CL cancels the clear. */
    writeq(&ioa, HP_ZX1_IOA_STATUS_CONTROL, HP_ZX1_IOA_SIC_CLEAR_ENABLE);
    writeq(&ioa, HP_ZX1_IOA_CONFIG_ADDRESS, 0x1000);
    read_size(&ioa, HP_ZX1_IOA_CONFIG_DATA, 4);
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_STATUS_CONTROL), ==, 0);
    writeq(&ioa, HP_ZX1_IOA_STATUS_CONTROL, HP_ZX1_IOA_SIC_CLEAR_LOG);
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_ERROR_STATUS), ==,
                    UINT64_C(0x4000000042c));
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_OUTBOUND_ERROR_ADDRESS), ==,
                    address);

    clear_error_log(&ioa);
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_ERROR_STATUS), ==, 0);
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_OUTBOUND_ERROR_ADDRESS), ==, 0);
    /* PCI Status has its own W1C mechanism, independent of CE/CL. */
    g_assert_cmphex(read_size(&ioa, HP_ZX1_IOA_FUNCTION_ID + 6, 2) & 0x2000,
                    ==, 0x2000);
    write_size(&ioa, HP_ZX1_IOA_FUNCTION_ID + 6, 2, 0x2000);
    g_assert_cmphex(read_size(&ioa, HP_ZX1_IOA_FUNCTION_ID + 6, 2) & 0x2000,
                    ==, 0);
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_STATUS_CONTROL), ==,
                    HP_ZX1_IOA_SIC_CLEAR_LOG);
    read_size(&ioa, HP_ZX1_IOA_CONFIG_DATA, 4);
    write_size(&ioa, HP_ZX1_IOA_STATUS_CONTROL + 4, 4, 0);
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_ERROR_STATUS), ==, 0x40c);

    /* Writing CE=CL=1 leaves the error log unchanged. */
    writeq(&ioa, HP_ZX1_IOA_STATUS_CONTROL,
           HP_ZX1_IOA_SIC_CLEAR_ENABLE | HP_ZX1_IOA_SIC_CLEAR_LOG);
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_ERROR_STATUS), ==, 0x40c);
    clear_error_log(&ioa);

    /* Log the control bits at the first error, not their current values. */
    writeq(&ioa, HP_ZX1_IOA_ERROR_CONFIGURATION, 0x20);
    writeq(&ioa, HP_ZX1_IOA_STATUS_CONTROL, HP_ZX1_IOA_SIC_HARD_FAIL);
    hp_zx1_ioa_regs_report_fault(&ioa, HP_ZX1_IOA_FAULT_CONFIG_ABORT, 0, 0);
    writeq(&ioa, HP_ZX1_IOA_ERROR_CONFIGURATION, 0);
    writeq(&ioa, HP_ZX1_IOA_STATUS_CONTROL, 0);
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_ERROR_STATUS), ==,
                    UINT64_C(0x30000040c));
    hp_zx1_ioa_regs_reset(&ioa);
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_ERROR_STATUS), ==, 0);
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_OUTBOUND_ERROR_ADDRESS), ==, 0);
}

static void test_error_log_priority(void)
{
    HPZX1IOARegs ioa;
    HPZX1IOARegsConfig config = test_config(HP_ZX1_IOA_MODE_PCI);

    g_assert_true(hp_zx1_ioa_regs_init(&ioa, &config));
    /* Seed other error classes, as when restoring an existing hardware log. */
    ioa.error_status = 0x801; /* Corrected error. */
    ioa.outbound_error_address = 0x1000;
    hp_zx1_ioa_regs_report_fault(&ioa, HP_ZX1_IOA_FAULT_CONFIG_ABORT, 0, 0);
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_ERROR_STATUS), ==, 0xc0c);
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_OUTBOUND_ERROR_ADDRESS), ==,
                    UINT64_C(0x4000000000010000));

    ioa.error_status = 0x21f; /* One fatal SERR, no overflow. */
    ioa.outbound_error_address = 0x2000;
    hp_zx1_ioa_regs_report_fault(&ioa, HP_ZX1_IOA_FAULT_CONFIG_ABORT, 0, 0);
    hp_zx1_ioa_regs_report_fault(&ioa, HP_ZX1_IOA_FAULT_CONFIG_ABORT, 0, 0);
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_ERROR_STATUS), ==,
                    UINT64_C(0x4000000061f));
    g_assert_cmphex(readq(&ioa, HP_ZX1_IOA_OUTBOUND_ERROR_ADDRESS), ==, 0x2000);
}

static void test_sapic_registers_edge_and_id_eid(void)
{
    HPZX1IOARegs ioa;
    HPZX1IOARegsConfig config = test_config(HP_ZX1_IOA_MODE_PCI);
    DeliveryLog log = { .ioa = &ioa, .watch_entry = 0 };

    config.deliver = record_delivery;
    config.delivery_opaque = &log;
    g_assert_true(hp_zx1_ioa_regs_init(&ioa, &config));

    write_size(&ioa, HP_ZX1_IOA_IOREGSEL, 4, 0x1234);
    g_assert_cmphex(read_size(&ioa, HP_ZX1_IOA_IOREGSEL, 4), ==, 0x34);
    g_assert_cmphex(sapic_window_read(&ioa, 1), ==, 0x000a0020);
    g_assert_cmphex(sapic_window_read(&ioa, 2), ==, 0);
    sapic_window_write(&ioa, 2, UINT32_MAX);
    g_assert_cmphex(sapic_window_read(&ioa, 2), ==, 0);
    g_assert_false(hp_zx1_ioa_regs_read(
                       &ioa, HP_ZX1_IOA_IOWIN, 8, &(uint64_t){ 0 }));

    sapic_program(&ioa, 0, HP_IO_SAPIC_RTE_MASK, UINT32_MAX);
    g_assert_cmphex(sapic_window_read(&ioa, rte_low(0) + 1), ==,
                    UINT32_C(0xffff0000));
    g_assert_false(hp_zx1_ioa_regs_set_input(&ioa, 0, true));
    g_assert_cmpuint(log.count, ==, 0);

    /* Unmasking does not resurrect an edge that occurred while masked. */
    sapic_window_write(&ioa, rte_low(0), 0x5a);
    g_assert_cmpuint(log.count, ==, 0);
    g_assert_false(hp_zx1_ioa_regs_set_input(&ioa, 0, false));
    sapic_select(&ioa, rte_low(0));
    g_assert_true(hp_zx1_ioa_regs_set_input(&ioa, 0, true));
    g_assert_cmpuint(log.count, ==, 1);
    g_assert_true(log.status_seen[0]);
    g_assert_cmphex(log.messages[0].address, ==, UINT64_C(0xfeeffff0));
    g_assert_cmphex(log.messages[0].data, ==, UINT32_C(0x0000405a));
    g_assert_cmphex(sapic_window_read(&ioa, rte_low(0)) &
                    HP_IO_SAPIC_RTE_STATUS, ==, 0);
    g_assert_false(hp_zx1_ioa_regs_set_input(&ioa, 0, true));
    g_assert_cmpuint(log.count, ==, 1);

    /* Program destination ID=0x12/EID=0x34 and redirect hint. */
    g_assert_false(hp_zx1_ioa_regs_set_input(&ioa, 0, false));
    sapic_program(&ioa, 0, 0x100 | 0xab, UINT32_C(0x12340000));
    sapic_select(&ioa, rte_low(0));
    g_assert_true(hp_zx1_ioa_regs_set_input(&ioa, 0, true));
    g_assert_cmpuint(log.count, ==, 2);
    g_assert_cmphex(log.messages[1].address, ==, UINT64_C(0xfee12348));
    g_assert_cmphex(log.messages[1].data, ==, UINT32_C(0x000041ab));

    sapic_window_write(&ioa, rte_low(0), UINT32_MAX);
    g_assert_cmphex(sapic_window_read(&ioa, rte_low(0)), ==,
                    UINT32_C(0x0001a7ff));
}

static void test_sapic_level_mask_eoi_and_software(void)
{
    HPZX1IOARegs ioa;
    HPZX1IOARegsConfig config = test_config(HP_ZX1_IOA_MODE_PCI);
    DeliveryLog log = { .ioa = &ioa, .watch_entry = 3 };
    uint32_t low = HP_IO_SAPIC_RTE_TRIGGER | 0x55;

    config.deliver = record_delivery;
    config.delivery_opaque = &log;
    g_assert_true(hp_zx1_ioa_regs_init(&ioa, &config));

    /* A masked asserted level is delivered immediately when unmasked. */
    sapic_program(&ioa, 3, low | HP_IO_SAPIC_RTE_MASK,
                  UINT32_C(0x01020000));
    g_assert_false(hp_zx1_ioa_regs_set_input(&ioa, 3, true));
    sapic_window_write(&ioa, rte_low(3), low);
    g_assert_cmpuint(log.count, ==, 1);
    g_assert_true(log.status_seen[0]);
    g_assert_cmphex(ioa.sapic_in_service, ==, 1U << 3);
    g_assert_false(hp_zx1_ioa_regs_set_input(&ioa, 3, true));

    write_size(&ioa, HP_ZX1_IOA_IOEOI, 4, 0x54);
    g_assert_cmpuint(log.count, ==, 1);
    write_size(&ioa, HP_ZX1_IOA_IOEOI, 4, 0x55);
    g_assert_cmpuint(log.count, ==, 2);
    g_assert_true(log.status_seen[1]);
    g_assert_cmphex(ioa.sapic_in_service, ==, 1U << 3);

    sapic_window_write(&ioa, rte_low(3), low | HP_IO_SAPIC_RTE_MASK);
    write_size(&ioa, HP_ZX1_IOA_IOEOI, 4, 0x55);
    g_assert_cmphex(ioa.sapic_in_service, ==, 0);
    g_assert_cmpuint(log.count, ==, 2);
    sapic_window_write(&ioa, rte_low(3), low);
    g_assert_cmpuint(log.count, ==, 3);
    g_assert_false(hp_zx1_ioa_regs_set_input(&ioa, 3, false));
    write_size(&ioa, HP_ZX1_IOA_IOEOI, 4, 0x55);
    g_assert_cmphex(ioa.sapic_in_service, ==, 0);
    g_assert_cmpuint(log.count, ==, 3);

    log.watch_entry = 10;
    sapic_program(&ioa, 10, 0x66, UINT32_C(0xabcd0000));
    sapic_select(&ioa, rte_low(10));
    write_size(&ioa, HP_ZX1_IOA_SOFTWARE_INTERRUPT, 4, UINT32_MAX);
    g_assert_cmpuint(log.count, ==, 4);
    g_assert_true(log.status_seen[3]);
    g_assert_cmphex(log.messages[3].address, ==, UINT64_C(0xfeeabcD0));
    g_assert_cmphex(log.messages[3].data, ==, UINT32_C(0x00004066));
    g_assert_cmphex(sapic_window_read(&ioa, rte_low(10)) &
                    HP_IO_SAPIC_RTE_STATUS, ==, 0);

    sapic_window_write(&ioa, rte_low(10),
                       HP_IO_SAPIC_RTE_MASK | 0x66);
    write_size(&ioa, HP_ZX1_IOA_SOFTWARE_INTERRUPT, 4, 0);
    g_assert_cmpuint(log.count, ==, 4);
    sapic_window_write(&ioa, rte_low(10),
                       HP_IO_SAPIC_RTE_POLARITY | 0x66);
    write_size(&ioa, HP_ZX1_IOA_SOFTWARE_INTERRUPT, 4, 0);
    g_assert_cmpuint(log.count, ==, 4);
    g_assert_cmphex(read_size(&ioa, HP_ZX1_IOA_SOFTWARE_INTERRUPT, 4), ==,
                    0);

    /* Function reset preserves RTE fields but restores every mask bit. */
    sapic_window_write(&ioa, rte_low(3), low);
    sapic_window_write(&ioa, rte_low(10), 0x66);
    log.watch_entry = 3;
    g_assert_true(hp_zx1_ioa_regs_set_input(&ioa, 3, true));
    g_assert_cmpuint(log.count, ==, 5);
    g_assert_cmphex(ioa.sapic_in_service, ==, 1U << 3);
    write_size(&ioa, HP_ZX1_IOA_STATUS_CONTROL, 1,
               HP_ZX1_IOA_SIC_RESET_FUNCTION);
    g_assert_cmphex(sapic_window_read(&ioa, rte_low(3)), ==,
                    low | HP_IO_SAPIC_RTE_MASK);
    g_assert_cmphex(sapic_window_read(&ioa, rte_low(10)), ==,
                    0x66 | HP_IO_SAPIC_RTE_MASK);
    g_assert_cmphex(ioa.sapic_in_service, ==, 0);
    g_assert_false(hp_zx1_ioa_regs_set_input(&ioa, 3, false));
    g_assert_false(hp_zx1_ioa_regs_set_input(&ioa, 3, true));
    write_size(&ioa, HP_ZX1_IOA_SOFTWARE_INTERRUPT, 4, 0);
    g_assert_cmpuint(log.count, ==, 5);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/hp-zx1-ioa/identity-reset-and-masks",
                    test_identity_reset_and_masks);
    g_test_add_func("/hp-zx1-ioa/modes-ropes-and-reset-straps",
                    test_modes_ropes_and_reset_straps);
    g_test_add_func("/hp-zx1-ioa/msi-ranges-and-reset-control",
                    test_msi_ranges_and_reset_control);
    g_test_add_func("/hp-zx1-ioa/configuration-cycles",
                    test_configuration_cycles);
    g_test_add_func("/hp-zx1-ioa/configuration-abort-no-mca",
                    test_configuration_abort_no_mca);
    g_test_add_func("/hp-zx1-ioa/error-log-clear", test_error_log_clear);
    g_test_add_func("/hp-zx1-ioa/error-log-priority", test_error_log_priority);
    g_test_add_func("/hp-zx1-ioa/sapic-registers-edge-and-id-eid",
                    test_sapic_registers_edge_and_id_eid);
    g_test_add_func("/hp-zx1-ioa/sapic-level-mask-eoi-and-software",
                    test_sapic_level_mask_eoi_and_software);
    return g_test_run();
}
