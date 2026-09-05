/*
 * HP Integrity rx2660 machine qtest
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/display/ati_regs.h"
#include "hw/ia64/hp_zx6000.h"
#include "hw/ia64/ia64_platform_abi.h"
#include "hw/net/bcm5704.h"
#include "hw/pci/pci.h"
#include "hw/pci-host/hp-zx1-ioa-regs.h"
#include "libqtest.h"
#include "qemu/bswap.h"
#include "qemu/sockets.h"
#include "qemu/timer.h"
#include "qemu/units.h"
#include "qobject/qdict.h"
#include "qobject/qlist.h"

#define RX2660_DESCRIPTOR_GPA UINT64_C(0x00300000)
#define RX2660_PCI_ROOT_COUNT 5U
#define RX2660_LOW_RAM_SIZE   UINT64_C(0x40000000)
#define RX2660_HIGH_RAM_BASE  UINT64_C(0x100000000)
#define RX2660_HIGH_RAM_SIZE  UINT64_C(0xc0000000)
#define RX2660_SPARSE_IO_BASE UINT64_C(0x00000ffffc000000)

#define RX2660_ATI_ES1000_ID  UINT32_C(0x515e1002)
#define RX2660_ATI_MMIO       UINT64_C(0x88020000)
#define RX2660_NEC_OHCI_ID    UINT32_C(0x00351033)
#define RX2660_NEC_EHCI_ID    UINT32_C(0x00e01033)
#define RX2660_LSI_SAS1068_ID UINT32_C(0x00541000)
#define RX2660_BCM5704_ID     UINT32_C(0x164814e4)
#define RX2660_MANAGEMENT_ID  UINT32_C(0x1303103c)
#define RX2660_MP_INTERFACE_ID UINT32_C(0x1302103c)
#define RX2660_CONSOLE_ID     UINT32_C(0x1048103c)
#define RX2660_CONSOLE_MMIO   UINT64_C(0x88033000)
#define RX2660_CONSOLE_RELOCATED_MMIO UINT64_C(0x88035000)

#define UART_RBR_THR_DLL 0
#define UART_IER_DLM     1
#define UART_IIR_FCR     2
#define UART_LCR         3
#define UART_MCR         4
#define UART_LSR         5
#define UART_SCR         7
#define UART_LSR_DR      0x01
#define UART_LSR_EMPTY   0x60
#define UART_IER_RDI     0x01
#define UART_IER_THRI    0x02
#define UART_IIR_NONE    0x01
#define UART_IIR_THRI    0x02
#define UART_IIR_RDI     0x04
#define UART_FCR_CLEAR   0x07
#define UART_LCR_DLAB    0x80
#define UART_LCR_8N1     0x03
#define UART_MCR_LOOP    0x10

#define RX2660_OHCI0_MMIO     UINT64_C(0x88032000)
#define RX2660_OHCI1_MMIO     UINT64_C(0x88031000)
#define RX2660_EHCI_MMIO      UINT64_C(0x88030000)
#define OHCI_RH_DESCRIPTOR_A  0x48
#define EHCI_HCS_PARAMS       0x04
#define OHCI_CONTROL          0x04
#define OHCI_INTR_STATUS      0x0c
#define OHCI_RH_PORT_STATUS_1 0x54
#define OHCI_USB_RESUME       0x40
#define OHCI_USB_SUSPEND      0xc0
#define OHCI_INTR_RHSC        (1U << 6)
#define OHCI_PORT_CCS         (1U << 0)
#define OHCI_PORT_PES         (1U << 1)
#define OHCI_PORT_PSS         (1U << 2)
#define OHCI_PORT_POCI        (1U << 3)
#define OHCI_PORT_PRS         (1U << 4)
#define OHCI_PORT_PSSC        (1U << 18)
#define OHCI_PORT_PRSC        (1U << 20)
#define OHCI_RESUME_SIGNAL_NS (20 * NANOSECONDS_PER_SECOND / 1000)
#define OHCI_RESUME_EOP_NS    (3 * NANOSECONDS_PER_SECOND / 1500000)
#define OHCI_RESUME_RECOVERY_NS (3 * NANOSECONDS_PER_SECOND / 1000)

typedef union RX2660DescriptorStorage {
    uint64_t alignment;
    uint8_t bytes[IA64_PLATFORM_DESC_MAX_SIZE];
} RX2660DescriptorStorage;

static const uint8_t rx2660_bus[RX2660_PCI_ROOT_COUNT] = {
    0, 1, 2, 3, 4,
};

static const uint32_t rx2660_rope[RX2660_PCI_ROOT_COUNT] = {
    0, 2, 3, 6, 7,
};

static const uint64_t rx2660_ioa[RX2660_PCI_ROOT_COUNT] = {
    HP_ZX6000_IOA_BASE + 0 * HP_ZX6000_IOA_STRIDE,
    HP_ZX6000_IOA_BASE + 2 * HP_ZX6000_IOA_STRIDE,
    HP_ZX6000_IOA_BASE + 3 * HP_ZX6000_IOA_STRIDE,
    HP_ZX6000_IOA_BASE + 6 * HP_ZX6000_IOA_STRIDE,
    HP_ZX6000_IOA_BASE + 7 * HP_ZX6000_IOA_STRIDE,
};

static const uint64_t rx2660_mmio_base[RX2660_PCI_ROOT_COUNT] = {
    UINT64_C(0x80000000), UINT64_C(0xa0000000),
    UINT64_C(0xb0000000), UINT64_C(0xe0000000),
    UINT64_C(0xf0000000),
};

static const uint64_t rx2660_mmio_size[RX2660_PCI_ROOT_COUNT] = {
    UINT64_C(0x10000000), UINT64_C(0x10000000),
    UINT64_C(0x10000000), UINT64_C(0x10000000),
    UINT64_C(0x0e000000),
};

static const uint64_t rx2660_mmio64_base[RX2660_PCI_ROOT_COUNT] = {
    UINT64_C(0x80004000000), UINT64_C(0x80204000000),
    UINT64_C(0x80304000000), UINT64_C(0x80604000000),
    UINT64_C(0x80704000000),
};

static const uint32_t rx2660_gsi_base[RX2660_PCI_ROOT_COUNT] = {
    16, 27, 38, 49, 60,
};

static const uint64_t rx2660_io_base[RX2660_PCI_ROOT_COUNT] = {
    0x0000, 0x4000, 0x2000, 0x6000, 0x8000,
};

static uint8_t rx2660_checksum(const void *data, size_t size)
{
    const uint8_t *bytes = data;
    uint8_t sum = 0;
    size_t i;

    for (i = 0; i < size; i++) {
        sum += bytes[i];
    }
    return sum;
}

static bool rx2660_qom_has_child(QTestState *qts, const char *name,
                                 const char *type)
{
    g_autoptr(QDict) response = qtest_qmp(
        qts, "{'execute':'qom-list','arguments':{'path':'/machine'}}");
    g_autofree char *child_type = g_strdup_printf("child<%s>", type);
    QList *children = qdict_get_qlist(response, "return");
    QListEntry *entry;

    QLIST_FOREACH_ENTRY(children, entry) {
        QDict *child = qobject_to(QDict, qlist_entry_obj(entry));

        if (g_str_equal(qdict_get_str(child, "name"), name) &&
            g_str_equal(qdict_get_str(child, "type"), child_type)) {
            return true;
        }
    }
    return false;
}

static char *rx2660_find_unattached_child(QTestState *qts,
                                           const char *qom_type)
{
    g_autoptr(QDict) response = qtest_qmp(
        qts, "{'execute':'qom-list','arguments':"
             " {'path':'/machine/unattached'}}");
    g_autofree char *child_type = g_strdup_printf("child<%s>", qom_type);
    QList *children = qdict_get_qlist(response, "return");
    QListEntry *entry;

    QLIST_FOREACH_ENTRY(children, entry) {
        QDict *child = qobject_to(QDict, qlist_entry_obj(entry));

        if (g_str_equal(qdict_get_str(child, "type"), child_type)) {
            return g_strdup_printf("/machine/unattached/%s",
                                   qdict_get_str(child, "name"));
        }
    }
    return NULL;
}

static void rx2660_assert_machine_identity(QTestState *qts)
{
    g_autoptr(QDict) response = qtest_qmp(
        qts, "{'execute':'query-machines'}");
    QList *machines = qdict_get_qlist(response, "return");
    QListEntry *entry;
    bool found = false;
    unsigned int root;

    QLIST_FOREACH_ENTRY(machines, entry) {
        QDict *machine = qobject_to(QDict, qlist_entry_obj(entry));

        if (g_str_equal(qdict_get_str(machine, "name"), "hp-rx2660")) {
            g_assert_cmpstr(qdict_get_str(machine, "default-cpu-type"), ==,
                            "montecito-9010-ia64-cpu");
            g_assert_cmpint(qdict_get_int(machine, "cpu-max"), ==, 8);
            g_assert_cmpstr(qdict_get_str(machine, "default-ram-id"), ==,
                            "hp-rx2660.ram");
            found = true;
            break;
        }
    }
    g_assert_true(found);
    g_assert_true(rx2660_qom_has_child(qts, "mio", "hp-zx2-mio"));
    g_assert_true(rx2660_qom_has_child(qts, "pdh", "hp-zx6000-pdh"));
    for (root = 0; root < RX2660_PCI_ROOT_COUNT; root++) {
        g_autofree char *name = g_strdup_printf("ioa%u", root);

        g_assert_true(rx2660_qom_has_child(qts, name, "hp-zx1-ioa"));
    }
}

static void rx2660_assert_descriptor(QTestState *qts)
{
    static const struct {
        uint8_t bus;
        uint8_t device;
        uint8_t pin;
        uint32_t gsi;
    } expected_routes[] = {
        { 0, 1, 0, 16 }, /* management serial function */
        { 0, 3, 0, 20 }, /* RN50 */
        { 0, 2, 0, 17 }, /* OHCI function 0 */
        { 0, 2, 1, 18 }, /* OHCI function 1 */
        { 0, 2, 2, 19 }, /* EHCI */
        { 1, 1, 0, 27 }, /* SAS1068 */
        { 1, 2, 0, 29 }, /* BCM5704 function 0 */
        { 1, 2, 1, 30 }, /* BCM5704 function 1 */
    };
    RX2660DescriptorStorage storage = { 0 };
    IA64PlatformDescriptor *descriptor = (void *)storage.bytes;
    const IA64PlatformRamRange *ram;
    const IA64PlatformPciRoot *roots;
    const IA64PlatformIoSapic *sapics;
    const IA64PlatformPciRoute *routes;
    uint32_t total_size;
    unsigned int root;
    unsigned int route;

    qtest_memread(qts, RX2660_DESCRIPTOR_GPA, storage.bytes,
                  sizeof(*descriptor));
    total_size = le32_to_cpu(descriptor->TotalSize);
    g_assert_cmpuint(total_size, >=, sizeof(*descriptor));
    g_assert_cmpuint(total_size, <=, sizeof(storage.bytes));
    qtest_memread(qts, RX2660_DESCRIPTOR_GPA, storage.bytes, total_size);

    g_assert_cmphex(le64_to_cpu(descriptor->Magic), ==,
                    IA64_PLATFORM_DESC_MAGIC);
    g_assert_cmpuint(le32_to_cpu(descriptor->FormatRevision), ==,
                     IA64_PLATFORM_DESC_REVISION);
    g_assert_cmpuint(le32_to_cpu(descriptor->PlatformId), ==,
                     IA64_PLATFORM_ID_HP_RX2660);
    g_assert_cmphex(le32_to_cpu(descriptor->Flags), ==,
                    IA64_PLATFORM_FLAG_NO_MCFG |
                    IA64_PLATFORM_FLAG_QEMU_EXTENSION);
    g_assert_cmphex(le64_to_cpu(descriptor->RamSize), ==, 4 * GiB);
    g_assert_cmphex(le64_to_cpu(descriptor->LowRamEnd), ==,
                    RX2660_LOW_RAM_SIZE);
    g_assert_cmpuint(le32_to_cpu(descriptor->ProcessorCount), ==, 2);
    g_assert_cmpuint(le32_to_cpu(descriptor->SocketCount), ==, 2);
    g_assert_cmpuint(le32_to_cpu(descriptor->CoresPerSocket), ==, 1);
    g_assert_cmpuint(le32_to_cpu(descriptor->ThreadsPerCore), ==, 1);
    g_assert_cmpuint(le32_to_cpu(descriptor->RamRangeCount), ==, 2);
    g_assert_cmpuint(le32_to_cpu(descriptor->PciRootCount), ==,
                     RX2660_PCI_ROOT_COUNT);
    g_assert_cmpuint(le32_to_cpu(descriptor->IoSapicCount), ==,
                     RX2660_PCI_ROOT_COUNT);
    g_assert_cmpuint(le32_to_cpu(descriptor->PciRouteCount), ==,
                     G_N_ELEMENTS(expected_routes));
    g_assert_cmpuint(le32_to_cpu(descriptor->ProfileCount), ==, 0);
    g_assert_cmphex(rx2660_checksum(storage.bytes, total_size), ==, 0);

    ram = (const IA64PlatformRamRange *)(
        storage.bytes + le32_to_cpu(descriptor->RamRangeOffset));
    g_assert_cmphex(le64_to_cpu(ram[0].Base), ==, 0);
    g_assert_cmphex(le64_to_cpu(ram[0].Size), ==, RX2660_LOW_RAM_SIZE);
    g_assert_cmphex(le64_to_cpu(ram[1].Base), ==, RX2660_HIGH_RAM_BASE);
    g_assert_cmphex(le64_to_cpu(ram[1].Size), ==, RX2660_HIGH_RAM_SIZE);

    roots = (const IA64PlatformPciRoot *)(
        storage.bytes + le32_to_cpu(descriptor->PciRootOffset));
    sapics = (const IA64PlatformIoSapic *)(
        storage.bytes + le32_to_cpu(descriptor->IoSapicOffset));
    for (root = 0; root < RX2660_PCI_ROOT_COUNT; root++) {
        g_assert_cmpuint(le16_to_cpu(roots[root].Segment), ==, 0);
        g_assert_cmpuint(roots[root].Bus, ==, rx2660_bus[root]);
        g_assert_cmpuint(roots[root].BusEnd, ==, rx2660_bus[root]);
        g_assert_cmpuint(roots[root].ConfigType, ==,
                         IA64_PLATFORM_PCI_CONFIG_ZX1_LBA);
        g_assert_cmphex(le32_to_cpu(roots[root].Flags), ==,
                        IA64_PLATFORM_PCI_ROOT_FLAG_IDENTITY_DMA |
                        IA64_PLATFORM_PCI_ROOT_FLAG_SPARSE_IO |
                        (root == 0 ?
                         IA64_PLATFORM_PCI_ROOT_FLAG_VGA_LEGACY : 0));
        g_assert_cmphex(le64_to_cpu(roots[root].ConfigBase), ==,
                        rx2660_ioa[root]);
        g_assert_cmphex(le64_to_cpu(roots[root].IoBase), ==,
                        rx2660_io_base[root]);
        g_assert_cmphex(le64_to_cpu(roots[root].IoSize), ==, 0x2000U);
        g_assert_cmphex(le64_to_cpu(roots[root].IoTranslationOffset), ==,
                        RX2660_SPARSE_IO_BASE);
        g_assert_cmphex(le64_to_cpu(roots[root].Mmio32Base), ==,
                        rx2660_mmio_base[root]);
        g_assert_cmphex(le64_to_cpu(roots[root].Mmio32Size), ==,
                        rx2660_mmio_size[root]);
        g_assert_cmphex(le64_to_cpu(roots[root].Mmio64Base), ==,
                        rx2660_mmio64_base[root]);
        g_assert_cmphex(le64_to_cpu(roots[root].Mmio64Size), ==,
                        UINT64_C(0xfc000000));
        g_assert_cmphex(
            le64_to_cpu(roots[root].Mmio64TranslationOffset), ==, 0);
        g_assert_cmphex(le64_to_cpu(roots[root].DmaBase), ==, 0);
        g_assert_cmphex(le64_to_cpu(roots[root].DmaSize), ==,
                        RX2660_LOW_RAM_SIZE);
        g_assert_cmpuint(le32_to_cpu(roots[root].Rope), ==,
                         rx2660_rope[root]);

        g_assert_cmphex(le64_to_cpu(sapics[root].Base), ==,
                        rx2660_ioa[root] +
                        IA64_PLATFORM_ZX1_IO_SAPIC_OFFSET);
        g_assert_cmpuint(le32_to_cpu(sapics[root].GsiBase), ==,
                         rx2660_gsi_base[root]);
        g_assert_cmpuint(le32_to_cpu(sapics[root].RedirectionEntries), ==,
                         10);
        g_assert_cmpuint(sapics[root].Id, ==, rx2660_rope[root]);
    }

    routes = (const IA64PlatformPciRoute *)(
        storage.bytes + le32_to_cpu(descriptor->PciRouteOffset));
    for (route = 0; route < G_N_ELEMENTS(expected_routes); route++) {
        g_assert_cmpuint(le16_to_cpu(routes[route].Segment), ==, 0);
        g_assert_cmpuint(routes[route].Bus, ==,
                         expected_routes[route].bus);
        g_assert_cmpuint(routes[route].Device, ==,
                         expected_routes[route].device);
        g_assert_cmpuint(routes[route].Pin, ==,
                         expected_routes[route].pin);
        g_assert_cmpuint(le32_to_cpu(routes[route].Gsi), ==,
                         expected_routes[route].gsi);
    }
}

static void rx2660_config_select(QTestState *qts, unsigned int root,
                                 unsigned int devfn, unsigned int reg)
{
    uint64_t selector;

    g_assert_cmpuint(root, <, RX2660_PCI_ROOT_COUNT);
    selector = (uint64_t)devfn << 8 | (reg & 0xfc);
    qtest_writeq(qts, rx2660_ioa[root] + HP_ZX1_IOA_CONFIG_ADDRESS,
                 selector);
}

static uint16_t rx2660_config_readw(QTestState *qts, unsigned int root,
                                    unsigned int devfn, unsigned int reg)
{
    rx2660_config_select(qts, root, devfn, reg);
    return qtest_readw(qts, rx2660_ioa[root] +
                       HP_ZX1_IOA_CONFIG_DATA + (reg & 3));
}

static uint8_t rx2660_config_readb(QTestState *qts, unsigned int root,
                                   unsigned int devfn, unsigned int reg)
{
    rx2660_config_select(qts, root, devfn, reg);
    return qtest_readb(qts, rx2660_ioa[root] +
                       HP_ZX1_IOA_CONFIG_DATA + (reg & 3));
}

static uint32_t rx2660_config_readl(QTestState *qts, unsigned int root,
                                    unsigned int devfn, unsigned int reg)
{
    rx2660_config_select(qts, root, devfn, reg);
    return qtest_readl(qts, rx2660_ioa[root] +
                       HP_ZX1_IOA_CONFIG_DATA + (reg & 3));
}

static void rx2660_assert_pci_device(QTestState *qts, unsigned int root,
                                     unsigned int devfn, uint32_t id,
                                     uint16_t command, uint8_t line,
                                     uint8_t pin)
{
    g_assert_cmphex(rx2660_config_readl(qts, root, devfn, PCI_VENDOR_ID),
                    ==, id);
    g_assert_cmphex(rx2660_config_readw(qts, root, devfn, PCI_COMMAND), ==,
                    command);
    g_assert_cmphex(rx2660_config_readw(qts, root, devfn,
                                        PCI_INTERRUPT_LINE), ==,
                    line | (pin << 8));
}

static void rx2660_assert_pci_layout(QTestState *qts)
{
    g_assert_cmphex(rx2660_config_readl(
                        qts, 0, PCI_DEVFN(1, 0), PCI_VENDOR_ID), ==,
                    RX2660_MANAGEMENT_ID);
    g_assert_cmphex(rx2660_config_readl(
                        qts, 0, PCI_DEVFN(1, 0), PCI_CLASS_REVISION) >> 8,
                    ==, 0xff0000);
    g_assert_cmphex(rx2660_config_readl(
                        qts, 0, PCI_DEVFN(1, 0), PCI_SUBSYSTEM_VENDOR_ID),
                    ==, 0x1303103c);
    g_assert_cmphex(rx2660_config_readw(
                        qts, 0, PCI_DEVFN(1, 0), PCI_INTERRUPT_LINE),
                    ==, 0x0100);
    g_assert_cmphex(rx2660_config_readl(
                        qts, 0, PCI_DEVFN(1, 1), PCI_VENDOR_ID), ==,
                    RX2660_MP_INTERFACE_ID);
    g_assert_cmphex(rx2660_config_readl(
                        qts, 0, PCI_DEVFN(1, 1), PCI_CLASS_REVISION) >> 8,
                    ==, 0x078000);
    g_assert_cmphex(rx2660_config_readl(
                        qts, 0, PCI_DEVFN(1, 1), PCI_SUBSYSTEM_VENDOR_ID),
                    ==, 0x1302103c);
    g_assert_cmphex(rx2660_config_readw(
                        qts, 0, PCI_DEVFN(1, 1), PCI_INTERRUPT_LINE),
                    ==, 0x0100);
    g_assert_cmphex(rx2660_config_readl(
                        qts, 0, PCI_DEVFN(1, 1), PCI_BASE_ADDRESS_1), ==,
                    0x88034004);
    g_assert_cmphex(rx2660_config_readl(
                        qts, 0, PCI_DEVFN(1, 1), PCI_BASE_ADDRESS_2), ==, 0);
    g_assert_cmphex(rx2660_config_readl(
                        qts, 0, PCI_DEVFN(1, 1), PCI_BASE_ADDRESS_3), ==,
                    0x8000000c);
    g_assert_cmphex(rx2660_config_readl(
                        qts, 0, PCI_DEVFN(1, 1), PCI_BASE_ADDRESS_4), ==,
                    0x00000800);
    rx2660_assert_pci_device(qts, 0, PCI_DEVFN(1, 2),
                             RX2660_CONSOLE_ID,
                             PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER,
                             0, 1);
    g_assert_cmphex(rx2660_config_readl(
                        qts, 0, PCI_DEVFN(1, 2), PCI_CLASS_REVISION) >> 8,
                    ==, 0x070002);
    g_assert_cmphex(rx2660_config_readl(
                        qts, 0, PCI_DEVFN(1, 2), PCI_SUBSYSTEM_VENDOR_ID),
                    ==, 0x1301103c);
    g_assert_cmphex(rx2660_config_readl(
                        qts, 0, PCI_DEVFN(1, 2), PCI_BASE_ADDRESS_1), ==,
                    0x88033004);
    g_assert_cmphex(rx2660_config_readl(
                        qts, 0, PCI_DEVFN(1, 2), PCI_BASE_ADDRESS_2), ==, 0);

    rx2660_assert_pci_device(qts, 0, PCI_DEVFN(3, 0),
                             RX2660_ATI_ES1000_ID,
                             PCI_COMMAND_IO | PCI_COMMAND_MEMORY |
                             PCI_COMMAND_MASTER, 20, 1);
    g_assert_cmphex(rx2660_config_readl(
                        qts, 0, PCI_DEVFN(3, 0), PCI_BASE_ADDRESS_0), ==,
                    0x80000008);
    g_assert_cmphex(rx2660_config_readl(
                        qts, 0, PCI_DEVFN(3, 0), PCI_BASE_ADDRESS_1), ==,
                    0x00001001);
    g_assert_cmphex(rx2660_config_readl(
                        qts, 0, PCI_DEVFN(3, 0), PCI_BASE_ADDRESS_2), ==,
                    0x88020000);
    g_assert_cmphex(rx2660_config_readl(
                        qts, 0, PCI_DEVFN(3, 0), PCI_ROM_ADDRESS), ==,
                    0x88000000);
    g_assert_cmphex(rx2660_config_readb(qts, 0, PCI_DEVFN(3, 0),
                                        PCI_REVISION_ID), ==, 0x02);
    g_assert_cmphex(rx2660_config_readl(
                        qts, 0, PCI_DEVFN(3, 0), PCI_SUBSYSTEM_VENDOR_ID),
                    ==, 0x1304103c);

    rx2660_assert_pci_device(qts, 0, PCI_DEVFN(2, 0),
                             RX2660_NEC_OHCI_ID,
                             PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER,
                             17, 1);
    rx2660_assert_pci_device(qts, 0, PCI_DEVFN(2, 1),
                             RX2660_NEC_OHCI_ID,
                             PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER,
                             18, 2);
    rx2660_assert_pci_device(qts, 0, PCI_DEVFN(2, 2),
                             RX2660_NEC_EHCI_ID,
                             PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER,
                             19, 3);
    g_assert_cmphex(rx2660_config_readb(qts, 0, PCI_DEVFN(2, 0),
                                        PCI_REVISION_ID), ==, 0x41);
    g_assert_cmphex(rx2660_config_readb(qts, 0, PCI_DEVFN(2, 1),
                                        PCI_REVISION_ID), ==, 0x41);
    g_assert_cmphex(rx2660_config_readb(qts, 0, PCI_DEVFN(2, 2),
                                        PCI_REVISION_ID), ==, 0x02);
    g_assert_cmphex(rx2660_config_readl(
                        qts, 0, PCI_DEVFN(2, 0), PCI_BASE_ADDRESS_0), ==,
                    0x88032000);
    g_assert_cmphex(rx2660_config_readl(
                        qts, 0, PCI_DEVFN(2, 1), PCI_BASE_ADDRESS_0), ==,
                    0x88031000);
    g_assert_cmphex(rx2660_config_readl(
                        qts, 0, PCI_DEVFN(2, 2), PCI_BASE_ADDRESS_0), ==,
                    0x88030000);
    g_assert_cmpuint(qtest_readl(qts, RX2660_OHCI0_MMIO +
                                     OHCI_RH_DESCRIPTOR_A) & 0xff,
                     ==, 3);
    g_assert_cmpuint(qtest_readl(qts, RX2660_OHCI1_MMIO +
                                     OHCI_RH_DESCRIPTOR_A) & 0xff,
                     ==, 2);
    g_assert_cmpuint(qtest_readb(qts, RX2660_EHCI_MMIO +
                                     EHCI_HCS_PARAMS) & 0xf,
                     ==, 5);

    rx2660_assert_pci_device(qts, 1, PCI_DEVFN(1, 0),
                             RX2660_LSI_SAS1068_ID,
                             PCI_COMMAND_IO | PCI_COMMAND_MEMORY |
                             PCI_COMMAND_MASTER, 27, 1);
    g_assert_cmphex(rx2660_config_readb(qts, 1, PCI_DEVFN(1, 0),
                                        PCI_REVISION_ID), ==, 0x01);
    g_assert_cmphex(rx2660_config_readl(
                        qts, 1, PCI_DEVFN(1, 0), PCI_SUBSYSTEM_VENDOR_ID),
                    ==, 0x1312103c);
    g_assert_cmphex(rx2660_config_readl(
                        qts, 1, PCI_DEVFN(1, 0), PCI_BASE_ADDRESS_0), ==,
                    0x00001001);
    g_assert_cmphex(rx2660_config_readl(
                        qts, 1, PCI_DEVFN(1, 0), PCI_BASE_ADDRESS_1), ==,
                    0xa0470004);
    g_assert_cmphex(rx2660_config_readl(
                        qts, 1, PCI_DEVFN(1, 0), PCI_BASE_ADDRESS_2), ==,
                    0);
    g_assert_cmphex(rx2660_config_readl(
                        qts, 1, PCI_DEVFN(1, 0), PCI_BASE_ADDRESS_3), ==,
                    0xa0460004);
    g_assert_cmphex(rx2660_config_readl(
                        qts, 1, PCI_DEVFN(1, 0), PCI_BASE_ADDRESS_4), ==,
                    0);
    g_assert_cmphex(rx2660_config_readl(
                        qts, 1, PCI_DEVFN(1, 0), PCI_ROM_ADDRESS), ==,
                    0xa0000000);

    rx2660_assert_pci_device(qts, 1, PCI_DEVFN(2, 0),
                             RX2660_BCM5704_ID,
                             PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER,
                             29, 1);
    rx2660_assert_pci_device(qts, 1, PCI_DEVFN(2, 1),
                             RX2660_BCM5704_ID,
                             PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER,
                             30, 2);
    g_assert_cmphex(rx2660_config_readb(qts, 1, PCI_DEVFN(2, 0),
                                        PCI_REVISION_ID), ==, 0x10);
    g_assert_cmphex(rx2660_config_readb(qts, 1, PCI_DEVFN(2, 1),
                                        PCI_REVISION_ID), ==, 0x10);
    g_assert_cmphex(rx2660_config_readl(
                        qts, 1, PCI_DEVFN(2, 0), PCI_SUBSYSTEM_VENDOR_ID),
                    ==, 0x164414e4);
    g_assert_cmphex(rx2660_config_readl(
                        qts, 1, PCI_DEVFN(2, 1), PCI_SUBSYSTEM_VENDOR_ID),
                    ==, 0x164414e4);
    g_assert_cmphex(rx2660_config_readl(
                        qts, 1, PCI_DEVFN(2, 0), PCI_BASE_ADDRESS_0), ==,
                    0xa0450004);
    g_assert_cmphex(rx2660_config_readl(
                        qts, 1, PCI_DEVFN(2, 0), PCI_BASE_ADDRESS_1), ==, 0);
    g_assert_cmphex(rx2660_config_readl(
                        qts, 1, PCI_DEVFN(2, 1), PCI_BASE_ADDRESS_0), ==,
                    0xa0440004);
    g_assert_cmphex(rx2660_config_readl(
                        qts, 1, PCI_DEVFN(2, 1), PCI_BASE_ADDRESS_1), ==, 0);
    g_assert_cmphex(rx2660_config_readl(
                        qts, 1, PCI_DEVFN(2, 0), PCI_ROM_ADDRESS), ==,
                    0xa0420000);
    g_assert_cmphex(rx2660_config_readl(
                        qts, 1, PCI_DEVFN(2, 1), PCI_ROM_ADDRESS), ==,
                    0xa0400000);
}

static void rx2660_assert_ohci_port_resume(QTestState *qts)
{
    uint64_t port = RX2660_OHCI0_MMIO + OHCI_RH_PORT_STATUS_1;
    uint32_t status = qtest_readl(qts, port);

    g_assert_cmphex(status & OHCI_PORT_CCS, ==, OHCI_PORT_CCS);
    qtest_writel(qts, port, OHCI_PORT_PSS);
    status = qtest_readl(qts, port);
    g_assert_cmphex(status & OHCI_PORT_PSS, ==, OHCI_PORT_PSS);

    qtest_writel(qts, port, OHCI_PORT_POCI);
    status = qtest_readl(qts, port);
    g_assert_cmphex(status & OHCI_PORT_PES, ==, 0);
    g_assert_cmphex(status & OHCI_PORT_PSS, ==, OHCI_PORT_PSS);
    g_assert_cmphex(status & OHCI_PORT_PSSC, ==, 0);

    qtest_clock_step(qts, OHCI_RESUME_SIGNAL_NS);
    status = qtest_readl(qts, port);
    g_assert_cmphex(status & OHCI_PORT_PSS, ==, OHCI_PORT_PSS);
    g_assert_cmphex(status & OHCI_PORT_PSSC, ==, 0);
    qtest_clock_step(qts,
                     OHCI_RESUME_EOP_NS + OHCI_RESUME_RECOVERY_NS - 1);
    status = qtest_readl(qts, port);
    g_assert_cmphex(status & OHCI_PORT_PSS, ==, OHCI_PORT_PSS);
    g_assert_cmphex(status & OHCI_PORT_PSSC, ==, 0);
    qtest_clock_step(qts, 1);
    status = qtest_readl(qts, port);
    g_assert_cmphex(status & OHCI_PORT_PSS, ==, 0);
    g_assert_cmphex(status & OHCI_PORT_PES, ==, OHCI_PORT_PES);
    g_assert_cmphex(status & OHCI_PORT_PSSC, ==, OHCI_PORT_PSSC);

    qtest_writel(qts, port, OHCI_PORT_PSSC);
    status = qtest_readl(qts, port);
    g_assert_cmphex(status & OHCI_PORT_PSSC, ==, 0);

    /* Global resume clears PSS without setting PSSC. */
    qtest_writel(qts, port, OHCI_PORT_PSS);
    qtest_writel(qts, RX2660_OHCI0_MMIO + OHCI_INTR_STATUS,
                 OHCI_INTR_RHSC);
    qtest_writel(qts, RX2660_OHCI0_MMIO + OHCI_CONTROL,
                 OHCI_USB_SUSPEND);
    qtest_writel(qts, port, OHCI_PORT_POCI);
    qtest_writel(qts, RX2660_OHCI0_MMIO + OHCI_CONTROL,
                 OHCI_USB_RESUME);
    status = qtest_readl(qts, port);
    g_assert_cmphex(status & OHCI_PORT_PES, ==, OHCI_PORT_PES);
    g_assert_cmphex(status & OHCI_PORT_PSS, ==, 0);
    g_assert_cmphex(status & OHCI_PORT_PSSC, ==, 0);
    g_assert_cmphex(qtest_readl(qts, RX2660_OHCI0_MMIO + OHCI_INTR_STATUS) &
                    OHCI_INTR_RHSC, ==, 0);
    qtest_clock_step(qts, OHCI_RESUME_SIGNAL_NS + OHCI_RESUME_EOP_NS +
                     OHCI_RESUME_RECOVERY_NS);
    g_assert_cmphex(qtest_readl(qts, port) & OHCI_PORT_PSSC, ==, 0);

    /* Port reset clears PSS and PSSC. */
    qtest_writel(qts, port, OHCI_PORT_PSSC);
    qtest_writel(qts, port, OHCI_PORT_PSS);
    qtest_writel(qts, RX2660_OHCI0_MMIO + OHCI_INTR_STATUS,
                 OHCI_INTR_RHSC);
    qtest_writel(qts, port, OHCI_PORT_POCI);
    qtest_writel(qts, port, OHCI_PORT_PRS);
    status = qtest_readl(qts, port);
    g_assert_cmphex(status & OHCI_PORT_PES, ==, OHCI_PORT_PES);
    g_assert_cmphex(status & OHCI_PORT_PSS, ==, 0);
    g_assert_cmphex(status & OHCI_PORT_PSSC, ==, 0);
    g_assert_cmphex(status & OHCI_PORT_PRSC, ==, OHCI_PORT_PRSC);
    g_assert_cmphex(qtest_readl(qts, RX2660_OHCI0_MMIO + OHCI_INTR_STATUS) &
                    OHCI_INTR_RHSC, ==, OHCI_INTR_RHSC);
    qtest_clock_step(qts, OHCI_RESUME_SIGNAL_NS + OHCI_RESUME_EOP_NS +
                     OHCI_RESUME_RECOVERY_NS);
    g_assert_cmphex(qtest_readl(qts, port) & OHCI_PORT_PSSC, ==, 0);
}

static void rx2660_assert_start_fails(const char *cpu, const char *smp,
                                     const char *message)
{
    const char *argv[] = {
        qtest_qemu_binary(NULL),
        "-machine", "hp-rx2660,nvram=none,firmware=none",
        "-cpu", cpu,
        "-m", "1G",
        "-smp", smp,
        "-S",
        "-display", "none",
        "-serial", "none",
        "-monitor", "none",
        "-net", "none",
        NULL,
    };
    g_autofree char *stderr_text = NULL;
    g_autoptr(GError) error = NULL;
    int wait_status;

    g_assert_true(g_spawn_sync(NULL, (char **)argv, NULL,
                               G_SPAWN_STDOUT_TO_DEV_NULL,
                               NULL, NULL, NULL, &stderr_text,
                               &wait_status, &error));
    g_assert_no_error(error);
    g_assert_true(WIFEXITED(wait_status));
    g_assert_cmpint(WEXITSTATUS(wait_status), ==, 1);
    g_assert_nonnull(strstr(stderr_text, message));
}

static void test_hp_rx2660_cpu_topology(void)
{
    QTestState *qts;

    rx2660_assert_start_fails(
        "montecito-9010", "2,sockets=1,cores=1,threads=2,maxcpus=2",
        "one thread per core");

    qts = qtest_init(
        "-machine hp-rx2660,nvram=none,firmware=none "
        "-cpu montecito-9040 -m 1G "
        "-smp 4,sockets=1,cores=2,threads=2,maxcpus=4 -S "
        "-display none -serial none -monitor none -net none");
    qtest_quit(qts);
}

static void test_hp_rx2660_smoke_and_pci(void)
{
    QTestState *qts = qtest_init(
        "-machine hp-rx2660,nvram=none,firmware=none "
        "-m 4G -smp 2,sockets=2,cores=1,threads=1 -S "
        "-display none -serial none -monitor none -net none");

    rx2660_assert_machine_identity(qts);
    rx2660_assert_descriptor(qts);
    rx2660_assert_pci_layout(qts);
    qtest_quit(qts);
}

static void test_hp_rx2660_radeon_clocks(void)
{
    QTestState *qts = qtest_init(
        "-machine hp-rx2660,nvram=none,firmware=none "
        "-m 1G -S -display none -serial none -monitor none -net none");
    unsigned int pass, clock;

    for (pass = 0; pass < 2; pass++) {
        uint16_t header = qtest_readw(qts, 0xc0048);
        uint16_t pll = qtest_readw(qts, 0xc0000 + header + 0x30);
        uint32_t divisors;

        qtest_writeb(qts, RX2660_ATI_MMIO + CLOCK_CNTL_INDEX,
                      R100_M_SPLL_REF_FB_DIV);
        divisors = qtest_readl(qts, RX2660_ATI_MMIO + CLOCK_CNTL_DATA);
        for (clock = 0; clock < 2; clock++) {
            uint32_t reference = qtest_readw(qts, 0xc0000 + pll +
                                              (clock ? 0x1a : 0x26));
            uint32_t divider = qtest_readw(qts, 0xc0000 + pll +
                                            (clock ? 0x1c : 0x28));
            uint32_t feedback = (divisors >> (8 + clock * 8)) & 0xff;

            g_assert_cmpuint(divider, >, 0);
            g_assert_cmpuint(divisors & 0xff, ==, divider);
            g_assert_cmpuint(qtest_readw(qts, 0xc0000 + pll +
                                          8 + clock * 2), ==, 20000);
            qtest_writeb(qts, RX2660_ATI_MMIO + CLOCK_CNTL_INDEX,
                          clock ? R100_SCLK_CNTL : R100_MCLK_CNTL);
            g_assert_cmphex(qtest_readl(qts, RX2660_ATI_MMIO +
                                        CLOCK_CNTL_DATA) & 7, ==, 2);
            g_assert_cmpuint(2 * reference * feedback / divider / 2,
                             ==, 20000);
        }
        if (pass == 0) {
            qtest_writeb(qts, RX2660_ATI_MMIO + CLOCK_CNTL_INDEX,
                          PLL_WR_EN | R100_M_SPLL_REF_FB_DIV);
            qtest_writel(qts, RX2660_ATI_MMIO + CLOCK_CNTL_DATA, 0);
            qtest_system_reset(qts);
        }
    }
    qtest_quit(qts);
}

static void test_hp_rx2660_default_usb_input(void)
{
    QTestState *qts = qtest_init(
        "-machine hp-rx2660,nvram=none,firmware=none "
        "-m 4G -smp 2,sockets=2,cores=1,threads=1 -S "
        "-display none -serial none -monitor none -net none");
    g_autofree char *keyboard =
        rx2660_find_unattached_child(qts, "usb-kbd");
    g_autofree char *tablet =
        rx2660_find_unattached_child(qts, "usb-tablet");

    g_assert_nonnull(keyboard);
    g_assert_nonnull(tablet);
    g_assert_false(qtest_qom_get_bool(qts, keyboard, "msos-desc"));
    g_assert_false(qtest_qom_get_bool(qts, tablet, "msos-desc"));
    qtest_quit(qts);
}

static void test_hp_rx2660_ohci_port_resume(void)
{
    QTestState *qts = qtest_init(
        "-machine hp-rx2660,nvram=none,firmware=none "
        "-m 4G -smp 1,sockets=1,cores=1,threads=1 -S "
        "-display none -serial none -monitor none -net none");

    qtest_qmp_assert_success(qts, "{'execute':'cont'}");
    rx2660_assert_ohci_port_resume(qts);
    qtest_quit(qts);
}

static void rx2660_console_assert_irq(QTestState *qts, bool asserted)
{
    uint16_t status = rx2660_config_readw(qts, 0, PCI_DEVFN(1, 2),
                                         PCI_STATUS);

    g_assert_cmphex(status & PCI_STATUS_INTERRUPT, ==,
                    asserted ? PCI_STATUS_INTERRUPT : 0);
}

static void test_hp_rx2660_console(void)
{
    g_autofree char *dir = g_dir_make_tmp("qtest-rx2660-console-XXXXXX", NULL);
    g_autofree char *path = NULL;
    g_autofree char *quoted_path = NULL;
    QTestState *qts;
    uint64_t base = RX2660_CONSOLE_MMIO;
    uint8_t byte;
    int listener;
    int fd;
    int64_t deadline;
    GPollFD pollfd;

    g_assert_nonnull(dir);
    path = g_build_filename(dir, "serial", NULL);
    quoted_path = g_shell_quote(path);
    listener = qtest_socket_server(path);
    qts = qtest_initf(
        "-machine hp-rx2660,nvram=none,firmware=none -m 1G -S "
        "-display vnc=none -monitor none -net none "
        "-chardev socket,id=console,path=%s "
        "-serial none -serial none -serial chardev:console", quoted_path);
    fd = qemu_accept(listener, NULL, NULL);
    g_assert_cmpint(fd, >=, 0);
    close(listener);
    g_assert_cmpint(g_unlink(path), ==, 0);
    g_assert_cmpint(g_rmdir(dir), ==, 0);

    g_assert_cmphex(qtest_readb(qts, base + UART_LSR), ==, UART_LSR_EMPTY);
    g_assert_cmphex(qtest_readb(qts, base + UART_IIR_FCR), ==, UART_IIR_NONE);
    rx2660_console_assert_irq(qts, false);

    /* Exercise the scratch register and divisor latch. */
    qtest_writeb(qts, base + UART_SCR, 0xa5);
    g_assert_cmphex(qtest_readb(qts, base + UART_SCR), ==, 0xa5);
    qtest_writeb(qts, base + UART_LCR, UART_LCR_DLAB | UART_LCR_8N1);
    qtest_writeb(qts, base + UART_RBR_THR_DLL, 1);
    qtest_writeb(qts, base + UART_IER_DLM, 0);
    g_assert_cmphex(qtest_readb(qts, base + UART_RBR_THR_DLL), ==, 1);
    g_assert_cmphex(qtest_readb(qts, base + UART_IER_DLM), ==, 0);
    qtest_writeb(qts, base + UART_LCR, UART_LCR_8N1);
    qtest_writeb(qts, base + UART_IIR_FCR, UART_FCR_CLEAR);

    /* Transmit to the third serial backend and acknowledge its PCI INTA. */
    qtest_writeb(qts, base + UART_IER_DLM, UART_IER_THRI);
    rx2660_console_assert_irq(qts, true);
    g_assert_cmphex(qtest_readb(qts, base + UART_IIR_FCR), ==,
                    0xc0 | UART_IIR_THRI);
    rx2660_console_assert_irq(qts, false);
    qtest_writeb(qts, base + UART_RBR_THR_DLL, 'Q');
    pollfd = (GPollFD) { .fd = fd, .events = G_IO_IN };
    g_assert_cmpint(g_poll(&pollfd, 1, 5000), ==, 1);
    g_assert_cmpint(recv(fd, &byte, 1, 0), ==, 1);
    g_assert_cmphex(byte, ==, 'Q');
    rx2660_console_assert_irq(qts, true);

    /* Receive from the host, including FIFO and interrupt acknowledgement. */
    qtest_writeb(qts, base + UART_IER_DLM, UART_IER_RDI);
    rx2660_console_assert_irq(qts, false);
    g_assert_cmpint(qemu_send_full(fd, "R", 1), ==, 1);
    deadline = g_get_monotonic_time() + 5 * G_TIME_SPAN_SECOND;
    while (!(qtest_readb(qts, base + UART_LSR) & UART_LSR_DR)) {
        g_assert_cmpint(g_get_monotonic_time(), <, deadline);
    }
    rx2660_console_assert_irq(qts, true);
    g_assert_cmphex(qtest_readb(qts, base + UART_IIR_FCR), ==,
                    0xc0 | UART_IIR_RDI);
    g_assert_cmphex(qtest_readb(qts, base + UART_RBR_THR_DLL), ==, 'R');
    rx2660_console_assert_irq(qts, false);

    /* Loopback and FIFO reset work without involving a host backend. */
    qtest_writeb(qts, base + UART_MCR, UART_MCR_LOOP);
    qtest_writeb(qts, base + UART_RBR_THR_DLL, 0x5a);
    g_assert_cmphex(qtest_readb(qts, base + UART_RBR_THR_DLL), ==, 0x5a);
    qtest_writeb(qts, base + UART_RBR_THR_DLL, 0xa5);
    rx2660_console_assert_irq(qts, true);
    qtest_writeb(qts, base + UART_IIR_FCR, UART_FCR_CLEAR);
    g_assert_cmphex(qtest_readb(qts, base + UART_LSR) & UART_LSR_DR, ==, 0);
    rx2660_console_assert_irq(qts, false);

    /* PCI resource reassignment must move the UART along with BAR1. */
    rx2660_config_select(qts, 0, PCI_DEVFN(1, 2), PCI_BASE_ADDRESS_1);
    qtest_writel(qts, rx2660_ioa[0] + HP_ZX1_IOA_CONFIG_DATA,
                 RX2660_CONSOLE_RELOCATED_MMIO);
    g_assert_cmphex(qtest_readb(qts, base + UART_SCR), !=, 0xa5);
    qtest_writeb(qts, base + UART_SCR, 0x5a);
    g_assert_cmphex(qtest_readb(qts, RX2660_CONSOLE_RELOCATED_MMIO + UART_SCR),
                    ==, 0xa5);

    qtest_system_reset(qts);
    g_assert_cmphex(qtest_readb(qts, base + UART_SCR), ==, 0);
    g_assert_cmphex(qtest_readb(qts, base + UART_IER_DLM), ==, 0);
    g_assert_cmphex(qtest_readb(qts, base + UART_LSR), ==, UART_LSR_EMPTY);
    rx2660_console_assert_irq(qts, false);
    close(fd);
    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/hp-rx2660/smoke-and-pci",
                   test_hp_rx2660_smoke_and_pci);
    qtest_add_func("/hp-rx2660/cpu-topology",
                   test_hp_rx2660_cpu_topology);
    qtest_add_func("/hp-rx2660/default-usb-input",
                   test_hp_rx2660_default_usb_input);
    qtest_add_func("/hp-rx2660/ohci-port-resume",
                   test_hp_rx2660_ohci_port_resume);
    qtest_add_func("/hp-rx2660/console", test_hp_rx2660_console);
    qtest_add_func("/hp-rx2660/radeon-clocks", test_hp_rx2660_radeon_clocks);
    return g_test_run();
}
