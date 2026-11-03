/*
 * IA-64 zx6000 ZX1 integration test
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_IA64_ZX6000_ZX1_TEST_H
#define HW_IA64_ZX6000_ZX1_TEST_H

#include "qemu/typedefs.h"
#include "qom/object.h"

#define TYPE_IA64_ZX6000_ZX1_TEST "ia64-zx6000-zx1-test"
OBJECT_DECLARE_SIMPLE_TYPE(IA64ZX6000ZX1TestState, IA64_ZX6000_ZX1_TEST)

#define IA64_ZX6000_ZX1_TEST_PROP_RAM "ram"

/* The device uses parent-owned RAM and PIB objects. */
PCIBus *ia64_zx6000_zx1_test_root_bus(IA64ZX6000ZX1TestState *fixture,
                                      unsigned int index);

/* Root probe and I/O SAPIC accessors. */
PCIDevice *
ia64_zx6000_zx1_test_create_test_probe(IA64ZX6000ZX1TestState *fixture,
                                       unsigned int root, unsigned int slot,
                                       Error **errp);
void ia64_zx6000_zx1_test_destroy_test_probe(PCIDevice *probe);
qemu_irq ia64_zx6000_zx1_test_io_sapic_input(IA64ZX6000ZX1TestState *fixture,
                                             unsigned int root,
                                             unsigned int input);

/* Qtest object paths and controls. */
#define TYPE_IA64_ZX6000_ZX1_QTEST "ia64-zx6000-zx1-qtest"
#define IA64_ZX6000_ZX1_QTEST_ID "zx6000-zx1-test"
#define IA64_ZX6000_ZX1_QTEST_QOM_PATH \
    "/machine/peripheral/" IA64_ZX6000_ZX1_QTEST_ID
#define IA64_ZX6000_ZX1_TEST_FIXTURE_CHILD "fixture"
#define IA64_ZX6000_ZX1_TEST_FIXTURE_QOM_PATH \
    IA64_ZX6000_ZX1_QTEST_QOM_PATH "/" \
    IA64_ZX6000_ZX1_TEST_FIXTURE_CHILD

/*
 * Each probe exposes four drive lines for pin coverage, but remains one PCI
 * function with one Interrupt Pin selector.  A qtest must deassert its active
 * line before asserting another pin on the same probe.
 */
#define IA64_ZX6000_ZX1_TEST_GPIO_INTX "x-test-intx"
#define IA64_ZX6000_ZX1_TEST_GPIO_MSI "x-test-msi"
#define IA64_ZX6000_ZX1_TEST_PROBES_PER_ROOT 2U
#define IA64_ZX6000_ZX1_TEST_PROBE_COUNT 4U
#define IA64_ZX6000_ZX1_TEST_PROBE_SLOT(probe) ((probe) + 1U)
#define IA64_ZX6000_ZX1_TEST_GPIO_LINE(root, probe, pin) \
    ((((root) * IA64_ZX6000_ZX1_TEST_PROBES_PER_ROOT + (probe)) * 4U) + \
     (pin))
#define IA64_ZX6000_ZX1_TEST_MSI_GPIO_LINE(root, probe) \
    ((root) * IA64_ZX6000_ZX1_TEST_PROBES_PER_ROOT + (probe))

#define IA64_ZX6000_ZX1_TEST_DELIVERY_COUNT_0 \
    "x-test-delivery-count-0"
#define IA64_ZX6000_ZX1_TEST_DELIVERY_COUNT_1 \
    "x-test-delivery-count-1"
#define IA64_ZX6000_ZX1_TEST_LAST_ADDRESS_0 \
    "x-test-last-address-0"
#define IA64_ZX6000_ZX1_TEST_LAST_ADDRESS_1 \
    "x-test-last-address-1"
#define IA64_ZX6000_ZX1_TEST_LAST_DATA_0 "x-test-last-data-0"
#define IA64_ZX6000_ZX1_TEST_LAST_DATA_1 "x-test-last-data-1"
#define IA64_ZX6000_ZX1_TEST_LAST_RESULT_0 "x-test-last-result-0"
#define IA64_ZX6000_ZX1_TEST_LAST_RESULT_1 "x-test-last-result-1"

/* No interrupt delivery has attempted a PIB transaction since reset. */
#define IA64_ZX6000_ZX1_TEST_DELIVERY_NOT_RUN UINT32_MAX

#endif /* HW_IA64_ZX6000_ZX1_TEST_H */
