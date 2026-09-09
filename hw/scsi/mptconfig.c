/*
 * QEMU LSI SAS1068 Host Bus Adapter emulation - configuration pages
 *
 * Copyright (c) 2016 Red Hat, Inc.
 *
 * Author: Paolo Bonzini
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 */
#include "qemu/osdep.h"
#include "hw/pci/pci.h"
#include "hw/scsi/scsi.h"

#include "mptsas.h"
#include "mpi.h"
#include "trace.h"

/* Generic functions for marshaling and unmarshaling.  */

#define repl1(x) x
#define repl2(x) x x
#define repl3(x) x x x
#define repl4(x) x x x x
#define repl5(x) x x x x x
#define repl6(x) x x x x x x
#define repl7(x) x x x x x x x
#define repl8(x) x x x x x x x x

#define repl(n, x) glue(repl, n)(x)

typedef union PackValue {
    uint64_t ll;
    char *str;
} PackValue;

static size_t vfill(uint8_t *data, size_t size, const char *fmt, va_list ap)
{
    size_t ofs;
    PackValue val;
    const char *p;

    ofs = 0;
    p = fmt;
    while (*p) {
        memset(&val, 0, sizeof(val));
        switch (*p) {
        case '*':
            p++;
            break;
        case 'b':
        case 'w':
        case 'l':
            val.ll = va_arg(ap, int);
            break;
        case 'q':
            val.ll = va_arg(ap, int64_t);
            break;
        case 's':
            val.str = va_arg(ap, void *);
            break;
        }
        switch (*p++) {
        case 'b':
            if (data) {
                stb_p(data + ofs, val.ll);
            }
            ofs++;
            break;
        case 'w':
            if (data) {
                stw_le_p(data + ofs, val.ll);
            }
            ofs += 2;
            break;
        case 'l':
            if (data) {
                stl_le_p(data + ofs, val.ll);
            }
            ofs += 4;
            break;
        case 'q':
            if (data) {
                stq_le_p(data + ofs, val.ll);
            }
            ofs += 8;
            break;
        case 's':
            {
                int cnt = atoi(p);
                if (data) {
                    if (val.str) {
                        strncpy((void *)data + ofs, val.str, cnt);
                    } else {
                        memset((void *)data + ofs, 0, cnt);
                    }
                }
                ofs += cnt;
                break;
            }
        }
    }

    return ofs;
}

static size_t vpack(uint8_t **p_data, const char *fmt, va_list ap1)
{
    size_t size = 0;
    uint8_t *data = NULL;

    if (p_data) {
        va_list ap2;

        va_copy(ap2, ap1);
        size = vfill(NULL, 0, fmt, ap2);
        *p_data = data = g_malloc(size);
        va_end(ap2);
    }
    return vfill(data, size, fmt, ap1);
}

static size_t fill(uint8_t *data, size_t size, const char *fmt, ...)
{
    va_list ap;
    size_t ret;

    va_start(ap, fmt);
    ret = vfill(data, size, fmt, ap);
    va_end(ap);

    return ret;
}

/* Functions to build the page header and fill in the length, always used
 * through the macros.
 */

#define MPTSAS_CONFIG_PACK(number, type, version, fmt, ...)                  \
    mptsas_config_pack(data, "b*bbb" fmt, version, number, type,             \
                       ## __VA_ARGS__)

static size_t mptsas_config_pack(uint8_t **data, const char *fmt, ...)
{
    va_list ap;
    size_t ret;

    va_start(ap, fmt);
    ret = vpack(data, fmt, ap);
    va_end(ap);

    if (data) {
        assert(ret / 4 < 256 && (ret % 4) == 0);
        stb_p(*data + 1, ret / 4);
    }
    return ret;
}

#define MPTSAS_CONFIG_PACK_EXT(number, type, version, fmt, ...)              \
    mptsas_config_pack_ext(data, "b*bbb*wb*b" fmt, version, number,          \
                           MPI_CONFIG_PAGETYPE_EXTENDED, type, ## __VA_ARGS__)

static size_t mptsas_config_pack_ext(uint8_t **data, const char *fmt, ...)
{
    va_list ap;
    size_t ret;

    va_start(ap, fmt);
    ret = vpack(data, fmt, ap);
    va_end(ap);

    if (data) {
        assert(ret < 65536 && (ret % 4) == 0);
        stw_le_p(*data + 4, ret / 4);
    }
    return ret;
}

/* Manufacturing pages */

static
size_t mptsas_config_manufacturing_0(MPTSASState *s, uint8_t **data, int address)
{
    const char *name = mptsas_is_spi(s) ? "QEMU LSI53C1030" :
                                          "QEMU MPT Fusion";
    const char *version = mptsas_is_spi(s) ? "1.0" : "2.5";

    return MPTSAS_CONFIG_PACK(0, MPI_CONFIG_PAGETYPE_MANUFACTURING, 0x00,
                              "s16s8s16s16s16",
                              name,
                              version,
                              name,
                              "QEMU",
                              "0000111122223333");
}

static
size_t mptsas_config_manufacturing_1(MPTSASState *s, uint8_t **data, int address)
{
    return MPTSAS_CONFIG_PACK(1, MPI_CONFIG_PAGETYPE_MANUFACTURING |
                              MPI_CONFIG_PAGEATTR_PERSISTENT,
                              0x00, "*s256");
}

static
size_t mptsas_config_manufacturing_2(MPTSASState *s, uint8_t **data, int address)
{
    PCIDeviceClass *pcic = PCI_DEVICE_GET_CLASS(s);
    return MPTSAS_CONFIG_PACK(2, MPI_CONFIG_PAGETYPE_MANUFACTURING, 0x00,
                              "wb*b*l",
                              pcic->device_id, pcic->revision);
}

static
size_t mptsas_config_manufacturing_3(MPTSASState *s, uint8_t **data, int address)
{
    PCIDeviceClass *pcic = PCI_DEVICE_GET_CLASS(s);
    return MPTSAS_CONFIG_PACK(3, MPI_CONFIG_PAGETYPE_MANUFACTURING, 0x00,
                              "wb*b*l",
                              pcic->device_id, pcic->revision);
}

static
size_t mptsas_config_manufacturing_4(MPTSASState *s, uint8_t **data, int address)
{
    return MPTSAS_CONFIG_PACK(4, MPI_CONFIG_PAGETYPE_MANUFACTURING |
                              MPI_CONFIG_PAGEATTR_PERSISTENT, 0x05,
                              "*l*b*b*b*b*b*b*w*s56*l*l*l*l*l*l"
                              "*b*b*w*b*b*w*l*l");
}

static
size_t mptsas_config_manufacturing_5(MPTSASState *s, uint8_t **data, int address)
{
    return MPTSAS_CONFIG_PACK(5, MPI_CONFIG_PAGETYPE_MANUFACTURING, 0x02,
                              "q*b*b*w*l*l",
                              mptsas_is_spi(s) ? 0 : s->sas_addr);
}

static
size_t mptsas_config_manufacturing_6(MPTSASState *s, uint8_t **data, int address)
{
    return MPTSAS_CONFIG_PACK(6, MPI_CONFIG_PAGETYPE_MANUFACTURING, 0x00,
                              "*l");
}

static
size_t mptsas_config_manufacturing_7(MPTSASState *s, uint8_t **data, int address)
{
    return MPTSAS_CONFIG_PACK(7, MPI_CONFIG_PAGETYPE_MANUFACTURING, 0x00,
                              "*l*l*l*s16*b*b*w", mptsas_num_ports(s));
}

static
size_t mptsas_config_manufacturing_8(MPTSASState *s, uint8_t **data, int address)
{
    return MPTSAS_CONFIG_PACK(8, MPI_CONFIG_PAGETYPE_MANUFACTURING, 0x00,
                              "*l");
}

static
size_t mptsas_config_manufacturing_9(MPTSASState *s, uint8_t **data, int address)
{
    return MPTSAS_CONFIG_PACK(9, MPI_CONFIG_PAGETYPE_MANUFACTURING, 0x00,
                              "*l");
}

static
size_t mptsas_config_manufacturing_10(MPTSASState *s, uint8_t **data, int address)
{
    return MPTSAS_CONFIG_PACK(10, MPI_CONFIG_PAGETYPE_MANUFACTURING, 0x00,
                              "*l");
}

/* I/O unit pages */

static
size_t mptsas_config_io_unit_0(MPTSASState *s, uint8_t **data, int address)
{
    PCIDevice *pci = PCI_DEVICE(s);
    uint64_t unique_value = 0x53504D554D4551LL;  /* "QEMUMPTx" */

    unique_value |= (uint64_t)pci->devfn << 56;
    return MPTSAS_CONFIG_PACK(0, MPI_CONFIG_PAGETYPE_IO_UNIT, 0x00,
                              "q", unique_value);
}

static
size_t mptsas_config_io_unit_1(MPTSASState *s, uint8_t **data, int address)
{
    return MPTSAS_CONFIG_PACK(1, MPI_CONFIG_PAGETYPE_IO_UNIT |
                              MPI_CONFIG_PAGEATTR_PERSISTENT, 0x02, "l",
                              MPI_IOUNITPAGE1_DISABLE_IR |
                              (mptsas_is_spi(s) ?
                               MPI_IOUNITPAGE1_MULTI_FUNCTION :
                               MPI_IOUNITPAGE1_SINGLE_FUNCTION));
}

static
size_t mptsas_config_io_unit_2(MPTSASState *s, uint8_t **data, int address)
{
    PCIDevice *pci = PCI_DEVICE(s);
    uint8_t devfn = pci->devfn;
    return MPTSAS_CONFIG_PACK(2, MPI_CONFIG_PAGETYPE_IO_UNIT, 0x02,
                              "llbbw*b*b*w*b*b*w*b*b*w*l",
                              0, 0x100, 0 /* pci bus? */, devfn, 0);
}

static
size_t mptsas_config_io_unit_3(MPTSASState *s, uint8_t **data, int address)
{
    return MPTSAS_CONFIG_PACK(3, MPI_CONFIG_PAGETYPE_IO_UNIT, 0x01,
                              "*b*b*w*l");
}

static
size_t mptsas_config_io_unit_4(MPTSASState *s, uint8_t **data, int address)
{
    return MPTSAS_CONFIG_PACK(4, MPI_CONFIG_PAGETYPE_IO_UNIT, 0x00, "*l*l*q");
}

/* I/O controller pages */

static
size_t mptsas_config_ioc_0(MPTSASState *s, uint8_t **data, int address)
{
    PCIDeviceClass *pcic = PCI_DEVICE_GET_CLASS(s);

    return MPTSAS_CONFIG_PACK(0, MPI_CONFIG_PAGETYPE_IOC, 0x01,
                              "*l*lwwb*b*b*blww",
                              pcic->vendor_id, pcic->device_id, pcic->revision,
                              pcic->class_id, pcic->subsystem_vendor_id,
                              pcic->subsystem_id);
}

static
size_t mptsas_config_ioc_1(MPTSASState *s, uint8_t **data, int address)
{
    return MPTSAS_CONFIG_PACK(1,
                              MPI_CONFIG_PAGEATTR_CHANGEABLE |
                              MPI_CONFIG_PAGETYPE_IOC,
                              0x03, "llb*b*w", s->ioc1_flags,
                              s->ioc1_coalescing_timeout,
                              s->ioc1_coalescing_depth);
}

static
size_t mptsas_config_ioc_2(MPTSASState *s, uint8_t **data, int address)
{
    return MPTSAS_CONFIG_PACK(2, MPI_CONFIG_PAGETYPE_IOC, 0x04,
                              "*l*b*b*b*b");
}

static
size_t mptsas_config_ioc_3(MPTSASState *s, uint8_t **data, int address)
{
    return MPTSAS_CONFIG_PACK(3, MPI_CONFIG_PAGETYPE_IOC, 0x00,
                              "*b*b*w");
}

static
size_t mptsas_config_ioc_4(MPTSASState *s, uint8_t **data, int address)
{
    return MPTSAS_CONFIG_PACK(4, MPI_CONFIG_PAGETYPE_IOC, 0x00,
                              "*b*b*w");
}

static
size_t mptsas_config_ioc_5(MPTSASState *s, uint8_t **data, int address)
{
    return MPTSAS_CONFIG_PACK(5, MPI_CONFIG_PAGETYPE_IOC, 0x00,
                              "*l*b*b*w");
}

static
size_t mptsas_config_ioc_6(MPTSASState *s, uint8_t **data, int address)
{
    return MPTSAS_CONFIG_PACK(6, MPI_CONFIG_PAGETYPE_IOC, 0x01,
                              "*l*b*b*b*b*b*b*b*b*b*b*w*l*l*l*l*b*b*w"
                              "*w*w*w*w*l*l*l");
}

/* Parallel SCSI port and target pages */

static int mptspi_port_addr_get(int address)
{
    uint32_t page_address = address;
    unsigned int port = page_address & MPI_SCSI_PORT_PGAD_PORT_MASK;

    if ((page_address & ~MPI_SCSI_PORT_PGAD_PORT_MASK) ||
        port >= MPTSPI_NUM_PORTS) {
        return -EINVAL;
    }
    return port;
}

static int mptspi_device_addr_get(int address)
{
    uint32_t page_address = address;
    unsigned int bus;
    unsigned int target;

    if ((page_address & MPI_SCSI_DEVICE_FORM_MASK) !=
        MPI_SCSI_DEVICE_FORM_BUS_TID ||
        (page_address & ~(MPI_SCSI_DEVICE_BUS_MASK |
                          MPI_SCSI_DEVICE_TARGET_ID_MASK))) {
        return -EINVAL;
    }

    bus = (page_address & MPI_SCSI_DEVICE_BUS_MASK) >>
          MPI_SCSI_DEVICE_BUS_SHIFT;
    target = (page_address & MPI_SCSI_DEVICE_TARGET_ID_MASK) >>
             MPI_SCSI_DEVICE_TARGET_ID_SHIFT;
    if (bus != 0 || target >= MPTSPI_MAX_TARGETS) {
        return -EINVAL;
    }
    return target;
}

static size_t mptspi_config_port_0(MPTSASState *s, uint8_t **data,
                                    int address)
{
    uint32_t capabilities;

    if (mptspi_port_addr_get(address) < 0) {
        return -EINVAL;
    }

    capabilities = MPI_SCSIPORTPAGE0_CAP_IU |
                   MPI_SCSIPORTPAGE0_CAP_DT |
                   MPI_SCSIPORTPAGE0_CAP_QAS |
                   (0x08 << MPI_SCSIPORTPAGE0_CAP_MIN_SYNC_PERIOD_SHIFT) |
                   (0xff << MPI_SCSIPORTPAGE0_CAP_MAX_SYNC_OFFSET_SHIFT) |
                   MPI_SCSIPORTPAGE0_CAP_WIDE;
    return MPTSAS_CONFIG_PACK(0, MPI_CONFIG_PAGETYPE_SCSI_PORT, 0x02,
                              "ll", capabilities,
                              MPI_SCSIPORTPAGE0_PHY_SIGNAL_LVD);
}

static size_t mptspi_config_port_1(MPTSASState *s, uint8_t **data,
                                    int address)
{
    if (mptspi_port_addr_get(address) < 0) {
        return -EINVAL;
    }

    return MPTSAS_CONFIG_PACK(1,
                              MPI_CONFIG_PAGEATTR_CHANGEABLE |
                              MPI_CONFIG_PAGETYPE_SCSI_PORT,
                              0x03, "llb*bw", s->spi_port_configuration,
                              s->spi_port_on_bus_timer, 0, 0);
}

static size_t mptspi_config_port_2(MPTSASState *s, uint8_t **data,
                                    int address)
{
    const uint16_t device_flags =
        MPI_SCSIPORTPAGE2_DEVICE_DISCONNECT_ENABLE |
        MPI_SCSIPORTPAGE2_DEVICE_ID_SCAN_ENABLE |
        MPI_SCSIPORTPAGE2_DEVICE_LUN_SCAN_ENABLE |
        MPI_SCSIPORTPAGE2_DEVICE_TAG_QUEUE_ENABLE;
    size_t size;
    int i;

    if (mptspi_port_addr_get(address) < 0) {
        return -EINVAL;
    }

    size = MPTSAS_CONFIG_PACK(2,
                              MPI_CONFIG_PAGEATTR_RO_PERSISTENT |
                              MPI_CONFIG_PAGETYPE_SCSI_PORT,
                              0x02, "ll" repl(8, "*s4") repl(8, "*s4"), 0,
                              MPI_SCSIPORTPAGE2_PORT_BIOS_OS_INIT_HBA |
                              MPTSPI_HOST_ID);
    if (data) {
        for (i = 0; i < MPTSPI_MAX_TARGETS; i++) {
            fill(*data + 12 + i * 4, 4, "bbw", 0, 0x08, device_flags);
        }
    }
    return size;
}

static size_t mptspi_config_device_0(MPTSASState *s, uint8_t **data,
                                      int address)
{
    int target = mptspi_device_addr_get(address);

    if (target < 0) {
        return -EINVAL;
    }

    return MPTSAS_CONFIG_PACK(0, MPI_CONFIG_PAGETYPE_SCSI_DEVICE, 0x04,
                              "ll", s->spi_requested_params[target],
                              MPI_SCSIDEVPAGE0_INFO_PARAMS_NEGOTIATED);
}

static size_t mptspi_config_device_1(MPTSASState *s, uint8_t **data,
                                      int address)
{
    int target = mptspi_device_addr_get(address);

    if (target < 0) {
        return -EINVAL;
    }

    return MPTSAS_CONFIG_PACK(1,
                              MPI_CONFIG_PAGEATTR_CHANGEABLE |
                              MPI_CONFIG_PAGETYPE_SCSI_DEVICE,
                              0x05, "l*ll",
                              s->spi_requested_params[target],
                              s->spi_configuration[target]);
}

static size_t mptspi_config_device_2(MPTSASState *s, uint8_t **data,
                                      int address)
{
    if (mptspi_device_addr_get(address) < 0) {
        return -EINVAL;
    }

    return MPTSAS_CONFIG_PACK(2,
                              MPI_CONFIG_PAGEATTR_CHANGEABLE |
                              MPI_CONFIG_PAGETYPE_SCSI_DEVICE,
                              0x01, "*l*l*l");
}

static size_t mptspi_config_device_3(MPTSASState *s, uint8_t **data,
                                      int address)
{
    if (mptspi_device_addr_get(address) < 0) {
        return -EINVAL;
    }

    return MPTSAS_CONFIG_PACK(3, MPI_CONFIG_PAGETYPE_SCSI_DEVICE, 0x00,
                              "*w*w*w*w");
}

/* SAS I/O unit pages (extended) */

#define MPTSAS_CONFIG_SAS_IO_UNIT_0_SIZE 16

#define MPI_SAS_IOUNIT0_RATE_FAILED_SPEED_NEGOTIATION 0x02
#define MPI_SAS_IOUNIT0_RATE_1_5                      0x08
#define MPI_SAS_IOUNIT0_RATE_3_0                      0x09

#define MPI_SAS_DEVICE_INFO_NO_DEVICE                 0x00000000
#define MPI_SAS_DEVICE_INFO_END_DEVICE                0x00000001
#define MPI_SAS_DEVICE_INFO_SSP_INITIATOR             0x00000040
#define MPI_SAS_DEVICE_INFO_SSP_TARGET                0x00000400
#define MPI_SAS_DEVICE_INFO_DIRECT_ATTACH             0x00000800

#define MPI_SAS_DEVICE0_ASTATUS_NO_ERRORS             0x00

#define MPI_SAS_DEVICE0_FLAGS_DEVICE_PRESENT          0x0001
#define MPI_SAS_DEVICE0_FLAGS_DEVICE_MAPPED           0x0002



static SCSIDevice *mptsas_phy_get_device(MPTSASState *s, int i,
                                         int *phy_handle, int *dev_handle)
{
    SCSIDevice *d = scsi_device_find(&s->bus, 0, i, 0);

    if (phy_handle) {
        *phy_handle = i + 1;
    }
    if (dev_handle) {
        *dev_handle = d ? i + 1 + MPTSAS_NUM_PORTS : 0;
    }
    return d;
}

static
size_t mptsas_config_sas_io_unit_0(MPTSASState *s, uint8_t **data, int address)
{
    size_t size = MPTSAS_CONFIG_PACK_EXT(0, MPI_CONFIG_EXTPAGETYPE_SAS_IO_UNIT, 0x04,
                                         "*w*wb*b*w"
                                         repl(MPTSAS_NUM_PORTS, "*s16"),
                                         MPTSAS_NUM_PORTS);

    if (data) {
        size_t ofs = size - MPTSAS_NUM_PORTS * MPTSAS_CONFIG_SAS_IO_UNIT_0_SIZE;
        int i;

        for (i = 0; i < MPTSAS_NUM_PORTS; i++) {
            int phy_handle, dev_handle;
            SCSIDevice *dev = mptsas_phy_get_device(s, i, &phy_handle, &dev_handle);

            fill(*data + ofs, MPTSAS_CONFIG_SAS_IO_UNIT_0_SIZE,
                 "bbbblwwl", i, 0, 0,
                 (dev
                  ? MPI_SAS_IOUNIT0_RATE_3_0
                  : MPI_SAS_IOUNIT0_RATE_FAILED_SPEED_NEGOTIATION),
                 MPI_SAS_DEVICE_INFO_END_DEVICE |
                 MPI_SAS_DEVICE_INFO_SSP_INITIATOR,
                 dev_handle,
                 phy_handle,
                 0);
            ofs += MPTSAS_CONFIG_SAS_IO_UNIT_0_SIZE;
        }
        assert(ofs == size);
    }
    return size;
}

#define MPTSAS_CONFIG_SAS_IO_UNIT_1_SIZE 12
#define MPI_SAS_IOUNIT1_AUTO_PORT_CONFIG 0x01
#define MPI_SAS_IOUNIT2_RESERVE_ID_0_FOR_BOOT 0x10
#define MPI_SAS_IOUNIT2_DA_STARTING_SLOT 0x20

unsigned mptsas_first_slot(const MPTSASState *s)
{
    unsigned slot = MPTSAS_CONFIG_SAS_IO_UNIT_2;

    return (s->config_current_written & (1 << slot)) &&
           (s->config_current[slot][13] & MPI_SAS_IOUNIT2_DA_STARTING_SLOT);
}

static
size_t mptsas_config_sas_io_unit_1(MPTSASState *s, uint8_t **data, int address)
{
    size_t size = MPTSAS_CONFIG_PACK_EXT(1, MPI_CONFIG_EXTPAGETYPE_SAS_IO_UNIT, 0x07,
                                         "*w*w*w*wb*b*b*b"
                                         repl(MPTSAS_NUM_PORTS, "*s12"),
                                         MPTSAS_NUM_PORTS);

    if (data) {
        size_t ofs = size - MPTSAS_NUM_PORTS * MPTSAS_CONFIG_SAS_IO_UNIT_1_SIZE;
        int i;

        (*data)[3] |= MPI_CONFIG_PAGEATTR_PERSISTENT;
        for (i = 0; i < MPTSAS_NUM_PORTS; i++) {
            fill(*data + ofs, MPTSAS_CONFIG_SAS_IO_UNIT_1_SIZE,
                 "bbbblww", i, 0, 0,
                 (MPI_SAS_IOUNIT0_RATE_3_0 << 4) | MPI_SAS_IOUNIT0_RATE_1_5,
                 MPI_SAS_DEVICE_INFO_END_DEVICE |
                 MPI_SAS_DEVICE_INFO_SSP_INITIATOR,
                 0, 0);
            ofs += MPTSAS_CONFIG_SAS_IO_UNIT_1_SIZE;
        }
        assert(ofs == size);
    }
    return size;
}

static
size_t mptsas_config_sas_io_unit_2(MPTSASState *s, uint8_t **data, int address)
{
    size_t size = MPTSAS_CONFIG_PACK_EXT(
        2, MPI_CONFIG_EXTPAGETYPE_SAS_IO_UNIT, 0x06, "*b*b*w*w*w*b*b*w");

    if (data) {
        (*data)[3] |= MPI_CONFIG_PAGEATTR_PERSISTENT;
    }
    return size;
}

static
size_t mptsas_config_sas_io_unit_3(MPTSASState *s, uint8_t **data, int address)
{
    return MPTSAS_CONFIG_PACK_EXT(3, MPI_CONFIG_EXTPAGETYPE_SAS_IO_UNIT, 0x06,
                                  "*l*l*l*l*l*l*l*l*l");
}

/* SAS PHY pages (extended) */

#define MPI_SAS_PHY0_FLAGS_SGPIO_DIRECT_ATTACH_ENC     0x01

static int mptsas_phy_addr_get(MPTSASState *s, int address)
{
    int i;
    if ((address >> MPI_SAS_PHY_PGAD_FORM_SHIFT) == 0) {
        i = address & 255;
    } else if ((address >> MPI_SAS_PHY_PGAD_FORM_SHIFT) == 1) {
        i = address & 65535;
    } else {
        return -EINVAL;
    }

    if (i >= MPTSAS_NUM_PORTS) {
        return -EINVAL;
    }

    return i;
}

static
size_t mptsas_config_phy_0(MPTSASState *s, uint8_t **data, int address)
{
    int phy_handle = -1;
    int dev_handle = -1;
    int i = mptsas_phy_addr_get(s, address);
    SCSIDevice *dev;

    if (i < 0) {
        trace_mptsas_config_sas_phy(s, address, i, phy_handle, dev_handle, 0);
        return i;
    }

    dev = mptsas_phy_get_device(s, i, &phy_handle, &dev_handle);
    trace_mptsas_config_sas_phy(s, address, i, phy_handle, dev_handle, 0);

    return MPTSAS_CONFIG_PACK_EXT(
        0, MPI_CONFIG_EXTPAGETYPE_SAS_PHY, 0x01, "w*wqwb*blbbb*bl",
        phy_handle, s->sas_addr + i, dev_handle, 0,
        dev ? MPI_SAS_DEVICE_INFO_END_DEVICE |
              MPI_SAS_DEVICE_INFO_SSP_TARGET |
              MPI_SAS_DEVICE_INFO_DIRECT_ATTACH :
              MPI_SAS_DEVICE_INFO_NO_DEVICE,
        (MPI_SAS_IOUNIT0_RATE_3_0 << 4) | MPI_SAS_IOUNIT0_RATE_1_5,
        (MPI_SAS_IOUNIT0_RATE_3_0 << 4) | MPI_SAS_IOUNIT0_RATE_1_5,
        MPI_SAS_PHY0_FLAGS_SGPIO_DIRECT_ATTACH_ENC,
        dev ? MPI_SAS_IOUNIT0_RATE_3_0 :
              MPI_SAS_IOUNIT0_RATE_FAILED_SPEED_NEGOTIATION);
}

static
size_t mptsas_config_phy_1(MPTSASState *s, uint8_t **data, int address)
{
    int phy_handle = -1;
    int dev_handle = -1;
    int i = mptsas_phy_addr_get(s, address);

    if (i < 0) {
        trace_mptsas_config_sas_phy(s, address, i, phy_handle, dev_handle, 1);
        return i;
    }

    (void) mptsas_phy_get_device(s, i, &phy_handle, &dev_handle);
    trace_mptsas_config_sas_phy(s, address, i, phy_handle, dev_handle, 1);

    return MPTSAS_CONFIG_PACK_EXT(1, MPI_CONFIG_EXTPAGETYPE_SAS_PHY, 0x01,
                                  "*l*l*l*l*l");
}

/* SAS device pages (extended) */

static int mptsas_device_addr_get(MPTSASState *s, int address, bool *initiator)
{
    uint32_t handle, i;
    uint32_t form = address >> MPI_SAS_PHY_PGAD_FORM_SHIFT;
    if (form == MPI_SAS_DEVICE_PGAD_FORM_GET_NEXT_HANDLE) {
        handle = address & MPI_SAS_DEVICE_PGAD_GNH_HANDLE_MASK;
        do {
            if (handle == 65535) {
                handle = MPTSAS_NUM_PORTS + 1;
            } else {
                ++handle;
            }
            i = handle - 1 - MPTSAS_NUM_PORTS;
        } while (i < MPTSAS_NUM_PORTS && !scsi_device_find(&s->bus, 0, i, 0));

    } else if (form == MPI_SAS_DEVICE_PGAD_FORM_BUS_TARGET_ID) {
        if (address & MPI_SAS_DEVICE_PGAD_BT_BUS_MASK) {
            return -EINVAL;
        }
        i = address & MPI_SAS_DEVICE_PGAD_BT_TID_MASK;

    } else if (form == MPI_SAS_DEVICE_PGAD_FORM_HANDLE) {
        handle = address & MPI_SAS_DEVICE_PGAD_H_HANDLE_MASK;
        if (handle >= 1 && handle <= MPTSAS_NUM_PORTS) {
            *initiator = true;
            return handle - 1;
        }
        i = handle - 1 - MPTSAS_NUM_PORTS;

    } else {
        return -EINVAL;
    }

    if (i >= MPTSAS_NUM_PORTS) {
        return -EINVAL;
    }

    return i;
}

static size_t mptsas_config_sas_device_page(MPTSASState *s, uint8_t **data,
                                          int address, int number,
                                          bool header_only)
{
    int phy_handle = 0;
    int dev_handle = 0;
    int i = 0;
    uint64_t wwn = 0;
    bool initiator = false;

    /* Page headers describe the format independently of a device handle. */
    if (!header_only) {
        SCSIDevice *dev;

        i = mptsas_device_addr_get(s, address, &initiator);
        if (i < 0) {
            trace_mptsas_config_sas_device(s, address, i, -1, -1, number);
            return i;
        }
        dev = mptsas_phy_get_device(s, i, &phy_handle, &dev_handle);
        trace_mptsas_config_sas_device(s, address, i, phy_handle, dev_handle,
                                       number);
        if (!dev && !initiator) {
            return -ENOENT;
        }
        if (initiator) {
            wwn = s->sas_addr + i;
            dev_handle = phy_handle;
            phy_handle = 0;
        } else {
            wwn = dev->port_wwn ? dev->port_wwn : dev->wwn;
            if (!wwn) {
                wwn = s->sas_addr + MPTSAS_NUM_PORTS + i;
            }
        }
    }

    switch (number) {
    case 0:
        return MPTSAS_CONFIG_PACK_EXT(0, MPI_CONFIG_EXTPAGETYPE_SAS_DEVICE,
                                      0x05,
                                      "wwqwbbwbblwb*b",
                                      initiator ? 0 : i + mptsas_first_slot(s),
                                      initiator ? 0 : MPTSAS_ENCLOSURE_HANDLE,
                                      wwn, phy_handle, i,
                                      MPI_SAS_DEVICE0_ASTATUS_NO_ERRORS,
                                      dev_handle, initiator ? 0xff : i, 0,
                                      MPI_SAS_DEVICE_INFO_END_DEVICE |
                                      (initiator ?
                                       MPI_SAS_DEVICE_INFO_SSP_INITIATOR :
                                       MPI_SAS_DEVICE_INFO_SSP_TARGET |
                                       MPI_SAS_DEVICE_INFO_DIRECT_ATTACH),
                                      MPI_SAS_DEVICE0_FLAGS_DEVICE_PRESENT |
                                      (initiator ? 0 :
                                       MPI_SAS_DEVICE0_FLAGS_DEVICE_MAPPED),
                                      i);
    case 1:
        return MPTSAS_CONFIG_PACK_EXT(1, MPI_CONFIG_EXTPAGETYPE_SAS_DEVICE,
                                      0x00,
                                      "*lq*lwbb*s20", wwn, dev_handle, i, 0);
    case 2:
        return MPTSAS_CONFIG_PACK_EXT(2, MPI_CONFIG_EXTPAGETYPE_SAS_DEVICE,
                                      0x01,
                                      "ql", wwn, 0);
    default:
        g_assert_not_reached();
    }
}

static size_t mptsas_config_sas_device_0(MPTSASState *s, uint8_t **data,
                                       int address)
{
    return mptsas_config_sas_device_page(s, data, address, 0, false);
}

static size_t mptsas_config_sas_device_1(MPTSASState *s, uint8_t **data,
                                       int address)
{
    return mptsas_config_sas_device_page(s, data, address, 1, false);
}

static size_t mptsas_config_sas_device_2(MPTSASState *s, uint8_t **data,
                                       int address)
{
    return mptsas_config_sas_device_page(s, data, address, 2, false);
}

static size_t mptsas_config_enclosure_0(MPTSASState *s, uint8_t **data,
                                       int address)
{
    uint32_t form = (uint32_t)address >> MPI_SAS_PHY_PGAD_FORM_SHIFT;
    uint32_t handle = address & MPI_SAS_ENCLOS_PGAD_H_HANDLE_MASK;

    if (form == MPI_SAS_ENCLOS_PGAD_FORM_GET_NEXT_HANDLE) {
        if (handle != UINT16_MAX && handle >= MPTSAS_ENCLOSURE_HANDLE) {
            return -ENOENT;
        }
    } else if (form != MPI_SAS_ENCLOS_PGAD_FORM_HANDLE ||
               handle != MPTSAS_ENCLOSURE_HANDLE) {
        return -EINVAL;
    }

    /* The controller manages slot indicators for its directly attached PHYs. */
    return MPTSAS_CONFIG_PACK_EXT(
        0, MPI_CONFIG_EXTPAGETYPE_ENCLOSURE, 0x01, "*lqwwwwbbbb*l*l",
        s->sas_addr, MPI_SAS_ENCLS0_FLAGS_START_BUS_ID_VALID |
                     MPI_SAS_ENCLS0_FLAGS_MNG_IOC_SGPIO,
        MPTSAS_ENCLOSURE_HANDLE, MPTSAS_NUM_PORTS,
        mptsas_first_slot(s), 0, 0, 0, 0);
}

typedef struct MPTSASConfigPage {
    uint8_t number;
    uint8_t type;
    size_t (*mpt_config_build)(MPTSASState *s, uint8_t **data, int address);
    uint8_t header_version;
    uint16_t header_length;
} MPTSASConfigPage;

static const MPTSASConfigPage mptsas_config_pages[] = {
    {
        0, MPI_CONFIG_PAGETYPE_MANUFACTURING,
        mptsas_config_manufacturing_0,
    }, {
        1, MPI_CONFIG_PAGETYPE_MANUFACTURING,
        mptsas_config_manufacturing_1,
    }, {
        2, MPI_CONFIG_PAGETYPE_MANUFACTURING,
        mptsas_config_manufacturing_2,
    }, {
        3, MPI_CONFIG_PAGETYPE_MANUFACTURING,
        mptsas_config_manufacturing_3,
    }, {
        4, MPI_CONFIG_PAGETYPE_MANUFACTURING,
        mptsas_config_manufacturing_4,
    }, {
        5, MPI_CONFIG_PAGETYPE_MANUFACTURING,
        mptsas_config_manufacturing_5,
    }, {
        6, MPI_CONFIG_PAGETYPE_MANUFACTURING,
        mptsas_config_manufacturing_6,
    }, {
        7, MPI_CONFIG_PAGETYPE_MANUFACTURING,
        mptsas_config_manufacturing_7,
    }, {
        8, MPI_CONFIG_PAGETYPE_MANUFACTURING,
        mptsas_config_manufacturing_8,
    }, {
        9, MPI_CONFIG_PAGETYPE_MANUFACTURING,
        mptsas_config_manufacturing_9,
    }, {
        10, MPI_CONFIG_PAGETYPE_MANUFACTURING,
        mptsas_config_manufacturing_10,
    }, {
        0, MPI_CONFIG_PAGETYPE_IO_UNIT,
        mptsas_config_io_unit_0,
    }, {
        1, MPI_CONFIG_PAGETYPE_IO_UNIT,
        mptsas_config_io_unit_1,
    }, {
        2, MPI_CONFIG_PAGETYPE_IO_UNIT,
        mptsas_config_io_unit_2,
    }, {
        3, MPI_CONFIG_PAGETYPE_IO_UNIT,
        mptsas_config_io_unit_3,
    }, {
        4, MPI_CONFIG_PAGETYPE_IO_UNIT,
        mptsas_config_io_unit_4,
    }, {
        0, MPI_CONFIG_PAGETYPE_IOC,
        mptsas_config_ioc_0,
    }, {
        1, MPI_CONFIG_PAGETYPE_IOC,
        mptsas_config_ioc_1,
    }, {
        2, MPI_CONFIG_PAGETYPE_IOC,
        mptsas_config_ioc_2,
    }, {
        3, MPI_CONFIG_PAGETYPE_IOC,
        mptsas_config_ioc_3,
    }, {
        4, MPI_CONFIG_PAGETYPE_IOC,
        mptsas_config_ioc_4,
    }, {
        5, MPI_CONFIG_PAGETYPE_IOC,
        mptsas_config_ioc_5,
    }, {
        6, MPI_CONFIG_PAGETYPE_IOC,
        mptsas_config_ioc_6,
    }, {
        0, MPI_CONFIG_PAGETYPE_SCSI_PORT,
        mptspi_config_port_0,
    }, {
        1, MPI_CONFIG_PAGETYPE_SCSI_PORT,
        mptspi_config_port_1,
    }, {
        2, MPI_CONFIG_PAGETYPE_SCSI_PORT,
        mptspi_config_port_2,
    }, {
        0, MPI_CONFIG_PAGETYPE_SCSI_DEVICE,
        mptspi_config_device_0,
    }, {
        1, MPI_CONFIG_PAGETYPE_SCSI_DEVICE,
        mptspi_config_device_1,
    }, {
        2, MPI_CONFIG_PAGETYPE_SCSI_DEVICE,
        mptspi_config_device_2,
    }, {
        3, MPI_CONFIG_PAGETYPE_SCSI_DEVICE,
        mptspi_config_device_3,
    }, {
        0, MPI_CONFIG_EXTPAGETYPE_SAS_IO_UNIT,
        mptsas_config_sas_io_unit_0,
    }, {
        1, MPI_CONFIG_EXTPAGETYPE_SAS_IO_UNIT,
        mptsas_config_sas_io_unit_1,
    }, {
        2, MPI_CONFIG_EXTPAGETYPE_SAS_IO_UNIT,
        mptsas_config_sas_io_unit_2,
    }, {
        3, MPI_CONFIG_EXTPAGETYPE_SAS_IO_UNIT,
        mptsas_config_sas_io_unit_3,
    }, {
        0, MPI_CONFIG_EXTPAGETYPE_SAS_PHY,
        mptsas_config_phy_0,
    }, {
        1, MPI_CONFIG_EXTPAGETYPE_SAS_PHY,
        mptsas_config_phy_1,
    }, {
        0, MPI_CONFIG_EXTPAGETYPE_SAS_DEVICE,
        mptsas_config_sas_device_0,
    }, {
        1, MPI_CONFIG_EXTPAGETYPE_SAS_DEVICE,
        mptsas_config_sas_device_1,
    }, {
       2,  MPI_CONFIG_EXTPAGETYPE_SAS_DEVICE,
        mptsas_config_sas_device_2,
    }, {
        0, MPI_CONFIG_EXTPAGETYPE_SAS_EXPANDER, NULL, 0x03, 36,
    }, {
        1, MPI_CONFIG_EXTPAGETYPE_SAS_EXPANDER, NULL, 0x01, 40,
    }, {
        0, MPI_CONFIG_EXTPAGETYPE_ENCLOSURE, mptsas_config_enclosure_0,
    }
};

static const MPTSASConfigPage *mptsas_find_config_page(MPTSASState *s,
                                                       int type, int number)
{
    const MPTSASConfigPage *page;
    int i;

    if (mptsas_is_spi(s)) {
        if (type > MPI_CONFIG_PAGETYPE_MASK) {
            return NULL;
        }
    } else if (type == MPI_CONFIG_PAGETYPE_SCSI_PORT ||
               type == MPI_CONFIG_PAGETYPE_SCSI_DEVICE) {
        return NULL;
    }

    for (i = 0; i < ARRAY_SIZE(mptsas_config_pages); i++) {
        page = &mptsas_config_pages[i];
        if (page->type == type && page->number == number) {
            return page;
        }
    }

    return NULL;
}

static int mptsas_write_ioc_1(MPTSASState *s, int address, uint64_t pa,
                              uint32_t dmalen)
{
    uint8_t page_data[16];
    uint32_t flags;

    if (address) {
        return MPI_IOCSTATUS_CONFIG_INVALID_PAGE;
    }
    if (dmalen < sizeof(page_data)) {
        return MPI_IOCSTATUS_CONFIG_INVALID_DATA;
    }

    pci_dma_read(PCI_DEVICE(s), pa, page_data, sizeof(page_data));
    if (page_data[0] != 0x03 || page_data[1] < sizeof(page_data) / 4 ||
        page_data[2] != 1 ||
        (page_data[3] & MPI_CONFIG_PAGETYPE_MASK) !=
        MPI_CONFIG_PAGETYPE_IOC) {
        return MPI_IOCSTATUS_CONFIG_INVALID_DATA;
    }

    flags = ldl_le_p(page_data + 4);
    /*
     * Reply format changes and EEDP are not advertised by this adapter.
     */
    if ((flags & ~(uint32_t)MPI_IOCPAGE1_REPLY_COALESCING) ||
        lduw_le_p(page_data + 14)) {
        return MPI_IOCSTATUS_CONFIG_INVALID_DATA;
    }

    s->ioc1_flags = flags;
    s->ioc1_coalescing_timeout = ldl_le_p(page_data + 8);
    s->ioc1_coalescing_depth = page_data[12];
    mptsas_coalescing_changed(s);
    return MPI_IOCSTATUS_SUCCESS;
}

static int mptspi_write_port_1(MPTSASState *s, int address, uint64_t pa,
                               uint32_t dmalen)
{
    const uint32_t configuration_mask =
        MPI_SCSIPORTPAGE1_CFG_PORT_SCSI_ID_MASK |
        MPI_SCSIPORTPAGE1_CFG_PORT_RESPONSE_ID_MASK;
    uint8_t page_data[16];
    uint32_t configuration;
    unsigned int port_id;

    if (mptspi_port_addr_get(address) < 0) {
        return MPI_IOCSTATUS_CONFIG_INVALID_PAGE;
    }
    if (dmalen < sizeof(page_data)) {
        return MPI_IOCSTATUS_CONFIG_INVALID_DATA;
    }

    pci_dma_read(PCI_DEVICE(s), pa, page_data, sizeof(page_data));
    if (page_data[0] != 0x03 || page_data[1] < sizeof(page_data) / 4 ||
        page_data[2] != 1 ||
        (page_data[3] & MPI_CONFIG_PAGETYPE_MASK) !=
        MPI_CONFIG_PAGETYPE_SCSI_PORT) {
        return MPI_IOCSTATUS_CONFIG_INVALID_DATA;
    }

    configuration = ldl_le_p(page_data + 4);
    port_id = configuration & MPI_SCSIPORTPAGE1_CFG_PORT_SCSI_ID_MASK;
    if ((configuration & ~configuration_mask) ||
        port_id >= MPTSPI_MAX_TARGETS || page_data[12] || page_data[13] ||
        lduw_le_p(page_data + 14)) {
        /* Target mode is not implemented by this initiator-only adapter. */
        return MPI_IOCSTATUS_CONFIG_INVALID_DATA;
    }

    s->spi_port_configuration = configuration;
    s->spi_port_on_bus_timer = ldl_le_p(page_data + 8);
    return MPI_IOCSTATUS_SUCCESS;
}

static int mptsas_write_current(MPTSASState *s, int type, int number,
                                int address, uint64_t pa, uint32_t dmalen)
{
    uint8_t page_data[16];
    int target;

    if (type == MPI_CONFIG_PAGETYPE_IOC && number == 1) {
        return mptsas_write_ioc_1(s, address, pa, dmalen);
    }
    if (mptsas_is_spi(s) && type == MPI_CONFIG_PAGETYPE_SCSI_PORT &&
        number == 1) {
        return mptspi_write_port_1(s, address, pa, dmalen);
    }

    if (!mptsas_is_spi(s) || type != MPI_CONFIG_PAGETYPE_SCSI_DEVICE ||
        number != 1) {
        return MPI_IOCSTATUS_CONFIG_CANT_COMMIT;
    }

    target = mptspi_device_addr_get(address);
    if (target < 0) {
        return MPI_IOCSTATUS_CONFIG_INVALID_PAGE;
    }
    if (dmalen < sizeof(page_data)) {
        return MPI_IOCSTATUS_CONFIG_INVALID_DATA;
    }

    pci_dma_read(PCI_DEVICE(s), pa, page_data, sizeof(page_data));
    if (page_data[0] != 0x05 || page_data[1] < sizeof(page_data) / 4 ||
        page_data[2] != 1 ||
        (page_data[3] & MPI_CONFIG_PAGETYPE_MASK) !=
        MPI_CONFIG_PAGETYPE_SCSI_DEVICE || ldl_le_p(page_data + 8)) {
        return MPI_IOCSTATUS_CONFIG_INVALID_DATA;
    }

    s->spi_requested_params[target] = ldl_le_p(page_data + 4);
    s->spi_configuration[target] = ldl_le_p(page_data + 12);
    return MPI_IOCSTATUS_SUCCESS;
}

static int mptsas_set_current_to_default(MPTSASState *s, int type,
                                         int number, int address)
{
    int target;

    if (type == MPI_CONFIG_PAGETYPE_IOC && number == 1) {
        if (address) {
            return MPI_IOCSTATUS_CONFIG_INVALID_PAGE;
        }
        s->ioc1_flags = 0;
        s->ioc1_coalescing_timeout = 0;
        s->ioc1_coalescing_depth = 0;
        mptsas_coalescing_changed(s);
        return MPI_IOCSTATUS_SUCCESS;
    }

    if (!mptsas_is_spi(s)) {
        /* Preserve the SAS1068 PAGE_DEFAULT success/no-op guest ABI. */
        return MPI_IOCSTATUS_SUCCESS;
    }

    if (type == MPI_CONFIG_PAGETYPE_SCSI_PORT && number == 1) {
        if (mptspi_port_addr_get(address) < 0) {
            return MPI_IOCSTATUS_CONFIG_INVALID_PAGE;
        }
        s->spi_port_configuration = MPTSPI_DEFAULT_PORT_CONFIGURATION;
        s->spi_port_on_bus_timer = 0;
        return MPI_IOCSTATUS_SUCCESS;
    }

    if (type == MPI_CONFIG_PAGETYPE_SCSI_DEVICE && number == 1) {
        target = mptspi_device_addr_get(address);
        if (target < 0) {
            return MPI_IOCSTATUS_CONFIG_INVALID_PAGE;
        }
        s->spi_requested_params[target] = 0;
        s->spi_configuration[target] = 0;
        return MPI_IOCSTATUS_SUCCESS;
    }

    return MPI_IOCSTATUS_CONFIG_CANT_COMMIT;
}

static int mptsas_persistent_slot(int type, int number)
{
    if (type == MPI_CONFIG_EXTPAGETYPE_SAS_IO_UNIT &&
        (number == 1 || number == 2)) {
        return number == 1 ? MPTSAS_CONFIG_SAS_IO_UNIT_1 :
                             MPTSAS_CONFIG_SAS_IO_UNIT_2;
    }
    if (type == MPI_CONFIG_PAGETYPE_IO_UNIT && number == 1) {
        return MPTSAS_CONFIG_IO_UNIT_1;
    }
    if (type != MPI_CONFIG_PAGETYPE_MANUFACTURING) {
        return -1;
    }
    if (number == 1) {
        return MPTSAS_CONFIG_MANUFACTURING_1;
    }
    return number == 4 ? MPTSAS_CONFIG_MANUFACTURING_4 : -1;
}

static int mptsas_write_config_page(MPTSASState *s, int type, int number,
                                   int address, uint64_t pa, uint32_t dmalen,
                                   const uint8_t *header, size_t length,
                                   bool nvram)
{
    uint8_t page[4 + sizeof(s->config_nvram[0])];
    int slot = mptsas_persistent_slot(type, number);

    if (slot < 0) {
        return MPI_IOCSTATUS_CONFIG_CANT_COMMIT;
    }
    if (address) {
        return MPI_IOCSTATUS_CONFIG_INVALID_PAGE;
    }
    if (length > sizeof(page) || dmalen < length) {
        return MPI_IOCSTATUS_CONFIG_INVALID_DATA;
    }
    if (pci_dma_read(PCI_DEVICE(s), pa, page, length) != MEMTX_OK) {
        return MPI_IOCSTATUS_INTERNAL_ERROR;
    }
    if (page[0] != header[0] || page[1] != header[1] || page[2] != number ||
        (page[3] & MPI_CONFIG_PAGETYPE_MASK) !=
        (header[3] & MPI_CONFIG_PAGETYPE_MASK) ||
        (type > MPI_CONFIG_PAGETYPE_MASK &&
         (lduw_le_p(page + 4) != length / 4 || page[6] != type))) {
        return MPI_IOCSTATUS_CONFIG_INVALID_DATA;
    }
    if (type == MPI_CONFIG_PAGETYPE_IO_UNIT) {
        uint32_t flags = ldl_le_p(page + 4);
        uint32_t fixed = mptsas_is_spi(s) ? MPI_IOUNITPAGE1_MULTI_FUNCTION :
                                           MPI_IOUNITPAGE1_SINGLE_FUNCTION;
        uint32_t writable = MPI_IOUNITPAGE1_DISABLE_IR |
                            MPI_IOUNITPAGE1_SATA_WRITE_CACHE_DISABLE;

        /*
         * Function layout is fixed; RAID availability comes from IOC Page 2.
         * The SATA cache policy has no effect on SSP or parallel targets.
         */
        if ((flags & ~writable) != fixed) {
            return MPI_IOCSTATUS_CONFIG_INVALID_DATA;
        }
    }
    if (type == MPI_CONFIG_EXTPAGETYPE_SAS_IO_UNIT && number == 1) {
        size_t i;

        for (i = 8; i < length; i++) {
            if (i >= 20 && (i - 20) % 12 == 0) {
                /* Automatic assignment derives port numbers from topology. */
                if (page[i + 1] & MPI_SAS_IOUNIT1_AUTO_PORT_CONFIG) {
                    continue;
                }
            } else if (i >= 20 && (i - 20) % 12 == 1) {
                if (!(page[i] & ~MPI_SAS_IOUNIT1_AUTO_PORT_CONFIG)) {
                    continue;
                }
            }
            if (page[i] != header[i]) {
                return MPI_IOCSTATUS_CONFIG_INVALID_DATA;
            }
        }
    } else if (type == MPI_CONFIG_EXTPAGETYPE_SAS_IO_UNIT) {
        uint8_t writable = MPI_SAS_IOUNIT2_DA_STARTING_SLOT |
                           MPI_SAS_IOUNIT2_RESERVE_ID_0_FOR_BOOT;
        size_t i;

        for (i = 8; i < length; i++) {
            if ((i >= 12 && i <= 16) || i >= 18) {
                /* Capacity and usage fields are supplied by the controller. */
                page[i] = header[i];
                continue;
            }
            if (i == 17 && !(page[i] & ~writable)) {
                /* Allocation preferences do not enable physical mapping. */
                continue;
            }
            if (page[i] != header[i]) {
                return MPI_IOCSTATUS_CONFIG_INVALID_DATA;
            }
        }
    }
    if (nvram) {
        memcpy(s->config_nvram[slot], page + 4, length - 4);
        s->config_nvram_written |= 1 << slot;
    } else {
        memcpy(s->config_current[slot], page + 4, length - 4);
        s->config_current_written |= 1 << slot;
    }
    return MPI_IOCSTATUS_SUCCESS;
}

void mptsas_process_config(MPTSASState *s, MPIMsgConfig *req)
{
    PCIDevice *pci = PCI_DEVICE(s);

    MPIMsgConfigReply reply;
    const MPTSASConfigPage *page;
    size_t length;
    uint8_t type;
    uint8_t *data = NULL;
    uint32_t flags_and_length;
    uint32_t dmalen;
    uint64_t pa;
    int persistent_slot;

    mptsas_fix_config_endianness(req);

    trace_mptsas_config_request(s, req->MsgContext, req->Action,
                                req->PageType, req->PageNumber,
                                req->PageAddress,
                                req->PageBufferSGE.FlagsLength,
                                req->PageBufferSGE.FlagsLength &
                                MPI_SGE_FLAGS_64_BIT_ADDRESSING ?
                                req->PageBufferSGE.u.Address64 :
                                req->PageBufferSGE.u.Address32);

    QEMU_BUILD_BUG_ON(sizeof(s->doorbell_msg) < sizeof(*req));
    QEMU_BUILD_BUG_ON(sizeof(s->doorbell_reply) < sizeof(reply));

    /* Copy common bits from the request into the reply. */
    memset(&reply, 0, sizeof(reply));
    reply.Action      = req->Action;
    reply.Function    = req->Function;
    reply.MsgContext  = req->MsgContext;
    reply.MsgLength   = sizeof(reply) / 4;
    reply.PageType    = req->PageType;
    reply.PageNumber  = req->PageNumber;
    reply.PageLength  = req->PageLength;
    reply.PageVersion = req->PageVersion;

    type = req->PageType & MPI_CONFIG_PAGETYPE_MASK;
    if (type == MPI_CONFIG_PAGETYPE_EXTENDED) {
        type = req->ExtPageType;
        if (type <= MPI_CONFIG_PAGETYPE_MASK) {
            reply.IOCStatus = MPI_IOCSTATUS_CONFIG_INVALID_TYPE;
            goto out;
        }

        reply.ExtPageType = req->ExtPageType;
    }

    page = mptsas_find_config_page(s, type, req->PageNumber);

    switch(req->Action) {
    case MPI_CONFIG_ACTION_PAGE_DEFAULT:
    case MPI_CONFIG_ACTION_PAGE_HEADER:
    case MPI_CONFIG_ACTION_PAGE_READ_NVRAM:
    case MPI_CONFIG_ACTION_PAGE_READ_CURRENT:
    case MPI_CONFIG_ACTION_PAGE_READ_DEFAULT:
    case MPI_CONFIG_ACTION_PAGE_WRITE_CURRENT:
    case MPI_CONFIG_ACTION_PAGE_WRITE_NVRAM:
        break;

    default:
        reply.IOCStatus = MPI_IOCSTATUS_CONFIG_INVALID_ACTION;
        goto out;
    }

    if (!page) {
        page = mptsas_find_config_page(s, type, 1);
        if (page) {
            reply.IOCStatus = MPI_IOCSTATUS_CONFIG_INVALID_PAGE;
        } else {
            reply.IOCStatus = MPI_IOCSTATUS_CONFIG_INVALID_TYPE;
        }
        goto out;
    }

    if (!page->mpt_config_build) {
        /* The format is defined even when no topology instance exists. */
        if (req->Action == MPI_CONFIG_ACTION_PAGE_HEADER) {
            reply.PageVersion = page->header_version;
            reply.ExtPageLength = page->header_length / 4;
        } else {
            reply.IOCStatus = MPI_IOCSTATUS_CONFIG_INVALID_PAGE;
        }
        goto out;
    }

    flags_and_length = req->PageBufferSGE.FlagsLength;
    dmalen = flags_and_length & MPI_SGE_LENGTH_MASK;
    if (flags_and_length & MPI_SGE_FLAGS_64_BIT_ADDRESSING) {
        pa = req->PageBufferSGE.u.Address64;
    } else {
        pa = req->PageBufferSGE.u.Address32;
    }

    if (req->Action == MPI_CONFIG_ACTION_PAGE_HEADER &&
        type == MPI_CONFIG_EXTPAGETYPE_SAS_DEVICE) {
        length = mptsas_config_sas_device_page(s, &data, req->PageAddress,
                                               page->number, true);
    } else if (req->Action == MPI_CONFIG_ACTION_PAGE_HEADER &&
               type == MPI_CONFIG_EXTPAGETYPE_ENCLOSURE) {
        length = mptsas_config_enclosure_0(s, &data, UINT16_MAX);
    } else {
        length = page->mpt_config_build(s, &data, req->PageAddress);
    }
    if ((ssize_t)length < 0) {
        reply.IOCStatus = MPI_IOCSTATUS_CONFIG_INVALID_PAGE;
        goto out;
    }

    persistent_slot = mptsas_persistent_slot(type, page->number);
    if (persistent_slot >= 0) {
        if (req->Action == MPI_CONFIG_ACTION_PAGE_READ_NVRAM &&
            (s->config_nvram_written & (1 << persistent_slot))) {
            memcpy(data + 4, s->config_nvram[persistent_slot],
                   length - 4);
        } else if (req->Action == MPI_CONFIG_ACTION_PAGE_READ_CURRENT &&
                   (s->config_current_written &
                    (1 << persistent_slot))) {
            if (type == MPI_CONFIG_EXTPAGETYPE_SAS_IO_UNIT &&
                page->number == 1) {
                size_t i;

                for (i = 21; i < length; i += 12) {
                    data[i] = s->config_current[persistent_slot][i - 4];
                }
            } else if (type == MPI_CONFIG_EXTPAGETYPE_SAS_IO_UNIT) {
                data[17] = s->config_current[persistent_slot][13];
            } else {
                memcpy(data + 4, s->config_current[persistent_slot],
                       length - 4);
            }
        }
    }
    assert(data[2] == page->number);
    reply.PageVersion = data[0];
    reply.PageNumber = data[2];
    reply.PageType = data[3];

    switch (req->Action) {
    case MPI_CONFIG_ACTION_PAGE_DEFAULT:
        if (persistent_slot >= 0 && !req->PageAddress) {
            s->config_current_written &= ~(1 << persistent_slot);
            break;
        }
        reply.IOCStatus = mptsas_set_current_to_default(s, type, page->number,
                                                        req->PageAddress);
        break;

    case MPI_CONFIG_ACTION_PAGE_HEADER:
        break;

    case MPI_CONFIG_ACTION_PAGE_READ_NVRAM:
    case MPI_CONFIG_ACTION_PAGE_READ_CURRENT:
    case MPI_CONFIG_ACTION_PAGE_READ_DEFAULT:
        if (type == MPI_CONFIG_PAGETYPE_IOC && page->number == 1 &&
            req->Action != MPI_CONFIG_ACTION_PAGE_READ_CURRENT) {
            memset(data + 4, 0, length - 4);
        } else if (mptsas_is_spi(s) &&
                   type == MPI_CONFIG_PAGETYPE_SCSI_PORT &&
                   page->number == 1 &&
                   req->Action != MPI_CONFIG_ACTION_PAGE_READ_CURRENT) {
            stl_le_p(data + 4, MPTSPI_DEFAULT_PORT_CONFIGURATION);
            memset(data + 8, 0, length - 8);
        } else if (mptsas_is_spi(s) &&
                   type == MPI_CONFIG_PAGETYPE_SCSI_DEVICE &&
                   page->number == 1 &&
                   req->Action != MPI_CONFIG_ACTION_PAGE_READ_CURRENT) {
            memset(data + 4, 0, length - 4);
        }
        if (dmalen) {
            pci_dma_write(pci, pa, data, MIN(length, dmalen));
        }
        break;

    case MPI_CONFIG_ACTION_PAGE_WRITE_CURRENT:
        if (persistent_slot >= 0) {
            reply.IOCStatus = mptsas_write_config_page(
                s, type, page->number, req->PageAddress, pa, dmalen,
                data, length, false);
            break;
        }
        reply.IOCStatus = mptsas_write_current(s, type, page->number,
                                               req->PageAddress, pa, dmalen);
        break;

    case MPI_CONFIG_ACTION_PAGE_WRITE_NVRAM:
        reply.IOCStatus = mptsas_write_config_page(
            s, type, page->number, req->PageAddress, pa, dmalen,
            data, length, true);
        break;

    default:
        abort();
    }

    if (type > MPI_CONFIG_PAGETYPE_MASK) {
        reply.ExtPageLength = lduw_le_p(data + 4);
        reply.ExtPageType = data[6];
    } else {
        reply.PageLength = data[1];
    }

out:
    trace_mptsas_config_reply(s, reply.MsgContext, reply.IOCStatus,
                              reply.PageType, reply.PageNumber,
                              reply.PageLength, reply.ExtPageLength,
                              reply.ExtPageType);
    mptsas_fix_config_reply_endianness(&reply);
    mptsas_reply(s, (MPIDefaultReply *)&reply);
    g_free(data);
}
