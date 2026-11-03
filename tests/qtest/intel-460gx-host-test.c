/*
 * Intel 460GX configuration host qtests
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/ia64/intel_460gx_host.h"
#include "libqtest.h"

#define HOST_TEST_BASE UINT64_C(0x80300000)

static QTestState *host_start_with_reset(const char *extra_args,
                                         unsigned initial_cbn,
                                         uint32_t initial_chipset_present)
{
    return qtest_initf("-machine ia64-vpc,nvram=none "
                       "-m 256M -smp 1 -S "
                       "-device %s,id=host0,x-initial-cbn=0x%x,"
                       "x-initial-chipset-present=0x%x,"
                       "x-test-io-base=0x%" PRIx64 " %s",
                       TYPE_INTEL_460GX_HOST, initial_cbn,
                       initial_chipset_present, HOST_TEST_BASE,
                       extra_args ?: "");
}

static QTestState *host_start(const char *extra_args)
{
    return host_start_with_reset(extra_args, 0x40, 0x11);
}

static uint32_t config_address(unsigned bus, unsigned device, unsigned function,
                               unsigned reg)
{
    return UINT32_C(0x80000000) | bus << 16 | device << 11 | function << 8 |
           (reg & 0xfc);
}

static void test_mechanism_absent_and_reset(void)
{
    QTestState *qts = host_start(NULL);
    uint32_t address = config_address(0x40, 4, 0, 0x80);

    g_assert_cmphex(qtest_readl(qts, HOST_TEST_BASE), ==, 0);
    g_assert_cmphex(qtest_readb(qts, HOST_TEST_BASE + 4), ==, UINT8_MAX);
    g_assert_cmphex(qtest_readw(qts, HOST_TEST_BASE + 4), ==, UINT16_MAX);
    g_assert_cmphex(qtest_readl(qts, HOST_TEST_BASE + 4), ==, UINT32_MAX);

    qtest_writel(qts, HOST_TEST_BASE, address | UINT32_C(0x7f000003));
    g_assert_cmphex(qtest_readl(qts, HOST_TEST_BASE), ==, address);

    /* Present-without-a-registered-target and absent devices both abort. */
    g_assert_cmphex(qtest_readl(qts, HOST_TEST_BASE + 4), ==, UINT32_MAX);
    qtest_writel(qts, HOST_TEST_BASE, config_address(0x40, 7, 0, 0));
    g_assert_cmphex(qtest_readl(qts, HOST_TEST_BASE + 4), ==, UINT32_MAX);
    qtest_writel(qts, HOST_TEST_BASE, config_address(0x33, 0, 0, 0));
    g_assert_cmphex(qtest_readl(qts, HOST_TEST_BASE + 4), ==, UINT32_MAX);

    qtest_system_reset(qts);
    g_assert_cmphex(qtest_readl(qts, HOST_TEST_BASE), ==, 0);
    qtest_quit(qts);
}

static void test_migration_state(void)
{
    const uint32_t address = config_address(0x52, 3, 1, 0xa0);
    g_autofree char *tmpdir = NULL;
    g_autofree char *disk_path = NULL;
    g_autofree char *quoted_disk_path = NULL;
    g_autofree char *args = NULL;
    g_autofree char *response = NULL;
    g_autoptr(GError) error = NULL;
    QTestState *qts;

    if (!have_qemu_img()) {
        g_test_skip("qemu-img is required for migration-state snapshot");
        return;
    }

    tmpdir = g_dir_make_tmp("intel-460gx-host-savevm-XXXXXX", &error);
    g_assert_no_error(error);
    g_assert_nonnull(tmpdir);
    disk_path = g_build_filename(tmpdir, "snapshot.qcow2", NULL);
    g_assert_true(mkimg(disk_path, "qcow2", 16));
    quoted_disk_path = g_shell_quote(disk_path);
    args = g_strdup_printf("-drive file=%s,format=qcow2,if=none",
                           quoted_disk_path);
    qts = host_start(args);

    qtest_writel(qts, HOST_TEST_BASE, address);
    response = qtest_hmp(qts, "savevm host-state");
    g_assert_cmpstr(response, ==, "");
    g_clear_pointer(&response, g_free);

    qtest_system_reset(qts);
    g_assert_cmphex(qtest_readl(qts, HOST_TEST_BASE), ==, 0);
    response = qtest_hmp(qts, "loadvm host-state");
    g_assert_cmpstr(response, ==, "");
    g_assert_cmphex(qtest_readl(qts, HOST_TEST_BASE), ==, address);

    /* A loaded state must retain the device's reset baseline. */
    qtest_system_reset(qts);
    g_assert_cmphex(qtest_readl(qts, HOST_TEST_BASE), ==, 0);

    qtest_quit(qts);
    g_assert_cmpint(g_unlink(disk_path), ==, 0);
    g_assert_cmpint(g_rmdir(tmpdir), ==, 0);
}

static void test_migration_reset_baseline(void)
{
    g_autofree char *tmpdir = NULL;
    g_autofree char *disk_path = NULL;
    g_autofree char *quoted_disk_path = NULL;
    g_autofree char *args = NULL;
    g_autofree char *response = NULL;
    g_autoptr(GError) error = NULL;
    QTestState *qts;

    if (!have_qemu_img()) {
        g_test_skip("qemu-img is required for migration-state snapshot");
        return;
    }

    tmpdir = g_dir_make_tmp("intel-460gx-host-reset-savevm-XXXXXX", &error);
    g_assert_no_error(error);
    g_assert_nonnull(tmpdir);
    disk_path = g_build_filename(tmpdir, "snapshot.qcow2", NULL);
    g_assert_true(mkimg(disk_path, "qcow2", 16));
    quoted_disk_path = g_shell_quote(disk_path);
    args = g_strdup_printf("-drive file=%s,format=qcow2,if=none",
                           quoted_disk_path);

    qts = host_start(args);
    response = qtest_hmp(qts, "savevm host-reset-baseline");
    g_assert_cmpstr(response, ==, "");
    g_clear_pointer(&response, g_free);
    qtest_quit(qts);

    /* A different CBN reset baseline must reject the v3 stream. */
    qts = host_start_with_reset(args, 0x41, 0x11);
    response = qtest_hmp(qts, "loadvm host-reset-baseline");
    g_assert_nonnull(strstr(response, "error while loading state"));
    g_assert_nonnull(strstr(response, TYPE_INTEL_460GX_HOST));
    g_clear_pointer(&response, g_free);
    qtest_quit(qts);

    /* The chipset-present reset mask is destination wiring as well. */
    qts = host_start_with_reset(args, 0x40, 0x10);
    response = qtest_hmp(qts, "loadvm host-reset-baseline");
    g_assert_nonnull(strstr(response, "error while loading state"));
    g_assert_nonnull(strstr(response, TYPE_INTEL_460GX_HOST));
    g_clear_pointer(&response, g_free);
    qtest_quit(qts);

    /* Matching destination wiring still accepts the snapshot. */
    qts = host_start(args);
    response = qtest_hmp(qts, "loadvm host-reset-baseline");
    g_assert_cmpstr(response, ==, "");
    qtest_quit(qts);

    g_assert_cmpint(g_unlink(disk_path), ==, 0);
    g_assert_cmpint(g_rmdir(tmpdir), ==, 0);
}

int main(int argc, char **argv)
{
    g_test_init(&argc, &argv, NULL);

    qtest_add_func("/intel-460gx-host/mechanism-absent-and-reset",
                   test_mechanism_absent_and_reset);
    qtest_add_func("/intel-460gx-host/migration-state", test_migration_state);
    qtest_add_func("/intel-460gx-host/migration-reset-baseline",
                   test_migration_reset_baseline);

    return g_test_run();
}
