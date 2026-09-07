/* SPDX-License-Identifier: GPL-2.0-or-later */
/* QTest testcase for the LSI53C1030 Fusion-MPT controller */

#include "qemu/osdep.h"
#include "hw/pci/pci_ids.h"
#include "hw/pci/pci_regs.h"
#include "hw/scsi/mpi.h"
#include "libqtest.h"
#include "qemu/bswap.h"
#include "qemu/module.h"
#include "libqos/qgraph.h"
#include "libqos/pci.h"

typedef struct QMptSpi {
    QOSGraphObject obj;
    QPCIDevice dev;
    QPCIBar bar;
} QMptSpi;

typedef struct MptSpiSnapshotData {
    char *tmpdir;
    char *disk_path;
} MptSpiSnapshotData;

static void *mptspi_get_driver(void *obj, const char *interface)
{
    QMptSpi *mpt = obj;

    if (!g_strcmp0(interface, "pci-device")) {
        return &mpt->dev;
    }

    g_assert_not_reached();
}

static void mptspi_destructor(QOSGraphObject *obj)
{
    QMptSpi *mpt = (QMptSpi *)obj;

    qpci_iounmap(&mpt->dev, mpt->bar);
}

static void *mptspi_create(void *pci_bus, QGuestAllocator *alloc, void *addr)
{
    QMptSpi *mpt = g_new0(QMptSpi, 1);

    qpci_device_init(&mpt->dev, pci_bus, addr);
    qpci_device_enable(&mpt->dev);
    mpt->bar = qpci_iomap(&mpt->dev, 1, NULL);
    mpt->obj.get_driver = mptspi_get_driver;
    mpt->obj.destructor = mptspi_destructor;
    mpt->obj.free = g_free;
    return &mpt->obj;
}

static uint32_t mptspi_probe_rom_size(QPCIDevice *dev)
{
    uint32_t saved_rom = qpci_config_readl(dev, PCI_ROM_ADDRESS);
    uint32_t rom_mask;

    qpci_config_writel(dev, PCI_ROM_ADDRESS, UINT32_MAX);
    rom_mask = qpci_config_readl(dev, PCI_ROM_ADDRESS);
    qpci_config_writel(dev, PCI_ROM_ADDRESS, saved_rom);

    rom_mask &= PCI_ROM_ADDRESS_MASK;
    return rom_mask ? ~rom_mask + 1 : 0;
}

static void mptspi_handshake(QMptSpi *mpt, const void *request,
                             size_t request_size, void *reply,
                             size_t reply_size)
{
    const uint8_t *request_bytes = request;
    uint8_t *reply_bytes = reply;
    size_t i;

    g_assert_cmpuint(request_size % sizeof(uint32_t), ==, 0);
    g_assert_cmpuint(reply_size % sizeof(uint16_t), ==, 0);

    qpci_io_writel(&mpt->dev, mpt->bar, MPI_DOORBELL_OFFSET,
                   (MPI_FUNCTION_HANDSHAKE << MPI_DOORBELL_FUNCTION_SHIFT) |
                   ((request_size / sizeof(uint32_t)) <<
                    MPI_DOORBELL_ADD_DWORDS_SHIFT));
    g_assert_cmphex(qpci_io_readl(&mpt->dev, mpt->bar,
                                  MPI_HOST_INTERRUPT_STATUS_OFFSET) &
                    MPI_HIS_DOORBELL_INTERRUPT, ==,
                    MPI_HIS_DOORBELL_INTERRUPT);
    qpci_io_writel(&mpt->dev, mpt->bar,
                   MPI_HOST_INTERRUPT_STATUS_OFFSET, 0);

    for (i = 0; i < request_size; i += sizeof(uint32_t)) {
        qpci_io_writel(&mpt->dev, mpt->bar, MPI_DOORBELL_OFFSET,
                       ldl_le_p(request_bytes + i));
    }

    g_assert_cmphex(qpci_io_readl(&mpt->dev, mpt->bar,
                                  MPI_HOST_INTERRUPT_STATUS_OFFSET) &
                    MPI_HIS_DOORBELL_INTERRUPT, ==,
                    MPI_HIS_DOORBELL_INTERRUPT);
    for (i = 0; i < reply_size; i += sizeof(uint16_t)) {
        stw_le_p(reply_bytes + i,
                 qpci_io_readl(&mpt->dev, mpt->bar,
                               MPI_DOORBELL_OFFSET));
        qpci_io_writel(&mpt->dev, mpt->bar,
                       MPI_HOST_INTERRUPT_STATUS_OFFSET, 0);
    }
}

static void mptspi_ioc_init(QMptSpi *mpt)
{
    MPIMsgIOCInit request = { 0 };
    MPIMsgIOCInitReply reply;

    request.WhoInit = MPI_WHOINIT_HOST_DRIVER;
    request.Function = MPI_FUNCTION_IOC_INIT;
    request.MaxDevices = 16;
    request.MaxBuses = 1;
    request.ReplyFrameSize = cpu_to_le16(80);
    request.MsgVersion = cpu_to_le16(0x0105);
    mptspi_handshake(mpt, &request, sizeof(request), &reply, sizeof(reply));
    g_assert_cmphex(le16_to_cpu(reply.IOCStatus), ==, MPI_IOCSTATUS_SUCCESS);
    g_assert_cmphex(qpci_io_readl(&mpt->dev, mpt->bar,
                                  MPI_DOORBELL_OFFSET) &
                    MPI_IOC_STATE_OPERATIONAL, ==,
                    MPI_IOC_STATE_OPERATIONAL);
}

static MPIMsgConfigReply mptspi_config(QMptSpi *mpt, uint8_t action,
                                       uint8_t page_type, uint8_t page_number,
                                       uint32_t page_address,
                                       uint64_t buffer_address,
                                       uint32_t buffer_length, bool write)
{
    MPIMsgConfig request = { 0 };
    MPIMsgConfigReply reply;
    uint32_t flags = MPI_SGE_FLAGS_SIMPLE_ELEMENT |
                     MPI_SGE_FLAGS_LAST_ELEMENT |
                     MPI_SGE_FLAGS_END_OF_BUFFER |
                     MPI_SGE_FLAGS_END_OF_LIST;

    g_assert_cmpuint(buffer_address, <=, UINT32_MAX);
    request.Action = action;
    request.Function = MPI_FUNCTION_CONFIG;
    request.PageNumber = page_number;
    request.PageType = page_type;
    request.PageAddress = cpu_to_le32(page_address);
    if (write) {
        flags |= MPI_SGE_FLAGS_HOST_TO_IOC;
    }
    request.PageBufferSGE.FlagsLength =
        cpu_to_le32(flags | buffer_length);
    request.PageBufferSGE.u.Address32 = cpu_to_le32(buffer_address);

    mptspi_handshake(mpt, &request, sizeof(request), &reply, sizeof(reply));
    return reply;
}

static void mptspi_test_facts(void *obj, void *data,
                              QGuestAllocator *alloc)
{
    QMptSpi *mpt = obj;
    QPCIBar diag_bar;
    MPIMsgIOCFacts facts_request = { 0 };
    MPIMsgIOCFactsReply facts_reply;
    MPIMsgPortFacts port_request = { 0 };
    MPIMsgPortFactsReply port_reply;
    uint16_t product_id = MPI_FW_HEADER_PID_TYPE_SCSI |
                          MPI_FW_HEADER_PID_PROD_INITIATOR_SCSI |
                          MPI_FW_HEADER_PID_FAMILY_1030C0_SCSI;
    uint64_t diag_size;

    g_assert_cmphex(qpci_config_readw(&mpt->dev, PCI_VENDOR_ID), ==,
                    PCI_VENDOR_ID_LSI_LOGIC);
    g_assert_cmphex(qpci_config_readw(&mpt->dev, PCI_DEVICE_ID), ==,
                    PCI_DEVICE_ID_LSI_53C1030);
    g_assert_cmphex(qpci_config_readb(&mpt->dev, PCI_REVISION_ID), ==, 0x07);
    g_assert_cmphex(qpci_config_readw(&mpt->dev,
                                      PCI_SUBSYSTEM_VENDOR_ID), ==,
                    PCI_VENDOR_ID_LSI_LOGIC);
    g_assert_cmphex(qpci_config_readw(&mpt->dev, PCI_SUBSYSTEM_ID), ==,
                    0x1000);
    g_assert_cmphex(qpci_config_readl(&mpt->dev, PCI_BASE_ADDRESS_1) &
                    PCI_BASE_ADDRESS_MEM_TYPE_MASK, ==,
                    PCI_BASE_ADDRESS_MEM_TYPE_32);
    g_assert_cmphex(qpci_config_readl(&mpt->dev, PCI_BASE_ADDRESS_2) &
                    PCI_BASE_ADDRESS_MEM_TYPE_MASK, ==,
                    PCI_BASE_ADDRESS_MEM_TYPE_32);
    diag_bar = qpci_iomap(&mpt->dev, 2, &diag_size);
    g_assert_cmpuint(diag_size, ==, 0x10000);
    qpci_iounmap(&mpt->dev, diag_bar);
    g_assert_cmpuint(mptspi_probe_rom_size(&mpt->dev), ==, 0);
    g_assert_cmphex(qpci_io_readl(&mpt->dev, mpt->bar,
                                  MPI_DOORBELL_OFFSET) &
                    MPI_IOC_STATE_READY, ==, MPI_IOC_STATE_READY);

    facts_request.Function = MPI_FUNCTION_IOC_FACTS;
    mptspi_handshake(mpt, &facts_request, sizeof(facts_request),
                     &facts_reply, sizeof(facts_reply));
    g_assert_cmphex(le16_to_cpu(facts_reply.IOCStatus), ==,
                    MPI_IOCSTATUS_SUCCESS);
    g_assert_cmphex(le16_to_cpu(facts_reply.ProductID), ==, product_id);
    g_assert_cmpuint(facts_reply.NumberOfPorts, ==, 1);
    g_assert_cmpuint(facts_reply.MaxDevices, ==, 16);
    g_assert_cmpuint(facts_reply.MaxBuses, ==, 1);

    port_request.Function = MPI_FUNCTION_PORT_FACTS;
    port_request.PortNumber = 0;
    mptspi_handshake(mpt, &port_request, sizeof(port_request),
                     &port_reply, sizeof(port_reply));
    g_assert_cmphex(le16_to_cpu(port_reply.IOCStatus), ==,
                    MPI_IOCSTATUS_SUCCESS);
    g_assert_cmphex(port_reply.PortType, ==, MPI_PORTFACTS_PORTTYPE_SCSI);
    g_assert_cmpuint(le16_to_cpu(port_reply.MaxDevices), ==, 16);
    g_assert_cmpuint(le16_to_cpu(port_reply.PortSCSIID), ==, 7);
    g_assert_cmphex(le16_to_cpu(port_reply.ProtocolFlags), ==,
                    MPI_PORTFACTS_PROTOCOL_INITIATOR);
}

static void mptspi_test_config_and_reset(void *obj, void *data,
                                         QGuestAllocator *alloc)
{
    QMptSpi *mpt = obj;
    MPIMsgConfigReply reply;
    uint8_t port_page[12];
    uint8_t port_page_1[16] = { 0 };
    uint8_t port_page_2[76];
    uint8_t io_unit_page_1[8];
    uint8_t ioc_page_1[16] = { 0 };
    uint8_t device_page_0[12];
    uint8_t device_page_1[16] = { 0 };
    uint64_t page_address;
    uint32_t capabilities;
    const uint32_t requested_params = 0x20ff0807;
    const uint8_t diag_keys[] = { 0x04, 0x0b, 0x02, 0x07, 0x0d };
    int i;

    mptspi_ioc_init(mpt);
    page_address = guest_alloc(alloc, 76);

    reply = mptspi_config(mpt, MPI_CONFIG_ACTION_PAGE_HEADER,
                          MPI_CONFIG_PAGETYPE_SCSI_PORT, 0, 0, 0, 0, false);
    g_assert_cmphex(le16_to_cpu(reply.IOCStatus), ==, MPI_IOCSTATUS_SUCCESS);
    g_assert_cmpuint(reply.PageVersion, ==, 0x02);
    g_assert_cmpuint(reply.PageLength, ==, sizeof(port_page) / 4);

    reply = mptspi_config(mpt, MPI_CONFIG_ACTION_PAGE_READ_CURRENT,
                          MPI_CONFIG_PAGETYPE_SCSI_PORT, 0, 0,
                          page_address, sizeof(port_page), false);
    g_assert_cmphex(le16_to_cpu(reply.IOCStatus), ==, MPI_IOCSTATUS_SUCCESS);
    qtest_memread(mpt->dev.bus->qts, page_address, port_page,
                  sizeof(port_page));
    capabilities = ldl_le_p(port_page + 4);
    g_assert_cmphex(capabilities & MPI_SCSIPORTPAGE0_CAP_WIDE, ==,
                    MPI_SCSIPORTPAGE0_CAP_WIDE);
    g_assert_cmphex(ldl_le_p(port_page + 8), ==,
                    MPI_SCSIPORTPAGE0_PHY_SIGNAL_LVD);

    reply = mptspi_config(mpt, MPI_CONFIG_ACTION_PAGE_HEADER,
                          MPI_CONFIG_PAGETYPE_SCSI_PORT, 1, 0,
                          0, 0, false);
    g_assert_cmphex(le16_to_cpu(reply.IOCStatus), ==,
                    MPI_IOCSTATUS_SUCCESS);
    g_assert_cmpuint(reply.PageVersion, ==, 0x03);
    g_assert_cmpuint(reply.PageLength, ==, sizeof(port_page_1) / 4);
    g_assert_cmphex(reply.PageType, ==,
                    MPI_CONFIG_PAGEATTR_CHANGEABLE |
                    MPI_CONFIG_PAGETYPE_SCSI_PORT);

    port_page_1[0] = 0x03;
    port_page_1[1] = sizeof(port_page_1) / 4;
    port_page_1[2] = 1;
    port_page_1[3] = MPI_CONFIG_PAGEATTR_CHANGEABLE |
                     MPI_CONFIG_PAGETYPE_SCSI_PORT;
    stl_le_p(port_page_1 + 4,
             6 |
             (1U << (6 +
                     MPI_SCSIPORTPAGE1_CFG_SHIFT_PORT_RESPONSE_ID)));
    qtest_memwrite(mpt->dev.bus->qts, page_address, port_page_1,
                   sizeof(port_page_1));
    reply = mptspi_config(mpt, MPI_CONFIG_ACTION_PAGE_WRITE_CURRENT,
                          MPI_CONFIG_PAGETYPE_SCSI_PORT, 1, 0,
                          page_address, sizeof(port_page_1), true);
    g_assert_cmphex(le16_to_cpu(reply.IOCStatus), ==,
                    MPI_IOCSTATUS_SUCCESS);

    memset(port_page_1, 0, sizeof(port_page_1));
    reply = mptspi_config(mpt, MPI_CONFIG_ACTION_PAGE_READ_CURRENT,
                          MPI_CONFIG_PAGETYPE_SCSI_PORT, 1, 0,
                          page_address, sizeof(port_page_1), false);
    g_assert_cmphex(le16_to_cpu(reply.IOCStatus), ==,
                    MPI_IOCSTATUS_SUCCESS);
    qtest_memread(mpt->dev.bus->qts, page_address, port_page_1,
                  sizeof(port_page_1));
    g_assert_cmphex(ldl_le_p(port_page_1 + 4), ==, 0x00400006);

    reply = mptspi_config(mpt, MPI_CONFIG_ACTION_PAGE_READ_DEFAULT,
                          MPI_CONFIG_PAGETYPE_SCSI_PORT, 1, 0,
                          page_address, sizeof(port_page_1), false);
    g_assert_cmphex(le16_to_cpu(reply.IOCStatus), ==,
                    MPI_IOCSTATUS_SUCCESS);
    qtest_memread(mpt->dev.bus->qts, page_address, port_page_1,
                  sizeof(port_page_1));
    g_assert_cmphex(ldl_le_p(port_page_1 + 4), ==, 0x00800007);

    port_page_1[12] = 1;
    qtest_memwrite(mpt->dev.bus->qts, page_address, port_page_1,
                   sizeof(port_page_1));
    reply = mptspi_config(mpt, MPI_CONFIG_ACTION_PAGE_WRITE_CURRENT,
                          MPI_CONFIG_PAGETYPE_SCSI_PORT, 1, 0,
                          page_address, sizeof(port_page_1), true);
    g_assert_cmphex(le16_to_cpu(reply.IOCStatus), ==,
                    MPI_IOCSTATUS_CONFIG_INVALID_DATA);

    reply = mptspi_config(mpt, MPI_CONFIG_ACTION_PAGE_READ_CURRENT,
                          MPI_CONFIG_PAGETYPE_SCSI_PORT, 1, 0,
                          page_address, sizeof(port_page_1), false);
    g_assert_cmphex(le16_to_cpu(reply.IOCStatus), ==,
                    MPI_IOCSTATUS_SUCCESS);
    qtest_memread(mpt->dev.bus->qts, page_address, port_page_1,
                  sizeof(port_page_1));
    g_assert_cmphex(ldl_le_p(port_page_1 + 4), ==, 0x00400006);

    port_page_1[13] = 1;
    qtest_memwrite(mpt->dev.bus->qts, page_address, port_page_1,
                   sizeof(port_page_1));
    reply = mptspi_config(mpt, MPI_CONFIG_ACTION_PAGE_WRITE_CURRENT,
                          MPI_CONFIG_PAGETYPE_SCSI_PORT, 1, 0,
                          page_address, sizeof(port_page_1), true);
    g_assert_cmphex(le16_to_cpu(reply.IOCStatus), ==,
                    MPI_IOCSTATUS_CONFIG_INVALID_DATA);

    reply = mptspi_config(mpt, MPI_CONFIG_ACTION_PAGE_DEFAULT,
                          MPI_CONFIG_PAGETYPE_SCSI_PORT, 1, 0,
                          0, 0, false);
    g_assert_cmphex(le16_to_cpu(reply.IOCStatus), ==,
                    MPI_IOCSTATUS_SUCCESS);
    reply = mptspi_config(mpt, MPI_CONFIG_ACTION_PAGE_READ_CURRENT,
                          MPI_CONFIG_PAGETYPE_SCSI_PORT, 1, 0,
                          page_address, sizeof(port_page_1), false);
    g_assert_cmphex(le16_to_cpu(reply.IOCStatus), ==,
                    MPI_IOCSTATUS_SUCCESS);
    qtest_memread(mpt->dev.bus->qts, page_address, port_page_1,
                  sizeof(port_page_1));
    g_assert_cmphex(ldl_le_p(port_page_1 + 4), ==, 0x00800007);
    g_assert_cmpuint(ldl_le_p(port_page_1 + 8), ==, 0);

    stl_le_p(port_page_1 + 4, 0x00400006);
    stl_le_p(port_page_1 + 8, 0x12345678);
    qtest_memwrite(mpt->dev.bus->qts, page_address, port_page_1,
                   sizeof(port_page_1));
    reply = mptspi_config(mpt, MPI_CONFIG_ACTION_PAGE_WRITE_CURRENT,
                          MPI_CONFIG_PAGETYPE_SCSI_PORT, 1, 0,
                          page_address, sizeof(port_page_1), true);
    g_assert_cmphex(le16_to_cpu(reply.IOCStatus), ==,
                    MPI_IOCSTATUS_SUCCESS);

    reply = mptspi_config(mpt, MPI_CONFIG_ACTION_PAGE_HEADER,
                          MPI_CONFIG_PAGETYPE_SCSI_PORT, 2, 0,
                          0, 0, false);
    g_assert_cmphex(le16_to_cpu(reply.IOCStatus), ==, MPI_IOCSTATUS_SUCCESS);
    g_assert_cmpuint(reply.PageVersion, ==, 0x02);
    g_assert_cmpuint(reply.PageLength, ==, sizeof(port_page_2) / 4);
    reply = mptspi_config(mpt, MPI_CONFIG_ACTION_PAGE_READ_NVRAM,
                          MPI_CONFIG_PAGETYPE_SCSI_PORT, 2, 0,
                          page_address, sizeof(port_page_2), false);
    g_assert_cmphex(le16_to_cpu(reply.IOCStatus), ==, MPI_IOCSTATUS_SUCCESS);
    qtest_memread(mpt->dev.bus->qts, page_address, port_page_2,
                  sizeof(port_page_2));
    g_assert_cmphex(ldl_le_p(port_page_2 + 8), ==,
                    MPI_SCSIPORTPAGE2_PORT_BIOS_OS_INIT_HBA | 7);
    g_assert_cmpuint(port_page_2[13], ==, 0x08);
    g_assert_cmphex(lduw_le_p(port_page_2 + 14), ==, 0x000f);
    g_assert_cmpuint(port_page_2[73], ==, 0x08);
    g_assert_cmphex(lduw_le_p(port_page_2 + 74), ==, 0x000f);

    reply = mptspi_config(mpt, MPI_CONFIG_ACTION_PAGE_READ_CURRENT,
                          MPI_CONFIG_PAGETYPE_IO_UNIT, 1, 0,
                          page_address, sizeof(io_unit_page_1), false);
    g_assert_cmphex(le16_to_cpu(reply.IOCStatus), ==, MPI_IOCSTATUS_SUCCESS);
    qtest_memread(mpt->dev.bus->qts, page_address, io_unit_page_1,
                  sizeof(io_unit_page_1));
    g_assert_cmphex(ldl_le_p(io_unit_page_1 + 4), ==,
                    MPI_IOUNITPAGE1_MULTI_FUNCTION |
                    MPI_IOUNITPAGE1_DISABLE_IR);

    reply = mptspi_config(mpt, MPI_CONFIG_ACTION_PAGE_HEADER,
                          MPI_CONFIG_PAGETYPE_IOC, 1, 0, 0, 0, false);
    g_assert_cmphex(le16_to_cpu(reply.IOCStatus), ==,
                    MPI_IOCSTATUS_SUCCESS);
    g_assert_cmpuint(reply.PageVersion, ==, 0x03);
    g_assert_cmpuint(reply.PageLength, ==, sizeof(ioc_page_1) / 4);
    g_assert_cmphex(reply.PageType, ==,
                    MPI_CONFIG_PAGEATTR_CHANGEABLE |
                    MPI_CONFIG_PAGETYPE_IOC);

    ioc_page_1[0] = 0x03;
    ioc_page_1[1] = sizeof(ioc_page_1) / 4;
    ioc_page_1[2] = 1;
    ioc_page_1[3] = MPI_CONFIG_PAGEATTR_CHANGEABLE |
                    MPI_CONFIG_PAGETYPE_IOC;
    stl_le_p(ioc_page_1 + 4, MPI_IOCPAGE1_REPLY_COALESCING);
    stl_le_p(ioc_page_1 + 8, 10);
    ioc_page_1[12] = 4;
    qtest_memwrite(mpt->dev.bus->qts, page_address, ioc_page_1,
                   sizeof(ioc_page_1));
    reply = mptspi_config(mpt, MPI_CONFIG_ACTION_PAGE_WRITE_CURRENT,
                          MPI_CONFIG_PAGETYPE_IOC, 1, 0, page_address,
                          sizeof(ioc_page_1), true);
    g_assert_cmphex(le16_to_cpu(reply.IOCStatus), ==,
                    MPI_IOCSTATUS_SUCCESS);

    memset(ioc_page_1, 0, sizeof(ioc_page_1));
    reply = mptspi_config(mpt, MPI_CONFIG_ACTION_PAGE_READ_CURRENT,
                          MPI_CONFIG_PAGETYPE_IOC, 1, 0, page_address,
                          sizeof(ioc_page_1), false);
    g_assert_cmphex(le16_to_cpu(reply.IOCStatus), ==,
                    MPI_IOCSTATUS_SUCCESS);
    qtest_memread(mpt->dev.bus->qts, page_address, ioc_page_1,
                  sizeof(ioc_page_1));
    g_assert_cmphex(ldl_le_p(ioc_page_1 + 4), ==,
                    MPI_IOCPAGE1_REPLY_COALESCING);
    g_assert_cmpuint(ldl_le_p(ioc_page_1 + 8), ==, 10);
    g_assert_cmpuint(ioc_page_1[12], ==, 4);

    reply = mptspi_config(mpt, MPI_CONFIG_ACTION_PAGE_READ_DEFAULT,
                          MPI_CONFIG_PAGETYPE_IOC, 1, 0, page_address,
                          sizeof(ioc_page_1), false);
    g_assert_cmphex(le16_to_cpu(reply.IOCStatus), ==,
                    MPI_IOCSTATUS_SUCCESS);
    qtest_memread(mpt->dev.bus->qts, page_address, ioc_page_1,
                  sizeof(ioc_page_1));
    g_assert_cmpuint(ldl_le_p(ioc_page_1 + 4), ==, 0);
    g_assert_cmpuint(ldl_le_p(ioc_page_1 + 8), ==, 0);
    g_assert_cmpuint(ioc_page_1[12], ==, 0);

    stl_le_p(ioc_page_1 + 4,
             MPI_IOCPAGE1_INITIATOR_CONTEXT_REPLY_DISABLE);
    qtest_memwrite(mpt->dev.bus->qts, page_address, ioc_page_1,
                   sizeof(ioc_page_1));
    reply = mptspi_config(mpt, MPI_CONFIG_ACTION_PAGE_WRITE_CURRENT,
                          MPI_CONFIG_PAGETYPE_IOC, 1, 0, page_address,
                          sizeof(ioc_page_1), true);
    g_assert_cmphex(le16_to_cpu(reply.IOCStatus), ==,
                    MPI_IOCSTATUS_CONFIG_INVALID_DATA);

    reply = mptspi_config(mpt, MPI_CONFIG_ACTION_PAGE_READ_CURRENT,
                          MPI_CONFIG_PAGETYPE_IOC, 1, 0, page_address,
                          sizeof(ioc_page_1), false);
    g_assert_cmphex(le16_to_cpu(reply.IOCStatus), ==,
                    MPI_IOCSTATUS_SUCCESS);
    qtest_memread(mpt->dev.bus->qts, page_address, ioc_page_1,
                  sizeof(ioc_page_1));
    g_assert_cmphex(ldl_le_p(ioc_page_1 + 4), ==,
                    MPI_IOCPAGE1_REPLY_COALESCING);

    reply = mptspi_config(mpt, MPI_CONFIG_ACTION_PAGE_DEFAULT,
                          MPI_CONFIG_PAGETYPE_IOC, 1, 0, 0, 0, false);
    g_assert_cmphex(le16_to_cpu(reply.IOCStatus), ==,
                    MPI_IOCSTATUS_SUCCESS);
    reply = mptspi_config(mpt, MPI_CONFIG_ACTION_PAGE_READ_CURRENT,
                          MPI_CONFIG_PAGETYPE_IOC, 1, 0, page_address,
                          sizeof(ioc_page_1), false);
    g_assert_cmphex(le16_to_cpu(reply.IOCStatus), ==,
                    MPI_IOCSTATUS_SUCCESS);
    qtest_memread(mpt->dev.bus->qts, page_address, ioc_page_1,
                  sizeof(ioc_page_1));
    g_assert_cmpuint(ldl_le_p(ioc_page_1 + 4), ==, 0);
    g_assert_cmpuint(ldl_le_p(ioc_page_1 + 8), ==, 0);
    g_assert_cmpuint(ioc_page_1[12], ==, 0);

    stl_le_p(ioc_page_1 + 4, MPI_IOCPAGE1_REPLY_COALESCING);
    stl_le_p(ioc_page_1 + 8, 10);
    ioc_page_1[12] = 4;
    qtest_memwrite(mpt->dev.bus->qts, page_address, ioc_page_1,
                   sizeof(ioc_page_1));
    reply = mptspi_config(mpt, MPI_CONFIG_ACTION_PAGE_WRITE_CURRENT,
                          MPI_CONFIG_PAGETYPE_IOC, 1, 0, page_address,
                          sizeof(ioc_page_1), true);
    g_assert_cmphex(le16_to_cpu(reply.IOCStatus), ==,
                    MPI_IOCSTATUS_SUCCESS);

    reply = mptspi_config(mpt, MPI_CONFIG_ACTION_PAGE_HEADER,
                          MPI_CONFIG_PAGETYPE_IOC, 4, 0, 0, 0, false);
    g_assert_cmphex(le16_to_cpu(reply.IOCStatus), ==, MPI_IOCSTATUS_SUCCESS);
    g_assert_cmpuint(reply.PageLength, ==, 2);
    reply = mptspi_config(mpt, MPI_CONFIG_ACTION_PAGE_DEFAULT,
                          MPI_CONFIG_PAGETYPE_IOC, 4, 0, 0, 0, false);
    g_assert_cmphex(le16_to_cpu(reply.IOCStatus), ==,
                    MPI_IOCSTATUS_CONFIG_CANT_COMMIT);

    reply = mptspi_config(mpt, MPI_CONFIG_ACTION_PAGE_HEADER,
                          MPI_CONFIG_PAGETYPE_SCSI_DEVICE, 1, 0,
                          0, 0, false);
    g_assert_cmphex(le16_to_cpu(reply.IOCStatus), ==, MPI_IOCSTATUS_SUCCESS);
    g_assert_cmpuint(reply.PageVersion, ==, 0x05);
    g_assert_cmpuint(reply.PageLength, ==, sizeof(device_page_1) / 4);

    device_page_1[0] = 0x05;
    device_page_1[1] = sizeof(device_page_1) / 4;
    device_page_1[2] = 1;
    device_page_1[3] = MPI_CONFIG_PAGEATTR_CHANGEABLE |
                       MPI_CONFIG_PAGETYPE_SCSI_DEVICE;
    stl_le_p(device_page_1 + 4, requested_params);
    stl_le_p(device_page_1 + 12, 0x08);
    qtest_memwrite(mpt->dev.bus->qts, page_address, device_page_1,
                   sizeof(device_page_1));
    reply = mptspi_config(mpt, MPI_CONFIG_ACTION_PAGE_WRITE_CURRENT,
                          MPI_CONFIG_PAGETYPE_SCSI_DEVICE, 1, 2,
                          page_address, sizeof(device_page_1), true);
    g_assert_cmphex(le16_to_cpu(reply.IOCStatus), ==, MPI_IOCSTATUS_SUCCESS);

    reply = mptspi_config(mpt, MPI_CONFIG_ACTION_PAGE_READ_CURRENT,
                          MPI_CONFIG_PAGETYPE_SCSI_DEVICE, 0, 2,
                          page_address, sizeof(device_page_0), false);
    g_assert_cmphex(le16_to_cpu(reply.IOCStatus), ==, MPI_IOCSTATUS_SUCCESS);
    qtest_memread(mpt->dev.bus->qts, page_address, device_page_0,
                  sizeof(device_page_0));
    g_assert_cmphex(ldl_le_p(device_page_0 + 4), ==, requested_params);

    stl_le_p(device_page_1 + 8, 1);
    qtest_memwrite(mpt->dev.bus->qts, page_address, device_page_1,
                   sizeof(device_page_1));
    reply = mptspi_config(mpt, MPI_CONFIG_ACTION_PAGE_WRITE_CURRENT,
                          MPI_CONFIG_PAGETYPE_SCSI_DEVICE, 1, 2,
                          page_address, sizeof(device_page_1), true);
    g_assert_cmphex(le16_to_cpu(reply.IOCStatus), ==,
                    MPI_IOCSTATUS_CONFIG_INVALID_DATA);
    stl_le_p(device_page_1 + 8, 0);

    reply = mptspi_config(mpt, MPI_CONFIG_ACTION_PAGE_READ_CURRENT,
                          MPI_CONFIG_PAGETYPE_SCSI_DEVICE, 1, 0x00010002,
                          page_address, sizeof(device_page_1), false);
    g_assert_cmphex(le16_to_cpu(reply.IOCStatus), ==,
                    MPI_IOCSTATUS_CONFIG_INVALID_PAGE);

    reply = mptspi_config(mpt, MPI_CONFIG_ACTION_PAGE_READ_DEFAULT,
                          MPI_CONFIG_PAGETYPE_SCSI_DEVICE, 1, 2,
                          page_address, sizeof(device_page_1), false);
    g_assert_cmphex(le16_to_cpu(reply.IOCStatus), ==,
                    MPI_IOCSTATUS_SUCCESS);
    qtest_memread(mpt->dev.bus->qts, page_address, device_page_1,
                  sizeof(device_page_1));
    g_assert_cmpuint(ldl_le_p(device_page_1 + 4), ==, 0);
    g_assert_cmpuint(ldl_le_p(device_page_1 + 12), ==, 0);

    reply = mptspi_config(mpt, MPI_CONFIG_ACTION_PAGE_DEFAULT,
                          MPI_CONFIG_PAGETYPE_SCSI_DEVICE, 1, 2,
                          0, 0, false);
    g_assert_cmphex(le16_to_cpu(reply.IOCStatus), ==,
                    MPI_IOCSTATUS_SUCCESS);
    reply = mptspi_config(mpt, MPI_CONFIG_ACTION_PAGE_READ_CURRENT,
                          MPI_CONFIG_PAGETYPE_SCSI_DEVICE, 0, 2,
                          page_address, sizeof(device_page_0), false);
    g_assert_cmphex(le16_to_cpu(reply.IOCStatus), ==,
                    MPI_IOCSTATUS_SUCCESS);
    qtest_memread(mpt->dev.bus->qts, page_address, device_page_0,
                  sizeof(device_page_0));
    g_assert_cmpuint(ldl_le_p(device_page_0 + 4), ==, 0);

    memset(device_page_1, 0, sizeof(device_page_1));
    device_page_1[0] = 0x05;
    device_page_1[1] = sizeof(device_page_1) / 4;
    device_page_1[2] = 1;
    device_page_1[3] = MPI_CONFIG_PAGEATTR_CHANGEABLE |
                       MPI_CONFIG_PAGETYPE_SCSI_DEVICE;
    stl_le_p(device_page_1 + 4, requested_params);
    stl_le_p(device_page_1 + 12, 0x08);
    qtest_memwrite(mpt->dev.bus->qts, page_address, device_page_1,
                   sizeof(device_page_1));
    reply = mptspi_config(mpt, MPI_CONFIG_ACTION_PAGE_WRITE_CURRENT,
                          MPI_CONFIG_PAGETYPE_SCSI_DEVICE, 1, 2,
                          page_address, sizeof(device_page_1), true);
    g_assert_cmphex(le16_to_cpu(reply.IOCStatus), ==,
                    MPI_IOCSTATUS_SUCCESS);

    for (i = 0; i < ARRAY_SIZE(diag_keys); i++) {
        qpci_io_writel(&mpt->dev, mpt->bar, MPI_WRITE_SEQUENCE_OFFSET,
                       diag_keys[i]);
    }
    g_assert_cmphex(qpci_io_readl(&mpt->dev, mpt->bar,
                                  MPI_DIAGNOSTIC_OFFSET) & MPI_DIAG_DRWE,
                    ==, MPI_DIAG_DRWE);
    qpci_io_writel(&mpt->dev, mpt->bar, MPI_DIAGNOSTIC_OFFSET,
                   MPI_DIAG_RESET_ADAPTER);
    g_assert_cmphex(qpci_io_readl(&mpt->dev, mpt->bar,
                                  MPI_DOORBELL_OFFSET) &
                    MPI_IOC_STATE_READY, ==, MPI_IOC_STATE_READY);
    g_assert_cmphex(qpci_io_readl(&mpt->dev, mpt->bar,
                                  MPI_HOST_INTERRUPT_MASK_OFFSET), ==,
                    MPI_HIM_DIM | MPI_HIM_RIM);
    g_assert_cmphex(qpci_io_readl(&mpt->dev, mpt->bar,
                                  MPI_DIAGNOSTIC_OFFSET), ==, 0);

    reply = mptspi_config(mpt, MPI_CONFIG_ACTION_PAGE_READ_CURRENT,
                          MPI_CONFIG_PAGETYPE_SCSI_DEVICE, 0, 2,
                          page_address, sizeof(device_page_0), false);
    g_assert_cmphex(le16_to_cpu(reply.IOCStatus), ==, MPI_IOCSTATUS_SUCCESS);
    qtest_memread(mpt->dev.bus->qts, page_address, device_page_0,
                  sizeof(device_page_0));
    g_assert_cmphex(ldl_le_p(device_page_0 + 4), ==, 0);

    reply = mptspi_config(mpt, MPI_CONFIG_ACTION_PAGE_READ_CURRENT,
                          MPI_CONFIG_PAGETYPE_IOC, 1, 0, page_address,
                          sizeof(ioc_page_1), false);
    g_assert_cmphex(le16_to_cpu(reply.IOCStatus), ==,
                    MPI_IOCSTATUS_SUCCESS);
    qtest_memread(mpt->dev.bus->qts, page_address, ioc_page_1,
                  sizeof(ioc_page_1));
    g_assert_cmpuint(ldl_le_p(ioc_page_1 + 4), ==, 0);
    g_assert_cmpuint(ldl_le_p(ioc_page_1 + 8), ==, 0);
    g_assert_cmpuint(ioc_page_1[12], ==, 0);

    reply = mptspi_config(mpt, MPI_CONFIG_ACTION_PAGE_READ_CURRENT,
                          MPI_CONFIG_PAGETYPE_SCSI_PORT, 1, 0,
                          page_address, sizeof(port_page_1), false);
    g_assert_cmphex(le16_to_cpu(reply.IOCStatus), ==,
                    MPI_IOCSTATUS_SUCCESS);
    qtest_memread(mpt->dev.bus->qts, page_address, port_page_1,
                  sizeof(port_page_1));
    g_assert_cmphex(ldl_le_p(port_page_1 + 4), ==, 0x00800007);

    guest_free(alloc, page_address);
}

static void mptspi_snapshot_data_free(void *opaque)
{
    MptSpiSnapshotData *snapshot = opaque;

    g_assert_cmpint(g_unlink(snapshot->disk_path), ==, 0);
    g_assert_cmpint(g_rmdir(snapshot->tmpdir), ==, 0);
    g_free(snapshot->disk_path);
    g_free(snapshot->tmpdir);
    g_free(snapshot);
}

static void *mptspi_snapshot_setup(GString *cmd_line, void *arg)
{
    g_autofree char *quoted_disk_path = NULL;
    g_autoptr(GError) error = NULL;
    MptSpiSnapshotData *snapshot;

    if (!have_qemu_img()) {
        return NULL;
    }

    snapshot = g_new0(MptSpiSnapshotData, 1);
    snapshot->tmpdir = g_dir_make_tmp("lsi53c1030-savevm-XXXXXX", &error);
    g_assert_no_error(error);
    g_assert_nonnull(snapshot->tmpdir);
    snapshot->disk_path = g_build_filename(snapshot->tmpdir,
                                            "snapshot.qcow2", NULL);
    g_assert_true(mkimg(snapshot->disk_path, "qcow2", 16));
    quoted_disk_path = g_shell_quote(snapshot->disk_path);
    g_string_append_printf(cmd_line,
                           " -drive file=%s,format=qcow2,if=none,id=snapshot",
                           quoted_disk_path);
    g_test_queue_destroy(mptspi_snapshot_data_free, snapshot);
    return snapshot;
}

static void mptspi_test_config_savevm(void *obj, void *data,
                                      QGuestAllocator *alloc)
{
    QMptSpi *mpt = obj;
    MPIMsgConfigReply reply;
    uint8_t device_page_1[16] = { 0 };
    uint8_t ioc_page_1[16] = { 0 };
    uint8_t port_page_1[16] = { 0 };
    uint64_t page_address;
    g_autofree char *response = NULL;

    if (!data) {
        g_test_skip("qemu-img is required for config-page savevm testing");
        return;
    }

    mptspi_ioc_init(mpt);
    page_address = guest_alloc(alloc, sizeof(ioc_page_1));

    ioc_page_1[0] = 0x03;
    ioc_page_1[1] = sizeof(ioc_page_1) / 4;
    ioc_page_1[2] = 1;
    ioc_page_1[3] = MPI_CONFIG_PAGEATTR_CHANGEABLE |
                    MPI_CONFIG_PAGETYPE_IOC;
    stl_le_p(ioc_page_1 + 4, MPI_IOCPAGE1_REPLY_COALESCING);
    stl_le_p(ioc_page_1 + 8, 0x10203040);
    ioc_page_1[12] = 0x20;
    qtest_memwrite(mpt->dev.bus->qts, page_address, ioc_page_1,
                   sizeof(ioc_page_1));
    reply = mptspi_config(mpt, MPI_CONFIG_ACTION_PAGE_WRITE_CURRENT,
                          MPI_CONFIG_PAGETYPE_IOC, 1, 0, page_address,
                          sizeof(ioc_page_1), true);
    g_assert_cmphex(le16_to_cpu(reply.IOCStatus), ==,
                    MPI_IOCSTATUS_SUCCESS);

    port_page_1[0] = 0x03;
    port_page_1[1] = sizeof(port_page_1) / 4;
    port_page_1[2] = 1;
    port_page_1[3] = MPI_CONFIG_PAGEATTR_CHANGEABLE |
                     MPI_CONFIG_PAGETYPE_SCSI_PORT;
    stl_le_p(port_page_1 + 4, 0x00400006);
    stl_le_p(port_page_1 + 8, 0x50607080);
    qtest_memwrite(mpt->dev.bus->qts, page_address, port_page_1,
                   sizeof(port_page_1));
    reply = mptspi_config(mpt, MPI_CONFIG_ACTION_PAGE_WRITE_CURRENT,
                          MPI_CONFIG_PAGETYPE_SCSI_PORT, 1, 0,
                          page_address, sizeof(port_page_1), true);
    g_assert_cmphex(le16_to_cpu(reply.IOCStatus), ==,
                    MPI_IOCSTATUS_SUCCESS);

    device_page_1[0] = 0x05;
    device_page_1[1] = sizeof(device_page_1) / 4;
    device_page_1[2] = 1;
    device_page_1[3] = MPI_CONFIG_PAGEATTR_CHANGEABLE |
                       MPI_CONFIG_PAGETYPE_SCSI_DEVICE;
    stl_le_p(device_page_1 + 4, 0x20ff0807);
    stl_le_p(device_page_1 + 12, 0x08);
    qtest_memwrite(mpt->dev.bus->qts, page_address, device_page_1,
                   sizeof(device_page_1));
    reply = mptspi_config(mpt, MPI_CONFIG_ACTION_PAGE_WRITE_CURRENT,
                          MPI_CONFIG_PAGETYPE_SCSI_DEVICE, 1, 2,
                          page_address, sizeof(device_page_1), true);
    g_assert_cmphex(le16_to_cpu(reply.IOCStatus), ==,
                    MPI_IOCSTATUS_SUCCESS);

    response = qtest_hmp(mpt->dev.bus->qts, "savevm config-pages");
    g_assert_cmpstr(response, ==, "");
    g_clear_pointer(&response, g_free);

    reply = mptspi_config(mpt, MPI_CONFIG_ACTION_PAGE_DEFAULT,
                          MPI_CONFIG_PAGETYPE_IOC, 1, 0, 0, 0, false);
    g_assert_cmphex(le16_to_cpu(reply.IOCStatus), ==,
                    MPI_IOCSTATUS_SUCCESS);
    reply = mptspi_config(mpt, MPI_CONFIG_ACTION_PAGE_DEFAULT,
                          MPI_CONFIG_PAGETYPE_SCSI_PORT, 1, 0,
                          0, 0, false);
    g_assert_cmphex(le16_to_cpu(reply.IOCStatus), ==,
                    MPI_IOCSTATUS_SUCCESS);
    reply = mptspi_config(mpt, MPI_CONFIG_ACTION_PAGE_DEFAULT,
                          MPI_CONFIG_PAGETYPE_SCSI_DEVICE, 1, 2,
                          0, 0, false);
    g_assert_cmphex(le16_to_cpu(reply.IOCStatus), ==,
                    MPI_IOCSTATUS_SUCCESS);

    response = qtest_hmp(mpt->dev.bus->qts, "loadvm config-pages");
    g_assert_cmpstr(response, ==, "");

    memset(ioc_page_1, 0, sizeof(ioc_page_1));
    reply = mptspi_config(mpt, MPI_CONFIG_ACTION_PAGE_READ_CURRENT,
                          MPI_CONFIG_PAGETYPE_IOC, 1, 0, page_address,
                          sizeof(ioc_page_1), false);
    g_assert_cmphex(le16_to_cpu(reply.IOCStatus), ==,
                    MPI_IOCSTATUS_SUCCESS);
    qtest_memread(mpt->dev.bus->qts, page_address, ioc_page_1,
                  sizeof(ioc_page_1));
    g_assert_cmphex(ldl_le_p(ioc_page_1 + 4), ==,
                    MPI_IOCPAGE1_REPLY_COALESCING);
    g_assert_cmphex(ldl_le_p(ioc_page_1 + 8), ==, 0x10203040);
    g_assert_cmphex(ioc_page_1[12], ==, 0x20);

    memset(port_page_1, 0, sizeof(port_page_1));
    reply = mptspi_config(mpt, MPI_CONFIG_ACTION_PAGE_READ_CURRENT,
                          MPI_CONFIG_PAGETYPE_SCSI_PORT, 1, 0,
                          page_address, sizeof(port_page_1), false);
    g_assert_cmphex(le16_to_cpu(reply.IOCStatus), ==,
                    MPI_IOCSTATUS_SUCCESS);
    qtest_memread(mpt->dev.bus->qts, page_address, port_page_1,
                  sizeof(port_page_1));
    g_assert_cmphex(ldl_le_p(port_page_1 + 4), ==, 0x00400006);
    g_assert_cmphex(ldl_le_p(port_page_1 + 8), ==, 0x50607080);

    memset(device_page_1, 0, sizeof(device_page_1));
    reply = mptspi_config(mpt, MPI_CONFIG_ACTION_PAGE_READ_CURRENT,
                          MPI_CONFIG_PAGETYPE_SCSI_DEVICE, 1, 2,
                          page_address, sizeof(device_page_1), false);
    g_assert_cmphex(le16_to_cpu(reply.IOCStatus), ==,
                    MPI_IOCSTATUS_SUCCESS);
    qtest_memread(mpt->dev.bus->qts, page_address, device_page_1,
                  sizeof(device_page_1));
    g_assert_cmphex(ldl_le_p(device_page_1 + 4), ==, 0x20ff0807);
    g_assert_cmphex(ldl_le_p(device_page_1 + 12), ==, 0x08);

    guest_free(alloc, page_address);
}

static void mptsas1068_test_compat(void *obj, void *data,
                                   QGuestAllocator *alloc)
{
    QMptSpi *mpt = obj;
    QPCIBar diag_bar;
    MPIMsgIOCFacts facts_request = { 0 };
    MPIMsgIOCFactsReply facts_reply;
    MPIMsgPortFacts port_request = { 0 };
    MPIMsgPortFactsReply port_reply;
    MPIMsgConfigReply config_reply;
    uint16_t product_id = MPI_FW_HEADER_PID_TYPE_SAS |
                          MPI_FW_HEADER_PID_PROD_INITIATOR_SCSI |
                          MPI_FW_HEADER_PID_FAMILY_1068_SAS;
    uint64_t diag_size;

    g_assert_cmphex(qpci_config_readw(&mpt->dev, PCI_VENDOR_ID), ==,
                    PCI_VENDOR_ID_LSI_LOGIC);
    g_assert_cmphex(qpci_config_readw(&mpt->dev, PCI_DEVICE_ID), ==,
                    PCI_DEVICE_ID_LSI_SAS1068);
    g_assert_cmphex(qpci_config_readb(&mpt->dev, PCI_REVISION_ID), ==, 0x01);
    g_assert_cmphex(qpci_config_readw(&mpt->dev,
                                      PCI_SUBSYSTEM_VENDOR_ID), ==,
                    PCI_VENDOR_ID_LSI_LOGIC);
    g_assert_cmphex(qpci_config_readw(&mpt->dev, PCI_SUBSYSTEM_ID), ==,
                    0x8000);
    g_assert_cmphex(qpci_config_readl(&mpt->dev, PCI_BASE_ADDRESS_1) &
                    PCI_BASE_ADDRESS_MEM_TYPE_MASK, ==,
                    PCI_BASE_ADDRESS_MEM_TYPE_64);
    g_assert_cmphex(qpci_config_readl(&mpt->dev, PCI_BASE_ADDRESS_3) &
                    PCI_BASE_ADDRESS_MEM_TYPE_MASK, ==,
                    PCI_BASE_ADDRESS_MEM_TYPE_64);
    diag_bar = qpci_iomap(&mpt->dev, 3, &diag_size);
    g_assert_cmpuint(diag_size, ==, 0x10000);
    qpci_iounmap(&mpt->dev, diag_bar);
    g_assert_cmpuint(mptspi_probe_rom_size(&mpt->dev), ==,
                     4U * 1024U * 1024U);

    /* The host may advertise more targets than this eight-PHY model. */
    mptspi_ioc_init(mpt);
    facts_request.Function = MPI_FUNCTION_IOC_FACTS;
    mptspi_handshake(mpt, &facts_request, sizeof(facts_request),
                     &facts_reply, sizeof(facts_reply));
    g_assert_cmphex(le16_to_cpu(facts_reply.IOCStatus), ==,
                    MPI_IOCSTATUS_SUCCESS);
    g_assert_cmphex(le16_to_cpu(facts_reply.ProductID), ==, product_id);
    g_assert_cmpuint(facts_reply.NumberOfPorts, ==, 8);
    g_assert_cmpuint(facts_reply.MaxDevices, ==, 8);
    g_assert_cmpuint(facts_reply.MaxBuses, ==, 1);

    port_request.Function = MPI_FUNCTION_PORT_FACTS;
    port_request.PortNumber = 7;
    mptspi_handshake(mpt, &port_request, sizeof(port_request),
                     &port_reply, sizeof(port_reply));
    g_assert_cmphex(le16_to_cpu(port_reply.IOCStatus), ==,
                    MPI_IOCSTATUS_SUCCESS);
    g_assert_cmphex(port_reply.PortType, ==, MPI_PORTFACTS_PORTTYPE_SAS);
    g_assert_cmpuint(le16_to_cpu(port_reply.MaxDevices), ==, 8);
    g_assert_cmpuint(le16_to_cpu(port_reply.PortSCSIID), ==, 8);
    g_assert_cmphex(le16_to_cpu(port_reply.ProtocolFlags), ==,
                    MPI_PORTFACTS_PROTOCOL_LOGBUSADDR |
                    MPI_PORTFACTS_PROTOCOL_INITIATOR);

    memset(&port_reply, 0, sizeof(port_reply));
    port_request.PortNumber = 8;
    mptspi_handshake(mpt, &port_request, sizeof(port_request),
                     &port_reply, sizeof(port_reply));
    g_assert_cmphex(le16_to_cpu(port_reply.IOCStatus), ==,
                    MPI_IOCSTATUS_INVALID_FIELD);

    /* Keep the pre-existing SAS1068 PAGE_DEFAULT success/no-op ABI. */
    config_reply = mptspi_config(mpt, MPI_CONFIG_ACTION_PAGE_DEFAULT,
                                 MPI_CONFIG_PAGETYPE_IOC, 4, 0,
                                 0, 0, false);
    g_assert_cmphex(le16_to_cpu(config_reply.IOCStatus), ==,
                    MPI_IOCSTATUS_SUCCESS);

    config_reply = mptspi_config(mpt, MPI_CONFIG_ACTION_PAGE_WRITE_CURRENT,
                                 MPI_CONFIG_PAGETYPE_IOC, 1, 0,
                                 0, 0, true);
    g_assert_cmphex(le16_to_cpu(config_reply.IOCStatus), ==,
                    MPI_IOCSTATUS_CONFIG_CANT_COMMIT);

    /* Parallel-SCSI pages must not leak into the SAS1068 personality. */
    config_reply = mptspi_config(mpt, MPI_CONFIG_ACTION_PAGE_HEADER,
                                 MPI_CONFIG_PAGETYPE_SCSI_PORT, 0, 0,
                                 0, 0, false);
    g_assert_cmphex(le16_to_cpu(config_reply.IOCStatus), ==,
                    MPI_IOCSTATUS_CONFIG_INVALID_TYPE);
}

static uint64_t mptsas1068_read_sas_address(QMptSpi *mpt, uint16_t handle,
                                           uint64_t buffer)
{
    MPIMsgConfig request = {
        .Action = MPI_CONFIG_ACTION_PAGE_READ_CURRENT,
        .Function = MPI_FUNCTION_CONFIG,
        .ExtPageType = MPI_CONFIG_EXTPAGETYPE_SAS_DEVICE,
        .PageType = MPI_CONFIG_PAGETYPE_EXTENDED,
    };
    MPIMsgConfigReply reply;
    uint8_t page[64];

    request.PageAddress = cpu_to_le32(0x20000000U | handle);
    request.PageBufferSGE.FlagsLength = cpu_to_le32(
        MPI_SGE_FLAGS_SIMPLE_ELEMENT | MPI_SGE_FLAGS_LAST_ELEMENT |
        MPI_SGE_FLAGS_END_OF_BUFFER | MPI_SGE_FLAGS_END_OF_LIST | sizeof(page));
    request.PageBufferSGE.u.Address32 = cpu_to_le32(buffer);
    mptspi_handshake(mpt, &request, sizeof(request), &reply, sizeof(reply));
    g_assert_cmphex(le16_to_cpu(reply.IOCStatus), ==, MPI_IOCSTATUS_SUCCESS);
    qtest_memread(mpt->dev.bus->qts, buffer, page, sizeof(page));
    return ldq_le_p(page + 12);
}

static void mptsas1068_test_sas_addresses(void *obj, void *data,
                                         QGuestAllocator *alloc)
{
    QMptSpi *first = obj;
    QMptSpi second = { 0 };
    QPCIAddress address = { .devfn = QPCI_DEVFN(5, 1) };
    uint64_t buffer = guest_alloc(alloc, 64);
    uint64_t sas_addresses[18];
    unsigned int count = 0;
    unsigned int controller, phy, i;

    qpci_device_init(&second.dev, first->dev.bus, &address);
    qpci_device_enable(&second.dev);
    second.bar = qpci_iomap(&second.dev, 1, NULL);

    for (controller = 0; controller < 2; controller++) {
        QMptSpi *mpt = controller ? &second : first;

        for (phy = 0; phy < 9; phy++) {
            uint64_t sas_address = mptsas1068_read_sas_address(
                mpt, phy + 1, buffer);

            g_assert_cmpuint(sas_address, !=, 0);
            for (i = 0; i < count; i++) {
                g_assert_cmphex(sas_address, !=, sas_addresses[i]);
            }
            sas_addresses[count++] = sas_address;
        }
    }
    qpci_iounmap(&second.dev, second.bar);
    guest_free(alloc, buffer);
}

static void mptsas1068_test_savevm(void *obj, void *data,
                                   QGuestAllocator *alloc)
{
    QMptSpi *mpt = obj;
    g_autofree char *response = NULL;

    if (!data) {
        g_test_skip("qemu-img is required for SAS1068 savevm testing");
        return;
    }

    mptspi_ioc_init(mpt);
    response = qtest_hmp(mpt->dev.bus->qts, "savevm sas-compat");
    g_assert_cmpstr(response, ==, "");
    g_clear_pointer(&response, g_free);

    qpci_io_writel(&mpt->dev, mpt->bar, MPI_DOORBELL_OFFSET,
                   MPI_FUNCTION_IOC_MESSAGE_UNIT_RESET <<
                   MPI_DOORBELL_FUNCTION_SHIFT);
    g_assert_cmphex(qpci_io_readl(&mpt->dev, mpt->bar,
                                  MPI_DOORBELL_OFFSET) &
                    MPI_IOC_STATE_READY, ==, MPI_IOC_STATE_READY);

    response = qtest_hmp(mpt->dev.bus->qts, "loadvm sas-compat");
    g_assert_cmpstr(response, ==, "");
    g_assert_cmphex(qpci_io_readl(&mpt->dev, mpt->bar,
                                  MPI_DOORBELL_OFFSET) &
                    MPI_IOC_STATE_OPERATIONAL, ==,
                    MPI_IOC_STATE_OPERATIONAL);
}

static void mptspi_register_nodes(void)
{
    QOSGraphEdgeOptions opts = {
        .extra_device_opts = "addr=04.0,id=mptspi",
    };
    QOSGraphEdgeOptions sas_opts = {
        .extra_device_opts = "addr=05.0,id=mptsas,multifunction=on,"
                             "x-pci-64bit-bars=on,"
                             "x-pci-rom-size=4194304",
    };
    QOSGraphTestOptions sas_address_opts = {
        .edge.after_cmd_line =
            "-device mptsas1068,addr=05.1,id=mptsas1 "
            "-device scsi-cd,bus=mptsas.0,scsi-id=0 "
            "-device scsi-cd,bus=mptsas1.0,scsi-id=0",
    };
    QOSGraphTestOptions snapshot_opts = {
        .before = mptspi_snapshot_setup,
    };

    add_qpci_address(&opts, &(QPCIAddress) { .devfn = QPCI_DEVFN(4, 0) });
    qos_node_create_driver("lsi53c1030", mptspi_create);
    qos_node_consumes("lsi53c1030", "pci-bus", &opts);
    qos_node_produces("lsi53c1030", "pci-device");
    qos_add_test("facts", "lsi53c1030", mptspi_test_facts, NULL);
    qos_add_test("config-reset", "lsi53c1030",
                 mptspi_test_config_and_reset, NULL);
    qos_add_test("config-savevm", "lsi53c1030",
                 mptspi_test_config_savevm, &snapshot_opts);

    add_qpci_address(&sas_opts, &(QPCIAddress) { .devfn = QPCI_DEVFN(5, 0) });
    qos_node_create_driver("mptsas1068", mptspi_create);
    qos_node_consumes("mptsas1068", "pci-bus", &sas_opts);
    qos_node_produces("mptsas1068", "pci-device");
    qos_add_test("compat", "mptsas1068", mptsas1068_test_compat, NULL);
    qos_add_test("sas-addresses", "mptsas1068",
                 mptsas1068_test_sas_addresses, &sas_address_opts);
    qos_add_test("compat-savevm", "mptsas1068",
                 mptsas1068_test_savevm, &snapshot_opts);
}

libqos_init(mptspi_register_nodes);
