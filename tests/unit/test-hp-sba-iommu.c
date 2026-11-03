/*
 * HP SBA IOMMU helper tests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/pci-host/hp-sba-iommu.h"

typedef struct TestContext {
    uint64_t bypass_value;
    uint64_t pdir_entry;
    uint64_t last_bypass_addr;
    uint64_t last_pdir_addr;
    unsigned int bypass_count;
    unsigned int pdir_read_count;
    bool pdir_read_ok;
} TestContext;

static uint64_t test_bypass(void *opaque, uint64_t addr)
{
    TestContext *context = opaque;

    context->last_bypass_addr = addr;
    context->bypass_count++;
    return context->bypass_value;
}

static bool test_pdir_read(void *opaque, uint64_t addr, uint64_t *entry)
{
    TestContext *context = opaque;

    context->last_pdir_addr = addr;
    context->pdir_read_count++;
    *entry = context->pdir_entry;
    return context->pdir_read_ok;
}

static HPSBAIOMMUParams test_params(TestContext *context)
{
    return (HPSBAIOMMUParams) {
        .ibase = UINT64_C(0x40000001),
        .imask = UINT64_C(0xfffffffffff00000),
        .pdir_base = UINT64_C(0x1000),
        .pdir_index_mask = UINT64_MAX,
        .page_shift = 12,
        .bypass = test_bypass,
        .pdir_read = test_pdir_read,
        .opaque = context,
    };
}

static TestContext test_context(void)
{
    return (TestContext) {
        .bypass_value = UINT64_C(0x1122334455667000),
        .pdir_read_ok = true,
    };
}

static void test_disabled_bypass(void)
{
    TestContext context = test_context();
    HPSBAIOMMUParams params = test_params(&context);
    HPSBAIOMMUEntry entry;

    params.ibase &= ~UINT64_C(1);
    g_assert_true(
        hp_sba_iommu_translate(&params, UINT64_C(0x40012345), &entry));
    g_assert_cmphex(entry.iova, ==, UINT64_C(0x40012000));
    g_assert_cmphex(entry.translated_addr, ==, context.bypass_value);
    g_assert_cmphex(entry.addr_mask, ==, UINT64_C(0xfff));
    g_assert_cmpuint(context.bypass_count, ==, 1);
    g_assert_cmphex(context.last_bypass_addr, ==, entry.iova);
    g_assert_cmpuint(context.pdir_read_count, ==, 0);
}

static void test_nonmatching_bypass(void)
{
    TestContext context = test_context();
    HPSBAIOMMUParams params = test_params(&context);
    HPSBAIOMMUEntry entry;

    params.ibase = UINT64_C(0x50000001);
    g_assert_true(
        hp_sba_iommu_translate(&params, UINT64_C(0x40012345), &entry));
    g_assert_cmphex(entry.iova, ==, UINT64_C(0x40012000));
    g_assert_cmphex(entry.translated_addr, ==, context.bypass_value);
    g_assert_cmphex(entry.addr_mask, ==, UINT64_C(0xfff));
    g_assert_cmpuint(context.bypass_count, ==, 1);
    g_assert_cmphex(context.last_bypass_addr, ==, entry.iova);
    g_assert_cmpuint(context.pdir_read_count, ==, 0);
}

static void test_valid_pte_absolute_index(void)
{
    TestContext context = test_context();
    HPSBAIOMMUParams params = test_params(&context);
    HPSBAIOMMUEntry entry;

    context.pdir_entry = HP_SBA_IOPDIR_VALID_BIT | UINT64_C(0x12345abc);
    g_assert_true(
        hp_sba_iommu_translate(&params, UINT64_C(0x40012345), &entry));
    g_assert_cmphex(entry.iova, ==, UINT64_C(0x40012000));
    g_assert_cmphex(entry.translated_addr, ==, UINT64_C(0x12345000));
    g_assert_cmphex(entry.addr_mask, ==, UINT64_C(0xfff));
    g_assert_cmpuint(context.bypass_count, ==, 1);
    g_assert_cmpuint(context.pdir_read_count, ==, 1);
    g_assert_cmphex(context.last_pdir_addr, ==, UINT64_C(0x201090));
}

static void test_invalid_pte(void)
{
    TestContext context = test_context();
    HPSBAIOMMUParams params = test_params(&context);
    HPSBAIOMMUEntry entry;

    context.pdir_entry = UINT64_C(0x12345000);
    g_assert_false(
        hp_sba_iommu_translate(&params, UINT64_C(0x40012345), &entry));
    g_assert_cmpuint(context.bypass_count, ==, 1);
    g_assert_cmpuint(context.pdir_read_count, ==, 1);
}

static void test_pdir_read_failure(void)
{
    TestContext context = test_context();
    HPSBAIOMMUParams params = test_params(&context);
    HPSBAIOMMUEntry entry;

    context.pdir_entry = HP_SBA_IOPDIR_VALID_BIT | UINT64_C(0x12345000);
    context.pdir_read_ok = false;
    g_assert_false(
        hp_sba_iommu_translate(&params, UINT64_C(0x40012345), &entry));
    g_assert_cmpuint(context.bypass_count, ==, 1);
    g_assert_cmpuint(context.pdir_read_count, ==, 1);
}

static void test_page_sizes_and_index_mask(void)
{
    static const struct {
        unsigned int page_shift;
        uint64_t iova;
        uint64_t pdir_ptr;
    } cases[] = {
        { 12, UINT64_C(0x123456789abcd000), UINT64_C(0xe68) },
        { 13, UINT64_C(0x123456789abcc000), UINT64_C(0xf30) },
        { 14, UINT64_C(0x123456789abcc000), UINT64_C(0xf98) },
        { 16, UINT64_C(0x123456789abc0000), UINT64_C(0xde0) },
    };
    unsigned int i;

    for (i = 0; i < G_N_ELEMENTS(cases); i++) {
        TestContext context = test_context();
        HPSBAIOMMUParams params = test_params(&context);
        HPSBAIOMMUEntry entry;
        uint64_t page_mask;

        params.ibase = 1;
        params.imask = 0;
        params.pdir_base = UINT64_C(0x800);
        params.pdir_index_mask = UINT64_C(0xff);
        params.page_shift = cases[i].page_shift;
        context.pdir_entry =
            HP_SBA_IOPDIR_VALID_BIT | UINT64_C(0x123456789abcdef0);

        g_assert_true(hp_sba_iommu_translate(
            &params, UINT64_C(0x123456789abcdef0), &entry));
        page_mask = (UINT64_C(1) << cases[i].page_shift) - 1;
        g_assert_cmphex(entry.iova, ==, cases[i].iova);
        g_assert_cmphex(entry.translated_addr, ==,
                        UINT64_C(0x123456789abcdef0) & ~page_mask);
        g_assert_cmphex(entry.addr_mask, ==, page_mask);
        g_assert_cmphex(context.last_pdir_addr, ==, cases[i].pdir_ptr);
    }
}

static void test_pdir_pointer_wrap(void)
{
    TestContext context = test_context();
    HPSBAIOMMUParams params = test_params(&context);
    HPSBAIOMMUEntry entry;

    params.ibase = 1;
    params.imask = 0;
    params.pdir_base = UINT64_C(0xff80000000000010);
    context.pdir_entry = HP_SBA_IOPDIR_VALID_BIT | UINT64_C(0x12345fff);

    g_assert_true(hp_sba_iommu_translate(&params, UINT64_MAX, &entry));
    g_assert_cmphex(context.last_bypass_addr, ==, UINT64_C(0xfffffffffffff000));
    g_assert_cmphex(context.last_pdir_addr, ==, UINT64_C(8));
    g_assert_cmphex(entry.iova, ==, UINT64_C(0xfffffffffffff000));
    g_assert_cmphex(entry.translated_addr, ==, UINT64_C(0x12345000));
}

static void test_invalid_configuration(void)
{
    TestContext context = test_context();
    HPSBAIOMMUParams params = test_params(&context);
    HPSBAIOMMUEntry entry;

    params.page_shift = 0;
    g_assert_false(
        hp_sba_iommu_translate(&params, UINT64_C(0x40012345), &entry));
    g_assert_cmpuint(context.bypass_count, ==, 0);
    g_assert_cmpuint(context.pdir_read_count, ==, 0);

    params.page_shift = 64;
    g_assert_false(
        hp_sba_iommu_translate(&params, UINT64_C(0x40012345), &entry));
    g_assert_cmpuint(context.bypass_count, ==, 0);
    g_assert_cmpuint(context.pdir_read_count, ==, 0);

    params = test_params(&context);
    params.bypass = NULL;
    g_assert_false(
        hp_sba_iommu_translate(&params, UINT64_C(0x40012345), &entry));
    g_assert_cmpuint(context.bypass_count, ==, 0);

    params = test_params(&context);
    params.pdir_read = NULL;
    g_assert_false(
        hp_sba_iommu_translate(&params, UINT64_C(0x40012345), &entry));
    g_assert_cmpuint(context.bypass_count, ==, 1);
    g_assert_cmpuint(context.pdir_read_count, ==, 0);
}

static HPZX1IOMMUWindow test_zx1_window(bool enabled, unsigned int page_shift)
{
    HPZX1IOMMUWindow window;

    g_assert_true(hp_zx1_iommu_decode_window(UINT64_C(0x40000000) | enabled,
                                             UINT64_C(0xf0000000), page_shift,
                                             &window));
    return window;
}

static void test_zx1_tcnfg(void)
{
    static const struct {
        uint64_t tcnfg;
        unsigned int page_shift;
    } valid[] = {
        { 0, 12 },
        { 1, 13 },
        { 2, 14 },
        { 3, 16 },
    };
    static const uint64_t invalid[] = {
        UINT64_C(4),
        UINT64_C(0x100000000),
        UINT64_MAX,
    };
    unsigned int page_shift;
    unsigned int i;

    for (i = 0; i < G_N_ELEMENTS(valid); i++) {
        page_shift = 0;
        g_assert_true(hp_zx1_iommu_decode_tcnfg(valid[i].tcnfg, &page_shift));
        g_assert_cmpuint(page_shift, ==, valid[i].page_shift);
    }

    for (i = 0; i < G_N_ELEMENTS(invalid); i++) {
        page_shift = 42;
        g_assert_false(hp_zx1_iommu_decode_tcnfg(invalid[i], &page_shift));
        g_assert_cmpuint(page_shift, ==, 42);
    }

    g_assert_false(hp_zx1_iommu_decode_tcnfg(0, NULL));
}

static void test_zx1_window_decode(void)
{
    static const unsigned int page_shifts[] = { 12, 13, 14, 16 };
    HPZX1IOMMUWindow window;
    unsigned int i;

    for (i = 0; i < G_N_ELEMENTS(page_shifts); i++) {
        uint64_t page_count;

        window = test_zx1_window(true, page_shifts[i]);
        page_count = UINT64_C(0x10000000) >> page_shifts[i];
        g_assert_true(window.enabled);
        g_assert_cmphex(window.base, ==, UINT64_C(0x40000000));
        g_assert_cmphex(window.mask, ==, UINT64_C(0xfffffffff0000000));
        g_assert_cmphex(window.aperture_size, ==, UINT64_C(0x10000000));
        g_assert_cmphex(window.pdir_index_mask, ==, page_count - 1);
        g_assert_cmpuint(window.page_shift, ==, page_shifts[i]);
    }

    g_assert_true(hp_zx1_iommu_decode_window(
        UINT64_C(0x40000000), UINT64_C(0x12345678f0000000), 12, &window));
    g_assert_false(window.enabled);
    g_assert_cmphex(window.mask, ==, UINT64_C(0xfffffffff0000000));

    g_assert_true(hp_zx1_iommu_decode_window(
        UINT64_C(0x40000001), UINT64_C(0xc0000000), 12, &window));
    g_assert_true(window.enabled);
    g_assert_cmphex(window.base, ==, UINT64_C(0x40000000));
    g_assert_cmphex(window.mask, ==, UINT64_C(0xffffffffc0000000));
    g_assert_cmphex(window.aperture_size, ==, UINT64_C(0x40000000));
    g_assert_cmphex(window.pdir_index_mask, ==, UINT64_C(0x3ffff));
}

static void test_zx1_window_invalid(void)
{
    const HPZX1IOMMUWindow unchanged = {
        .base = UINT64_C(0x1111111111111111),
        .mask = UINT64_C(0x2222222222222222),
        .aperture_size = UINT64_C(0x3333333333333333),
        .pdir_index_mask = UINT64_C(0x4444444444444444),
        .page_shift = 55,
        .enabled = true,
    };
    HPZX1IOMMUWindow window;

    window = unchanged;
    g_assert_false(hp_zx1_iommu_decode_window(
        UINT64_C(0x40000001), UINT64_C(0xf0000000), 0, &window));
    g_assert_cmpmem(&window, sizeof(window), &unchanged, sizeof(unchanged));

    window = unchanged;
    g_assert_false(hp_zx1_iommu_decode_window(
        UINT64_C(0x40000001), UINT64_C(0xf0000000), 64, &window));
    g_assert_cmpmem(&window, sizeof(window), &unchanged, sizeof(unchanged));

    window = unchanged;
    g_assert_false(hp_zx1_iommu_decode_window(
        UINT64_C(0x40000001), UINT64_C(0xe8000000), 12, &window));
    g_assert_cmpmem(&window, sizeof(window), &unchanged, sizeof(unchanged));

    window = unchanged;
    g_assert_false(hp_zx1_iommu_decode_window(
        UINT64_C(0x48000001), UINT64_C(0xf0000000), 12, &window));
    g_assert_cmpmem(&window, sizeof(window), &unchanged, sizeof(unchanged));

    window = unchanged;
    g_assert_false(hp_zx1_iommu_decode_window(
        UINT64_C(0x40000001), UINT64_C(0xfffff000), 13, &window));
    g_assert_cmpmem(&window, sizeof(window), &unchanged, sizeof(unchanged));

    window = unchanged;
    g_assert_false(hp_zx1_iommu_decode_window(
        UINT64_C(0xfffffffff0000001), UINT64_C(0xf0000000), 12, &window));
    g_assert_cmpmem(&window, sizeof(window), &unchanged, sizeof(unchanged));

    g_assert_false(hp_zx1_iommu_decode_window(UINT64_C(0x40000001),
                                              UINT64_C(0xf0000000), 12, NULL));
}

static void test_zx1_bypass(void)
{
    HPZX1IOMMUWindow window = test_zx1_window(true, 12);

    g_assert_false(
        hp_zx1_iommu_iova_is_bypass(&window, UINT64_C(0x40000000), false));
    g_assert_false(
        hp_zx1_iommu_iova_is_bypass(&window, UINT64_C(0x4fffffff), false));
    g_assert_true(
        hp_zx1_iommu_iova_is_bypass(&window, UINT64_C(0x3fffffff), false));
    g_assert_true(
        hp_zx1_iommu_iova_is_bypass(&window, UINT64_C(0x50000000), false));

    /* High IMASK bits prevent a 64-bit DAC from aliasing the window. */
    g_assert_true(hp_zx1_iommu_iova_is_bypass(
        &window, UINT64_C(0x0000000140001000), false));
    g_assert_true(
        hp_zx1_iommu_iova_is_bypass(&window, UINT64_C(0x40001000), true));

    window.enabled = false;
    g_assert_true(
        hp_zx1_iommu_iova_is_bypass(&window, UINT64_C(0x40001000), false));
    g_assert_true(
        hp_zx1_iommu_iova_is_bypass(NULL, UINT64_C(0x40001000), false));
}

static void test_zx1_pdir_index(void)
{
    HPZX1IOMMUWindow window = test_zx1_window(true, 12);
    uint64_t index;

    g_assert_true(
        hp_zx1_iommu_pdir_index(&window, UINT64_C(0x40012345), false, &index));
    g_assert_cmphex(index, ==, UINT64_C(0x12));

    g_assert_true(
        hp_zx1_iommu_pdir_index(&window, UINT64_C(0x4fffffff), false, &index));
    g_assert_cmphex(index, ==, UINT64_C(0xffff));

    index = UINT64_C(0xdeadbeef);
    g_assert_false(
        hp_zx1_iommu_pdir_index(&window, UINT64_C(0x40012345), true, &index));
    g_assert_cmphex(index, ==, UINT64_C(0xdeadbeef));
    g_assert_false(
        hp_zx1_iommu_pdir_index(&window, UINT64_C(0x50000000), false, &index));
    g_assert_cmphex(index, ==, UINT64_C(0xdeadbeef));
    g_assert_false(hp_zx1_iommu_pdir_index(
        &window, UINT64_C(0x0000000140001000), false, &index));
    g_assert_cmphex(index, ==, UINT64_C(0xdeadbeef));

    window.enabled = false;
    g_assert_false(
        hp_zx1_iommu_pdir_index(&window, UINT64_C(0x40012345), false, &index));
    g_assert_cmphex(index, ==, UINT64_C(0xdeadbeef));
    g_assert_false(
        hp_zx1_iommu_pdir_index(&window, UINT64_C(0x40012345), false, NULL));
}

static void test_zx1_window_page_walk_composition(void)
{
    static const unsigned int page_shifts[] = { 12, 13, 14, 16 };
    unsigned int i;

    for (i = 0; i < G_N_ELEMENTS(page_shifts); i++) {
        TestContext context = test_context();
        HPZX1IOMMUWindow window = test_zx1_window(true, page_shifts[i]);
        const uint64_t page_size = UINT64_C(1) << page_shifts[i];
        const uint64_t pdir_base = UINT64_C(0x00100000);
        const uint64_t iova = window.base + 5 * page_size + 0x123;
        const uint64_t target = UINT64_C(0x0000000123400000);
        HPSBAIOMMUParams params = {
            .ibase = window.base | 1,
            .imask = window.mask,
            .pdir_base = pdir_base,
            .pdir_index_mask = window.pdir_index_mask,
            .page_shift = window.page_shift,
            .bypass = test_bypass,
            .pdir_read = test_pdir_read,
            .opaque = &context,
        };
        HPSBAIOMMUEntry entry;

        context.pdir_entry = HP_SBA_IOPDIR_VALID_BIT | target | 0x123;
        g_assert_true(hp_sba_iommu_translate(&params, iova, &entry));
        g_assert_cmphex(entry.iova, ==, iova & ~(page_size - 1));
        g_assert_cmphex(entry.translated_addr, ==, target & ~(page_size - 1));
        g_assert_cmphex(entry.addr_mask, ==, page_size - 1);
        g_assert_cmphex(context.last_pdir_addr, ==, pdir_base + 5 * 8);

        context.pdir_entry &= ~HP_SBA_IOPDIR_VALID_BIT;
        g_assert_false(hp_sba_iommu_translate(&params, iova, &entry));

        context.pdir_read_count = 0;
        context.bypass_value = UINT64_C(0x0000000140001234) & ~(page_size - 1);
        g_assert_true(hp_sba_iommu_translate(
            &params, UINT64_C(0x0000000140001234), &entry));
        g_assert_cmphex(entry.translated_addr, ==, context.bypass_value);
        g_assert_cmpuint(context.pdir_read_count, ==, 0);
    }
}

static void test_zx1_pcom(void)
{
    HPZX1IOMMUWindow window = test_zx1_window(true, 12);
    HPZX1IOMMUPurge purge;

    g_assert_true(
        hp_zx1_iommu_decode_pcom(&window, UINT64_C(0x40012000) | 12, &purge));
    g_assert_cmphex(purge.iova, ==, UINT64_C(0x40012000));
    g_assert_cmphex(purge.size, ==, UINT64_C(0x1000));

    g_assert_true(
        hp_zx1_iommu_decode_pcom(&window, UINT64_C(0x48000000) | 27, &purge));
    g_assert_cmphex(purge.iova, ==, UINT64_C(0x48000000));
    g_assert_cmphex(purge.size, ==, UINT64_C(0x08000000));

    g_assert_true(
        hp_zx1_iommu_decode_pcom(&window, UINT64_C(0x40000000) | 28, &purge));
    g_assert_cmphex(purge.iova, ==, UINT64_C(0x40000000));
    g_assert_cmphex(purge.size, ==, UINT64_C(0x10000000));

    /* PCOM invalidates tags and may safely cover outside the aperture. */
    g_assert_true(
        hp_zx1_iommu_decode_pcom(&window, UINT64_C(0x40000000) | 29, &purge));
    g_assert_cmphex(purge.iova, ==, UINT64_C(0x40000000));
    g_assert_cmphex(purge.size, ==, UINT64_C(0x20000000));
    g_assert_true(
        hp_zx1_iommu_decode_pcom(&window, UINT64_C(0x50000000) | 12, &purge));
    g_assert_cmphex(purge.iova, ==, UINT64_C(0x50000000));
    g_assert_cmphex(purge.size, ==, UINT64_C(0x1000));

    window.enabled = false;
    g_assert_true(
        hp_zx1_iommu_decode_pcom(&window, UINT64_C(0x40010000) | 16, &purge));

    g_assert_true(
        hp_zx1_iommu_decode_window(UINT64_C(1), UINT64_C(0), 12, &window));
    g_assert_true(
        hp_zx1_iommu_decode_pcom(&window, UINT64_C(0x80000000) | 31, &purge));
    g_assert_cmphex(purge.iova, ==, UINT64_C(0x80000000));
    g_assert_cmphex(purge.size, ==, UINT64_C(0x80000000));

    g_assert_true(hp_sba_iommu_decode_pcom(UINT64_C(0x0000000100000000) | 16,
                                           16, &purge));
    g_assert_cmphex(purge.iova, ==, UINT64_C(0x0000000100000000));
    g_assert_cmphex(purge.size, ==, UINT64_C(0x10000));

    /* The last naturally aligned page ends at UINT64_MAX without wrapping. */
    g_assert_true(hp_sba_iommu_decode_pcom(UINT64_C(0xfffffffffffff000) | 12,
                                           12, &purge));
    g_assert_cmphex(purge.iova, ==, UINT64_C(0xfffffffffffff000));
    g_assert_cmphex(purge.size, ==, UINT64_C(0x1000));
}

static void test_zx1_pcom_invalid(void)
{
    const HPZX1IOMMUPurge unchanged = {
        .iova = UINT64_C(0x1111111111111111),
        .size = UINT64_C(0x2222222222222222),
    };
    HPZX1IOMMUWindow window = test_zx1_window(true, 12);
    HPZX1IOMMUPurge purge;

    purge = unchanged;
    g_assert_false(
        hp_zx1_iommu_decode_pcom(&window, UINT64_C(0x40000000) | 11, &purge));
    g_assert_cmpmem(&purge, sizeof(purge), &unchanged, sizeof(unchanged));

    purge = unchanged;
    g_assert_false(
        hp_zx1_iommu_decode_pcom(&window, UINT64_C(0x40001000) | 13, &purge));
    g_assert_cmpmem(&purge, sizeof(purge), &unchanged, sizeof(unchanged));

    g_assert_false(
        hp_zx1_iommu_decode_pcom(NULL, UINT64_C(0x40000000) | 12, &purge));
    g_assert_false(
        hp_zx1_iommu_decode_pcom(&window, UINT64_C(0x40000000) | 12, NULL));

    purge = unchanged;
    g_assert_false(
        hp_sba_iommu_decode_pcom(UINT64_C(0x40000000) | 12, 0, &purge));
    g_assert_cmpmem(&purge, sizeof(purge), &unchanged, sizeof(unchanged));

    purge = unchanged;
    g_assert_false(
        hp_sba_iommu_decode_pcom(UINT64_C(0x40000000) | 31, 32, &purge));
    g_assert_cmpmem(&purge, sizeof(purge), &unchanged, sizeof(unchanged));

    g_assert_false(
        hp_sba_iommu_decode_pcom(UINT64_C(0x40000000) | 12, 12, NULL));
}

static HPSBAIOMMUEntry test_iotlb_entry(uint64_t iova, uint64_t target,
                                        uint64_t mask)
{
    return (HPSBAIOMMUEntry) {
        .iova = iova,
        .translated_addr = target,
        .addr_mask = mask,
    };
}

static void test_zx1_iotlb_policy_free_slots(void)
{
    const HPSBAIOMMUEntry unchanged = {
        .iova = UINT64_C(0x1111111111111111),
        .translated_addr = UINT64_C(0x2222222222222222),
        .addr_mask = UINT64_C(0x3333333333333333),
    };
    HPZX1IOTLB iotlb;
    HPSBAIOMMUEntry entry;
    HPSBAIOMMUEntry result;
    unsigned int i;

    hp_zx1_iotlb_clear(&iotlb);
    for (i = 0; i < HP_ZX1_IOTLB_SLOT_COUNT; i++) {
        unsigned int slot = (i * 5) % HP_ZX1_IOTLB_SLOT_COUNT;

        entry = test_iotlb_entry(UINT64_C(0x40000000) + i * 0x1000,
                                 UINT64_C(0x10000000) + i * 0x1000,
                                 UINT64_C(0xfff));
        g_assert_true(hp_zx1_iotlb_store_slot(&iotlb, slot, &entry));
    }

    /* Every slot participates in a fully-associative lookup. */
    for (i = 0; i < HP_ZX1_IOTLB_SLOT_COUNT; i++) {
        g_assert_true(hp_zx1_iotlb_lookup(
            &iotlb, UINT64_C(0x40000000) + i * 0x1000 + 0xabc, &result));
        g_assert_cmphex(result.iova, ==, UINT64_C(0x40000000) + i * 0x1000);
        g_assert_cmphex(result.translated_addr, ==,
                        UINT64_C(0x10000000) + i * 0x1000);
        g_assert_cmphex(result.addr_mask, ==, UINT64_C(0xfff));
    }

    result = unchanged;
    g_assert_false(hp_zx1_iotlb_lookup(&iotlb, UINT64_C(0x50000000), &result));
    g_assert_cmpmem(&result, sizeof(result), &unchanged, sizeof(unchanged));

    /* Capacity does not imply a victim: the caller must name a valid slot. */
    entry = test_iotlb_entry(UINT64_C(0x50000000), UINT64_C(0x20000000),
                             UINT64_C(0xfff));
    g_assert_false(
        hp_zx1_iotlb_store_slot(&iotlb, HP_ZX1_IOTLB_SLOT_COUNT, &entry));
    result = unchanged;
    g_assert_false(hp_zx1_iotlb_lookup(&iotlb, UINT64_C(0x50000000), &result));
    g_assert_cmpmem(&result, sizeof(result), &unchanged, sizeof(unchanged));

    /* Replacing an explicitly selected slot does not disturb other slots. */
    g_assert_true(hp_zx1_iotlb_store_slot(&iotlb, 3, &entry));
    g_assert_true(hp_zx1_iotlb_lookup(&iotlb, UINT64_C(0x50000abc), &result));
    g_assert_cmpmem(&result, sizeof(result), &entry, sizeof(entry));
    g_assert_false(hp_zx1_iotlb_lookup(&iotlb, UINT64_C(0x40007000), &result));
    g_assert_true(hp_zx1_iotlb_lookup(&iotlb, UINT64_C(0x40006000), &result));

    entry.addr_mask = UINT64_C(0xffe);
    g_assert_false(hp_zx1_iotlb_store_slot(&iotlb, 3, &entry));
    g_assert_true(hp_zx1_iotlb_lookup(&iotlb, UINT64_C(0x50000000), &result));

    g_assert_false(hp_zx1_iotlb_store_slot(NULL, 0, &entry));
    g_assert_false(hp_zx1_iotlb_store_slot(&iotlb, 0, NULL));
    g_assert_false(hp_zx1_iotlb_lookup(NULL, 0, &result));
    g_assert_false(hp_zx1_iotlb_lookup(&iotlb, 0, NULL));

    hp_zx1_iotlb_clear(&iotlb);
    result = unchanged;
    g_assert_false(hp_zx1_iotlb_lookup(&iotlb, UINT64_C(0x50000000), &result));
    g_assert_cmpmem(&result, sizeof(result), &unchanged, sizeof(unchanged));
    hp_zx1_iotlb_clear(NULL);
}

static void test_zx1_iotlb_pcom_range(void)
{
    static const uint64_t iovas[] = {
        UINT64_C(0x3ffff000),
        UINT64_C(0x40000000),
        UINT64_C(0x40001000),
        UINT64_C(0x40002000),
    };
    HPZX1IOTLB iotlb;
    HPSBAIOMMUPurge purge;
    HPSBAIOMMUEntry entry;
    HPSBAIOMMUEntry result;
    unsigned int i;

    hp_zx1_iotlb_clear(&iotlb);
    for (i = 0; i < G_N_ELEMENTS(iovas); i++) {
        entry = test_iotlb_entry(iovas[i], UINT64_C(0x10000000) + i * 0x1000,
                                 UINT64_C(0xfff));
        g_assert_true(hp_zx1_iotlb_store_slot(&iotlb, i, &entry));
    }

    g_assert_true(
        hp_sba_iommu_decode_pcom(UINT64_C(0x40000000) | 13, 12, &purge));
    g_assert_true(hp_zx1_iotlb_invalidate(&iotlb, &purge));
    g_assert_true(hp_zx1_iotlb_lookup(&iotlb, iovas[0], &result));
    g_assert_false(hp_zx1_iotlb_lookup(&iotlb, iovas[1], &result));
    g_assert_false(hp_zx1_iotlb_lookup(&iotlb, iovas[2], &result));
    g_assert_true(hp_zx1_iotlb_lookup(&iotlb, iovas[3], &result));

    purge = (HPSBAIOMMUPurge) {
        .iova = UINT64_C(0x40001000),
        .size = UINT64_C(0x2000),
    };
    g_assert_false(hp_zx1_iotlb_invalidate(&iotlb, &purge));
    g_assert_true(hp_zx1_iotlb_lookup(&iotlb, iovas[3], &result));
    g_assert_false(hp_zx1_iotlb_invalidate(NULL, &purge));
    g_assert_false(hp_zx1_iotlb_invalidate(&iotlb, NULL));
}

static void test_zx1_iotlb_stale_until_pcom(void)
{
    TestContext context = test_context();
    HPZX1IOMMUWindow window = test_zx1_window(true, 12);
    HPSBAIOMMUParams params = {
        .ibase = window.base | 1,
        .imask = window.mask,
        .pdir_base = UINT64_C(0x00100000),
        .pdir_index_mask = window.pdir_index_mask,
        .page_shift = window.page_shift,
        .bypass = test_bypass,
        .pdir_read = test_pdir_read,
        .opaque = &context,
    };
    const uint64_t iova = UINT64_C(0x40005123);
    const HPSBAIOMMUEntry unchanged = {
        .iova = UINT64_C(0x1111111111111111),
        .translated_addr = UINT64_C(0x2222222222222222),
        .addr_mask = UINT64_C(0x3333333333333333),
    };
    HPZX1IOTLB iotlb;
    HPSBAIOMMUPurge purge;
    HPSBAIOMMUEntry entry;

    hp_zx1_iotlb_clear(&iotlb);
    context.pdir_entry = HP_SBA_IOPDIR_VALID_BIT | UINT64_C(0x0000000012345000);
    g_assert_true(hp_sba_iommu_translate(&params, iova, &entry));
    g_assert_true(hp_zx1_iotlb_store_slot(&iotlb, 9, &entry));

    context.pdir_entry = HP_SBA_IOPDIR_VALID_BIT | UINT64_C(0x0000000023456000);
    entry = unchanged;
    g_assert_true(hp_zx1_iotlb_lookup(&iotlb, iova, &entry));
    g_assert_cmphex(entry.translated_addr, ==, UINT64_C(0x0000000012345000));

    g_assert_true(
        hp_sba_iommu_decode_pcom(UINT64_C(0x40005000) | 12, 12, &purge));
    g_assert_true(hp_zx1_iotlb_invalidate(&iotlb, &purge));
    entry = unchanged;
    g_assert_false(hp_zx1_iotlb_lookup(&iotlb, iova, &entry));
    g_assert_cmpmem(&entry, sizeof(entry), &unchanged, sizeof(unchanged));

    g_assert_true(hp_sba_iommu_translate(&params, iova, &entry));
    g_assert_cmphex(entry.translated_addr, ==, UINT64_C(0x0000000023456000));
    g_assert_true(hp_zx1_iotlb_store_slot(&iotlb, 9, &entry));

    g_assert_true(hp_zx1_iotlb_invalidate(&iotlb, &purge));
    context.pdir_entry &= ~HP_SBA_IOPDIR_VALID_BIT;
    g_assert_false(hp_sba_iommu_translate(&params, iova, &entry));
    g_assert_false(hp_zx1_iotlb_lookup(&iotlb, iova, &entry));
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/hp-sba-iommu/disabled-bypass", test_disabled_bypass);
    g_test_add_func("/hp-sba-iommu/nonmatching-bypass",
                    test_nonmatching_bypass);
    g_test_add_func("/hp-sba-iommu/valid-pte-absolute-index",
                    test_valid_pte_absolute_index);
    g_test_add_func("/hp-sba-iommu/invalid-pte", test_invalid_pte);
    g_test_add_func("/hp-sba-iommu/pdir-read-failure", test_pdir_read_failure);
    g_test_add_func("/hp-sba-iommu/page-sizes-index-mask",
                    test_page_sizes_and_index_mask);
    g_test_add_func("/hp-sba-iommu/pdir-pointer-wrap", test_pdir_pointer_wrap);
    g_test_add_func("/hp-sba-iommu/invalid-configuration",
                    test_invalid_configuration);
    g_test_add_func("/hp-sba-iommu/zx1/tcnfg", test_zx1_tcnfg);
    g_test_add_func("/hp-sba-iommu/zx1/window-decode", test_zx1_window_decode);
    g_test_add_func("/hp-sba-iommu/zx1/window-invalid",
                    test_zx1_window_invalid);
    g_test_add_func("/hp-sba-iommu/zx1/bypass", test_zx1_bypass);
    g_test_add_func("/hp-sba-iommu/zx1/pdir-index", test_zx1_pdir_index);
    g_test_add_func("/hp-sba-iommu/zx1/window-page-walk-composition",
                    test_zx1_window_page_walk_composition);
    g_test_add_func("/hp-sba-iommu/zx1/pcom", test_zx1_pcom);
    g_test_add_func("/hp-sba-iommu/zx1/pcom-invalid", test_zx1_pcom_invalid);
    g_test_add_func("/hp-sba-iommu/zx1/iotlb/policy-free-slots",
                    test_zx1_iotlb_policy_free_slots);
    g_test_add_func("/hp-sba-iommu/zx1/iotlb/pcom-range",
                    test_zx1_iotlb_pcom_range);
    g_test_add_func("/hp-sba-iommu/zx1/iotlb/stale-until-pcom",
                    test_zx1_iotlb_stale_until_pcom);
    return g_test_run();
}
