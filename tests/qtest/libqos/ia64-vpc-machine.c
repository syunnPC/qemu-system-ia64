/* SPDX-License-Identifier: GPL-2.0-or-later */
/* PCI device tests on the IA-64 virtual platform. */
#include "qemu/osdep.h"
#include "qemu/module.h"
#include "hw/ia64/ia64_pci.h"
#include "generic-pcihost.h"
#include "libqos-malloc.h"
#include "qgraph.h"
#include "../libqtest.h"

typedef struct QIA64Machine {
    QOSGraphObject obj;
    QGuestAllocator alloc;
    QGenericPCIHost bridge;
} QIA64Machine;

#define IA64_PIO_READ(bits, type) \
static type ia64_pio_read ## bits(QPCIBus *bus, uint32_t port) \
{ \
    return qtest_read ## bits(bus->qts, IA64_LEGACY_IO_PORT_PA(port)); \
}
#define IA64_PIO_WRITE(bits, type) \
static void ia64_pio_write ## bits(QPCIBus *bus, uint32_t port, type value) \
{ \
    qtest_write ## bits(bus->qts, IA64_LEGACY_IO_PORT_PA(port), value); \
}
IA64_PIO_READ(b, uint8_t)
IA64_PIO_READ(w, uint16_t)
IA64_PIO_READ(l, uint32_t)
IA64_PIO_READ(q, uint64_t)
IA64_PIO_WRITE(b, uint8_t)
IA64_PIO_WRITE(w, uint16_t)
IA64_PIO_WRITE(l, uint32_t)
IA64_PIO_WRITE(q, uint64_t)

static void ia64_destructor(QOSGraphObject *obj)
{
    QIA64Machine *machine = (QIA64Machine *)obj;

    alloc_destroy(&machine->alloc);
}

static void *ia64_get_driver(void *obj, const char *interface)
{
    QIA64Machine *machine = obj;

    g_assert_cmpstr(interface, ==, "memory");
    return &machine->alloc;
}

static QOSGraphObject *ia64_get_device(void *obj, const char *device)
{
    QIA64Machine *machine = obj;

    g_assert_cmpstr(device, ==, "ia64-pci-host");
    return &machine->bridge.pci.obj;
}

static void *ia64_pci_get_driver(void *obj, const char *interface)
{
    QGenericPCIBus *pci = obj;

    g_assert_cmpstr(interface, ==, "ia64-pci-bus");
    return &pci->bus;
}

static void *ia64_create(QTestState *qts)
{
    QIA64Machine *machine = g_new0(QIA64Machine, 1);
    QGenericPCIBus *pci = &machine->bridge.pci;

    alloc_init(&machine->alloc, 0, 0x100000, 0x08000000, 0x1000);
    qos_create_generic_pcihost(&machine->bridge, qts, &machine->alloc);
    pci->obj.get_driver = ia64_pci_get_driver;
    pci->ecam_alloc_ptr = IA64_PCI_CONFIG_BASE;
    pci->bus.mmio_alloc_ptr = IA64_PCI_MMIO_BASE + 0x1000000;
    pci->bus.mmio_limit = IA64_PCI_MMIO_BASE + IA64_PCI_MMIO_SIZE;
    pci->bus.pio_alloc_ptr = 0x4000;
    pci->bus.pio_readb = ia64_pio_readb;
    pci->bus.pio_readw = ia64_pio_readw;
    pci->bus.pio_readl = ia64_pio_readl;
    pci->bus.pio_readq = ia64_pio_readq;
    pci->bus.pio_writeb = ia64_pio_writeb;
    pci->bus.pio_writew = ia64_pio_writew;
    pci->bus.pio_writel = ia64_pio_writel;
    pci->bus.pio_writeq = ia64_pio_writeq;
    machine->obj.get_driver = ia64_get_driver;
    machine->obj.get_device = ia64_get_device;
    machine->obj.destructor = ia64_destructor;
    return machine;
}

static void ia64_register_nodes(void)
{
    qos_node_create_machine_args("ia64/ia64-vpc", ia64_create,
                                 " -machine nvram=none -bios none -m 256M "
                                 "-nodefaults -vga none");
    qos_node_contains("ia64/ia64-vpc", "ia64-pci-host", NULL);
    qos_node_create_driver("ia64-pci-host", NULL);
    qos_node_produces("ia64-pci-host", "ia64-pci-bus");
}

libqos_init(ia64_register_nodes);
