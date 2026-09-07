/*
 * Host-side tests for the freestanding IA-64 firmware UART policy.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "fw-uart-policy.h"
#include "dsdt-i2000.h"
#include "ssdt-platform-devices.h"

#define TEST_LEGACY_IO_BASE 0x0000000100000000ULL
#define TEST_LEGACY_IO_SIZE 0x0000000004000000ULL

static int test_i2000_policy(void)
{
    FW_UART_POLICY policy;

    if (!fw_i2000_uart_policy_init(
            TEST_LEGACY_IO_BASE, TEST_LEGACY_IO_SIZE,
            IA64_I2000_PROFILE_UART_PORT, IA64_I2000_PROFILE_UART_SIZE,
            IA64_I2000_PROFILE_REQUIRED_FLAGS, &policy) ||
        policy.LogicalPort != 0x03f8U || policy.RegisterCount != 8U ||
        policy.DevicePathKind != FW_UART_DEVICE_PATH_ACPI_PNP0501 ||
        policy.DevicePathHid != 0x050141d0U || policy.DevicePathUid != 0 ||
        policy.HcdpSpaceId != FW_UART_HCDP_GAS_SYSTEM_IO ||
        policy.HcdpAddress != 0x03f8U ||
        policy.HcdpGlobalInterrupt != 0 ||
        policy.HcdpFlags != FW_UART_HCDP_FLAG_PRIMARY_CONSOLE ||
        policy.HcdpConOutIndex != 0 ||
        policy.HcdpAcpiHid != 0x0105d041U) {
        return 1;
    }
    return 0;
}

static int test_sparse_register_addresses(void)
{
    static const struct {
        UINTN reg;
        UINT64 offset;
    } cases[] = {
        { 0, 0x000fe3f8ULL },
        { 3, 0x000fe3fbULL },
        { 4, 0x000ff3fcULL },
        { 7, 0x000ff3ffULL },
    };
    FW_UART_POLICY policy;
    UINT64 address[4];
    UINTN i;

    if (!fw_i2000_uart_policy_init(
            TEST_LEGACY_IO_BASE, TEST_LEGACY_IO_SIZE,
            IA64_I2000_PROFILE_UART_PORT, IA64_I2000_PROFILE_UART_SIZE,
            IA64_I2000_PROFILE_REQUIRED_FLAGS, &policy)) {
        return 1;
    }
    for (i = 0; i < FW_ARRAY_SIZE(cases); i++) {
        if (!fw_uart_policy_reg_address(&policy, cases[i].reg,
                                        &address[i]) ||
            address[i] != TEST_LEGACY_IO_BASE + cases[i].offset) {
            return 1;
        }
    }
    if (address[2] == address[0] + 4U ||
        fw_uart_policy_reg_address(&policy, 8, &address[0])) {
        return 1;
    }
    return 0;
}

static int test_policy_rejections(void)
{
    FW_UART_POLICY policy;
    UINT64 address;

    if (fw_i2000_uart_policy_init(
            TEST_LEGACY_IO_BASE, TEST_LEGACY_IO_SIZE,
            IA64_I2000_PROFILE_UART_PORT, IA64_I2000_PROFILE_UART_SIZE,
            IA64_I2000_PROFILE_REQUIRED_FLAGS, NULL) ||
        fw_i2000_uart_policy_init(
            TEST_LEGACY_IO_BASE, TEST_LEGACY_IO_SIZE,
            IA64_I2000_PROFILE_UART_PORT, IA64_I2000_PROFILE_UART_SIZE - 1U,
            IA64_I2000_PROFILE_REQUIRED_FLAGS, &policy) ||
        fw_i2000_uart_policy_init(
            TEST_LEGACY_IO_BASE, TEST_LEGACY_IO_SIZE,
            IA64_I2000_PROFILE_UART_PORT + 1U, IA64_I2000_PROFILE_UART_SIZE,
            IA64_I2000_PROFILE_REQUIRED_FLAGS, &policy) ||
        fw_i2000_uart_policy_init(
            TEST_LEGACY_IO_BASE, TEST_LEGACY_IO_SIZE,
            IA64_I2000_PROFILE_UART_PORT, IA64_I2000_PROFILE_UART_SIZE,
            IA64_I2000_PROFILE_REQUIRED_FLAGS &
                ~IA64_I2000_PROFILE_FLAG_CONSOLE_POLL_ONLY,
            &policy) ||
        fw_i2000_uart_policy_init(
            TEST_LEGACY_IO_BASE, 0x000ff3ffULL,
            IA64_I2000_PROFILE_UART_PORT, IA64_I2000_PROFILE_UART_SIZE,
            IA64_I2000_PROFILE_REQUIRED_FLAGS, &policy) ||
        fw_i2000_uart_policy_init(
            ~(UINT64)0 - TEST_LEGACY_IO_SIZE + 1U,
            TEST_LEGACY_IO_SIZE, IA64_I2000_PROFILE_UART_PORT,
            IA64_I2000_PROFILE_UART_SIZE,
            IA64_I2000_PROFILE_REQUIRED_FLAGS, &policy)) {
        return 1;
    }

    if (!fw_i2000_uart_policy_init(
            TEST_LEGACY_IO_BASE, TEST_LEGACY_IO_SIZE,
            IA64_I2000_PROFILE_UART_PORT, IA64_I2000_PROFILE_UART_SIZE,
            IA64_I2000_PROFILE_REQUIRED_FLAGS, &policy)) {
        return 1;
    }
    if (fw_uart_policy_reg_address(NULL, 0, &address) ||
        fw_uart_policy_reg_address(&policy, 0, NULL)) {
        return 1;
    }

    policy.LogicalPort = 0xfffcU;
    if (fw_uart_policy_reg_address(&policy, 7, &address)) {
        return 1;
    }
    policy.LogicalPort = IA64_I2000_PROFILE_UART_PORT;
    policy.LegacyIoBase = ~(UINT64)0 -
        IA64_LEGACY_IO_PORT_OFFSET(IA64_I2000_PROFILE_UART_PORT) + 1U;
    if (fw_uart_policy_reg_address(&policy, 0, &address)) {
        return 1;
    }
    return 0;
}

static UINTN byte_sequence_offset(const UINT8 *haystack, UINTN haystack_size,
                                  const UINT8 *needle, UINTN needle_size)
{
    UINTN i;
    UINTN j;

    if (needle_size == 0 || needle_size > haystack_size) {
        return ~(UINTN)0;
    }
    for (i = 0; i <= haystack_size - needle_size; i++) {
        for (j = 0; j < needle_size; j++) {
            if (haystack[i + j] != needle[j]) {
                break;
            }
        }
        if (j == needle_size) {
            return i;
        }
    }
    return ~(UINTN)0;
}

static BOOLEAN byte_sequence_present(const UINT8 *haystack, UINTN haystack_size,
                                     const UINT8 *needle, UINTN needle_size)
{
    return byte_sequence_offset(haystack, haystack_size,
                                needle, needle_size) != ~(UINTN)0;
}

static int test_i2000_dsdt_contract(void)
{
    static const UINT8 pci_hid[] = {
        0x08, '_', 'H', 'I', 'D', 0x0d,
        'P', 'N', 'P', '0', 'A', '0', '3', 0x00,
    };
    static const UINT8 pci0[] = { 'P', 'C', 'I', '0' };
    static const UINT8 pci1[] = { 'P', 'C', 'I', '1' };
    static const UINT8 pci2[] = { 'P', 'C', 'I', '2' };
    static const UINT8 pci3[] = { 'P', 'C', 'I', '3' };
    static const UINT8 ifb0[] = { 'I', 'F', 'B', '0' };
    static const UINT8 ps2k[] = { 'P', 'S', '2', 'K' };
    static const UINT8 ps2m[] = { 'P', 'S', '2', 'M' };
    static const UINT8 ifb_address[] = {
        0x08, '_', 'A', 'D', 'R', 0x0c, 0x00, 0x00, 0x03, 0x00,
    };
    static const UINT8 empty_prt[] = {
        0x08, '_', 'P', 'R', 'T', 0x12, 0x02, 0x00,
    };
    static const UINT8 audio_prt[] = {
        0x12, 0x0b, 0x04, 0x0c, 0xff, 0xff, 0x04, 0x00,
        0x00, 0x00, 0x0a, 0x10,
    };
    static const UINT8 hid[] = {
        0x08, '_', 'H', 'I', 'D', 0x0c, 0x41, 0xd0, 0x05, 0x01,
    };
    static const UINT8 uid[] = {
        0x08, '_', 'U', 'I', 'D', 0x00,
    };
    static const UINT8 io[] = {
        0x47, 0x01, 0xf8, 0x03, 0xf8, 0x03, 0x01, 0x08,
    };
    static const UINT8 irq[] = {
        0x23, 0x10, 0x00, 0x01,
    };
    static const UINT8 ps2_io[] = {
        0x47, 0x01, 0x60, 0x00, 0x60, 0x00, 0x01, 0x01,
        0x47, 0x01, 0x64, 0x00, 0x64, 0x00, 0x01, 0x01,
    };
    static const UINT8 keyboard_irq[] = { 0x22, 0x02, 0x00 };
    static const UINT8 mouse_irq[] = { 0x22, 0x00, 0x10 };
    UINTN pci0_offset = byte_sequence_offset(
        mI2000DsdtAmlTemplate, sizeof(mI2000DsdtAmlTemplate),
        pci0, sizeof(pci0));
    UINTN ifb0_offset = byte_sequence_offset(
        mI2000DsdtAmlTemplate, sizeof(mI2000DsdtAmlTemplate),
        ifb0, sizeof(ifb0));
    UINTN ps2k_offset = byte_sequence_offset(
        mI2000DsdtAmlTemplate, sizeof(mI2000DsdtAmlTemplate),
        ps2k, sizeof(ps2k));
    UINTN ps2m_offset = byte_sequence_offset(
        mI2000DsdtAmlTemplate, sizeof(mI2000DsdtAmlTemplate),
        ps2m, sizeof(ps2m));
    UINTN pci1_offset = byte_sequence_offset(
        mI2000DsdtAmlTemplate, sizeof(mI2000DsdtAmlTemplate),
        pci1, sizeof(pci1));
    UINTN pci2_offset = byte_sequence_offset(
        mI2000DsdtAmlTemplate, sizeof(mI2000DsdtAmlTemplate),
        pci2, sizeof(pci2));
    UINTN pci3_offset = byte_sequence_offset(
        mI2000DsdtAmlTemplate, sizeof(mI2000DsdtAmlTemplate),
        pci3, sizeof(pci3));
    UINTN empty_prt_offset = byte_sequence_offset(
        mI2000DsdtAmlTemplate, sizeof(mI2000DsdtAmlTemplate),
        empty_prt, sizeof(empty_prt));

    if (IA64_I2000_DSDT_AML_SIZE != 1186U ||
        !byte_sequence_present(mI2000DsdtAmlTemplate,
                               sizeof(mI2000DsdtAmlTemplate),
                               pci_hid, sizeof(pci_hid)) ||
        !byte_sequence_present(mI2000DsdtAmlTemplate,
                               sizeof(mI2000DsdtAmlTemplate),
                               pci0, sizeof(pci0)) ||
        !byte_sequence_present(mI2000DsdtAmlTemplate,
                               sizeof(mI2000DsdtAmlTemplate),
                               pci1, sizeof(pci1)) ||
        !byte_sequence_present(mI2000DsdtAmlTemplate,
                               sizeof(mI2000DsdtAmlTemplate),
                               pci2, sizeof(pci2)) ||
        !byte_sequence_present(mI2000DsdtAmlTemplate,
                               sizeof(mI2000DsdtAmlTemplate),
                               pci3, sizeof(pci3)) ||
        !byte_sequence_present(mI2000DsdtAmlTemplate,
                               sizeof(mI2000DsdtAmlTemplate),
                               audio_prt, sizeof(audio_prt)) ||
        pci0_offset == ~(UINTN)0 || ifb0_offset == ~(UINTN)0 ||
        ps2k_offset == ~(UINTN)0 ||
        ps2m_offset == ~(UINTN)0 || pci1_offset == ~(UINTN)0 ||
        pci2_offset == ~(UINTN)0 || pci3_offset == ~(UINTN)0 ||
        empty_prt_offset == ~(UINTN)0 ||
        ifb0_offset <= pci0_offset || ps2k_offset <= ifb0_offset ||
        ps2m_offset <= ps2k_offset ||
        pci1_offset <= ps2m_offset || pci2_offset <= pci1_offset ||
        empty_prt_offset <= pci2_offset || pci3_offset <= empty_prt_offset ||
        !byte_sequence_present(mI2000DsdtAmlTemplate,
                               sizeof(mI2000DsdtAmlTemplate),
                               ifb_address, sizeof(ifb_address)) ||
        !byte_sequence_present(mI2000DsdtAmlTemplate,
                               sizeof(mI2000DsdtAmlTemplate),
                               hid, sizeof(hid)) ||
        !byte_sequence_present(mI2000DsdtAmlTemplate,
                               sizeof(mI2000DsdtAmlTemplate),
                               uid, sizeof(uid)) ||
        !byte_sequence_present(mI2000DsdtAmlTemplate,
                               sizeof(mI2000DsdtAmlTemplate),
                               io, sizeof(io)) ||
        !byte_sequence_present(mI2000DsdtAmlTemplate,
                               sizeof(mI2000DsdtAmlTemplate),
                               irq, sizeof(irq)) ||
        !byte_sequence_present(mI2000DsdtAmlTemplate,
                               sizeof(mI2000DsdtAmlTemplate),
                               ps2_io, sizeof(ps2_io)) ||
        !byte_sequence_present(mI2000DsdtAmlTemplate,
                               sizeof(mI2000DsdtAmlTemplate),
                               keyboard_irq, sizeof(keyboard_irq)) ||
        !byte_sequence_present(mI2000DsdtAmlTemplate,
                               sizeof(mI2000DsdtAmlTemplate),
                               mouse_irq, sizeof(mouse_irq))) {
        return 1;
    }
    return 0;
}

static int test_platform_ssdt_legacy_policy(void)
{
    static const UINT8 cpu0[] = { 'C', 'P', 'U', '0' };
    static const UINT8 cpu63[] = { 'C', 'P', '6', '3' };
    static const UINT8 cpu0_enabled[] = { 'C', '0', 'E', 'N' };
    static const UINT8 cpu63_enabled[] = { 'E', '0', '6', '3' };
    static const UINT8 uart[] = { 'U', 'A', 'R', '0' };
    static const UINT8 uart_enabled[] = { 'U', '0', 'E', 'N' };
    static const UINT8 uart_hid[] = {
        0x08, '_', 'H', 'I', 'D', 0x0c, 0x41, 0xd0, 0x05, 0x01,
    };
    static const UINT8 uart_io[] = {
        0x47, 0x01, 0xf8, 0x03, 0xf8, 0x03, 0x01, 0x08,
    };
    static const UINT8 uart_irq[] = { 0x22, 0x10, 0x00 };
    static const UINT8 ps2_enabled[] = { 'P', '2', 'E', 'N' };
    static const UINT8 ps2_keyboard[] = { 'P', 'S', '2', 'K' };
    static const UINT8 ps2_mouse[] = { 'P', 'S', '2', 'M' };
    static const UINT8 pci0_scope[] = {
        0x10, 0x49, 0x0b, 0x5c, 0x2e,
        '_', 'S', 'B', '_', 'P', 'C', 'I', '0',
    };
    static const UINT8 ps2k_device[] = {
        0x5b, 0x82, 0x39, 'P', 'S', '2', 'K',
    };
    static const UINT8 ps2m_device[] = {
        0x5b, 0x82, 0x29, 'P', 'S', '2', 'M',
    };
    static const UINT8 ps2_io[] = {
        0x47, 0x01, 0x60, 0x00, 0x60, 0x00, 0x01, 0x01,
        0x47, 0x01, 0x64, 0x00, 0x64, 0x00, 0x01, 0x01,
    };
    static const UINT8 keyboard_irq[] = { 0x22, 0x02, 0x00 };
    static const UINT8 mouse_irq[] = { 0x22, 0x00, 0x10 };
    UINTN pci0_scope_offset = byte_sequence_offset(
        mSsdtAmlTemplate, sizeof(mSsdtAmlTemplate),
        pci0_scope, sizeof(pci0_scope));
    UINTN uart_offset = byte_sequence_offset(
        mSsdtAmlTemplate, sizeof(mSsdtAmlTemplate),
        uart, sizeof(uart));
    UINTN ps2k_offset = byte_sequence_offset(
        mSsdtAmlTemplate, sizeof(mSsdtAmlTemplate),
        ps2k_device, sizeof(ps2k_device));
    UINTN ps2m_offset = byte_sequence_offset(
        mSsdtAmlTemplate, sizeof(mSsdtAmlTemplate),
        ps2m_device, sizeof(ps2m_device));

    if (IA64_SSDT_AML_SIZE != 2242U ||
        pci0_scope_offset == ~(UINTN)0 || uart_offset == ~(UINTN)0 ||
        ps2k_offset == ~(UINTN)0 || ps2m_offset == ~(UINTN)0 ||
        uart_offset <= pci0_scope_offset || ps2k_offset <= uart_offset ||
        ps2m_offset <= ps2k_offset ||
        !byte_sequence_present(mSsdtAmlTemplate,
                               sizeof(mSsdtAmlTemplate),
                               cpu0, sizeof(cpu0)) ||
        !byte_sequence_present(mSsdtAmlTemplate,
                               sizeof(mSsdtAmlTemplate),
                               cpu63, sizeof(cpu63)) ||
        !byte_sequence_present(mSsdtAmlTemplate,
                               sizeof(mSsdtAmlTemplate),
                               cpu0_enabled, sizeof(cpu0_enabled)) ||
        !byte_sequence_present(mSsdtAmlTemplate,
                               sizeof(mSsdtAmlTemplate),
                               cpu63_enabled, sizeof(cpu63_enabled)) ||
        !byte_sequence_present(mSsdtAmlTemplate,
                               sizeof(mSsdtAmlTemplate),
                               uart_enabled, sizeof(uart_enabled)) ||
        !byte_sequence_present(mSsdtAmlTemplate,
                               sizeof(mSsdtAmlTemplate),
                               uart_hid, sizeof(uart_hid)) ||
        !byte_sequence_present(mSsdtAmlTemplate,
                               sizeof(mSsdtAmlTemplate),
                               uart_io, sizeof(uart_io)) ||
        !byte_sequence_present(mSsdtAmlTemplate,
                               sizeof(mSsdtAmlTemplate),
                               uart_irq, sizeof(uart_irq)) ||
        !byte_sequence_present(mSsdtAmlTemplate,
                               sizeof(mSsdtAmlTemplate),
                               ps2_enabled, sizeof(ps2_enabled)) ||
        !byte_sequence_present(mSsdtAmlTemplate,
                               sizeof(mSsdtAmlTemplate),
                               ps2_keyboard, sizeof(ps2_keyboard)) ||
        !byte_sequence_present(mSsdtAmlTemplate,
                               sizeof(mSsdtAmlTemplate),
                               ps2_mouse, sizeof(ps2_mouse)) ||
        !byte_sequence_present(mSsdtAmlTemplate,
                               sizeof(mSsdtAmlTemplate),
                               ps2_io, sizeof(ps2_io)) ||
        !byte_sequence_present(mSsdtAmlTemplate,
                               sizeof(mSsdtAmlTemplate),
                               keyboard_irq, sizeof(keyboard_irq)) ||
        !byte_sequence_present(mSsdtAmlTemplate,
                               sizeof(mSsdtAmlTemplate),
                               mouse_irq, sizeof(mouse_irq)) ||
        !byte_sequence_present(mSsdtAmlTemplate,
                               sizeof(mSsdtAmlTemplate),
                               pci0_scope, sizeof(pci0_scope))) {
        return 1;
    }
    return 0;
}

int main(void)
{
    return test_i2000_policy() || test_sparse_register_addresses() ||
        test_policy_rejections() || test_i2000_dsdt_contract() ||
        test_platform_ssdt_legacy_policy();
}
