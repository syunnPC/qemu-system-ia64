/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "qemu/units.h"
#include "hw/ia64/intel_7500.h"
#include "hw/ia64/ia64_ras_abi.h"
#include "hw/ia64/ia64_vpc_abi.h"
#include "hw/i386/intel_iommu_internal.h"
#include "hw/misc/iommu-testdev.h"
#include "libqtest.h"
#include "qobject/qdict.h"

#define ECAM 0x7ff0000000ULL
#define BAR 0xc2000000ULL
#define ROOT 0x1000000ULL
#define CTX 0x1001000ULL
#define PT 0x1002000ULL
#define TARGET 0x2000000ULL
#define IOVA 0x400000ULL
#define DEVFN PCI_DEVFN(6, 0)

static QTestState *start(const char *opts)
{
    return qtest_initf("-machine itanium2-vpc-iommu,nvram=none,"
                       "firmware-console=serial,pci-intx-lines=8%s "
                       "-m 256M -nodefaults -device iommu-testdev,addr=6",
                       opts);
}

static void pci_setup(QTestState *qts)
{
    qtest_writel(qts, ECAM + DEVFN * 4096 + PCI_BASE_ADDRESS_0, BAR);
    qtest_writew(qts, ECAM + DEVFN * 4096 + PCI_COMMAND,
                  PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);
}

static uint32_t dma_at(QTestState *qts, uint64_t bar, uint64_t iova,
                       uint64_t target)
{
    qtest_writel(qts, bar + ITD_REG_DMA_GVA_LO, iova);
    qtest_writel(qts, bar + ITD_REG_DMA_GVA_HI, iova >> 32);
    qtest_writel(qts, bar + ITD_REG_DMA_GPA_LO, target);
    qtest_writel(qts, bar + ITD_REG_DMA_GPA_HI, target >> 32);
    qtest_writel(qts, bar + ITD_REG_DMA_LEN, 4);
    qtest_writel(qts, bar + ITD_REG_DMA_DBELL, ITD_DMA_DBELL_ARM);
    qtest_writel(qts, bar + ITD_REG_DMA_TRIGGERING, 1);
    qtest_readl(qts, bar + ITD_REG_DMA_TRIGGERING);
    return qtest_readl(qts, bar + ITD_REG_DMA_RESULT);
}

static uint32_t dma(QTestState *qts, uint64_t iova, uint64_t target)
{
    return dma_at(qts, BAR, iova, target);
}

/* The machine assigns BARs to -device endpoints; enable bus mastering. */
static uint64_t assigned_bar(QTestState *qts, uint64_t cfg)
{
    uint64_t bar = qtest_readl(qts, cfg + PCI_BASE_ADDRESS_0) &
                   PCI_BASE_ADDRESS_MEM_MASK;

    g_assert_cmphex(bar, !=, 0);
    g_assert_cmphex(qtest_readw(qts, cfg + PCI_COMMAND) & PCI_COMMAND_MEMORY,
                    ==, PCI_COMMAND_MEMORY);
    qtest_writew(qts, cfg + PCI_COMMAND,
                 PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);
    return bar;
}

static void tables(QTestState *qts, uint64_t target)
{
    unsigned level;
    qtest_writeq(qts, ROOT, CTX | 1);
    qtest_writeq(qts, CTX + DEVFN * 16, PT | 1);
    qtest_writeq(qts, CTX + DEVFN * 16 + 8, 2 | (1 << 8));
    for (level = 4; level; level--) {
        uint64_t table = PT + (4 - level) * 4096;
        uint64_t next = level == 1 ? target : table + 4096;
        qtest_writeq(qts, table + ((IOVA >> (12 + 9 * (level - 1))) & 511) * 8,
                      next | 3);
    }
}

static void enable(QTestState *qts, uint64_t base)
{
    qtest_writeq(qts, base + DMAR_RTADDR_REG, ROOT);
    qtest_writel(qts, base + DMAR_GCMD_REG, VTD_GCMD_SRTP);
    g_assert_cmphex(qtest_readl(qts, base + DMAR_GSTS_REG), ==, VTD_GCMD_SRTP);
    qtest_writel(qts, base + DMAR_GCMD_REG, VTD_GCMD_TE);
    g_assert_cmphex(qtest_readl(qts, base + DMAR_GSTS_REG) & VTD_GCMD_TE,
                    ==, VTD_GCMD_TE);
}

static void test_registers(void)
{
    QTestState *qts = start("");
    unsigned unit;
    uint64_t cap, ecap;
    for (unit = 0; unit < 2; unit++) {
        uint64_t base = INTEL_7500_VTBAR + unit * 4096;
        g_assert_cmphex(qtest_readl(qts, base), ==, 0x10);
        cap = qtest_readq(qts, base + DMAR_CAP_REG);
        ecap = qtest_readq(qts, base + DMAR_ECAP_REG);
        g_assert_cmphex((cap >> 8) & 31, ==, 4);
        g_assert_cmphex((cap >> 16) & 63, ==, 47);
        g_assert_cmphex((cap >> 24) & 1023, ==, 16);
        g_assert_cmphex((cap >> 40) & 255, ==, 7);
        g_assert_cmphex((cap >> 48) & 63, ==, 9);
        g_assert_cmphex(cap & ((1 << 7) | (1 << 4) | (15ULL << 34)), ==, 0);
        g_assert_cmphex(ecap & 15, ==, 10); /* C=0, QI=1, DT=0, IR=1 */
        g_assert_cmphex((ecap >> 8) & 1023, ==, 32);
        g_assert_cmphex((ecap >> 7) & 1, ==, unit == 0);
        g_assert_cmphex(qtest_readl(qts, base + DMAR_GSTS_REG), ==, 0);
        /* Read-only and reserved registers cannot be overwritten. */
        qtest_writeq(qts, base + DMAR_CAP_REG, UINT64_MAX);
        g_assert_cmphex(qtest_readq(qts, base + DMAR_CAP_REG), ==, cap);
        qtest_writeq(qts, base + DMAR_ECAP_REG, UINT64_MAX);
        g_assert_cmphex(qtest_readq(qts, base + DMAR_ECAP_REG), ==, ecap);
    }
    qtest_quit(qts);
    qts = start(",vtd-coherent=on,vtd-high-mmio=on");
    g_assert_cmphex(qtest_readq(qts, (1ULL << 48) + INTEL_7500_VTBAR + 16) & 1,
                    ==, 1);
    qtest_quit(qts);
}

static void test_translation_faults(void)
{
    QTestState *qts = start("");
    const uint64_t base = INTEL_7500_VTBAR;
    uint64_t fault;
    pci_setup(qts);
    g_assert_cmphex(dma(qts, TARGET, TARGET), ==, 0); /* TE reset clear */
    tables(qts, TARGET);
    enable(qts, base);
    qtest_writel(qts, TARGET, 0xa5a5a5a5);
    g_assert_cmphex(dma(qts, IOVA, TARGET), ==, 0);
    g_assert_cmphex(qtest_readl(qts, TARGET), ==, ITD_DMA_WRITE_VAL);
    qtest_writel(qts, TARGET, 0xa5a5a5a5);
    g_assert_cmphex(dma(qts, IOVA + 4096, TARGET), ==, ITD_DMA_ERR_TX_FAIL);
    g_assert_cmphex(qtest_readl(qts, TARGET), ==, 0xa5a5a5a5);
    g_assert_cmphex(qtest_readl(qts, base + DMAR_FSTS_REG) & 2, ==, 2);
    g_assert_cmphex(qtest_readq(qts, base + 0x100), ==, IOVA + 4096);
    fault = qtest_readq(qts, base + 0x108);
    g_assert_cmphex(fault & (1ULL << 63), !=, 0);
    g_assert_cmphex(fault & 0xffff, ==, DEVFN);
    g_assert_cmphex((fault >> 32) & 255, ==, VTD_FR_WRITE);
    qtest_writel(qts, base + 0x10c, 1U << 31);
    g_assert_cmphex(qtest_readl(qts, base + DMAR_FSTS_REG) & 2, ==, 0);
    /* A 2^48 IOVA faults even though the output addresses can be wider. */
    g_assert_cmphex(dma(qts, 1ULL << 48, TARGET), ==, ITD_DMA_ERR_TX_FAIL);
    fault = qtest_readq(qts, base + 0x118);
    g_assert_cmphex((fault >> 32) & 255, ==, VTD_FR_ADDR_BEYOND_MGAW);
    /* Fault processing disable suppresses records, not the DMA error. */
    qtest_writeq(qts, base + 0x118, 1ULL << 63);
    qtest_writeq(qts, CTX + DEVFN * 16, PT | 3);
    g_assert_cmphex(dma(qts, IOVA + 4096, TARGET), ==, ITD_DMA_ERR_TX_FAIL);
    g_assert_cmphex(qtest_readl(qts, base + DMAR_FSTS_REG) & 2, ==, 0);
    qtest_quit(qts);
}

static void test_invalidation(void)
{
    QTestState *qts = start("");
    const uint64_t base = INTEL_7500_VTBAR;
    const uint64_t queue = 0x1800000;
    const uint64_t completion = queue + 4096;
    uint64_t pte = PT + 3 * 4096 + ((IOVA >> 12) & 511) * 8;
    pci_setup(qts);
    tables(qts, TARGET);
    enable(qts, base);
    g_assert_cmphex(dma(qts, IOVA, TARGET), ==, 0);
    qtest_writeq(qts, pte, (TARGET + 4096) | 3);
    qtest_writeq(qts, base + DMAR_CCMD_REG, (1ULL << 63) | (1ULL << 61));
    g_assert_cmphex(qtest_readq(qts, base + DMAR_CCMD_REG) >> 59, ==, 5);
    qtest_writeq(qts, base + 0x200, IOVA);
    qtest_writeq(qts, base + 0x208, (1ULL << 63) | (3ULL << 60) | (1ULL << 32));
    g_assert_cmphex(qtest_readq(qts, base + 0x208) & (1ULL << 63), ==, 0);
    g_assert_cmphex((qtest_readq(qts, base + 0x208) >> 57) & 3, ==, 1);
    g_assert_cmphex(dma(qts, IOVA, TARGET + 4096), ==, 0);
    qtest_writeq(qts, queue, 1 | (1 << 4));
    qtest_writeq(qts, queue + 16, 2 | (1 << 4));
    qtest_writeq(qts, queue + 32, 5 | (1 << 5) | (0x12345678ULL << 32));
    qtest_writeq(qts, queue + 40, completion);
    qtest_writeq(qts, base + DMAR_IQA_REG, queue);
    qtest_writel(qts, base + DMAR_GCMD_REG, VTD_GCMD_TE | VTD_GCMD_QIE);
    qtest_writeq(qts, base + DMAR_IQT_REG, 48);
    g_assert_cmphex(qtest_readq(qts, base + DMAR_IQH_REG), ==, 48);
    g_assert_cmphex(qtest_readl(qts, completion), ==, 0x12345678);
    /* Malformed descriptor stops the queue at the failing entry. */
    qtest_writeq(qts, queue + 48, 15);
    qtest_writeq(qts, base + DMAR_IQT_REG, 64);
    g_assert_cmphex(qtest_readq(qts, base + DMAR_IQH_REG), ==, 48);
    g_assert_cmphex(qtest_readl(qts, base + DMAR_FSTS_REG) & 16, ==, 16);
    qtest_writeq(qts, queue + 48, 5 | (1 << 5) | (0xabcdef01ULL << 32));
    qtest_writeq(qts, queue + 56, completion);
    qtest_writel(qts, base + DMAR_FSTS_REG, 16);
    g_assert_cmphex(qtest_readq(qts, base + DMAR_IQH_REG), ==, 64);
    g_assert_cmphex(qtest_readl(qts, completion), ==, 0xabcdef01);
    qtest_writel(qts, base + DMAR_GCMD_REG, 0);
    qtest_writel(qts, base + DMAR_PLMBASE_REG, TARGET);
    qtest_writel(qts, base + DMAR_PLMLIMIT_REG, TARGET);
    qtest_writel(qts, base + DMAR_PMEN_REG, 1U << 31);
    g_assert_cmphex(qtest_readl(qts, base + DMAR_PMEN_REG), ==, 0x80000001);
    g_assert_cmphex(dma(qts, TARGET, TARGET), ==, ITD_DMA_ERR_TX_FAIL);
    g_assert_cmphex(qtest_readl(qts, base + DMAR_FSTS_REG) & 2, ==, 0);
    qtest_writel(qts, base + DMAR_PMEN_REG, 0);
    g_assert_cmphex(dma(qts, TARGET, TARGET), ==, 0);
    qtest_quit(qts);
}

#define FAULT_RECORDS 8
#define FRCD_HI(i) (0x108 + (i) * 16)
#define FSTS_PFO 1U
#define FSTS_PPF 2U
#define FECTL_IM (1U << 31)
#define FECTL_IP (1U << 30)

static void test_fault_overflow(void)
{
    const uint64_t base = INTEL_7500_VTBAR;
    const uint64_t event = TARGET + 0x100;
    g_autoptr(GString) args = g_string_new(
        "-machine itanium2-vpc-iommu,nvram=none -m 256M -nodefaults");
    uint64_t bars[FAULT_RECORDS + 1];
    QTestState *qts;
    uint64_t fault;
    unsigned i;

    /* Faults collapse per requester, so each record needs its own device. */
    for (i = 0; i <= FAULT_RECORDS; i++) {
        g_string_append_printf(args, " -device iommu-testdev,addr=%x", 8 + i);
    }
    qts = qtest_init(args->str);
    for (i = 0; i <= FAULT_RECORDS; i++) {
        bars[i] = assigned_bar(qts, ECAM + PCI_DEVFN(8 + i, 0) * 4096);
    }
    qtest_writel(qts, base + DMAR_FEDATA_REG, 0x5a);
    qtest_writel(qts, base + DMAR_FEADDR_REG, event);
    qtest_writel(qts, base + DMAR_FEUADDR_REG, 0);
    g_assert_cmphex(qtest_readl(qts, base + DMAR_FECTL_REG), ==, FECTL_IM);
    enable(qts, base); /* The root table is empty: every DMA faults. */

    for (i = 0; i < FAULT_RECORDS; i++) {
        g_assert_cmphex(dma_at(qts, bars[i], TARGET, TARGET), ==,
                        ITD_DMA_ERR_TX_FAIL);
    }
    g_assert_cmphex(qtest_readl(qts, base + DMAR_FSTS_REG), ==, FSTS_PPF);
    g_assert_cmphex(qtest_readl(qts, base + DMAR_FECTL_REG), ==,
                    FECTL_IM | FECTL_IP);
    /* The ninth requester finds the next record in use. */
    g_assert_cmphex(dma_at(qts, bars[FAULT_RECORDS], TARGET, TARGET), ==,
                    ITD_DMA_ERR_TX_FAIL);
    g_assert_cmphex(qtest_readl(qts, base + DMAR_FSTS_REG), ==,
                    FSTS_PPF | FSTS_PFO);

    /* Freeing the next record does not resume logging while PFO is Set. */
    qtest_writel(qts, base + FRCD_HI(0) + 4, 1U << 31);
    g_assert_cmphex(dma_at(qts, bars[FAULT_RECORDS], TARGET, TARGET), ==,
                    ITD_DMA_ERR_TX_FAIL);
    g_assert_cmphex(qtest_readq(qts, base + FRCD_HI(0)) & (1ULL << 63),
                    ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + DMAR_FECTL_REG), ==,
                    FECTL_IM | FECTL_IP);

    /* Clearing PFO resumes logging in the freed record. */
    qtest_writel(qts, base + DMAR_FSTS_REG, FSTS_PFO);
    g_assert_cmphex(dma_at(qts, bars[FAULT_RECORDS], TARGET, TARGET), ==,
                    ITD_DMA_ERR_TX_FAIL);
    fault = qtest_readq(qts, base + FRCD_HI(0));
    g_assert_cmphex(fault & (1ULL << 63), !=, 0);
    g_assert_cmphex(fault & 0xffff, ==, PCI_DEVFN(8 + FAULT_RECORDS, 0));
    g_assert_cmphex((fault >> 32) & 255, ==, VTD_FR_ROOT_ENTRY_P);

    /* IP stays Set until every status field is serviced, then clears. */
    for (i = 0; i < FAULT_RECORDS - 1; i++) {
        qtest_writel(qts, base + FRCD_HI(i) + 4, 1U << 31);
    }
    g_assert_cmphex(qtest_readl(qts, base + DMAR_FECTL_REG), ==,
                    FECTL_IM | FECTL_IP);
    qtest_writel(qts, base + FRCD_HI(FAULT_RECORDS - 1) + 4, 1U << 31);
    g_assert_cmphex(qtest_readl(qts, base + DMAR_FSTS_REG) & 0x73, ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + DMAR_FECTL_REG), ==, FECTL_IM);
    /* Unmasking a serviced event sends no stale message. */
    qtest_writel(qts, base + DMAR_FECTL_REG, 0);
    g_assert_cmphex(qtest_readl(qts, event), ==, 0);
    /* The next fault is a new event and is delivered immediately. */
    g_assert_cmphex(dma_at(qts, bars[0], TARGET, TARGET), ==,
                    ITD_DMA_ERR_TX_FAIL);
    g_assert_cmphex(qtest_readl(qts, event), ==, 0x5a);
    g_assert_cmphex(qtest_readl(qts, base + DMAR_FECTL_REG), ==, 0);
    qtest_quit(qts);
}

static void test_queue_fault_event(void)
{
    QTestState *qts = start("");
    const uint64_t base = INTEL_7500_VTBAR;
    const uint64_t queue = 0x1800000;

    pci_setup(qts);
    enable(qts, base);
    g_assert_cmphex(dma(qts, TARGET, TARGET), ==, ITD_DMA_ERR_TX_FAIL);
    g_assert_cmphex(qtest_readl(qts, base + DMAR_FECTL_REG), ==,
                    FECTL_IM | FECTL_IP);
    /* An IQE raised while PPF is pending is not a new event... */
    qtest_writeq(qts, queue, 15);
    qtest_writeq(qts, base + DMAR_IQA_REG, queue);
    qtest_writel(qts, base + DMAR_GCMD_REG, VTD_GCMD_TE | VTD_GCMD_QIE);
    qtest_writeq(qts, base + DMAR_IQT_REG, 16);
    g_assert_cmphex(qtest_readl(qts, base + DMAR_FSTS_REG) & 0x13, ==, 0x12);
    /* ...and IP stays Set until both PPF and IQE are serviced. */
    qtest_writel(qts, base + 0x10c, 1U << 31);
    g_assert_cmphex(qtest_readl(qts, base + DMAR_FECTL_REG), ==,
                    FECTL_IM | FECTL_IP);
    qtest_writeq(qts, queue, 5); /* Replace it with a plain wait. */
    qtest_writel(qts, base + DMAR_FSTS_REG, 0x10);
    g_assert_cmphex(qtest_readq(qts, base + DMAR_IQH_REG), ==, 16);
    g_assert_cmphex(qtest_readl(qts, base + DMAR_FECTL_REG), ==, FECTL_IM);
    qtest_quit(qts);
}

static void test_queue_disable(void)
{
    QTestState *qts = start("");
    const uint64_t base = INTEL_7500_VTBAR;
    const uint64_t queue = 0x1800000;
    const uint64_t completion = queue + 4096;

    qtest_writeq(qts, queue, 5 | (1 << 5) | (0x11111111ULL << 32));
    qtest_writeq(qts, queue + 8, completion);
    qtest_writeq(qts, base + DMAR_IQA_REG, queue);
    qtest_writel(qts, base + DMAR_GCMD_REG, VTD_GCMD_QIE);
    qtest_writeq(qts, base + DMAR_IQT_REG, 16);
    g_assert_cmphex(qtest_readq(qts, base + DMAR_IQH_REG), ==, 16);
    g_assert_cmphex(qtest_readl(qts, completion), ==, 0x11111111);
    /* Disabling the drained queue resets IQH; IQT belongs to software. */
    qtest_writel(qts, base + DMAR_GCMD_REG, 0);
    g_assert_cmphex(qtest_readl(qts, base + DMAR_GSTS_REG) & VTD_GCMD_QIE,
                    ==, 0);
    g_assert_cmphex(qtest_readq(qts, base + DMAR_IQH_REG), ==, 0);
    g_assert_cmphex(qtest_readq(qts, base + DMAR_IQT_REG), ==, 16);
    /* Re-enabling without rewriting IQA replays no old descriptor. */
    qtest_writel(qts, completion, 0);
    qtest_writeq(qts, base + DMAR_IQT_REG, 0);
    qtest_writel(qts, base + DMAR_GCMD_REG, VTD_GCMD_QIE);
    g_assert_cmphex(qtest_readl(qts, base + DMAR_GSTS_REG) & VTD_GCMD_QIE,
                    ==, VTD_GCMD_QIE);
    g_assert_cmphex(qtest_readq(qts, base + DMAR_IQH_REG), ==, 0);
    g_assert_cmphex(qtest_readl(qts, base + DMAR_FSTS_REG) & 0x10, ==, 0);
    g_assert_cmphex(qtest_readl(qts, completion), ==, 0);
    /* The queue restarts at its base. */
    qtest_writeq(qts, base + DMAR_IQT_REG, 16);
    g_assert_cmphex(qtest_readq(qts, base + DMAR_IQH_REG), ==, 16);
    g_assert_cmphex(qtest_readl(qts, completion), ==, 0x11111111);
    qtest_quit(qts);
}

static void test_completion_ack(void)
{
    QTestState *qts = start("");
    const uint64_t queue = 0x1800000;
    const uint64_t event = TARGET + 0x100;
    unsigned unit;

    for (unit = 0; unit < 2; unit++) {
        uint64_t base = INTEL_7500_VTBAR + unit * INTEL_7500_UNIT_SIZE;

        qtest_writel(qts, event, 0);
        qtest_writel(qts, base + DMAR_IEDATA_REG, 0x5a);
        qtest_writel(qts, base + DMAR_IEADDR_REG, event);
        qtest_writeq(qts, queue, VTD_INV_DESC_WAIT | VTD_INV_DESC_WAIT_IF);
        qtest_writeq(qts, queue + 16,
                     VTD_INV_DESC_WAIT | VTD_INV_DESC_WAIT_IF);
        qtest_writeq(qts, base + DMAR_IQA_REG, queue);
        qtest_writel(qts, base + DMAR_GCMD_REG, VTD_GCMD_QIE);
        qtest_writeq(qts, base + DMAR_IQT_REG, 16);
        g_assert_cmphex(qtest_readl(qts, base + DMAR_ICS_REG), ==, VTD_ICS_IWC);
        g_assert_cmphex(qtest_readl(qts, base + DMAR_IECTL_REG), ==,
                       VTD_IECTL_IM | VTD_IECTL_IP);

        /* IWC is W1C: writing zero leaves the masked event pending. */
        qtest_writel(qts, base + DMAR_ICS_REG, 0);
        g_assert_cmphex(qtest_readl(qts, base + DMAR_ICS_REG), ==, VTD_ICS_IWC);
        g_assert_cmphex(qtest_readl(qts, base + DMAR_IECTL_REG), ==,
                       VTD_IECTL_IM | VTD_IECTL_IP);

        /* Acknowledging IWC also cancels the pending interrupt. */
        qtest_writel(qts, base + DMAR_ICS_REG, VTD_ICS_IWC);
        g_assert_cmphex(qtest_readl(qts, base + DMAR_ICS_REG), ==, 0);
        g_assert_cmphex(qtest_readl(qts, base + DMAR_IECTL_REG), ==,
                       VTD_IECTL_IM);
        qtest_writel(qts, base + DMAR_IECTL_REG, 0);
        g_assert_cmphex(qtest_readl(qts, event), ==, 0);

        /* The next completion still generates a new interrupt. */
        qtest_writeq(qts, base + DMAR_IQT_REG, 32);
        g_assert_cmphex(qtest_readq(qts, base + DMAR_IQH_REG), ==, 32);
        g_assert_cmphex(qtest_readl(qts, base + DMAR_ICS_REG), ==, VTD_ICS_IWC);
        g_assert_cmphex(qtest_readl(qts, base + DMAR_IECTL_REG), ==, 0);
        g_assert_cmphex(qtest_readl(qts, event), ==, 0x5a);
    }
    qtest_quit(qts);
}

static void test_completion_coalescing(void)
{
    QTestState *qts = start("");
    const uint64_t queue = 0x1800000;
    const uint64_t completion = queue + 4096;
    const uint64_t event = TARGET + 0x100;
    unsigned unit, i;

    for (unit = 0; unit < 2; unit++) {
        uint64_t base = INTEL_7500_VTBAR + unit * INTEL_7500_UNIT_SIZE;

        qtest_writel(qts, completion, 0);
        qtest_writel(qts, event, 0);
        qtest_writel(qts, base + DMAR_IEDATA_REG, 0x5a);
        qtest_writel(qts, base + DMAR_IEADDR_REG, event);
        for (i = 0; i < 4; i++) {
            qtest_writeq(qts, queue + i * 16,
                         VTD_INV_DESC_WAIT | VTD_INV_DESC_WAIT_IF |
                         VTD_INV_DESC_WAIT_SW | ((uint64_t)(i + 1) << 32));
            qtest_writeq(qts, queue + i * 16 + 8, completion);
        }
        qtest_writeq(qts, base + DMAR_IQA_REG, queue);
        qtest_writel(qts, base + DMAR_GCMD_REG, VTD_GCMD_QIE);
        qtest_writeq(qts, base + DMAR_IQT_REG, 16);
        g_assert_cmphex(qtest_readl(qts, base + DMAR_IECTL_REG), ==,
                       VTD_IECTL_IM | VTD_IECTL_IP);
        qtest_writel(qts, base + DMAR_IECTL_REG, 0);
        g_assert_cmphex(qtest_readl(qts, event), ==, 0x5a);
        g_assert_cmphex(qtest_readl(qts, base + DMAR_IECTL_REG), ==, 0);
        g_assert_cmphex(qtest_readl(qts, base + DMAR_ICS_REG), ==, VTD_ICS_IWC);
        g_assert_cmphex(qtest_readl(qts, completion), ==, 1);
        qtest_writel(qts, event, 0);

        /* Further waits write status but cannot notify until IWC is cleared. */
        qtest_writeq(qts, base + DMAR_IQT_REG, 32);
        g_assert_cmphex(qtest_readq(qts, base + DMAR_IQH_REG), ==, 32);
        g_assert_cmphex(qtest_readl(qts, completion), ==, 2);
        g_assert_cmphex(qtest_readl(qts, event), ==, 0);
        g_assert_cmphex(qtest_readl(qts, base + DMAR_IECTL_REG), ==, 0);

        qtest_writel(qts, base + DMAR_IECTL_REG, VTD_IECTL_IM);
        qtest_writeq(qts, base + DMAR_IQT_REG, 48);
        g_assert_cmphex(qtest_readq(qts, base + DMAR_IQH_REG), ==, 48);
        g_assert_cmphex(qtest_readl(qts, completion), ==, 3);
        g_assert_cmphex(qtest_readl(qts, base + DMAR_IECTL_REG), ==,
                       VTD_IECTL_IM);
        qtest_writel(qts, base + DMAR_ICS_REG, VTD_ICS_IWC);
        qtest_writel(qts, base + DMAR_IECTL_REG, 0);
        g_assert_cmphex(qtest_readl(qts, event), ==, 0);

        qtest_writeq(qts, base + DMAR_IQT_REG, 64);
        g_assert_cmphex(qtest_readq(qts, base + DMAR_IQH_REG), ==, 64);
        g_assert_cmphex(qtest_readl(qts, completion), ==, 4);
        g_assert_cmphex(qtest_readl(qts, event), ==, 0x5a);
        g_assert_cmphex(qtest_readl(qts, base + DMAR_ICS_REG), ==, VTD_ICS_IWC);
        g_assert_cmphex(qtest_readl(qts, base + DMAR_IECTL_REG), ==, 0);
    }
    qtest_quit(qts);
}

static void test_migration_blocked(void)
{
    QTestState *qts = start("");
    QDict *rsp = qtest_qmp(qts, "{'execute': 'migrate', 'arguments': "
                                "{'uri': 'exec:cat >/dev/null'}}");
    QDict *error = qdict_get_qdict(rsp, "error");

    g_assert_nonnull(error);
    g_assert_nonnull(strstr(qdict_get_str(error, "desc"),
                            "non-migratable device 'intel-7500-iommu'"));
    qobject_unref(rsp);
    qtest_quit(qts);
}

#define IOSAPIC_BASE 0x80110000ULL
#define IOSAPIC_RTE(gsi) (0x10 + (gsi) * 2)
#define IOSAPIC_RTE_REMOTE_IRR (1U << 14)
#define IOSAPIC_RTE_LEVEL (1U << 15)

static uint32_t iosapic_read(QTestState *qts, uint32_t reg)
{
    qtest_writel(qts, IOSAPIC_BASE, reg);
    return qtest_readl(qts, IOSAPIC_BASE + 0x10);
}

static void iosapic_write(QTestState *qts, uint32_t reg, uint32_t value)
{
    qtest_writel(qts, IOSAPIC_BASE, reg);
    qtest_writel(qts, IOSAPIC_BASE + 0x10, value);
}

static void test_second_unit(void)
{
    QTestState *qts = qtest_init(
        "-machine itanium2-vpc-iommu,nvram=none -m 256M -nodefaults "
        "-device iommu-testdev,bus=ioh7500-secondary,addr=0 "
        "-device pci-testdev,bus=ioh7500-secondary,addr=1 "
        "-device edu,bus=ioh7500-secondary,addr=2");
    const uint64_t bridge = ECAM + PCI_DEVFN(7, 0) * 4096;
    const uint64_t endpoint = ECAM + (1ULL << 20);
    const uint64_t testdev = endpoint + PCI_DEVFN(1, 0) * 4096;
    const uint64_t edu = endpoint + PCI_DEVFN(2, 0) * 4096;
    const uint8_t vector = 0x58;
    uint64_t mem_base, mem_limit, bar, edu_bar;
    uint32_t io_base, io_limit, io_bar;

    /* The machine numbers the bridge and opens windows around its bus. */
    g_assert_cmphex(qtest_readl(qts, bridge + PCI_PRIMARY_BUS) & 0xffffff,
                    ==, 0x010100);
    g_assert_cmphex(qtest_readw(qts, bridge + PCI_COMMAND), ==,
                    PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);
    mem_base = (uint64_t)(qtest_readw(qts, bridge + PCI_MEMORY_BASE) &
                          0xfff0) << 16;
    mem_limit = ((uint64_t)(qtest_readw(qts, bridge + PCI_MEMORY_LIMIT) &
                            0xfff0) << 16) | 0xfffff;
    io_base = (qtest_readb(qts, bridge + PCI_IO_BASE) & 0xf0) << 8;
    io_limit = ((qtest_readb(qts, bridge + PCI_IO_LIMIT) & 0xf0) << 8) | 0xfff;
    g_assert_cmphex(mem_base, <, mem_limit);
    g_assert_cmphex(io_base, <, io_limit);
    /* Prefetchable decoding is closed (base above limit). */
    g_assert_cmphex(qtest_readw(qts, bridge + PCI_PREF_MEMORY_BASE) & 0xfff0,
                    >, qtest_readw(qts, bridge + PCI_PREF_MEMORY_LIMIT) &
                       0xfff0);

    bar = assigned_bar(qts, endpoint);
    g_assert_cmphex(bar, >=, mem_base);
    g_assert_cmphex(bar + 4095, <=, mem_limit);
    g_assert_cmphex(qtest_readl(qts, testdev + PCI_BASE_ADDRESS_0) &
                    PCI_BASE_ADDRESS_MEM_MASK, >=, mem_base);
    io_bar = qtest_readl(qts, testdev + PCI_BASE_ADDRESS_1) &
             PCI_BASE_ADDRESS_IO_MASK;
    g_assert_cmphex(io_bar, >=, io_base);
    g_assert_cmphex(io_bar, <=, io_limit);
    g_assert_cmphex(qtest_readw(qts, testdev + PCI_COMMAND) & PCI_COMMAND_IO,
                    ==, PCI_COMMAND_IO);
    edu_bar = assigned_bar(qts, edu);
    g_assert_cmphex(edu_bar, >=, mem_base);
    g_assert_cmphex(edu_bar + 0xfffff, <=, mem_limit);

    /*
     * 01:02.0 INTA swizzles to INTC at the slot 7 bridge, which the
     * default eight-line routing sends to GSI 16 + (7 + 2) % 8 = 17.
     */
    g_assert_cmpuint(qtest_readb(qts, edu + PCI_INTERRUPT_LINE), ==, 17);
    iosapic_write(qts, IOSAPIC_RTE(17), vector | IOSAPIC_RTE_LEVEL);
    qtest_writel(qts, edu_bar + 0x60, 1);
    g_assert_cmphex(iosapic_read(qts, IOSAPIC_RTE(17)) &
                    IOSAPIC_RTE_REMOTE_IRR, !=, 0);
    qtest_writel(qts, edu_bar + 0x64, 1);
    qtest_writel(qts, IOSAPIC_BASE + 0x40, vector);
    g_assert_cmphex(iosapic_read(qts, IOSAPIC_RTE(17)) &
                    IOSAPIC_RTE_REMOTE_IRR, ==, 0);

    /* Conventional bridge aliases DMA to its root requester ID. */
    qtest_writeq(qts, ROOT, CTX | 1);
    /* Pass-through context for the bridge's requester ID. */
    qtest_writeq(qts, CTX + PCI_DEVFN(7, 0) * 16, 1 | (2 << 2));
    enable(qts, INTEL_7500_VTBAR + 4096);
    g_assert_cmphex(dma_at(qts, bar, TARGET, TARGET), ==, 0);
    qtest_writeq(qts, CTX + PCI_DEVFN(7, 0) * 16, 0);
    g_assert_cmphex(dma_at(qts, bar, TARGET, TARGET), ==, ITD_DMA_ERR_TX_FAIL);
    g_assert_cmphex(qtest_readl(qts, INTEL_7500_VTBAR + 4096 +
                                DMAR_FSTS_REG) & 2, ==, 2);
    g_assert_cmphex(qtest_readl(qts, INTEL_7500_VTBAR + DMAR_FSTS_REG), ==, 0);

    /* A system reset restores the same assignment. */
    qtest_system_reset(qts);
    g_assert_cmphex(qtest_readl(qts, bridge + PCI_PRIMARY_BUS) & 0xffffff,
                    ==, 0x010100);
    g_assert_cmphex(qtest_readl(qts, endpoint + PCI_BASE_ADDRESS_0) &
                    PCI_BASE_ADDRESS_MEM_MASK, ==, bar);
    qtest_quit(qts);
}

static bool irr_contains(QTestState *qts, unsigned vector)
{
    g_autofree char *regs = qtest_hmp(qts, "info registers");
    const char *line = strstr(regs, "SAPIC IRR:");
    uint64_t irr[4];

    g_assert_nonnull(line);
    g_assert_cmpint(sscanf(line, "SAPIC IRR: %" SCNx64 " %" SCNx64
                          " %" SCNx64 " %" SCNx64,
                          &irr[0], &irr[1], &irr[2], &irr[3]), ==, 4);
    return !!(irr[vector / 64] & (1ULL << (vector % 64)));
}

static void wait_vector(QTestState *qts, unsigned vector)
{
    unsigned i;
    for (i = 0; i < 1000 && !irr_contains(qts, vector); i++) {
        g_usleep(1000);
    }
    g_assert_true(irr_contains(qts, vector));
}

static void test_msi_fault_events(void)
{
    const uint64_t base = INTEL_7500_VTBAR;
    QTestState *qts = qtest_init(
        "-machine itanium2-vpc-iommu,nvram=none,firmware-console=serial,"
        "pci-intx-lines=8 -m 256M -nodefaults "
        "-device iommu-testdev,addr=6 -device edu,addr=5");

    pci_setup(qts);
    enable(qts, base); /* No root entry: DMA fails, MSI still reaches PIB. */
    {
        uint64_t cfg = ECAM + PCI_DEVFN(5, 0) * 4096;
        unsigned cap, flags;
        qtest_writel(qts, cfg + PCI_BASE_ADDRESS_0, BAR + 0x100000);
        qtest_writew(qts, cfg + PCI_COMMAND,
                      PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);
        for (cap = qtest_readb(qts, cfg + PCI_CAPABILITY_LIST);
             cap && qtest_readb(qts, cfg + cap) != PCI_CAP_ID_MSI;
             cap = qtest_readb(qts, cfg + cap + 1)) {
        }
        g_assert_cmpuint(cap, !=, 0);
        flags = qtest_readw(qts, cfg + cap + PCI_MSI_FLAGS);
        g_assert_true(flags & PCI_MSI_FLAGS_64BIT);
        qtest_writel(qts, cfg + cap + PCI_MSI_ADDRESS_LO, 0xfee00008);
        qtest_writel(qts, cfg + cap + PCI_MSI_ADDRESS_HI, 0);
        qtest_writew(qts, cfg + cap + PCI_MSI_DATA_64, 0x66);
        qtest_writew(qts, cfg + cap + PCI_MSI_FLAGS,
                      flags | PCI_MSI_FLAGS_ENABLE);
        g_assert_cmphex(qtest_readl(qts, BAR + 0x100000), ==, 0x010000ed);
        qtest_writel(qts, BAR + 0x100060, 1);
        wait_vector(qts, 0x66);
        g_assert_cmphex(qtest_readl(qts, base + DMAR_FSTS_REG) & 2, ==, 0);
    }
    qtest_writel(qts, base + DMAR_FEDATA_REG, 0x67);
    qtest_writel(qts, base + DMAR_FEADDR_REG, 0xfee00008);
    qtest_writel(qts, base + DMAR_FEUADDR_REG, 0);
    qtest_writel(qts, base + DMAR_FECTL_REG, 0);
    g_assert_cmphex(qtest_readl(qts, base + DMAR_FEDATA_REG), ==, 0x67);
    g_assert_cmphex(qtest_readl(qts, base + DMAR_FEADDR_REG), ==, 0xfee00008);
    g_assert_cmphex(dma(qts, IOVA, TARGET), ==, ITD_DMA_ERR_TX_FAIL);
    wait_vector(qts, 0x67);
    g_assert_cmphex(qtest_readl(qts, base + DMAR_FECTL_REG), ==, 0);
    qtest_quit(qts);
}

static void checksum(QTestState *qts, uint64_t pa, unsigned length)
{
    uint8_t *buf = g_malloc(length);
    unsigned sum = 0, i;
    qtest_memread(qts, pa, buf, length);
    for (i = 0; i < length; i++) {
        sum += buf[i];
    }
    g_assert_cmphex(sum & 255, ==, 0);
    g_free(buf);
}

#define RSDP 0x802000ULL

/* Boot the firmware under TCG until it has published its ACPI tables. */
static QTestState *boot_to_acpi(const char *options, const char *memory,
                                uint64_t *xsdt_out)
{
    QTestState *qts = qtest_initf("-machine itanium2-vpc-iommu,nvram=none%s,"
                                  "firmware-console=serial "
                                  "-m %s -nodefaults -accel tcg -S",
                                  options, memory);
    uint64_t xsdt = 0;
    int64_t deadline = g_get_monotonic_time() + 30 * G_TIME_SPAN_SECOND;

    qtest_qmp_assert_success(qts, "{'execute':'cont'}");
    do {
        if (qtest_readq(qts, RSDP) == 0x2052545020445352ULL) {
            xsdt = qtest_readq(qts, RSDP + 24);
            if (xsdt && qtest_readl(qts, xsdt) == 0x54445358) {
                break;
            }
        }
        g_usleep(10000);
    } while (g_get_monotonic_time() < deadline);
    qtest_qmp_assert_success(qts, "{'execute':'stop'}");
    g_assert_cmphex(xsdt, !=, 0);
    *xsdt_out = xsdt;
    return qts;
}

static uint64_t find_xsdt_table(QTestState *qts, uint64_t xsdt,
                                uint32_t signature)
{
    unsigned length = qtest_readl(qts, xsdt + 4);
    unsigned i;

    for (i = 36; i < length; i += 8) {
        uint64_t pa = qtest_readq(qts, xsdt + i);
        if (qtest_readl(qts, pa) == signature) {
            return pa;
        }
    }
    return 0;
}

static void check_firmware_dmar(const char *options, uint64_t base)
{
    const uint64_t rsdp = RSDP;
    uint64_t xsdt, dmar;
    unsigned i, length;
    QTestState *qts = boot_to_acpi(options, "256M", &xsdt);

    checksum(qts, rsdp, 20);
    checksum(qts, rsdp, 36);
    length = qtest_readl(qts, xsdt + 4);
    g_assert_cmpuint(length, >=, 36);
    g_assert_cmpuint(length, <=, 116);
    checksum(qts, xsdt, length);
    dmar = find_xsdt_table(qts, xsdt, 0x52414d44);
    g_assert_cmphex(dmar, !=, 0);
    g_assert_cmpuint(qtest_readl(qts, dmar + 4), ==, 88);
    checksum(qts, dmar, 88);
    g_assert_cmpuint(qtest_readb(qts, dmar + 36), ==, 50);
    g_assert_cmpuint(qtest_readb(qts, dmar + 37), ==, 0);
    g_assert_cmpuint(qtest_readw(qts, dmar + 50), ==, 24);
    g_assert_cmpuint(qtest_readb(qts, dmar + 64), ==, 2);
    g_assert_cmpuint(qtest_readb(qts, dmar + 70), ==, 7);
    g_assert_cmphex(qtest_readq(qts, dmar + 56), ==, base + 4096);
    g_assert_cmpuint(qtest_readw(qts, dmar + 74), ==, 16);
    g_assert_cmpuint(qtest_readb(qts, dmar + 76), ==, 1);
    g_assert_cmphex(qtest_readq(qts, dmar + 80), ==, base);
    /* RSDT carries the same table and a valid checksum. */
    {
        uint64_t rsdt = qtest_readl(qts, rsdp + 16);
        bool found = false;
        length = qtest_readl(qts, rsdt + 4);
        g_assert_cmpuint(length, >=, 36);
        g_assert_cmpuint(length, <=, 76);
        checksum(qts, rsdt, length);
        for (i = 36; i < length; i += 4) {
            found |= qtest_readl(qts, rsdt + i) == dmar;
        }
        g_assert_true(found);
    }
    g_assert_cmphex(qtest_readl(qts, base), ==, 0x10);
    qtest_quit(qts);
}

static void test_firmware_dmar(void)
{
    check_firmware_dmar("", INTEL_7500_VTBAR);
    check_firmware_dmar(",vtd-high-mmio=on", INTEL_7500_VTBAR | (1ULL << 48));
}

/* VPC RAM windows below 4 GiB; VTBAR splits the range above the RAS hub. */
#define LOW_RAM_END 0x80000000ULL
#define HIGH_RAM_BASE 0x80200000ULL
#define RAS_END (IA64_RAS_HUB_DEFAULT_BASE + IA64_RAS_HUB_SIZE)
#define LOCAL_SAPIC 0xfee00000ULL
#define VTD_END (INTEL_7500_VTBAR + 2 * INTEL_7500_UNIT_SIZE)
#define RAM_ABOVE_4G 0x100000000ULL
#define SPLIT_RAM (4 * GiB)
#define RAM_BELOW_4G \
    (LOW_RAM_END + (IA64_PCI_MMIO_BASE - HIGH_RAM_BASE) + \
     (IA64_RAS_HUB_DEFAULT_BASE - (IA64_PCI_MMIO_BASE + IA64_PCI_MMIO_SIZE)) + \
     (INTEL_7500_VTBAR - RAS_END) + (LOCAL_SAPIC - VTD_END))

static void assert_ram(QTestState *qts, uint64_t pa, bool ram)
{
    const uint64_t pattern = 0x5aa5c33c00000000ULL | (pa & 0xffffffff);

    qtest_writeq(qts, pa, pattern);
    if (ram) {
        g_assert_cmphex(qtest_readq(qts, pa), ==, pattern);
    } else {
        g_assert_cmphex(qtest_readq(qts, pa), !=, pattern);
    }
}

static void test_ram_split(void)
{
    QTestState *qts = qtest_init("-machine itanium2-vpc-iommu,nvram=none "
                                "-m 4G -nodefaults");
    uint64_t end = RAM_ABOVE_4G + SPLIT_RAM - RAM_BELOW_4G;

    /* RAM stops at VTBAR and resumes after both remapping units. */
    assert_ram(qts, INTEL_7500_VTBAR - 8, true);
    g_assert_cmphex(qtest_readl(qts, INTEL_7500_VTBAR), ==, 0x10);
    g_assert_cmphex(qtest_readl(qts, INTEL_7500_VTBAR + 4096), ==, 0x10);
    assert_ram(qts, VTD_END, true);
    assert_ram(qts, LOCAL_SAPIC - 8, true);
    /* The displaced 8 KiB extend the RAM above 4 GiB. */
    assert_ram(qts, end - 8, true);
    assert_ram(qts, end, false);
    qtest_quit(qts);

    /* With the aperture above 2^48 nothing is displaced below 4 GiB. */
    qts = qtest_init("-machine itanium2-vpc-iommu,nvram=none,"
                     "vtd-high-mmio=on -m 4G -nodefaults");
    end -= 2 * INTEL_7500_UNIT_SIZE;
    assert_ram(qts, INTEL_7500_VTBAR, true);
    assert_ram(qts, INTEL_7500_VTBAR + 4096, true);
    g_assert_cmphex(qtest_readl(qts, (1ULL << 48) | INTEL_7500_VTBAR), ==,
                    0x10);
    assert_ram(qts, end - 8, true);
    assert_ram(qts, end, false);
    qtest_quit(qts);
}

/* Firmware SRAT ranges skip the aperture and still account for all RAM. */
static void test_firmware_ram_split(void)
{
    uint64_t xsdt, srat, total = 0;
    bool below = false, above = false;
    unsigned offset, length, entry_length;
    QTestState *qts = boot_to_acpi("", "4G", &xsdt);

    srat = find_xsdt_table(qts, xsdt, 0x54415253);
    g_assert_cmphex(srat, !=, 0);
    length = qtest_readl(qts, srat + 4);
    checksum(qts, srat, length);
    for (offset = 48; offset < length; offset += entry_length) {
        uint64_t base, size;

        entry_length = qtest_readb(qts, srat + offset + 1);
        g_assert_cmpuint(entry_length, !=, 0);
        if (qtest_readb(qts, srat + offset) != 1 ||
            !(qtest_readl(qts, srat + offset + 28) & 1)) {
            continue;
        }
        base = qtest_readq(qts, srat + offset + 8);
        size = qtest_readq(qts, srat + offset + 16);
        g_assert_true(base + size <= INTEL_7500_VTBAR || base >= VTD_END);
        below |= base + size == INTEL_7500_VTBAR;
        above |= base == VTD_END && base + size == LOCAL_SAPIC;
        total += size;
    }
    g_assert_true(below);
    g_assert_true(above);
    g_assert_cmphex(total, ==, SPLIT_RAM);
    qtest_quit(qts);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    qtest_add_func("/vtd/registers", test_registers);
    qtest_add_func("/vtd/translation-faults", test_translation_faults);
    qtest_add_func("/vtd/invalidation", test_invalidation);
    qtest_add_func("/vtd/fault-overflow", test_fault_overflow);
    qtest_add_func("/vtd/queue-fault-event", test_queue_fault_event);
    qtest_add_func("/vtd/queue-disable", test_queue_disable);
    qtest_add_func("/vtd/completion-ack", test_completion_ack);
    qtest_add_func("/vtd/completion-coalescing", test_completion_coalescing);
    qtest_add_func("/vtd/migration-blocked", test_migration_blocked);
    qtest_add_func("/vtd/second-unit", test_second_unit);
    qtest_add_func("/vtd/msi-fault-events", test_msi_fault_events);
    qtest_add_func("/vtd/firmware-dmar", test_firmware_dmar);
    qtest_add_func("/vtd/ram-split", test_ram_split);
    qtest_add_func("/vtd/firmware-ram-split", test_firmware_ram_split);
    return g_test_run();
}
