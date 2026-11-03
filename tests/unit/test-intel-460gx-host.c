/*
 * Intel 460GX configuration routing engine tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/ia64/intel_460gx_host_internal.h"
#include "hw/ia64/intel_460gx_root.h"
#include "hw/pci/pci_host.h"
#include "qapi/error.h"

typedef struct TestTarget {
    uint32_t read_value;
    uint32_t write_value;
    uint16_t offset;
    unsigned size;
    unsigned reads;
    unsigned writes;
} TestTarget;

static PCIBus *expected_pci_bus;
static uint32_t downstream_address;
static uint32_t downstream_value;
static unsigned downstream_size;
static unsigned downstream_reads;
static unsigned downstream_writes;

/* PCI helper definitions used by the routing engine. */
uint32_t pci_data_read(PCIBus *bus, uint32_t address, unsigned size)
{
    g_assert_true(bus == expected_pci_bus);
    downstream_address = address;
    downstream_size = size;
    downstream_reads++;
    return UINT32_C(0x78563412);
}

void pci_data_write(PCIBus *bus, uint32_t address, uint32_t value,
                    unsigned size)
{
    g_assert_true(bus == expected_pci_bus);
    downstream_address = address;
    downstream_value = value;
    downstream_size = size;
    downstream_writes++;
}

static uint32_t target_read(void *opaque, uint16_t offset, unsigned size)
{
    TestTarget *target = opaque;

    target->offset = offset;
    target->size = size;
    target->reads++;
    return target->read_value;
}

static void target_write(void *opaque, uint16_t offset, uint32_t value,
                         unsigned size)
{
    TestTarget *target = opaque;

    target->offset = offset;
    target->write_value = value;
    target->size = size;
    target->writes++;
}

static const Intel460GXConfigTargetOps target_ops = {
    .read = target_read,
    .write = target_write,
};

static uint32_t config_address(unsigned bus, unsigned device, unsigned function,
                               unsigned reg)
{
    return INTEL_460GX_CONFIG_ENABLE | bus << 16 | device << 11 |
           function << 8 | (reg & 0xfc);
}

static void core_init(Intel460GXHostCore *core, uint8_t cbn, uint32_t present)
{
    memset(core, 0, sizeof(*core));
    intel_460gx_host_core_init(core, cbn, present);
}

static void assert_error_and_clear(Error **errp)
{
    g_assert_nonnull(*errp);
    error_free(*errp);
    *errp = NULL;
}

static void test_root_intx_identity(void)
{
    unsigned pin;

    for (pin = 0; pin < PCI_NUM_PINS; pin++) {
        g_assert_cmpint(intel_460gx_root_intx_index(pin), ==, pin);
    }
    g_assert_cmpint(intel_460gx_root_intx_index(-1), ==, -1);
    g_assert_cmpint(intel_460gx_root_intx_index(PCI_NUM_PINS), ==, -1);
}

static void test_mechanism_one_and_absent(void)
{
    Intel460GXHostCore core;

    core_init(&core, 0x40, 0);
    g_assert_cmphex(intel_460gx_host_core_address_read(&core, 4), ==, 0);
    g_assert_cmphex(intel_460gx_host_core_data_read(&core, 0, 1), ==,
                    UINT8_MAX);
    g_assert_cmphex(intel_460gx_host_core_data_read(&core, 0, 2), ==,
                    UINT16_MAX);
    g_assert_cmphex(intel_460gx_host_core_data_read(&core, 0, 4), ==,
                    UINT32_MAX);

    intel_460gx_host_core_address_write(&core, UINT32_C(0xffffffff), 2);
    g_assert_cmphex(core.config_address, ==, 0);
    g_assert_cmphex(intel_460gx_host_core_address_read(&core, 2), ==,
                    UINT16_MAX);

    intel_460gx_host_core_address_write(&core, UINT32_C(0xffffffff), 4);
    g_assert_cmphex(core.config_address, ==, INTEL_460GX_CONFIG_ADDRESS_MASK);
    g_assert_cmphex(intel_460gx_host_core_data_read(&core, 0, 4), ==,
                    UINT32_MAX);

    /* A data access may not cross the four-byte CFC..CFF window. */
    g_assert_cmphex(intel_460gx_host_core_data_read(&core, 3, 2), ==,
                    UINT16_MAX);
}

static void test_bootstrap_sac_priority(void)
{
    Intel460GXHostCore core;
    TestTarget target = { .read_value = UINT32_C(0xa1b2c3d4) };
    Error *err = NULL;

    /* CBN zero shares the bootstrap bus. */
    core_init(&core, 0, BIT(INTEL_460GX_BOOTSTRAP_SAC_DEVICE));
    g_assert_true(intel_460gx_host_core_register_bootstrap(
        &core, 0, &target_ops, &target, &err));
    g_assert_null(err);

    intel_460gx_host_core_address_write(
        &core, config_address(0, INTEL_460GX_BOOTSTRAP_SAC_DEVICE, 0, 0x20), 4);
    g_assert_cmphex(intel_460gx_host_core_data_read(&core, 1, 1), ==, 0xd4);
    g_assert_cmpuint(target.offset, ==, 0x21);
    g_assert_cmpuint(target.reads, ==, 1);

    intel_460gx_host_core_data_write(&core, 2, 0x7654, 2);
    g_assert_cmpuint(target.offset, ==, 0x22);
    g_assert_cmphex(target.write_value, ==, 0x7654);
    g_assert_cmpuint(target.writes, ==, 1);

    /* Unregistered SAC functions terminate as absent, not downstream. */
    intel_460gx_host_core_address_write(
        &core, config_address(0, INTEL_460GX_BOOTSTRAP_SAC_DEVICE, 1, 0), 4);
    g_assert_cmphex(intel_460gx_host_core_data_read(&core, 0, 4), ==,
                    UINT32_MAX);
}

static void test_cbn_partition_and_presence(void)
{
    Intel460GXHostCore core;
    TestTarget target = { .read_value = UINT32_C(0x11223344) };
    Error *err = NULL;

    core_init(&core, 0x40, 0);
    g_assert_true(intel_460gx_host_core_register_chipset(
        &core, 4, 2, &target_ops, &target, &err));
    g_assert_null(err);

    intel_460gx_host_core_address_write(&core, config_address(0x40, 4, 2, 0x30),
                                        4);
    g_assert_cmphex(intel_460gx_host_core_data_read(&core, 2, 2), ==,
                    UINT16_MAX);
    g_assert_cmpuint(target.reads, ==, 0);

    g_assert_true(intel_460gx_host_core_set_present(&core, 4, true, &err));
    g_assert_null(err);
    g_assert_cmphex(intel_460gx_host_core_data_read(&core, 2, 2), ==,
                    UINT16_C(0x3344));
    g_assert_cmpuint(target.offset, ==, 0x32);
    g_assert_cmpuint(target.reads, ==, 1);

    core.cbn = 0x41;
    g_assert_cmphex(intel_460gx_host_core_data_read(&core, 0, 4), ==,
                    UINT32_MAX);
    intel_460gx_host_core_address_write(&core, config_address(0x41, 4, 2, 0x30),
                                        4);
    g_assert_cmphex(intel_460gx_host_core_data_read(&core, 0, 4), ==,
                    target.read_value);

    g_assert_false(intel_460gx_host_core_register_chipset(
        &core, 2, 0, &target_ops, &target, &err));
    assert_error_and_clear(&err);
    g_assert_false(intel_460gx_host_core_set_present(&core, 0x1f, true, &err));
    assert_error_and_clear(&err);
}

static void test_downstream_attachment(void)
{
    Intel460GXHostCore core;
    Error *err = NULL;

    core_init(&core, 0x40, 0);
    expected_pci_bus = (PCIBus *)(uintptr_t)0x1234;
    downstream_reads = 0;
    downstream_writes = 0;
    g_assert_true(intel_460gx_host_core_attach_downstream(
        &core, 0, expected_pci_bus, 5, 8, &err));
    g_assert_null(err);

    intel_460gx_host_core_address_write(&core, config_address(6, 3, 1, 0x44),
                                        4);
    g_assert_cmphex(intel_460gx_host_core_data_read(&core, 0, 4), ==,
                    UINT32_C(0x78563412));
    g_assert_cmpuint(downstream_reads, ==, 1);
    g_assert_cmphex(downstream_address, ==, config_address(6, 3, 1, 0x44));

    intel_460gx_host_core_data_write(&core, 1, 0xab, 1);
    g_assert_cmpuint(downstream_writes, ==, 1);
    g_assert_cmphex(downstream_address, ==, config_address(6, 3, 1, 0x44) | 1);
    g_assert_cmphex(downstream_value, ==, 0xab);
    g_assert_cmpuint(downstream_size, ==, 1);

    g_assert_false(intel_460gx_host_core_attach_downstream(
        &core, 1, (PCIBus *)(uintptr_t)0x5678, 8, 9, &err));
    assert_error_and_clear(&err);

    intel_460gx_host_core_address_write(&core, config_address(9, 0, 0, 0), 4);
    g_assert_cmphex(intel_460gx_host_core_data_read(&core, 0, 4), ==,
                    UINT32_MAX);
}

static void test_downstream_range_update(void)
{
    Intel460GXHostCore core;
    PCIBus *bus0 = (PCIBus *)(uintptr_t)0x1234;
    PCIBus *bus1 = (PCIBus *)(uintptr_t)0x5678;
    Error *err = NULL;

    core_init(&core, 0x40, 0);
    g_assert_true(
        intel_460gx_host_core_attach_downstream(&core, 0, bus0, 5, 8, &err));
    g_assert_null(err);

    g_assert_true(
        intel_460gx_host_core_set_downstream_range(&core, 0, 10, 12, &err));
    g_assert_null(err);
    g_assert_cmpuint(core.downstream[0].first_bus, ==, 10);
    g_assert_cmpuint(core.downstream[0].last_bus, ==, 12);
    g_assert_cmpuint(core.downstream[0].reset_first_bus, ==, 5);
    g_assert_cmpuint(core.downstream[0].reset_last_bus, ==, 8);

    /* Attachment must also preserve non-overlapping reset baselines. */
    g_assert_false(
        intel_460gx_host_core_attach_downstream(&core, 1, bus1, 6, 7, &err));
    assert_error_and_clear(&err);
    g_assert_false(core.downstream[1].attached);

    g_assert_true(
        intel_460gx_host_core_attach_downstream(&core, 1, bus1, 20, 24, &err));
    g_assert_null(err);

    /* An overlapping update is rejected without changing either endpoint. */
    g_assert_false(
        intel_460gx_host_core_set_downstream_range(&core, 0, 22, 25, &err));
    assert_error_and_clear(&err);
    g_assert_cmpuint(core.downstream[0].first_bus, ==, 10);
    g_assert_cmpuint(core.downstream[0].last_bus, ==, 12);

    g_assert_false(
        intel_460gx_host_core_set_downstream_range(&core, 0, 13, 12, &err));
    assert_error_and_clear(&err);
    g_assert_cmpuint(core.downstream[0].first_bus, ==, 10);
    g_assert_cmpuint(core.downstream[0].last_bus, ==, 12);

    g_assert_false(
        intel_460gx_host_core_set_downstream_range(&core, 2, 30, 31, &err));
    assert_error_and_clear(&err);
    g_assert_false(intel_460gx_host_core_set_downstream_range(
        &core, INTEL_460GX_DOWNSTREAM_PORTS, 30, 31, &err));
    assert_error_and_clear(&err);

    expected_pci_bus = bus0;
    downstream_reads = 0;
    intel_460gx_host_core_address_write(&core, config_address(11, 3, 1, 0x44),
                                        4);
    g_assert_cmphex(intel_460gx_host_core_data_read(&core, 0, 4), ==,
                    UINT32_C(0x78563412));
    g_assert_cmpuint(downstream_reads, ==, 1);
    intel_460gx_host_core_address_write(&core, config_address(6, 3, 1, 0x44),
                                        4);
    g_assert_cmphex(intel_460gx_host_core_data_read(&core, 0, 4), ==,
                    UINT32_MAX);
    g_assert_cmpuint(downstream_reads, ==, 1);

    intel_460gx_host_core_reset(&core);
    g_assert_cmpuint(core.downstream[0].first_bus, ==, 5);
    g_assert_cmpuint(core.downstream[0].last_bus, ==, 8);
    g_assert_cmpuint(core.downstream[1].first_bus, ==, 20);
    g_assert_cmpuint(core.downstream[1].last_bus, ==, 24);
    intel_460gx_host_core_address_write(&core, config_address(6, 3, 1, 0x44),
                                        4);
    g_assert_cmphex(intel_460gx_host_core_data_read(&core, 0, 4), ==,
                    UINT32_C(0x78563412));
    g_assert_cmpuint(downstream_reads, ==, 2);
    g_assert_true(intel_460gx_host_core_validate_downstream(&core, &err));
    g_assert_null(err);
}

static void assert_decoded_state_equal(const Intel460GXHostCore *actual,
                                       const Intel460GXHostCore *expected)
{
    unsigned i;

    g_assert_cmpuint(actual->cbn, ==, expected->cbn);
    g_assert_cmphex(actual->chipset_present, ==, expected->chipset_present);
    g_assert_cmpuint(actual->reset_cbn, ==, expected->reset_cbn);
    g_assert_cmphex(actual->reset_chipset_present, ==,
                    expected->reset_chipset_present);
    for (i = 0; i < INTEL_460GX_DOWNSTREAM_PORTS; i++) {
        const Intel460GXDownstreamRoute *route = &actual->downstream[i];
        const Intel460GXDownstreamRoute *expected_route =
            &expected->downstream[i];

        g_assert_true(route->bus == expected_route->bus);
        g_assert_cmpuint(route->first_bus, ==, expected_route->first_bus);
        g_assert_cmpuint(route->last_bus, ==, expected_route->last_bus);
        g_assert_cmpuint(route->reset_first_bus, ==,
                         expected_route->reset_first_bus);
        g_assert_cmpuint(route->reset_last_bus, ==,
                         expected_route->reset_last_bus);
        g_assert_cmpint(route->attached, ==, expected_route->attached);
    }
}

static void test_decoded_update_success(void)
{
    Intel460GXHostCore core;
    PCIBus *bus0 = (PCIBus *)(uintptr_t)0x1234;
    PCIBus *bus1 = (PCIBus *)(uintptr_t)0x5678;
    Intel460GXDecodedStateUpdate update = {
        .has_cbn = true,
        .cbn = 0x55,
        .has_chipset_present = true,
        .chipset_present = BIT(4) | BIT(0x10),
        .route_mask = BIT(0) | BIT(1),
        .routes = {
            [0] = { .first_bus = 30, .last_bus = 35 },
            [1] = { .first_bus = 40, .last_bus = 45 },
        },
    };
    Error *err = NULL;

    core_init(&core, 0x40, BIT(0));
    g_assert_true(
        intel_460gx_host_core_attach_downstream(&core, 0, bus0, 10, 19, &err));
    g_assert_null(err);
    g_assert_true(
        intel_460gx_host_core_attach_downstream(&core, 1, bus1, 20, 29, &err));
    g_assert_null(err);

    g_assert_true(
        intel_460gx_host_core_apply_decoded_update(&core, &update, &err));
    g_assert_null(err);
    g_assert_cmpuint(core.cbn, ==, 0x55);
    g_assert_cmphex(core.chipset_present, ==, BIT(4) | BIT(0x10));
    g_assert_cmpuint(core.downstream[0].first_bus, ==, 30);
    g_assert_cmpuint(core.downstream[0].last_bus, ==, 35);
    g_assert_cmpuint(core.downstream[1].first_bus, ==, 40);
    g_assert_cmpuint(core.downstream[1].last_bus, ==, 45);

    /* A decoded update must not rewrite any reset baseline. */
    g_assert_cmpuint(core.reset_cbn, ==, 0x40);
    g_assert_cmphex(core.reset_chipset_present, ==, BIT(0));
    g_assert_cmpuint(core.downstream[0].reset_first_bus, ==, 10);
    g_assert_cmpuint(core.downstream[0].reset_last_bus, ==, 19);
    g_assert_cmpuint(core.downstream[1].reset_first_bus, ==, 20);
    g_assert_cmpuint(core.downstream[1].reset_last_bus, ==, 29);

    intel_460gx_host_core_reset(&core);
    g_assert_cmpuint(core.cbn, ==, 0x40);
    g_assert_cmphex(core.chipset_present, ==, BIT(0));
    g_assert_cmpuint(core.downstream[0].first_bus, ==, 10);
    g_assert_cmpuint(core.downstream[0].last_bus, ==, 19);
    g_assert_cmpuint(core.downstream[1].first_bus, ==, 20);
    g_assert_cmpuint(core.downstream[1].last_bus, ==, 29);
}

static void test_decoded_update_rollback(void)
{
    Intel460GXHostCore core;
    Intel460GXHostCore expected;
    PCIBus *bus0 = (PCIBus *)(uintptr_t)0x1234;
    PCIBus *bus1 = (PCIBus *)(uintptr_t)0x5678;
    Intel460GXDecodedStateUpdate update;
    Error *err = NULL;

    core_init(&core, 0x40, BIT(0));
    g_assert_true(
        intel_460gx_host_core_attach_downstream(&core, 0, bus0, 10, 19, &err));
    g_assert_null(err);
    g_assert_true(
        intel_460gx_host_core_attach_downstream(&core, 1, bus1, 20, 29, &err));
    g_assert_null(err);
    expected = core;

    /* Reserved presence bits reject all otherwise-valid fields. */
    update = (Intel460GXDecodedStateUpdate) {
        .has_cbn = true,
        .cbn = 0x41,
        .has_chipset_present = true,
        .chipset_present = BIT(2),
        .route_mask = BIT(0),
        .routes[0] = { .first_bus = 30, .last_bus = 39 },
    };
    g_assert_false(
        intel_460gx_host_core_apply_decoded_update(&core, &update, &err));
    assert_error_and_clear(&err);
    assert_decoded_state_equal(&core, &expected);

    /* A range may only be supplied for an attached destination. */
    update = (Intel460GXDecodedStateUpdate) {
        .has_cbn = true,
        .cbn = 0x42,
        .has_chipset_present = true,
        .chipset_present = BIT(4),
        .route_mask = BIT(2),
        .routes[2] = { .first_bus = 30, .last_bus = 39 },
    };
    g_assert_false(
        intel_460gx_host_core_apply_decoded_update(&core, &update, &err));
    assert_error_and_clear(&err);
    assert_decoded_state_equal(&core, &expected);

    /* Reversed endpoints reject the whole candidate. */
    update = (Intel460GXDecodedStateUpdate) {
        .has_cbn = true,
        .cbn = 0x43,
        .has_chipset_present = true,
        .chipset_present = BIT(4),
        .route_mask = BIT(0),
        .routes[0] = { .first_bus = 39, .last_bus = 30 },
    };
    g_assert_false(
        intel_460gx_host_core_apply_decoded_update(&core, &update, &err));
    assert_error_and_clear(&err);
    assert_decoded_state_equal(&core, &expected);

    /* Validation includes unchanged ranges in the full candidate. */
    update = (Intel460GXDecodedStateUpdate) {
        .has_cbn = true,
        .cbn = 0x44,
        .has_chipset_present = true,
        .chipset_present = BIT(4),
        .route_mask = BIT(0),
        .routes[0] = { .first_bus = 25, .last_bus = 35 },
    };
    g_assert_false(
        intel_460gx_host_core_apply_decoded_update(&core, &update, &err));
    assert_error_and_clear(&err);
    assert_decoded_state_equal(&core, &expected);

    /* Multiple updated ranges are also checked against one another. */
    update = (Intel460GXDecodedStateUpdate) {
        .has_cbn = true,
        .cbn = 0x45,
        .has_chipset_present = true,
        .chipset_present = BIT(4),
        .route_mask = BIT(0) | BIT(1),
        .routes = {
            [0] = { .first_bus = 30, .last_bus = 39 },
            [1] = { .first_bus = 35, .last_bus = 44 },
        },
    };
    g_assert_false(
        intel_460gx_host_core_apply_decoded_update(&core, &update, &err));
    assert_error_and_clear(&err);
    assert_decoded_state_equal(&core, &expected);
}

static void test_decoded_update_atomic_swap(void)
{
    Intel460GXHostCore core;
    PCIBus *bus0 = (PCIBus *)(uintptr_t)0x1234;
    PCIBus *bus1 = (PCIBus *)(uintptr_t)0x5678;
    Intel460GXDecodedStateUpdate update = {
        .route_mask = BIT(0) | BIT(1),
        .routes = {
            [0] = { .first_bus = 20, .last_bus = 29 },
            [1] = { .first_bus = 10, .last_bus = 19 },
        },
    };
    Error *err = NULL;

    core_init(&core, 0x40, BIT(0));
    g_assert_true(
        intel_460gx_host_core_attach_downstream(&core, 0, bus0, 10, 19, &err));
    g_assert_null(err);
    g_assert_true(
        intel_460gx_host_core_attach_downstream(&core, 1, bus1, 20, 29, &err));
    g_assert_null(err);

    g_assert_true(
        intel_460gx_host_core_apply_decoded_update(&core, &update, &err));
    g_assert_null(err);
    g_assert_cmpuint(core.downstream[0].first_bus, ==, 20);
    g_assert_cmpuint(core.downstream[0].last_bus, ==, 29);
    g_assert_cmpuint(core.downstream[1].first_bus, ==, 10);
    g_assert_cmpuint(core.downstream[1].last_bus, ==, 19);
    g_assert_cmpuint(core.downstream[0].reset_first_bus, ==, 10);
    g_assert_cmpuint(core.downstream[0].reset_last_bus, ==, 19);
    g_assert_cmpuint(core.downstream[1].reset_first_bus, ==, 20);
    g_assert_cmpuint(core.downstream[1].reset_last_bus, ==, 29);
}

static void test_downstream_partition_priority(void)
{
    Intel460GXHostCore core;
    PCIBus *bus = (PCIBus *)(uintptr_t)0x1234;
    uint8_t unattached_first;
    uint8_t unattached_last;
    Error *err = NULL;

    core_init(&core, 0x40, 0);
    unattached_first = core.downstream[2].first_bus;
    unattached_last = core.downstream[2].last_bus;
    g_assert_true(
        intel_460gx_host_core_attach_downstream(&core, 0, bus, 0, 0, &err));
    g_assert_null(err);

    expected_pci_bus = bus;
    downstream_reads = 0;
    intel_460gx_host_core_address_write(&core, config_address(0, 3, 0, 0), 4);
    g_assert_cmphex(intel_460gx_host_core_data_read(&core, 0, 4), ==,
                    UINT32_C(0x78563412));
    g_assert_cmpuint(downstream_reads, ==, 1);

    /* Bus-0 device 10h remains the bootstrap SAC partition. */
    intel_460gx_host_core_address_write(
        &core, config_address(0, INTEL_460GX_BOOTSTRAP_SAC_DEVICE, 0, 0), 4);
    g_assert_cmphex(intel_460gx_host_core_data_read(&core, 0, 4), ==,
                    UINT32_MAX);
    g_assert_cmpuint(downstream_reads, ==, 1);

    g_assert_true(
        intel_460gx_host_core_set_downstream_range(&core, 0, 0x40, 0x40, &err));
    g_assert_null(err);
    /* CBN remains wholly reserved even when an xXB range includes it. */
    intel_460gx_host_core_address_write(&core, config_address(0x40, 3, 0, 0),
                                        4);
    g_assert_cmphex(intel_460gx_host_core_data_read(&core, 0, 4), ==,
                    UINT32_MAX);
    g_assert_cmpuint(downstream_reads, ==, 1);

    core.downstream[0].first_bus = 0x41;
    g_assert_false(intel_460gx_host_core_validate_downstream(&core, &err));
    assert_error_and_clear(&err);
    core.downstream[0].first_bus = 0x40;

    /* A loaded valid range on an unattached port is a topology mismatch. */
    core.downstream[2].first_bus = 0x50;
    core.downstream[2].last_bus = 0x50;
    g_assert_false(intel_460gx_host_core_validate_downstream(&core, &err));
    assert_error_and_clear(&err);
    core.downstream[2].first_bus = unattached_first;
    core.downstream[2].last_bus = unattached_last;
    g_assert_true(intel_460gx_host_core_validate_downstream(&core, &err));
    g_assert_null(err);
}

static void test_reset_state(void)
{
    Intel460GXHostCore core;
    Error *err = NULL;

    core_init(&core, 0x52, BIT(0) | BIT(4));
    intel_460gx_host_core_address_write(&core, config_address(7, 1, 0, 0x80),
                                        4);
    core.cbn = 0x61;
    g_assert_true(intel_460gx_host_core_set_present(&core, 4, false, &err));
    g_assert_null(err);

    intel_460gx_host_core_reset(&core);
    g_assert_cmphex(core.config_address, ==, 0);
    g_assert_cmpuint(core.cbn, ==, 0x52);
    g_assert_cmphex(core.chipset_present, ==, BIT(0) | BIT(4));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    g_test_add_func("/intel-460gx-host/root-intx-identity",
                    test_root_intx_identity);
    g_test_add_func("/intel-460gx-host/mechanism-one-and-absent",
                    test_mechanism_one_and_absent);
    g_test_add_func("/intel-460gx-host/bootstrap-sac-priority",
                    test_bootstrap_sac_priority);
    g_test_add_func("/intel-460gx-host/cbn-partition-and-presence",
                    test_cbn_partition_and_presence);
    g_test_add_func("/intel-460gx-host/downstream-attachment",
                    test_downstream_attachment);
    g_test_add_func("/intel-460gx-host/downstream-range-update",
                    test_downstream_range_update);
    g_test_add_func("/intel-460gx-host/decoded-update/success",
                    test_decoded_update_success);
    g_test_add_func("/intel-460gx-host/decoded-update/rollback",
                    test_decoded_update_rollback);
    g_test_add_func("/intel-460gx-host/decoded-update/atomic-swap",
                    test_decoded_update_atomic_swap);
    g_test_add_func("/intel-460gx-host/downstream-partition-priority",
                    test_downstream_partition_priority);
    g_test_add_func("/intel-460gx-host/reset-state", test_reset_state);

    return g_test_run();
}
