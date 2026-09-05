/*
 * Broadcom BCM5701/BCM5704 PCI Ethernet controllers
 *
 * Technical references are listed in
 * docs/devel/device-emulation-provenance.rst.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/core/qdev-properties.h"
#include "hw/net/bcm5704.h"
#include "hw/pci/pci_device.h"
#include "migration/vmstate.h"
#include "net/net.h"
#include "net/checksum.h"
#include "qemu/bswap.h"
#include "trace.h"
#include "qapi/error.h"
#include "qemu/bitops.h"
#include "qemu/module.h"

#define BCM57XX_PCI_PCIX_CAP             0x40

#define BCM57XX_PCI_MISC_HOST_CTRL       0x68
#define BCM57XX_MISC_HOST_CTRL_RW_MASK   0x000003feU
#define BCM57XX_MISC_HOST_CTRL_CHIPREV_SHIFT 16

#define BCM5701_CHIPREV_ID_B5            0x0105
#define BCM5704_CHIPREV_ID_B0            0x2100

#define BCM57XX_PCIX_COMMAND_RW_MASK     \
    (PCI_X_CMD_DPERR_E | PCI_X_CMD_ERO | PCI_X_CMD_MAX_READ | \
     PCI_X_CMD_MAX_SPLIT)
#define BCM57XX_PCIX_STATUS_W1C_MASK     \
    (PCI_X_STATUS_SPL_DISC | PCI_X_STATUS_UNX_SPL | PCI_X_STATUS_SPL_ERR)

/* BCM5704: 64-bit/133 MHz, DMMRBC=2, DMOST=0, DMCRS=1. */
#define BCM5704_PCIX_STATUS_CAPS          \
    (PCI_X_STATUS_64BIT | PCI_X_STATUS_133MHZ | (2U << 21) | (1U << 26))

/*
 * Embedded processor execution is not implemented.  Reset provides the board
 * data and firmware-mailbox handshake exposed by the modeled interface.
 */
#define BCM_SRAM_SIZE           0x20000
#define BCM_EEPROM_SIZE         0x10000
#define BCM_MAX_FRAME           9216
#define BCM_MAX_TSO             0x10100
#define BCM_RINGS               16
#define REG(s, a)               ((s)->regs[(a) / 4])
#define SRAM(s, a)              ((s)->sram[(a) / 4])
#define MAC_MODE                0x0400
#define MAC_STATUS              0x0404
#define MAC_EVENT               0x0408
#define MAC_MI_COM              0x044c
#define MAC_TX_MODE             0x045c
#define MAC_RX_MODE             0x0468
#define HOSTCC_MODE             0x3c00
#define GRC_MODE                0x6800
#define GRC_MISC_CFG            0x6804
#define GRC_LOCAL_CTRL          0x6808
#define GRC_EEPROM_ADDR         0x6838
#define NVRAM_CMD               0x7000
#define FW_MBOX                 0x0b50
#define FW_MAGIC                0x4b657654U

struct BCM57xxState {
    PCIDevice parent_obj;
    MemoryRegion mmio;
    MemoryRegion pci_rom;
    NICConf conf;
    NICState *nic;
    uint32_t regs[BCM57XX_MMIO_SIZE / 4];
    uint32_t sram[BCM_SRAM_SIZE / 4];
    uint8_t eeprom[BCM_EEPROM_SIZE];
    uint16_t phy[32];
    uint16_t dsp[0x10000];
    uint16_t tx_cons[BCM_RINGS];
    uint16_t rx_cons[3]; /* standard, jumbo, mini */
    uint16_t rx_prod;
    uint8_t status_tag;
    uint32_t status_flags;
    bool irq_pending;
    bool irq_mailbox_mask;
    bool tx_busy;
    uint32_t tx_len[BCM_RINGS];
    uint32_t tx_flags[BCM_RINGS];
    uint32_t tx_vlan[BCM_RINGS];
    uint8_t tx_buf[BCM_RINGS][BCM_MAX_TSO];
};

static void bcm57xx_core_reset(BCM57xxState *s);
static uint64_t bcm57xx_mmio_read(void *opaque, hwaddr addr, unsigned size);
static void bcm57xx_mmio_write(void *opaque, hwaddr addr, uint64_t v,
                              unsigned size);
static uint32_t bcm57xx_config_read(PCIDevice *pdev, uint32_t addr, int len);
static void bcm57xx_config_write(PCIDevice *pdev, uint32_t addr, uint32_t v,
                                int len);

static bool bcm57xx_link_up(BCM57xxState *s)
{
    return s->nic && !qemu_get_queue(s->nic)->link_down &&
           !(s->phy[0] & 0x0800);
}

static void bcm57xx_update_irq(BCM57xxState *s)
{
    bool level = s->irq_pending && !s->irq_mailbox_mask &&
        !(pci_get_long(s->parent_obj.config + 0x68) & 2);
    uint32_t state = pci_get_long(s->parent_obj.config + 0x70);

    pci_set_long(s->parent_obj.config + 0x70,
                 (state & ~2U) | (level ? 0 : 2));
    pci_set_irq(&s->parent_obj, level);
}

static uint32_t bcm57xx_desc_load(BCM57xxState *s, const void *p)
{
    return (REG(s, GRC_MODE) & 2) ? ldl_be_p(p) : ldl_le_p(p);
}

static void bcm57xx_desc_store(BCM57xxState *s, void *p, uint32_t v)
{
    if (REG(s, GRC_MODE) & 2) {
        stl_be_p(p, v);
    } else {
        stl_le_p(p, v);
    }
}

static uint64_t bcm57xx_address(uint32_t hi, uint32_t lo)
{
    return ((uint64_t)hi << 32) | lo;
}

static bool bcm57xx_bus_master(BCM57xxState *s)
{
    return (pci_get_word(s->parent_obj.config + PCI_COMMAND) &
            PCI_COMMAND_MASTER) &&
           !(pci_get_word(s->parent_obj.config + 0x4c) &
             PCI_PM_CTRL_STATE_MASK);
}

/* Statistics SRAM uses a high/low pair for each 64-bit counter. */
static void bcm57xx_stat_add(BCM57xxState *s, unsigned off, uint64_t n)
{
    uint64_t v = bcm57xx_address(SRAM(s, off), SRAM(s, off + 4)) + n;

    SRAM(s, off) = v >> 32;
    SRAM(s, off + 4) = v;
}

static void bcm57xx_stats_dma(BCM57xxState *s)
{
    uint64_t addr = bcm57xx_address(REG(s, 0x3c30), REG(s, 0x3c34));
    uint8_t stats[0x800];
    unsigned i;

    if (!addr || !REG(s, 0x3c28)) {
        return;
    }
    for (i = 0; i < sizeof(stats); i += 4) {
        bcm57xx_desc_store(s, stats + i, SRAM(s, 0x300 + i));
    }
    if (pci_dma_write(&s->parent_obj, addr, stats, sizeof(stats))) {
        REG(s, 0x4c04) |= 8;
    }
}

static void bcm57xx_status(BCM57xxState *s, uint32_t flags, bool interrupt)
{
    uint8_t status[80] = { 0 };
    uint64_t addr = bcm57xx_address(REG(s, 0x3c38), REG(s, 0x3c3c));
    unsigned i, len = sizeof(status);

    if (!(REG(s, HOSTCC_MODE) & 2) || !bcm57xx_bus_master(s)) {
        return;
    }
    bcm57xx_stats_dma(s);
    s->status_tag++;
    s->status_flags |= flags;
    bcm57xx_desc_store(s, status, s->status_flags | 1);
    bcm57xx_desc_store(s, status + 4, s->status_tag);
    bcm57xx_desc_store(s, status + 8,
                       ((uint32_t)s->rx_cons[0] << 16) | s->rx_cons[1]);
    bcm57xx_desc_store(s, status + 12, s->rx_cons[2]);
    for (i = 0; i < BCM_RINGS; i++) {
        bcm57xx_desc_store(s, status + 16 + i * 4,
                           ((uint32_t)s->tx_cons[i] << 16) |
                           (i ? 0 : s->rx_prod));
    }
    if (REG(s, HOSTCC_MODE) & 0x100) {
        len = 32;
    } else if (REG(s, HOSTCC_MODE) & 0x80) {
        len = 64;
    }
    if (pci_dma_write(&s->parent_obj, addr + 4, status + 4, len - 4) ||
        pci_dma_write(&s->parent_obj, addr, status, 4)) {
        REG(s, 0x4c04) |= 8; /* DMA master abort */
        return;
    }
    if (interrupt) {
        s->irq_pending = true;
    }
    bcm57xx_update_irq(s);
}

static void bcm57xx_set_link(NetClientState *nc)
{
    BCM57xxState *s = qemu_get_nic_opaque(nc);

    REG(s, MAC_STATUS) |= 0x1000;
    s->phy[0x1a] |= 2;
    bcm57xx_status(s, 2, !!(REG(s, MAC_EVENT) & 0x1000));
}

static void bcm57xx_phy_reset(BCM57xxState *s)
{
    bool is5701 = pci_get_word(s->parent_obj.config + PCI_DEVICE_ID) ==
                  BCM5701_PCI_DEVICE_ID;

    memset(s->phy, 0, sizeof(s->phy));
    s->phy[0] = 0x1140; /* autonegotiation, full duplex, 1000BASE-T */
    s->phy[1] = 0x7909;
    s->phy[2] = 0x0020;
    s->phy[3] = is5701 ? 0x6111 : 0x6191;
    s->phy[4] = 0x0de1;
    s->phy[5] = 0xcde1;
    s->phy[6] = 1;
    s->phy[9] = 0x0300;
    s->phy[10] = 0x3c00;
    s->phy[15] = 0x3000;
    s->phy[0x1b] = 0xffff;
}

static uint16_t bcm57xx_phy_read(BCM57xxState *s, unsigned reg)
{
    uint16_t v;
    bool link = bcm57xx_link_up(s) || (s->phy[0] & 0x4000);

    switch (reg) {
    case 1:
        return s->phy[1] | (link ? 0x24 : 0);
    case 0x11:
        return link ? 0x0100 : 0;
    case 0x19:
        if (!link) {
            return 0;
        }
        if ((s->phy[0] & 0x1000) || (s->phy[0] & 0x0040)) {
            v = 0x0700;
        } else if (s->phy[0] & 0x2000) {
            v = 0x0300;
        } else {
            v = 0x0100;
        }
        if (!(s->phy[0] & 0x1040) && (s->phy[0] & 0x0100)) {
            v += (s->phy[0] & 0x2000) ? 0x0200 : 0x0100;
        }
        return v | 0x8004;
    case 0x15:
        v = s->dsp[s->phy[0x17]];
        if (s->phy[0x16] & 2) {
            s->phy[0x17]++;
        }
        return v;
    case 0x1a:
        v = s->phy[reg];
        s->phy[reg] = 0;
        return v;
    default:
        return s->phy[reg];
    }
}

static void bcm57xx_phy_write(BCM57xxState *s, unsigned reg, uint16_t v)
{
    switch (reg) {
    case 0:
        if (v & 0x8000) {
            bcm57xx_phy_reset(s);
        } else {
            s->phy[0] = v & ~0x0200; /* restart autonegotiation self-clears */
        }
        bcm57xx_set_link(qemu_get_queue(s->nic));
        break;
    case 1: case 2: case 3: case 5: case 6: case 10: case 15:
    case 0x11: case 0x19: case 0x1a:
        break;
    case 0x15:
        s->dsp[s->phy[0x17]] = v;
        if (s->phy[0x16] & 2) {
            s->phy[0x17]++;
        }
        break;
    case 0x16:
        s->phy[reg] = v & ~0x1000; /* DSP transfer completes synchronously */
        break;
    default:
        s->phy[reg] = v;
        break;
    }
}

static void bcm57xx_mii(BCM57xxState *s, uint32_t v)
{
    unsigned phy = (v >> 21) & 31;
    unsigned reg = (v >> 16) & 31;

    if (!(v & 0x20000000)) {
        return;
    }
    v &= ~0x30000000U;
    if (phy != 1) {
        v |= 0x1000ffff; /* no PHY at this address */
    } else if (v & 0x08000000) {
        v = (v & ~0xffffU) | bcm57xx_phy_read(s, reg);
    } else if (v & 0x04000000) {
        bcm57xx_phy_write(s, reg, v);
    }
    REG(s, MAC_MI_COM) = v;
    REG(s, MAC_STATUS) |= 0x00400000;
}

static bool bcm57xx_can_receive(NetClientState *nc)
{
    BCM57xxState *s = qemu_get_nic_opaque(nc);

    return (REG(s, MAC_RX_MODE) & 2) && bcm57xx_bus_master(s) &&
           (bcm57xx_link_up(s) || (REG(s, MAC_MODE) & 0x10) ||
            (s->phy[0] & 0x4000));
}

static bool bcm57xx_accept(BCM57xxState *s, const uint8_t *buf)
{
    unsigned i;
    uint32_t hi = lduw_be_p(buf), lo = ldl_be_p(buf + 2);

    if ((REG(s, MAC_RX_MODE) & 0x100) ||
        (hi == 0xffff && lo == UINT32_MAX)) {
        return true;
    }
    if (buf[0] & 1) {
        unsigned hash = net_crc32_le(buf, 6) & 0x7f;
        return REG(s, 0x470 + (hash >> 5) * 4) & (1U << (hash & 31));
    }
    for (i = 0; i < 4; i++) {
        if ((REG(s, 0x410 + i * 8) & 0xffff) == hi &&
            REG(s, 0x414 + i * 8) == lo) {
            return true;
        }
    }
    return false;
}

static ssize_t bcm57xx_receive(NetClientState *nc, const uint8_t *buf,
                               size_t size)
{
    BCM57xxState *s = qemu_get_nic_opaque(nc);
    uint8_t desc[32], frame[BCM_MAX_FRAME + 4];
    uint32_t rcb, prodmb, idxlen, flags, vlan = 0;
    uint32_t retcfg = SRAM(s, 0x208);
    uint32_t retsize = retcfg >> 16;
    uint64_t ringaddr, dataaddr, retaddr;
    unsigned ring = 0, ringlen = 512, next, length;

    if (!bcm57xx_can_receive(nc)) {
        return 0;
    }
    if (size < 14 || size > BCM_MAX_FRAME - 4 ||
        !bcm57xx_accept(s, buf)) {
        return size;
    }
    if (size + 4 > (REG(s, 0x43c) & 0xffff) &&
        !(REG(s, MAC_RX_MODE) & 0x20)) {
        REG(s, 0x8b0)++;
        return size;
    }
    if (size > 1518) {
        ring = 1;
        ringlen = 256;
    } else if (!(REG(s, 0x2468) & 2) &&
               (REG(s, 0x2468) >> 16) >= size + 4) {
        ring = 2;
        ringlen = 1024;
    }
    rcb = ring == 0 ? 0x2450 : ring == 1 ? 0x2440 : 0x2460;
    prodmb = ring == 0 ? 0x26c : ring == 1 ? 0x274 : 0x27c;
    if ((REG(s, rcb + 8) & 2) || (retcfg & 2) ||
        retsize < 2 || retsize > 2048 ||
        s->rx_cons[ring] == (REG(s, prodmb) % ringlen)) {
        return 0;
    }
    next = (s->rx_prod + 1) % retsize;
    if (next == REG(s, 0x284) % retsize) {
        return 0;
    }
    ringaddr = bcm57xx_address(REG(s, rcb), REG(s, rcb + 4));
    if (pci_dma_read(&s->parent_obj, ringaddr + s->rx_cons[ring] * 32,
                     desc, sizeof(desc))) {
        REG(s, 0x4804) |= 8;
        return size;
    }
    dataaddr = bcm57xx_address(bcm57xx_desc_load(s, desc),
                               bcm57xx_desc_load(s, desc + 4));
    idxlen = bcm57xx_desc_load(s, desc + 8);
    flags = 4 | (ring == 1 ? 0x20 : ring == 2 ? 0x800 : 0);
    length = size;
    memcpy(frame, buf, size);
    if (lduw_be_p(buf + 12) == 0x8100 && size >= 18 &&
        !(REG(s, MAC_RX_MODE) & 0x400)) {
        vlan = lduw_be_p(buf + 14);
        memmove(frame + 12, frame + 16, size - 16);
        length -= 4;
        flags |= 0x40;
    }
    if (length < 60) {
        memset(frame + length, 0, 60 - length);
        length = 60;
    }
    /* Return lengths include the Ethernet FCS, as required by Tigon3. */
    stl_le_p(frame + length, ~net_crc32_le(frame, length));
    length += 4;
    if (length > (idxlen & 0xffff)) {
        REG(s, 0x2404) |= 8;
        return size;
    }
    if (pci_dma_write(&s->parent_obj, dataaddr, frame, length)) {
        REG(s, 0x4c04) |= 8;
        return size;
    }
    bcm57xx_desc_store(s, desc + 8, (idxlen & 0xffff0000) | length);
    bcm57xx_desc_store(s, desc + 12, flags);
    bcm57xx_desc_store(s, desc + 16, 0); /* software checksums */
    bcm57xx_desc_store(s, desc + 20, vlan);
    retaddr = bcm57xx_address(SRAM(s, 0x200), SRAM(s, 0x204));
    if (pci_dma_write(&s->parent_obj, retaddr + s->rx_prod * 32,
                      desc, sizeof(desc))) {
        REG(s, 0x4c04) |= 8;
        return size;
    }
    s->rx_cons[ring] = (s->rx_cons[ring] + 1) % ringlen;
    s->rx_prod = next;
    REG(s, ring == 0 ? 0x2474 : ring == 1 ? 0x2470 : 0x2478) =
        s->rx_cons[ring];
    REG(s, ring == 0 ? 0x3c54 : ring == 1 ? 0x3c50 : 0x3c58) =
        s->rx_cons[ring];
    REG(s, 0x3c80) = s->rx_prod;
    REG(s, 0x880) += size;
    bcm57xx_stat_add(s, 0x400, size);
    if (!memcmp(buf, "\xff\xff\xff\xff\xff\xff", 6)) {
        REG(s, 0x894)++;
        bcm57xx_stat_add(s, 0x428, 1);
    } else if (buf[0] & 1) {
        REG(s, 0x890)++;
        bcm57xx_stat_add(s, 0x420, 1);
    } else {
        REG(s, 0x88c)++;
        bcm57xx_stat_add(s, 0x418, 1);
    }
    bcm57xx_status(s, 0, !(REG(s, GRC_MODE) & 0x4000));
    return size;
}

static void bcm57xx_send_frame(BCM57xxState *s, uint8_t *frame, unsigned len)
{
    if ((REG(s, MAC_MODE) & 0x10) || (s->phy[0] & 0x4000)) {
        qemu_receive_packet(qemu_get_queue(s->nic), frame, len);
    } else if (bcm57xx_link_up(s)) {
        qemu_send_packet(qemu_get_queue(s->nic), frame, len);
    }
    REG(s, 0x800) += len;
    bcm57xx_stat_add(s, 0x600, len);
    if (!memcmp(frame, "\xff\xff\xff\xff\xff\xff", 6)) {
        REG(s, 0x874)++;
        bcm57xx_stat_add(s, 0x6e8, 1);
    } else if (frame[0] & 1) {
        REG(s, 0x870)++;
        bcm57xx_stat_add(s, 0x6e0, 1);
    } else {
        REG(s, 0x86c)++;
        bcm57xx_stat_add(s, 0x6d8, 1);
    }
}

/*
 * BCM5701/5704 use the TX CPU's segmentation service: CPU_PRE/POST_DMA
 * flags request IPv4 TCP segmentation; the first BD supplies the MSS in
 * the high half of its VLAN word.  Implement the service at packet level.
 */
static bool bcm57xx_tso(BCM57xxState *s, uint8_t *packet, unsigned len,
                        unsigned mss)
{
    uint8_t frame[BCM_MAX_FRAME];
    unsigned l2 = 14, ihl, thl, hdr, sent, payload;
    uint32_t seq;
    uint16_t id;
    uint8_t *ip, *tcp;

    if (len < 14) {
        return false;
    }
    if (lduw_be_p(packet + 12) == 0x8100) {
        l2 += 4;
    }
    if (len < l2 + 40 || lduw_be_p(packet + l2 - 2) != 0x0800 ||
        (packet[l2] >> 4) != 4 || packet[l2 + 9] != 6) {
        return false;
    }
    ihl = (packet[l2] & 15) * 4;
    if (ihl < 20 || len < l2 + ihl + 20 ||
        (lduw_be_p(packet + l2 + 6) & 0x3fff)) {
        return false;
    }
    thl = (packet[l2 + ihl + 12] >> 4) * 4;
    hdr = l2 + ihl + thl;
    if (thl < 20 || hdr > len || !mss || mss > sizeof(frame) - hdr) {
        return false;
    }
    trace_bcm57xx_tso(len, mss, hdr);
    payload = len - hdr;
    seq = ldl_be_p(packet + l2 + ihl + 4);
    id = lduw_be_p(packet + l2 + 4);
    for (sent = 0; sent < payload; sent += mss) {
        unsigned n = MIN(mss, payload - sent);
        unsigned frame_len = hdr + n;

        memcpy(frame, packet, hdr);
        memcpy(frame + hdr, packet + hdr + sent, n);
        ip = frame + l2;
        tcp = ip + ihl;
        stw_be_p(ip + 2, ihl + thl + n);
        stw_be_p(ip + 4, id++);
        stw_be_p(ip + 10, 0);
        stw_be_p(ip + 10, net_raw_checksum(ip, ihl));
        stl_be_p(tcp + 4, seq + sent);
        if (sent + n < payload) {
            tcp[13] &= ~0x09; /* FIN and PSH belong to the final segment */
        }
        if (sent) {
            tcp[13] &= ~0x80; /* CWR belongs to the first segment */
        }
        stw_be_p(tcp + 16, 0);
        stw_be_p(tcp + 16,
                  net_checksum_tcpudp(thl + n, 6, ip + 12, tcp));
        if (frame_len < 60) {
            memset(frame + frame_len, 0, 60 - frame_len);
            frame_len = 60;
        }
        bcm57xx_send_frame(s, frame, frame_len);
    }
    return payload != 0;
}

static void bcm57xx_transmit(BCM57xxState *s, unsigned ring, bool host)
{
    uint32_t rcb = 0x100 + ring * 16;
    uint32_t cfg = SRAM(s, rcb + 8);
    uint32_t count = host ? cfg >> 16 : 512;
    uint32_t producer = REG(s, (host ? 0x304 : 0x384) + ring * 8);
    uint64_t base = bcm57xx_address(SRAM(s, rcb), SRAM(s, rcb + 4));
    unsigned budget;
    bool completed = false;

    if (s->tx_busy || !(REG(s, MAC_TX_MODE) & 2) ||
        !bcm57xx_bus_master(s) || (cfg & 2) || count < 2 || count > 512) {
        return;
    }
    s->tx_busy = true;
    for (budget = 0; budget < count && s->tx_cons[ring] != producer % count;
         budget++) {
        uint8_t desc[16];
        uint32_t lf, vlan, len, limit;
        bool tso;
        uint64_t dataaddr;

        if (host) {
            if (pci_dma_read(&s->parent_obj, base + s->tx_cons[ring] * 16,
                             desc, sizeof(desc))) {
                REG(s, 0x4804) |= 8;
                break;
            }
            dataaddr = bcm57xx_address(bcm57xx_desc_load(s, desc),
                                       bcm57xx_desc_load(s, desc + 4));
            lf = bcm57xx_desc_load(s, desc + 8);
            vlan = bcm57xx_desc_load(s, desc + 12);
        } else {
            uint32_t off = SRAM(s, rcb + 12) + s->tx_cons[ring] * 16;
            if (off > BCM_SRAM_SIZE - 16 || (off & 3)) {
                break;
            }
            dataaddr = bcm57xx_address(SRAM(s, off), SRAM(s, off + 4));
            lf = SRAM(s, off + 8);
            vlan = SRAM(s, off + 12);
        }
        trace_bcm57xx_tx_descriptor(ring, lf, vlan, dataaddr);
        len = lf >> 16;
        if (!s->tx_len[ring]) {
            s->tx_flags[ring] = lf;
            s->tx_vlan[ring] = vlan;
        }
        tso = (s->tx_flags[ring] & 0x300) && (s->tx_vlan[ring] >> 16);
        limit = tso ? BCM_MAX_TSO : BCM_MAX_FRAME;
        if (s->tx_len[ring] > limit || len > limit - s->tx_len[ring] ||
            pci_dma_read(&s->parent_obj, dataaddr,
                         s->tx_buf[ring] + s->tx_len[ring], len)) {
            REG(s, 0x4804) |= 8;
            s->tx_len[ring] = 0;
            break;
        }
        s->tx_len[ring] += len;
        s->tx_cons[ring] = (s->tx_cons[ring] + 1) % count;
        completed = true;
        if (!(lf & 4)) {
            continue;
        }
        len = s->tx_len[ring];
        if (len >= 14) {
            int csum = ((s->tx_flags[ring] & 2) ? CSUM_IP : 0) |
                       ((s->tx_flags[ring] & 1) ? CSUM_TCP | CSUM_UDP : 0);
            if (!tso) {
                net_checksum_calculate(s->tx_buf[ring], len, csum);
            }
            if ((s->tx_flags[ring] & 0x40) && len <= limit - 4) {
                memmove(s->tx_buf[ring] + 16, s->tx_buf[ring] + 12, len - 12);
                stw_be_p(s->tx_buf[ring] + 12, 0x8100);
                stw_be_p(s->tx_buf[ring] + 14, s->tx_vlan[ring]);
                len += 4;
            }
            if (len < 60) {
                memset(s->tx_buf[ring] + len, 0, 60 - len);
                len = 60;
            }
            if (!tso && (s->tx_flags[ring] & 0x1000)) {
                uint32_t slot = (s->tx_flags[ring] >> 13) & 3;
                stw_be_p(s->tx_buf[ring] + 6, REG(s, 0x410 + slot * 8));
                stl_be_p(s->tx_buf[ring] + 8, REG(s, 0x414 + slot * 8));
            }
            if (tso) {
                if (!bcm57xx_tso(s, s->tx_buf[ring], len,
                                 s->tx_vlan[ring] >> 16)) {
                    REG(s, 0x460) |= 0x10;
                    s->status_flags |= 4;
                }
            } else {
                bcm57xx_send_frame(s, s->tx_buf[ring], len);
            }
        }
        s->tx_len[ring] = 0;
        REG(s, 0x3cc0 + ring * 4) = s->tx_cons[ring];
    }
    s->tx_busy = false;
    if (completed) {
        bcm57xx_status(s, 0, !(REG(s, GRC_MODE) & 0x2000));
    }
}

/* Process descriptors posted to the auxiliary read and write DMA queues. */
static void bcm57xx_dma_queue(BCM57xxState *s, uint32_t off, bool read)
{
    uint32_t nicaddr, len, cq;
    uint64_t addr;
    uint8_t *buf;
    unsigned i;
    MemTxResult result;

    if (!bcm57xx_bus_master(s) || off > BCM_SRAM_SIZE - 32 || (off & 3)) {
        return;
    }
    addr = bcm57xx_address(SRAM(s, off), SRAM(s, off + 4));
    nicaddr = SRAM(s, off + 8);
    len = SRAM(s, off + 12) & 0xffff;
    cq = (SRAM(s, off + 12) >> 24) & 0x1f;
    if (nicaddr > BCM_SRAM_SIZE || len > BCM_SRAM_SIZE - nicaddr ||
        (nicaddr & 3) || (len & 3)) {
        return;
    }
    buf = g_malloc(len);
    if (read) {
        result = pci_dma_read(&s->parent_obj, addr, buf, len);
        if (result == MEMTX_OK) {
            for (i = 0; i < len; i += 4) {
                SRAM(s, nicaddr + i) = bcm57xx_desc_load(s, buf + i);
            }
        }
    } else {
        for (i = 0; i < len; i += 4) {
            bcm57xx_desc_store(s, buf + i, SRAM(s, nicaddr + i));
        }
        result = pci_dma_write(&s->parent_obj, addr, buf, len);
    }
    g_free(buf);
    if (result == MEMTX_OK && cq >= 1 && cq <= 17) {
        REG(s, 0x5c08 + cq * 16) = off;
    } else if (result != MEMTX_OK) {
        REG(s, read ? 0x4804 : 0x4c04) |= 8;
    }
}

static uint64_t bcm57xx_mmio_read(void *opaque, hwaddr addr, unsigned size)
{
    BCM57xxState *s = opaque;
    uint32_t v, off = addr & ~3U;

    if (off < 0x100) {
        v = bcm57xx_config_read(&s->parent_obj, off, 4);
    } else if (off >= 0x8000) {
        uint32_t base = pci_get_long(s->parent_obj.config + 0x7c);
        uint64_t sa = (uint64_t)(base & ~0x7fffU) + off - 0x8000;
        v = sa < BCM_SRAM_SIZE ? SRAM(s, sa) : UINT32_MAX;
    } else if (off == 0x460) {
        v = REG(s, off) | (bcm57xx_link_up(s) ? 8 : 0);
    } else if (off == GRC_LOCAL_CTRL) {
        v = REG(s, off) | (s->irq_pending ? 1 : 0);
    } else {
        v = REG(s, off);
    }
    trace_bcm57xx_mmio_read(addr, v, size);
    return (v >> ((addr & 3) * 8)) & MAKE_64BIT_MASK(0, size * 8);
}

static void bcm57xx_mmio_write(void *opaque, hwaddr addr, uint64_t value,
                              unsigned size)
{
    BCM57xxState *s = opaque;
    uint32_t off = addr & ~3U, v = value;
    unsigned i;

    trace_bcm57xx_mmio_write(addr, value, size);
    if (off < 0x100) {
        bcm57xx_config_write(&s->parent_obj, addr, value, size);
        return;
    }
    if (size < 4) {
        uint32_t mask = MAKE_64BIT_MASK((addr & 3) * 8, size * 8);
        v = (bcm57xx_mmio_read(s, off, 4) & ~mask) |
            ((v << ((addr & 3) * 8)) & mask);
    }
    if (off >= 0x8000) {
        uint32_t base = pci_get_long(s->parent_obj.config + 0x7c);
        uint64_t sa = (uint64_t)(base & ~0x7fffU) + off - 0x8000;
        if (sa < BCM_SRAM_SIZE) {
            SRAM(s, sa) = v;
        }
        return;
    }
    switch (off) {
    case MAC_STATUS:
        if (v & 0x1000) {
            s->status_flags &= ~2U;
        }
        REG(s, off) &= ~v;
        return;
    case GRC_MISC_CFG:
        if (v & 1) {
            bcm57xx_core_reset(s);
            return;
        }
        break;
    case MAC_MI_COM:
        REG(s, off) = v;
        bcm57xx_mii(s, v);
        return;
    case 0x204:
        REG(s, off) = v;
        s->irq_mailbox_mask = !!(v & 1);
        s->irq_pending = !s->irq_mailbox_mask &&
            (pci_get_long(s->parent_obj.config + 0x68) & 0x200) &&
            (uint8_t)(v >> 24) != s->status_tag;
        bcm57xx_update_irq(s);
        return;
    case GRC_EEPROM_ADDR:
        if (v & 0x02000000) {
            unsigned ea = (v & 0xffff) & ~3U;
            if (v & 0x80000000) {
                REG(s, 0x683c) = ldl_le_p(s->eeprom + ea);
            } else if (REG(s, GRC_MODE) & 0x200000) {
                stl_le_p(s->eeprom + ea, REG(s, 0x683c));
            }
            v = (v & ~0x02000000U) | 0x40000000;
        }
        v &= ~0x20000000U;
        break;
    case NVRAM_CMD:
        if (v & 0x10) {
            unsigned ea = REG(s, 0x700c) & (BCM_EEPROM_SIZE - 4);
            if (v & 0x20) {
                if (REG(s, GRC_MODE) & 0x200000) {
                    stl_be_p(s->eeprom + ea, REG(s, 0x7008));
                }
            } else {
                REG(s, 0x7010) = ldl_be_p(s->eeprom + ea);
            }
            v = (v & ~0x10U) | 8;
        } else if (v & 8) {
            v = REG(s, off) & ~8U;
        }
        v &= ~1U;
        break;
    case 0x7020: {
        uint32_t req = (REG(s, off) >> 12) & 15;
        req = (req | (v & 15)) & ~((v >> 4) & 15);
        REG(s, off) = (req << 12) | (req << 8);
        return;
    }
    case 0x5c28: case 0x5c18:
        bcm57xx_dma_queue(s, v, true);
        return;
    case 0x5c78: case 0x5c68:
        bcm57xx_dma_queue(s, v, false);
        return;
    case GRC_LOCAL_CTRL:
        if (v & 2) {
            s->irq_pending = false;
        }
        if (v & 4) {
            s->irq_pending = true;
        }
        bcm57xx_update_irq(s);
        v &= ~7U;
        break;
    default:
        break;
    }
    /* All DMA/MAC functional blocks implement a self-clearing reset pulse. */
    if ((off >= 0x400 && off <= 0x4c00 && !(off & 0x3ff)) ||
        off == 0x6000 || off == 0x6400 ||
        off == MAC_TX_MODE || off == MAC_RX_MODE) {
        v &= ~1U;
    }
    REG(s, off) = v;
    if (off == HOSTCC_MODE && (v & 8)) {
        REG(s, off) &= ~8U;
        bcm57xx_status(s, 0, !(v & 0x800));
    }
    if (off >= 0x304 && off <= 0x37c && (off & 7) == 4) {
        bcm57xx_transmit(s, (off - 0x304) / 8, true);
    } else if (off >= 0x384 && off <= 0x3fc && (off & 7) == 4) {
        bcm57xx_transmit(s, (off - 0x384) / 8, false);
    } else if (off == MAC_TX_MODE && (v & 2)) {
        for (i = 0; i < BCM_RINGS; i++) {
            bcm57xx_transmit(s, i, !!(REG(s, GRC_MODE) & 0x20000));
        }
    }
    if (off == MAC_RX_MODE || off == 0x26c || off == 0x274 ||
        off == 0x27c || off == 0x284) {
        qemu_flush_queued_packets(qemu_get_queue(s->nic));
    }
}

static const MemoryRegionOps bcm57xx_mmio_ops = {
    .read = bcm57xx_mmio_read,
    .write = bcm57xx_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl = { .min_access_size = 1, .max_access_size = 4 },
};

static uint64_t bcm57xx_absent_read(void *opaque, hwaddr addr, unsigned size)
{
    return MAKE_64BIT_MASK(0, size * 8);
}

static void bcm57xx_absent_write(void *opaque, hwaddr addr, uint64_t value,
                                 unsigned size)
{
}

static const MemoryRegionOps bcm57xx_absent_ops = {
    .read = bcm57xx_absent_read,
    .write = bcm57xx_absent_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = { .min_access_size = 1, .max_access_size = 8 },
    .impl = { .min_access_size = 1, .max_access_size = 4 },
};

static NetClientInfo bcm57xx_net_info = {
    .type = NET_CLIENT_DRIVER_NIC,
    .size = sizeof(NICState),
    .can_receive = bcm57xx_can_receive,
    .receive = bcm57xx_receive,
    .link_status_changed = bcm57xx_set_link,
};

static uint32_t bcm57xx_config_read(PCIDevice *pdev, uint32_t addr, int len)
{
    BCM57xxState *s = BCM57XX(pdev);
    uint32_t off = addr & ~3U, v;

    switch (off) {
    case 0x80:
        v = pci_get_long(pdev->config + 0x78);
        v = v >= 0x100 && v < BCM57XX_MMIO_SIZE ?
            bcm57xx_mmio_read(s, v & ~3U, 4) : UINT32_MAX;
        break;
    case 0x84:
        v = pci_get_long(pdev->config + 0x7c);
        v = v < BCM_SRAM_SIZE ? SRAM(s, v & ~3U) : UINT32_MAX;
        break;
    case 0x88:
        v = REG(s, GRC_MODE);
        break;
    case 0x8c:
        v = REG(s, GRC_MISC_CFG);
        break;
    case 0x90:
        v = REG(s, GRC_LOCAL_CTRL);
        break;
    default:
        v = pci_default_read_config(pdev, off, 4);
        break;
    }
    v >>= (addr & 3) * 8;
    trace_bcm57xx_config_read(addr, v, len);
    return v & MAKE_64BIT_MASK(0, len * 8);
}

static void bcm57xx_config_write(PCIDevice *pdev, uint32_t addr, uint32_t v,
                                int len)
{
    BCM57xxState *s = BCM57XX(pdev);
    uint32_t off = addr & ~3U;

    trace_bcm57xx_config_write(addr, v, len);
    pci_default_write_config(pdev, addr, v, len);
    if (len < 4 && off >= 0x80 && off <= 0x90) {
        uint32_t mask = MAKE_64BIT_MASK((addr & 3) * 8, len * 8);
        v = (bcm57xx_config_read(pdev, off, 4) & ~mask) |
            ((v << ((addr & 3) * 8)) & mask);
    }
    switch (off) {
    case 0x68:
        if (v & 1) {
            s->irq_pending = false;
        }
        bcm57xx_update_irq(s);
        break;
    case 0x80: {
        uint32_t reg = pci_get_long(pdev->config + 0x78);
        if (reg >= 0x100 && reg < BCM57XX_MMIO_SIZE) {
            bcm57xx_mmio_write(s, reg & ~3U, v, 4);
        }
        break;
    }
    case 0x84: {
        uint32_t sa = pci_get_long(pdev->config + 0x7c);
        if (sa < BCM_SRAM_SIZE) {
            SRAM(s, sa & ~3U) = v;
        }
        break;
    }
    case 0x88:
        bcm57xx_mmio_write(s, GRC_MODE, v, 4);
        break;
    case 0x8c:
        bcm57xx_mmio_write(s, GRC_MISC_CFG, v, 4);
        break;
    case 0x90:
        bcm57xx_mmio_write(s, GRC_LOCAL_CTRL, v, 4);
        break;
    case 0x9c:
        bcm57xx_mmio_write(s, 0x26c, v, 4);
        break;
    case 0xa4:
        bcm57xx_mmio_write(s, 0x284, v, 4);
        break;
    case 0xac:
        bcm57xx_mmio_write(s, 0x304, v, 4);
        break;
    default:
        break;
    }
}

static void bcm57xx_board_data(BCM57xxState *s)
{
    const uint8_t *mac = s->conf.macaddr.a;
    uint32_t hi = lduw_be_p(mac), lo = ldl_be_p(mac + 2);

    REG(s, 0x410) = hi;
    REG(s, 0x414) = lo;
    SRAM(s, 0x0c14) = 0x484b0000 | hi;
    SRAM(s, 0x0c18) = lo;
    SRAM(s, 0x0b54) = FW_MAGIC;
    SRAM(s, 0x0b58) = 0x10; /* copper PHY, ASF disabled */
    SRAM(s, 0x0b74) = ((uint32_t)s->phy[2] << 16) | s->phy[3];
}

static void bcm57xx_core_reset(BCM57xxState *s)
{
    uint32_t host = pci_get_long(s->parent_obj.config + 0x68);

    memset(s->regs, 0, sizeof(s->regs));
    memset(s->tx_cons, 0, sizeof(s->tx_cons));
    memset(s->rx_cons, 0, sizeof(s->rx_cons));
    s->rx_prod = 0;
    s->status_tag = 0;
    s->status_flags = 0;
    s->irq_pending = false;
    s->irq_mailbox_mask = false;
    memset(s->tx_len, 0, sizeof(s->tx_len));
    memset(s->tx_flags, 0, sizeof(s->tx_flags));
    memset(s->tx_vlan, 0, sizeof(s->tx_vlan));
    REG(s, 0x43c) = 1518;
    REG(s, 0x2448) = 2;
    REG(s, 0x2458) = 2;
    REG(s, 0x2468) = 2;
    REG(s, 0x5000) = 0x400; /* embedded CPUs halted */
    REG(s, 0x5400) = 0x400;
    bcm57xx_board_data(s);
    /* Complete the firmware-mailbox reset handshake after board-data load. */
    if (SRAM(s, FW_MBOX) == FW_MAGIC) {
        SRAM(s, FW_MBOX) = ~FW_MAGIC;
    }
    pci_set_long(s->parent_obj.config + 0x68, host);
    bcm57xx_update_irq(s);
}

static uint16_t bcm57xx_chiprev_id(PCIDevice *pdev)
{
    switch (pci_get_word(pdev->config + PCI_DEVICE_ID)) {
    case BCM5701_PCI_DEVICE_ID:
        return BCM5701_CHIPREV_ID_B5;
    case BCM5704_PCI_DEVICE_ID:
        return BCM5704_CHIPREV_ID_B0;
    default:
        g_assert_not_reached();
    }
}

static uint32_t bcm57xx_pcix_status(PCIDevice *pdev)
{
    if (pci_get_word(pdev->config + PCI_DEVICE_ID) ==
        BCM5701_PCI_DEVICE_ID) {
        /* BCM5701 reports zero here while operating in conventional PCI. */
        return 0;
    }

    return BCM5704_PCIX_STATUS_CAPS |
           ((uint32_t)pci_dev_bus_num(pdev) << 8) | pdev->devfn;
}

static void bcm57xx_reset_config(PCIDevice *pdev)
{
    pci_set_word(pdev->config + BCM57XX_PCI_PCIX_CAP + PCI_X_CMD, 0);
    pci_set_long(pdev->config + BCM57XX_PCI_PCIX_CAP + PCI_X_STATUS,
                 bcm57xx_pcix_status(pdev));
    pci_set_long(pdev->config + BCM57XX_PCI_MISC_HOST_CTRL,
                 (uint32_t)bcm57xx_chiprev_id(pdev) <<
                 BCM57XX_MISC_HOST_CTRL_CHIPREV_SHIFT);
    pci_set_long(pdev->config + 0x70,
                 pci_get_word(pdev->config + PCI_DEVICE_ID) ==
                 BCM5701_PCI_DEVICE_ID ? 0x6 : 0xa);
    pci_set_word(pdev->config + 0x4c, 0);
    pci_set_long(pdev->config + 0x7c, 0);
    pci_set_long(pdev->config + 0xb8, PCI_FUNC(pdev->devfn) ? 4 : 0);
}

static bool bcm57xx_init_config(PCIDevice *pdev, Error **errp)
{
    if (pci_pm_init(pdev, 0x48, errp) < 0) {
        return false;
    }
    if (pci_add_capability(pdev, PCI_CAP_ID_PCIX, BCM57XX_PCI_PCIX_CAP,
                           PCI_CAP_PCIX_SIZEOF_V0, errp) < 0) {
        return false;
    }

    pci_set_word(pdev->config + 0x48 + PCI_PM_PMC, PCI_PM_CAP_VER_1_1);
    pci_set_word(pdev->wmask + 0x48 + PCI_PM_CTRL, PCI_PM_CTRL_STATE_MASK);

    pci_set_word(pdev->wmask + BCM57XX_PCI_PCIX_CAP + PCI_X_CMD,
                 BCM57XX_PCIX_COMMAND_RW_MASK);
    pci_set_long(pdev->w1cmask + BCM57XX_PCI_PCIX_CAP + PCI_X_STATUS,
                 BCM57XX_PCIX_STATUS_W1C_MASK);

    /* CLEAR_INT (bit 0) is a write pulse and is not stored. */
    pci_set_long(pdev->wmask + BCM57XX_PCI_MISC_HOST_CTRL,
                 BCM57XX_MISC_HOST_CTRL_RW_MASK);
    pci_set_long(pdev->cmask + BCM57XX_PCI_MISC_HOST_CTRL, UINT32_MAX);
    pci_set_long(pdev->wmask + 0x6c, UINT32_MAX);
    pci_set_long(pdev->wmask + 0x70, 0x00002160);
    pci_set_long(pdev->wmask + 0x74, UINT32_MAX);
    pci_set_long(pdev->wmask + 0x78, UINT32_MAX);
    pci_set_long(pdev->wmask + 0x7c, UINT32_MAX);
    bcm57xx_reset_config(pdev);
    return true;
}

static void bcm57xx_reset(DeviceState *dev)
{
    BCM57xxState *s = BCM57XX(dev);

    memset(s->sram, 0, sizeof(s->sram));
    bcm57xx_phy_reset(s);
    bcm57xx_reset_config(&s->parent_obj);
    bcm57xx_core_reset(s);
}

static int bcm57xx_post_load(void *opaque, int version_id)
{
    BCM57xxState *s = opaque;

    if (version_id < 2) {
        bcm57xx_phy_reset(s);
        bcm57xx_core_reset(s);
    }
    if (s->rx_prod >= 2048 ||
        s->rx_cons[0] >= 512 || s->rx_cons[1] >= 256 ||
        s->rx_cons[2] >= 1024) {
        return -EINVAL;
    }
    for (unsigned i = 0; i < BCM_RINGS; i++) {
        if (s->tx_cons[i] >= 512 || s->tx_len[i] > BCM_MAX_TSO) {
            return -EINVAL;
        }
    }
    s->tx_busy = false;
    bcm57xx_update_irq(s);
    return 0;
}

static const VMStateDescription vmstate_bcm5701 = {
    .name = TYPE_BCM5701,
    .version_id = 2,
    .minimum_version_id = 1,
    .post_load = bcm57xx_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, BCM57xxState),
        VMSTATE_MACADDR(conf.macaddr, BCM57xxState),
        VMSTATE_UINT32_ARRAY_V(regs, BCM57xxState,
                               BCM57XX_MMIO_SIZE / 4, 2),
        VMSTATE_UINT32_ARRAY_V(sram, BCM57xxState, BCM_SRAM_SIZE / 4, 2),
        VMSTATE_UINT8_ARRAY_V(eeprom, BCM57xxState, BCM_EEPROM_SIZE, 2),
        VMSTATE_UINT16_ARRAY_V(phy, BCM57xxState, 32, 2),
        VMSTATE_UINT16_ARRAY_V(dsp, BCM57xxState, 0x10000, 2),
        VMSTATE_UINT16_ARRAY_V(tx_cons, BCM57xxState, BCM_RINGS, 2),
        VMSTATE_UINT16_ARRAY_V(rx_cons, BCM57xxState, 3, 2),
        VMSTATE_UINT16_V(rx_prod, BCM57xxState, 2),
        VMSTATE_UINT8_V(status_tag, BCM57xxState, 2),
        VMSTATE_UINT32_V(status_flags, BCM57xxState, 2),
        VMSTATE_BOOL_V(irq_pending, BCM57xxState, 2),
        VMSTATE_BOOL_V(irq_mailbox_mask, BCM57xxState, 2),
        VMSTATE_UINT32_ARRAY_V(tx_len, BCM57xxState, BCM_RINGS, 2),
        VMSTATE_UINT32_ARRAY_V(tx_flags, BCM57xxState, BCM_RINGS, 2),
        VMSTATE_UINT32_ARRAY_V(tx_vlan, BCM57xxState, BCM_RINGS, 2),
        VMSTATE_UINT8_2DARRAY_V(tx_buf, BCM57xxState, BCM_RINGS,
                               BCM_MAX_TSO, 2),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_bcm5704 = {
    .name = TYPE_BCM5704,
    .version_id = 2,
    .minimum_version_id = 1,
    .post_load = bcm57xx_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, BCM57xxState),
        VMSTATE_MACADDR(conf.macaddr, BCM57xxState),
        VMSTATE_UINT32_ARRAY_V(regs, BCM57xxState,
                               BCM57XX_MMIO_SIZE / 4, 2),
        VMSTATE_UINT32_ARRAY_V(sram, BCM57xxState, BCM_SRAM_SIZE / 4, 2),
        VMSTATE_UINT8_ARRAY_V(eeprom, BCM57xxState, BCM_EEPROM_SIZE, 2),
        VMSTATE_UINT16_ARRAY_V(phy, BCM57xxState, 32, 2),
        VMSTATE_UINT16_ARRAY_V(dsp, BCM57xxState, 0x10000, 2),
        VMSTATE_UINT16_ARRAY_V(tx_cons, BCM57xxState, BCM_RINGS, 2),
        VMSTATE_UINT16_ARRAY_V(rx_cons, BCM57xxState, 3, 2),
        VMSTATE_UINT16_V(rx_prod, BCM57xxState, 2),
        VMSTATE_UINT8_V(status_tag, BCM57xxState, 2),
        VMSTATE_UINT32_V(status_flags, BCM57xxState, 2),
        VMSTATE_BOOL_V(irq_pending, BCM57xxState, 2),
        VMSTATE_BOOL_V(irq_mailbox_mask, BCM57xxState, 2),
        VMSTATE_UINT32_ARRAY_V(tx_len, BCM57xxState, BCM_RINGS, 2),
        VMSTATE_UINT32_ARRAY_V(tx_flags, BCM57xxState, BCM_RINGS, 2),
        VMSTATE_UINT32_ARRAY_V(tx_vlan, BCM57xxState, BCM_RINGS, 2),
        VMSTATE_UINT8_2DARRAY_V(tx_buf, BCM57xxState, BCM_RINGS,
                               BCM_MAX_TSO, 2),
        VMSTATE_END_OF_LIST()
    },
};

static void bcm57xx_realize(PCIDevice *pdev, Error **errp)
{
    BCM57xxState *s = BCM57XX(pdev);
    DeviceState *dev = DEVICE(pdev);

    if (pdev->romfile && pdev->romfile[0]) {
        error_setg(errp,
                   "romfile is not supported by the BCM57xx boot service");
        return;
    }

    if (!bcm57xx_init_config(pdev, errp)) {
        return;
    }

    pdev->config[PCI_INTERRUPT_PIN] = 1; /* INTA */
    memory_region_init_io(&s->mmio, OBJECT(s), &bcm57xx_mmio_ops, s,
                          TYPE_BCM57XX ".mmio", BCM57XX_MMIO_SIZE);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY |
                              PCI_BASE_ADDRESS_MEM_TYPE_64, &s->mmio);

    memory_region_init_io(&s->pci_rom, OBJECT(s), &bcm57xx_absent_ops, s,
                          TYPE_BCM57XX ".pci-rom", BCM57XX_ROM_SIZE);
    pci_register_bar(pdev, PCI_ROM_SLOT, 0, &s->pci_rom);

    qemu_macaddr_default_if_unset(&s->conf.macaddr);
    s->nic = qemu_new_nic(&bcm57xx_net_info, &s->conf,
                          object_get_typename(OBJECT(s)), dev->id,
                          &dev->mem_reentrancy_guard, s);
    qemu_format_nic_info_str(qemu_get_queue(s->nic), s->conf.macaddr.a);

    memset(s->eeprom, 0, sizeof(s->eeprom));
    stl_be_p(s->eeprom, 0x669955aa);
    for (unsigned i = 0; i < 2; i++) {
        memcpy(s->eeprom + (i ? 0xce : 0x7e), s->conf.macaddr.a, 6);
    }
    bcm57xx_reset(dev);
}

static void bcm57xx_exit(PCIDevice *pdev)
{
    BCM57xxState *s = BCM57XX(pdev);

    pci_set_irq(pdev, 0);
    qemu_del_nic(s->nic);
    s->nic = NULL;
}

static const Property bcm57xx_properties[] = {
    DEFINE_NIC_PROPERTIES(BCM57xxState, conf),
};

static void bcm57xx_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PCIDeviceClass *pc = PCI_DEVICE_CLASS(oc);

    pc->realize = bcm57xx_realize;
    pc->config_read = bcm57xx_config_read;
    pc->config_write = bcm57xx_config_write;
    pc->exit = bcm57xx_exit;
    pc->vendor_id = BCM57XX_PCI_VENDOR_ID;
    pc->class_id = PCI_CLASS_NETWORK_ETHERNET;
    device_class_set_legacy_reset(dc, bcm57xx_reset);
    device_class_set_props(dc, bcm57xx_properties);
    set_bit(DEVICE_CATEGORY_NETWORK, dc->categories);
}

static void bcm5701_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PCIDeviceClass *pc = PCI_DEVICE_CLASS(oc);

    pc->device_id = BCM5701_PCI_DEVICE_ID;
    pc->revision = BCM5701_PCI_REVISION;
    pc->subsystem_vendor_id = BCM5701_PCI_SUBSYSTEM_VENDOR_ID;
    pc->subsystem_id = BCM5701_PCI_SUBSYSTEM_ID;
    dc->desc = "Broadcom BCM5701 Gigabit Ethernet";
    dc->vmsd = &vmstate_bcm5701;
}

static void bcm5704_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PCIDeviceClass *pc = PCI_DEVICE_CLASS(oc);

    pc->device_id = BCM5704_PCI_DEVICE_ID;
    pc->revision = BCM5704_PCI_REVISION;
    pc->subsystem_vendor_id = BCM5704_PCI_SUBSYSTEM_VENDOR_ID;
    pc->subsystem_id = BCM5704_PCI_SUBSYSTEM_ID;
    dc->desc = "Broadcom BCM5704 Gigabit Ethernet";
    dc->vmsd = &vmstate_bcm5704;
}

static const TypeInfo bcm57xx_info = {
    .name = TYPE_BCM57XX,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(BCM57xxState),
    .abstract = true,
    .class_init = bcm57xx_class_init,
    .interfaces = (const InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static const TypeInfo bcm5701_info = {
    .name = TYPE_BCM5701,
    .parent = TYPE_BCM57XX,
    .class_init = bcm5701_class_init,
};

static const TypeInfo bcm5704_info = {
    .name = TYPE_BCM5704,
    .parent = TYPE_BCM57XX,
    .class_init = bcm5704_class_init,
};

static void bcm57xx_register_types(void)
{
    type_register_static(&bcm57xx_info);
    type_register_static(&bcm5701_info);
    type_register_static(&bcm5704_info);
}

type_init(bcm57xx_register_types)
