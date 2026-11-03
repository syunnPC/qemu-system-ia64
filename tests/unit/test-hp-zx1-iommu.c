/*
 * HP zx1 IOC IOMMU frontend tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/pci-host/hp-zx1-iommu.h"

typedef struct TestContext {
    uint64_t pte;
    uint64_t last_pdir_address;
    unsigned int read_count;
    bool readable;
} TestContext;

static const HPSBAIOMMUEntry unchanged_entry = {
    .iova = UINT64_C(0x1111111111111111),
    .translated_addr = UINT64_C(0x2222222222222222),
    .addr_mask = UINT64_C(0x3333333333333333),
};

static const HPZX1IOMMUEvictionResult unchanged_eviction = {
    .evicted = true,
    .range = {
        .iova = UINT64_C(0x4444444444444444),
        .size = UINT64_C(0x5555555555555555),
    },
};

static void assert_no_eviction(const HPZX1IOMMUEvictionResult *result)
{
    g_assert_false(result->evicted);
    g_assert_cmphex(result->range.iova, ==, 0);
    g_assert_cmphex(result->range.size, ==, 0);
}

static void assert_eviction_unchanged(const HPZX1IOMMUEvictionResult *result)
{
    g_assert_true(result->evicted);
    g_assert_cmphex(result->range.iova, ==, unchanged_eviction.range.iova);
    g_assert_cmphex(result->range.size, ==, unchanged_eviction.range.size);
}

static bool test_pdir_read(void *opaque, uint64_t address, uint64_t *pte)
{
    TestContext *context = opaque;

    context->last_pdir_address = address;
    context->read_count++;
    if (!context->readable) {
        return false;
    }
    *pte = context->pte;
    return true;
}

static HPZX1IOMMUResetConfig test_config(uint64_t tcnfg)
{
    return (HPZX1IOMMUResetConfig) {
        .ibase = UINT64_C(0x40000001),
        .imask = UINT64_C(0xf0000000),
        .pcom = 0,
        .tcnfg = tcnfg,
        .pdir_base = UINT64_C(0x00100000),
    };
}

static HPZX1IOMMUFrontend test_iommu(uint64_t tcnfg)
{
    HPZX1IOMMUFrontend iommu;
    HPZX1IOMMUResetConfig config = test_config(tcnfg);

    g_assert_true(hp_zx1_iommu_frontend_reset(&iommu, &config));
    return iommu;
}

static TestContext test_context(uint64_t pte)
{
    return (TestContext) {
        .pte = pte,
        .readable = true,
    };
}

static void test_page_sizes_nonzero_index(void)
{
    static const struct {
        uint64_t tcnfg;
        unsigned int page_shift;
    } cases[] = {
        { 0, 12 },
        { 1, 13 },
        { 2, 14 },
        { 3, 16 },
    };
    unsigned int i;

    for (i = 0; i < G_N_ELEMENTS(cases); i++) {
        HPZX1IOMMUFrontend iommu = test_iommu(cases[i].tcnfg);
        uint64_t page_size = UINT64_C(1) << cases[i].page_shift;
        uint64_t iova = UINT64_C(0x40000000) + 5 * page_size + 0x123;
        uint64_t target = UINT64_C(0x0000012345600000);
        TestContext context =
            test_context(HP_SBA_IOPDIR_VALID_BIT | target | 0x5a);
        HPSBAIOMMUEntry entry;

        g_assert_cmpint(hp_zx1_iommu_frontend_translate(&iommu, iova, false,
                                                        test_pdir_read,
                                                        &context, &entry, NULL),
                        ==, HP_ZX1_IOMMU_TRANSLATE_TRANSLATED);
        g_assert_cmphex(entry.iova, ==, iova & ~(page_size - 1));
        g_assert_cmphex(entry.translated_addr, ==, target & ~(page_size - 1));
        g_assert_cmphex(entry.addr_mask, ==, page_size - 1);
        g_assert_cmphex(context.last_pdir_address, ==,
                        UINT64_C(0x00100000) + 5 * sizeof(uint64_t));
        g_assert_cmpuint(context.read_count, ==, 1);
    }
}

static void test_identity_paths(void)
{
    HPZX1IOMMUFrontend iommu = test_iommu(0);
    TestContext context =
        test_context(HP_SBA_IOPDIR_VALID_BIT | UINT64_C(0x12345000));
    HPSBAIOMMUEntry entry;

    g_assert_cmpint(
        hp_zx1_iommu_frontend_translate(&iommu, UINT64_C(0x40005123), true,
                                        test_pdir_read, &context, &entry, NULL),
        ==, HP_ZX1_IOMMU_TRANSLATE_IDENTITY);
    g_assert_cmphex(entry.iova, ==, UINT64_C(0x40005000));
    g_assert_cmphex(entry.translated_addr, ==, UINT64_C(0x40005000));
    g_assert_cmphex(entry.addr_mask, ==, UINT64_C(0xfff));

    g_assert_cmpint(
        hp_zx1_iommu_frontend_translate(&iommu, UINT64_C(0x50006123), false,
                                        test_pdir_read, &context, &entry, NULL),
        ==, HP_ZX1_IOMMU_TRANSLATE_IDENTITY);
    g_assert_cmphex(entry.translated_addr, ==, UINT64_C(0x50006000));

    iommu.ibase &= ~UINT64_C(1);
    g_assert_cmpint(
        hp_zx1_iommu_frontend_translate(&iommu, UINT64_C(0x40007123), false,
                                        test_pdir_read, &context, &entry, NULL),
        ==, HP_ZX1_IOMMU_TRANSLATE_IDENTITY);
    g_assert_cmphex(entry.translated_addr, ==, UINT64_C(0x40007000));
    g_assert_cmpuint(context.read_count, ==, 0);
}

static void test_identity_boundaries(void)
{
    HPZX1IOMMUFrontend iommu = test_iommu(0);
    TestContext context =
        test_context(HP_SBA_IOPDIR_VALID_BIT | UINT64_C(0x12345000));
    HPSBAIOMMUEntry entry;

    g_assert_cmpint(
        hp_zx1_iommu_frontend_translate(&iommu, HP_ZX1_IOMMU_PHYS_MASK, true,
                                        test_pdir_read, &context, &entry, NULL),
        ==, HP_ZX1_IOMMU_TRANSLATE_IDENTITY);
    g_assert_cmphex(entry.iova, ==, HP_ZX1_IOMMU_PHYS_MASK & ~UINT64_C(0xfff));
    g_assert_cmphex(entry.translated_addr, ==, entry.iova);
    g_assert_cmphex(entry.addr_mask, ==, UINT64_C(0xfff));

    entry = unchanged_entry;
    g_assert_cmpint(hp_zx1_iommu_frontend_translate(
                        &iommu, UINT64_C(1) << HP_ZX1_IOMMU_PHYS_BITS, true,
                        test_pdir_read, &context, &entry, NULL),
                    ==, HP_ZX1_IOMMU_TRANSLATE_BLOCKED);
    g_assert_cmpmem(&entry, sizeof(entry), &unchanged_entry,
                    sizeof(unchanged_entry));

    iommu.ibase &= ~UINT64_C(1);
    g_assert_cmpint(hp_zx1_iommu_frontend_translate(
                        &iommu, UINT64_C(1) << HP_ZX1_IOMMU_PHYS_BITS, false,
                        test_pdir_read, &context, &entry, NULL),
                    ==, HP_ZX1_IOMMU_TRANSLATE_BLOCKED);

    iommu.tcnfg = 4;
    g_assert_cmpint(
        hp_zx1_iommu_frontend_translate(&iommu, UINT64_C(0x40005123), false,
                                        test_pdir_read, &context, &entry, NULL),
        ==, HP_ZX1_IOMMU_TRANSLATE_IDENTITY);
    g_assert_cmphex(entry.translated_addr, ==, UINT64_C(0x40005000));
    g_assert_cmphex(entry.addr_mask, ==, UINT64_C(0xfff));

    iommu.ibase |= 1;
    g_assert_cmpint(
        hp_zx1_iommu_frontend_translate(&iommu, UINT64_C(0x40005123), true,
                                        test_pdir_read, &context, &entry, NULL),
        ==, HP_ZX1_IOMMU_TRANSLATE_IDENTITY);
    g_assert_cmphex(entry.addr_mask, ==, UINT64_C(0xfff));
    g_assert_cmpuint(context.read_count, ==, 0);
}

static void test_pte_validity_and_50_bit_bound(void)
{
    HPZX1IOMMUFrontend iommu = test_iommu(0);
    TestContext context = test_context(UINT64_C(0x12345000));
    HPSBAIOMMUEntry entry = unchanged_entry;

    g_assert_cmpint(
        hp_zx1_iommu_frontend_translate(&iommu, UINT64_C(0x40005123), false,
                                        test_pdir_read, &context, &entry, NULL),
        ==, HP_ZX1_IOMMU_TRANSLATE_BLOCKED);
    g_assert_cmpmem(&entry, sizeof(entry), &unchanged_entry,
                    sizeof(unchanged_entry));

    context.pte =
        HP_SBA_IOPDIR_VALID_BIT | (HP_ZX1_IOMMU_PHYS_MASK & ~UINT64_C(0xfff));
    g_assert_cmpint(
        hp_zx1_iommu_frontend_translate(&iommu, UINT64_C(0x40005123), false,
                                        test_pdir_read, &context, &entry, NULL),
        ==, HP_ZX1_IOMMU_TRANSLATE_TRANSLATED);
    g_assert_cmphex(entry.translated_addr, ==,
                    HP_ZX1_IOMMU_PHYS_MASK & ~UINT64_C(0xfff));

    hp_zx1_iotlb_clear(&iommu.iotlb);
    context.pte =
        HP_SBA_IOPDIR_VALID_BIT | (UINT64_C(1) << 50) | UINT64_C(0x12345000);
    entry = unchanged_entry;
    g_assert_cmpint(
        hp_zx1_iommu_frontend_translate(&iommu, UINT64_C(0x40005123), false,
                                        test_pdir_read, &context, &entry, NULL),
        ==, HP_ZX1_IOMMU_TRANSLATE_BLOCKED);
    g_assert_cmpmem(&entry, sizeof(entry), &unchanged_entry,
                    sizeof(unchanged_entry));
}

static void test_unreadable_pdir(void)
{
    HPZX1IOMMUFrontend iommu = test_iommu(0);
    TestContext context =
        test_context(HP_SBA_IOPDIR_VALID_BIT | UINT64_C(0x12345000));
    HPSBAIOMMUEntry entry = unchanged_entry;

    context.readable = false;
    g_assert_cmpint(
        hp_zx1_iommu_frontend_translate(&iommu, UINT64_C(0x40005123), false,
                                        test_pdir_read, &context, &entry, NULL),
        ==, HP_ZX1_IOMMU_TRANSLATE_BLOCKED);
    g_assert_cmpuint(context.read_count, ==, 1);
    g_assert_cmpmem(&entry, sizeof(entry), &unchanged_entry,
                    sizeof(unchanged_entry));
}

static void test_pdir_address_validation(void)
{
    HPZX1IOMMUFrontend iommu = test_iommu(0);
    TestContext context =
        test_context(HP_SBA_IOPDIR_VALID_BIT | UINT64_C(0x12345000));
    HPSBAIOMMUEntry entry = unchanged_entry;
    const uint64_t last_base =
        HP_ZX1_IOMMU_PHYS_MASK - 7 - UINT64_C(0xffff) * sizeof(uint64_t);

    /* The final byte of the complete 64K-entry PDIR is exactly in range. */
    iommu.pdir_base = last_base;
    g_assert_cmpint(
        hp_zx1_iommu_frontend_translate(&iommu, UINT64_C(0x40000123), false,
                                        test_pdir_read, &context, &entry, NULL),
        ==, HP_ZX1_IOMMU_TRANSLATE_TRANSLATED);
    g_assert_cmphex(context.last_pdir_address, ==, last_base);
    g_assert_cmpuint(context.read_count, ==, 1);

    hp_zx1_iotlb_clear(&iommu.iotlb);
    g_assert_cmpint(
        hp_zx1_iommu_frontend_translate(&iommu, UINT64_C(0x4ffff123), false,
                                        test_pdir_read, &context, &entry, NULL),
        ==, HP_ZX1_IOMMU_TRANSLATE_TRANSLATED);
    g_assert_cmphex(context.last_pdir_address, ==, HP_ZX1_IOMMU_PHYS_MASK - 7);
    g_assert_cmpuint(context.read_count, ==, 2);

    hp_zx1_iotlb_clear(&iommu.iotlb);
    iommu.pdir_base = last_base + 8;
    context.read_count = 0;
    entry = unchanged_entry;
    g_assert_cmpint(
        hp_zx1_iommu_frontend_translate(&iommu, UINT64_C(0x40000123), false,
                                        test_pdir_read, &context, &entry, NULL),
        ==, HP_ZX1_IOMMU_TRANSLATE_BLOCKED);
    g_assert_cmpuint(context.read_count, ==, 0);
    g_assert_cmpmem(&entry, sizeof(entry), &unchanged_entry,
                    sizeof(unchanged_entry));

    /* One entry fits, but the complete aperture's PDIR does not. */
    iommu.pdir_base = HP_ZX1_IOMMU_PHYS_MASK - 7;
    g_assert_cmpint(
        hp_zx1_iommu_frontend_translate(&iommu, UINT64_C(0x40000123), false,
                                        test_pdir_read, &context, &entry, NULL),
        ==, HP_ZX1_IOMMU_TRANSLATE_BLOCKED);
    g_assert_cmpuint(context.read_count, ==, 0);

    iommu.pdir_base = UINT64_C(0x00100004);
    g_assert_cmpint(
        hp_zx1_iommu_frontend_translate(&iommu, UINT64_C(0x40000123), false,
                                        test_pdir_read, &context, &entry, NULL),
        ==, HP_ZX1_IOMMU_TRANSLATE_BLOCKED);
    g_assert_cmpuint(context.read_count, ==, 0);

    iommu.pdir_base = UINT64_C(1) << HP_ZX1_IOMMU_PHYS_BITS;
    g_assert_cmpint(
        hp_zx1_iommu_frontend_translate(&iommu, UINT64_C(0x40000123), false,
                                        test_pdir_read, &context, &entry, NULL),
        ==, HP_ZX1_IOMMU_TRANSLATE_BLOCKED);
    g_assert_cmpuint(context.read_count, ==, 0);
    g_assert_cmpmem(&entry, sizeof(entry), &unchanged_entry,
                    sizeof(unchanged_entry));

    /*
     * Only 8-byte entry alignment is asserted while the register mask is
     * OPEN.
     */
    iommu.pdir_base = UINT64_C(0x00100008);
    entry = unchanged_entry;
    g_assert_cmpint(
        hp_zx1_iommu_frontend_translate(&iommu, UINT64_C(0x40005123), false,
                                        test_pdir_read, &context, &entry, NULL),
        ==, HP_ZX1_IOMMU_TRANSLATE_TRANSLATED);
    g_assert_cmphex(context.last_pdir_address, ==, UINT64_C(0x00100030));
    g_assert_cmpuint(context.read_count, ==, 1);
}

static void test_cache_stale_until_pcom(void)
{
    HPZX1IOMMUFrontend iommu = test_iommu(0);
    const uint64_t iova = UINT64_C(0x40005123);
    TestContext context =
        test_context(HP_SBA_IOPDIR_VALID_BIT | UINT64_C(0x12345000));
    HPZX1IOMMUWriteResult write_result;
    HPSBAIOMMUEntry entry;

    g_assert_cmpint(hp_zx1_iommu_frontend_translate(&iommu, iova, false,
                                                    test_pdir_read, &context,
                                                    &entry, NULL),
                    ==, HP_ZX1_IOMMU_TRANSLATE_TRANSLATED);
    g_assert_cmphex(entry.translated_addr, ==, UINT64_C(0x12345000));
    g_assert_cmpuint(context.read_count, ==, 1);

    context.pte = HP_SBA_IOPDIR_VALID_BIT | UINT64_C(0x23456000);
    g_assert_cmpint(hp_zx1_iommu_frontend_translate(&iommu, iova, false,
                                                    test_pdir_read, &context,
                                                    &entry, NULL),
                    ==, HP_ZX1_IOMMU_TRANSLATE_TRANSLATED);
    g_assert_cmphex(entry.translated_addr, ==, UINT64_C(0x12345000));
    g_assert_cmpuint(context.read_count, ==, 1);

    g_assert_true(hp_zx1_iommu_frontend_reg_write(&iommu, HP_ZX1_IOC_IOMMU_PCOM,
                                                  UINT64_C(0x40005000) | 12,
                                                  UINT8_MAX, &write_result));
    g_assert_true(write_result.purged);
    g_assert_cmphex(write_result.purge.iova, ==, UINT64_C(0x40005000));
    g_assert_cmphex(write_result.purge.size, ==, UINT64_C(0x1000));

    g_assert_cmpint(hp_zx1_iommu_frontend_translate(&iommu, iova, false,
                                                    test_pdir_read, &context,
                                                    &entry, NULL),
                    ==, HP_ZX1_IOMMU_TRANSLATE_TRANSLATED);
    g_assert_cmphex(entry.translated_addr, ==, UINT64_C(0x23456000));
    g_assert_cmpuint(context.read_count, ==, 2);
}

static void test_cache_hit_without_pdir(void)
{
    HPZX1IOMMUFrontend iommu = test_iommu(0);
    const uint64_t iova = UINT64_C(0x40005123);
    TestContext context =
        test_context(HP_SBA_IOPDIR_VALID_BIT | UINT64_C(0x12345000));
    HPZX1IOMMUWriteResult write_result;
    HPSBAIOMMUEntry entry;

    g_assert_cmpint(hp_zx1_iommu_frontend_translate(&iommu, iova, false,
                                                    test_pdir_read, &context,
                                                    &entry, NULL),
                    ==, HP_ZX1_IOMMU_TRANSLATE_TRANSLATED);
    g_assert_cmpuint(context.read_count, ==, 1);

    entry = unchanged_entry;
    g_assert_cmpint(hp_zx1_iommu_frontend_translate(&iommu, iova, false, NULL,
                                                    NULL, &entry, NULL),
                    ==, HP_ZX1_IOMMU_TRANSLATE_TRANSLATED);
    g_assert_cmphex(entry.translated_addr, ==, UINT64_C(0x12345000));

    g_assert_true(hp_zx1_iommu_frontend_reg_write(&iommu, HP_ZX1_IOC_IOMMU_PCOM,
                                                  UINT64_C(0x40005000) | 12,
                                                  UINT8_MAX, &write_result));
    g_assert_true(write_result.purged);

    entry = unchanged_entry;
    g_assert_cmpint(hp_zx1_iommu_frontend_translate(&iommu, iova, false, NULL,
                                                    NULL, &entry, NULL),
                    ==, HP_ZX1_IOMMU_TRANSLATE_BLOCKED);
    g_assert_cmpmem(&entry, sizeof(entry), &unchanged_entry,
                    sizeof(unchanged_entry));
}

static void test_eviction_output_contract(void)
{
    HPZX1IOMMUFrontend iommu = test_iommu(0);
    HPZX1IOMMUFrontend before;
    TestContext context =
        test_context(HP_SBA_IOPDIR_VALID_BIT | UINT64_C(0x12345000));
    HPZX1IOMMUEvictionResult eviction = unchanged_eviction;
    HPSBAIOMMUEntry entry;

    before = iommu;
    g_assert_cmpint(hp_zx1_iommu_frontend_translate(
                        &iommu, UINT64_C(0x40005123), true, test_pdir_read,
                        &context, &entry, &eviction),
                    ==, HP_ZX1_IOMMU_TRANSLATE_IDENTITY);
    assert_no_eviction(&eviction);
    g_assert_cmpmem(&iommu, sizeof(iommu), &before, sizeof(before));

    eviction = unchanged_eviction;
    g_assert_cmpint(hp_zx1_iommu_frontend_translate(
                        &iommu, UINT64_C(0x40005123), false, test_pdir_read,
                        &context, &entry, &eviction),
                    ==, HP_ZX1_IOMMU_TRANSLATE_TRANSLATED);
    assert_no_eviction(&eviction);
    g_assert_cmpuint(context.read_count, ==, 1);

    eviction = unchanged_eviction;
    g_assert_cmpint(
        hp_zx1_iommu_frontend_translate(&iommu, UINT64_C(0x40005123), false,
                                        NULL, NULL, &entry, &eviction),
        ==, HP_ZX1_IOMMU_TRANSLATE_TRANSLATED);
    assert_no_eviction(&eviction);
    g_assert_cmphex(entry.translated_addr, ==, UINT64_C(0x12345000));

    before = iommu;
    context.readable = false;
    entry = unchanged_entry;
    eviction = unchanged_eviction;
    g_assert_cmpint(hp_zx1_iommu_frontend_translate(
                        &iommu, UINT64_C(0x40006123), false, test_pdir_read,
                        &context, &entry, &eviction),
                    ==, HP_ZX1_IOMMU_TRANSLATE_BLOCKED);
    g_assert_cmpmem(&entry, sizeof(entry), &unchanged_entry,
                    sizeof(unchanged_entry));
    assert_eviction_unchanged(&eviction);
    g_assert_cmpmem(&iommu, sizeof(iommu), &before, sizeof(before));
}

static void test_round_robin_replacement(void)
{
    HPZX1IOMMUFrontend iommu = test_iommu(0);
    TestContext context = test_context(0);
    HPZX1IOMMUEvictionResult eviction;
    HPZX1IOMMUWriteResult write_result;
    HPSBAIOMMUEntry entry;
    unsigned int slot;

    /* Fill invalid slots in order, then replace them round-robin. */
    for (slot = 0; slot < HP_ZX1_IOTLB_SLOT_COUNT; slot++) {
        uint64_t iova = UINT64_C(0x40000000) + (uint64_t)slot * 0x1000;

        context.pte = HP_SBA_IOPDIR_VALID_BIT |
                      (UINT64_C(0x10000000) + (uint64_t)slot * 0x1000);
        eviction = unchanged_eviction;
        g_assert_cmpint(
            hp_zx1_iommu_frontend_translate(&iommu, iova, false, test_pdir_read,
                                            &context, &entry, &eviction),
            ==, HP_ZX1_IOMMU_TRANSLATE_TRANSLATED);
        assert_no_eviction(&eviction);
        g_assert_true(iommu.iotlb.slots[slot].valid);
        g_assert_cmphex(iommu.iotlb.slots[slot].entry.iova, ==, iova);
    }
    g_assert_cmpuint(iommu.rr_next, ==, 0);

    context.pte = HP_SBA_IOPDIR_VALID_BIT | UINT64_C(0x20000000);
    eviction = unchanged_eviction;
    g_assert_cmpint(hp_zx1_iommu_frontend_translate(
                        &iommu, UINT64_C(0x40010000), false, test_pdir_read,
                        &context, &entry, &eviction),
                    ==, HP_ZX1_IOMMU_TRANSLATE_TRANSLATED);
    g_assert_true(eviction.evicted);
    g_assert_cmphex(eviction.range.iova, ==, UINT64_C(0x40000000));
    g_assert_cmphex(eviction.range.size, ==, UINT64_C(0x1000));
    g_assert_cmphex(iommu.iotlb.slots[0].entry.iova, ==, UINT64_C(0x40010000));
    g_assert_cmpuint(iommu.rr_next, ==, 1);

    g_assert_true(hp_zx1_iommu_frontend_reg_write(&iommu, HP_ZX1_IOC_IOMMU_PCOM,
                                                  UINT64_C(0x40005000) | 12,
                                                  UINT8_MAX, &write_result));
    g_assert_true(write_result.purged);
    g_assert_false(iommu.iotlb.slots[5].valid);

    context.pte = HP_SBA_IOPDIR_VALID_BIT | UINT64_C(0x30000000);
    eviction = unchanged_eviction;
    g_assert_cmpint(hp_zx1_iommu_frontend_translate(
                        &iommu, UINT64_C(0x40011000), false, test_pdir_read,
                        &context, &entry, &eviction),
                    ==, HP_ZX1_IOMMU_TRANSLATE_TRANSLATED);
    assert_no_eviction(&eviction);
    g_assert_true(iommu.iotlb.slots[5].valid);
    g_assert_cmphex(iommu.iotlb.slots[5].entry.iova, ==, UINT64_C(0x40011000));
    g_assert_cmpuint(iommu.rr_next, ==, 1);
}

static void test_arbitrary_byte_lanes(void)
{
    HPZX1IOMMUFrontend iommu;
    HPZX1IOMMUResetConfig config = {
        .ibase = UINT64_C(0x0011223344556677),
        .imask = UINT64_C(0x1021324354657687),
        .pcom = 0,
        .tcnfg = 0,
        .pdir_base = UINT64_C(0x2031425364758697),
    };
    HPZX1IOMMUWriteResult write_result;
    uint64_t value;

    g_assert_true(hp_zx1_iommu_frontend_reset(&iommu, &config));
    g_assert_true(hp_zx1_iommu_frontend_reg_write(
        &iommu, HP_ZX1_IOC_IOMMU_IBASE, UINT64_C(0xffeeddccbbaa9988), 0x81,
        &write_result));
    g_assert_false(write_result.purged);
    g_assert_true(hp_zx1_iommu_frontend_reg_latch(
        &iommu, HP_ZX1_IOC_IOMMU_IBASE, &value));
    g_assert_cmphex(value, ==, UINT64_C(0xff11223344556688));

    g_assert_true(hp_zx1_iommu_frontend_reg_write(
        &iommu, HP_ZX1_IOC_IOMMU_IMASK, UINT64_C(0x8877665544332211), 0x24,
        &write_result));
    g_assert_true(hp_zx1_iommu_frontend_reg_latch(
        &iommu, HP_ZX1_IOC_IOMMU_IMASK, &value));
    g_assert_cmphex(value, ==, UINT64_C(0x1021664354337687));

    g_assert_true(hp_zx1_iommu_frontend_reg_write(&iommu, HP_ZX1_IOC_IOMMU_PCOM,
                                                  UINT64_C(0x0000000040005000),
                                                  0x0e, &write_result));
    g_assert_false(write_result.purged);
    g_assert_true(hp_zx1_iommu_frontend_reg_write(&iommu, HP_ZX1_IOC_IOMMU_PCOM,
                                                  12, 0x01, &write_result));
    g_assert_true(write_result.purged);
    g_assert_cmphex(write_result.purge.iova, ==, UINT64_C(0x40005000));

    g_assert_true(hp_zx1_iommu_frontend_reg_write(&iommu, HP_ZX1_IOC_IOMMU_PCOM,
                                                  UINT64_C(0x5a00000000000000),
                                                  0x80, &write_result));
    g_assert_false(write_result.purged);
    g_assert_true(
        hp_zx1_iommu_frontend_reg_latch(&iommu, HP_ZX1_IOC_IOMMU_PCOM, &value));
    g_assert_cmphex(value, ==, UINT64_C(0x5a0000004000500c));

    /* Any enabled byte in the logical low command dword commits the latch. */
    g_assert_true(hp_zx1_iommu_frontend_reg_write(&iommu, HP_ZX1_IOC_IOMMU_PCOM,
                                                  0, 0x04, &write_result));
    g_assert_true(write_result.purged);
    g_assert_cmphex(write_result.purge.iova, ==, UINT64_C(0x5a00000040005000));

    g_assert_true(hp_zx1_iommu_frontend_reg_write(
        &iommu, HP_ZX1_IOC_IOMMU_TCNFG, 3, 0x01, &write_result));
    g_assert_cmphex(iommu.tcnfg, ==, UINT64_C(3));

    value = iommu.ibase;
    g_assert_false(hp_zx1_iommu_frontend_reg_write(
        &iommu, UINT64_C(0x328), UINT64_MAX, UINT8_MAX, &write_result));
    g_assert_cmphex(iommu.ibase, ==, value);
}

static void test_invalid_configuration(void)
{
    HPZX1IOMMUFrontend iommu = test_iommu(4);
    TestContext context =
        test_context(HP_SBA_IOPDIR_VALID_BIT | UINT64_C(0x12345000));
    HPSBAIOMMUEntry entry = unchanged_entry;

    g_assert_cmpint(
        hp_zx1_iommu_frontend_translate(&iommu, UINT64_C(0x40005123), false,
                                        test_pdir_read, &context, &entry, NULL),
        ==, HP_ZX1_IOMMU_TRANSLATE_BLOCKED);
    g_assert_cmpuint(context.read_count, ==, 0);

    iommu.tcnfg = 0;
    iommu.imask = UINT64_C(0xe8000000);
    g_assert_cmpint(
        hp_zx1_iommu_frontend_translate(&iommu, UINT64_C(0x40005123), false,
                                        test_pdir_read, &context, &entry, NULL),
        ==, HP_ZX1_IOMMU_TRANSLATE_BLOCKED);
    g_assert_cmpmem(&entry, sizeof(entry), &unchanged_entry,
                    sizeof(unchanged_entry));
}

static void test_failed_api_calls_are_atomic(void)
{
    HPZX1IOMMUFrontend iommu = test_iommu(0);
    HPZX1IOMMUFrontend before = iommu;
    HPZX1IOMMUWriteResult result = {
        .purged = true,
        .purge = {
            .iova = UINT64_C(0x11110000),
            .size = UINT64_C(0x1000),
        },
    };
    const HPZX1IOMMUWriteResult result_before = result;
    uint64_t value = UINT64_C(0xa5a5a5a5a5a5a5a5);

    g_assert_false(hp_zx1_iommu_frontend_reg_write(
        &iommu, UINT64_C(0x328), UINT64_MAX, UINT8_MAX, &result));
    g_assert_cmpmem(&iommu, sizeof(iommu), &before, sizeof(before));
    g_assert_cmpmem(&result, sizeof(result), &result_before,
                    sizeof(result_before));

    g_assert_false(
        hp_zx1_iommu_frontend_reg_latch(&iommu, UINT64_C(0x328), &value));
    g_assert_cmphex(value, ==, UINT64_C(0xa5a5a5a5a5a5a5a5));

    g_assert_false(hp_zx1_iommu_frontend_reset(&iommu, NULL));
    g_assert_cmpmem(&iommu, sizeof(iommu), &before, sizeof(before));
}

static void test_explicit_reset(void)
{
    HPZX1IOMMUFrontend iommu = test_iommu(0);
    HPZX1IOMMUResetConfig config = {
        .ibase = UINT64_C(0x8877665544332211),
        .imask = UINT64_C(0x1020304050607080),
        .pcom = UINT64_C(0x0123456789abcdef),
        .tcnfg = UINT64_C(3),
        .pdir_base = UINT64_C(0x0000000123456000),
    };
    TestContext context =
        test_context(HP_SBA_IOPDIR_VALID_BIT | UINT64_C(0x12345000));
    HPSBAIOMMUEntry entry;
    unsigned int slot;

    g_assert_cmpint(
        hp_zx1_iommu_frontend_translate(&iommu, UINT64_C(0x40005123), false,
                                        test_pdir_read, &context, &entry, NULL),
        ==, HP_ZX1_IOMMU_TRANSLATE_TRANSLATED);
    iommu.rr_next = 9;

    g_assert_true(hp_zx1_iommu_frontend_reset(&iommu, &config));
    g_assert_cmphex(iommu.ibase, ==, config.ibase);
    g_assert_cmphex(iommu.imask, ==, config.imask);
    g_assert_cmphex(iommu.pcom, ==, config.pcom);
    g_assert_cmphex(iommu.tcnfg, ==, config.tcnfg);
    g_assert_cmphex(iommu.pdir_base, ==, config.pdir_base);
    g_assert_cmpuint(iommu.rr_next, ==, 0);
    for (slot = 0; slot < HP_ZX1_IOTLB_SLOT_COUNT; slot++) {
        g_assert_false(iommu.iotlb.slots[slot].valid);
    }

    g_assert_false(hp_zx1_iommu_frontend_reset(NULL, &config));
    g_assert_false(hp_zx1_iommu_frontend_reset(&iommu, NULL));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/hp-zx1-iommu/page-sizes-nonzero-index",
                    test_page_sizes_nonzero_index);
    g_test_add_func("/hp-zx1-iommu/identity-paths", test_identity_paths);
    g_test_add_func("/hp-zx1-iommu/identity-boundaries",
                    test_identity_boundaries);
    g_test_add_func("/hp-zx1-iommu/pte-validity-50-bit",
                    test_pte_validity_and_50_bit_bound);
    g_test_add_func("/hp-zx1-iommu/pdir-unreadable", test_unreadable_pdir);
    g_test_add_func("/hp-zx1-iommu/pdir-address-validation",
                    test_pdir_address_validation);
    g_test_add_func("/hp-zx1-iommu/cache-stale-until-pcom",
                    test_cache_stale_until_pcom);
    g_test_add_func("/hp-zx1-iommu/cache-hit-without-pdir",
                    test_cache_hit_without_pdir);
    g_test_add_func("/hp-zx1-iommu/eviction-output",
                    test_eviction_output_contract);
    g_test_add_func("/hp-zx1-iommu/round-robin-replacement",
                    test_round_robin_replacement);
    g_test_add_func("/hp-zx1-iommu/arbitrary-byte-lanes",
                    test_arbitrary_byte_lanes);
    g_test_add_func("/hp-zx1-iommu/invalid-configuration",
                    test_invalid_configuration);
    g_test_add_func("/hp-zx1-iommu/failed-api-calls-atomic",
                    test_failed_api_calls_are_atomic);
    g_test_add_func("/hp-zx1-iommu/explicit-reset", test_explicit_reset);
    return g_test_run();
}
