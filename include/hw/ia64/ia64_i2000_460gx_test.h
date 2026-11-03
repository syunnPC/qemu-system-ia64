/*
 * IA-64 i2000 460GX integration test
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_IA64_I2000_460GX_TEST_H
#define HW_IA64_I2000_460GX_TEST_H

#include "qemu/typedefs.h"
#include "qom/object.h"

#define TYPE_IA64_I2000_460GX_TEST "ia64-i2000-460gx-test"
OBJECT_DECLARE_SIMPLE_TYPE(IA64I2000460GXTestState, IA64_I2000_460GX_TEST)

#define IA64_I2000_460GX_TEST_PROP_RAM "ram"
#define IA64_I2000_460GX_TEST_PROP_LEGACY_PIN "legacy-pin"
#define IA64_I2000_460GX_TEST_LEGACY_PIN_DISCONNECTED UINT32_MAX

/*
 * The caller provides the two-GiB RAM mapping and shared PIB.  Realization
 * constructs the 460GX host topology without a platform descriptor.
 */
PCIBus *ia64_i2000_460gx_test_root_bus(IA64I2000460GXTestState *fixture,
                                       unsigned index);
qemu_irq ia64_i2000_460gx_test_legacy_irq(IA64I2000460GXTestState *fixture);
uint32_t
ia64_i2000_460gx_test_legacy_pin(const IA64I2000460GXTestState *fixture);

/* Qtest object paths and controls. */
#define TYPE_IA64_I2000_460GX_QTEST "ia64-i2000-460gx-qtest"
#define IA64_I2000_460GX_QTEST_ID "i2000-460gx-test"
#define IA64_I2000_460GX_QTEST_QOM_PATH \
    "/machine/peripheral/" IA64_I2000_460GX_QTEST_ID
#define IA64_I2000_460GX_TEST_DEVICE_CHILD "fixture"
#define IA64_I2000_460GX_TEST_DEVICE_QOM_PATH \
    IA64_I2000_460GX_QTEST_QOM_PATH "/" \
    IA64_I2000_460GX_TEST_DEVICE_CHILD
#define IA64_I2000_460GX_TEST_GPIO_INTX "x-test-intx"
#define IA64_I2000_460GX_TEST_GPIO_LEGACY "x-test-legacy"
#define IA64_I2000_460GX_TEST_CF8_SUBDWORD_COUNT \
    "x-test-cf8-subdword-count"
#define IA64_I2000_460GX_TEST_DESCRIPTOR_INSTALLED \
    "x-test-descriptor-installed"
#define IA64_I2000_460GX_TEST_PCI_SLOT 1U

#endif /* HW_IA64_I2000_460GX_TEST_H */
