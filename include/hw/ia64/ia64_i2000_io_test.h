/*
 * IA-64 i2000 I/O test device
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_IA64_I2000_IO_TEST_H
#define HW_IA64_I2000_IO_TEST_H

#include "qemu/typedefs.h"
#include "qom/object.h"

typedef struct IDEBus IDEBus;

#define TYPE_IA64_I2000_IO_TEST "ia64-i2000-io-test"
OBJECT_DECLARE_SIMPLE_TYPE(IA64I2000IoTestState, IA64_I2000_IO_TEST)

#define IA64_I2000_IO_TEST_PROP_460GX_TEST "460gx-test"
#define IA64_I2000_IO_TEST_PROP_I82559_NETDEV "i82559-netdev"

/* Optional internal board-builder inputs; configure them before realize. */
bool ia64_i2000_io_test_set_uart_chardev(IA64I2000IoTestState *io_test,
                                         Chardev *chardev, Error **errp);
bool ia64_i2000_io_test_set_cd_drive(IA64I2000IoTestState *io_test,
                                     DriveInfo *drive, Error **errp);

/* Restore the fixed PCI resources after a machine-wide reset. */
void ia64_i2000_io_test_restore_pci_resources(IA64I2000IoTestState *io_test);

ISABus *ia64_i2000_io_test_isa_bus(IA64I2000IoTestState *io_test);
IDEBus *ia64_i2000_io_test_ide_bus(IA64I2000IoTestState *io_test);

/* Qtest object paths. */
#define TYPE_IA64_I2000_IO_QTEST "ia64-i2000-io-qtest"
#define IA64_I2000_IO_QTEST_ID "i2000-io-test"
#define IA64_I2000_IO_QTEST_QOM_PATH \
    "/machine/peripheral/" IA64_I2000_IO_QTEST_ID
#define IA64_I2000_IO_TEST_460GX_TEST_CHILD "460gx-test"
#define IA64_I2000_IO_TEST_460GX_TEST_QOM_PATH \
    IA64_I2000_IO_QTEST_QOM_PATH "/" \
    IA64_I2000_IO_TEST_460GX_TEST_CHILD
#define IA64_I2000_IO_TEST_DEVICE_CHILD "io-test"
#define IA64_I2000_IO_TEST_DEVICE_QOM_PATH \
    IA64_I2000_IO_QTEST_QOM_PATH "/" \
    IA64_I2000_IO_TEST_DEVICE_CHILD

#endif /* HW_IA64_I2000_IO_TEST_H */
