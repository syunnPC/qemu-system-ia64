/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Host unit tests for pure IA-64 packed-integer architecture logic.
 */

#include "qemu/osdep.h"
#include "target/ia64/arch/simd-ops.h"

typedef const char *(*TestFn)(void);

typedef struct TestCase {
    const char *name;
    TestFn fn;
} TestCase;

static char failure[160];

static const char *expect_u64(const char *operation, uint64_t actual,
                              uint64_t expected)
{
    if (actual == expected) {
        return NULL;
    }
    snprintf(failure, sizeof(failure),
             "%s: expected %016" PRIx64 " got %016" PRIx64, operation, expected,
             actual);
    return failure;
}

static const char *test_average_compare_minmax(void)
{
    const char *error;

    error = expect_u64("pavg1",
                       ia64_simd_pavg_value(0, UINT64_C(0x0001020304050607),
                                            UINT64_C(0x0807060504030201)),
                       UINT64_C(0x0404040404040404));
    if (error) {
        return error;
    }
    error = expect_u64("pcmpeq1",
                       ia64_simd_pcmp_value(0, UINT64_C(0x0011223344556677),
                                            UINT64_C(0x0011aa3344bb6677)),
                       UINT64_C(0xffff00ffff00ffff));
    if (error) {
        return error;
    }
    error = expect_u64("pmax1.u",
                       ia64_simd_pminmax_value(0, UINT64_C(0x0102030405060708),
                                               UINT64_C(0x0807060504030201)),
                       UINT64_C(0x0807060505060708));
    if (error) {
        return error;
    }
    return expect_u64("pmin1.u",
                      ia64_simd_pminmax_value(1, UINT64_C(0x0102030405060708),
                                              UINT64_C(0x0807060504030201)),
                      UINT64_C(0x0102030404030201));
}

static const char *test_multiply_and_sad(void)
{
    const uint64_t a = UINT64_C(0x0004000300020001);
    const uint64_t b = UINT64_C(0x0008000700060005);
    const char *error;

    error = expect_u64("pmpy2.l", ia64_simd_pmpy_value(0, a, b, 0),
                       UINT64_C(0x000000200000000c));
    if (error) {
        return error;
    }
    error = expect_u64("pmpy2.r", ia64_simd_pmpy_value(1, a, b, 0),
                       UINT64_C(0x0000001500000005));
    if (error) {
        return error;
    }
    return expect_u64(
        "psad1", ia64_simd_psad1_value(0, UINT64_C(0x0101010101010101)), 8);
}

static const char *test_permutations(void)
{
    const uint64_t a = UINT64_C(0x4444333322221111);
    const uint64_t b = UINT64_C(0x8888777766665555);
    const char *error;

    error =
        expect_u64("mux1 broadcast", ia64_simd_mux_value(0, UINT64_C(0x5a), 0),
                   UINT64_C(0x5a5a5a5a5a5a5a5a));
    if (error) {
        return error;
    }
    error = expect_u64("mux2 reverse", ia64_simd_mux_value(1, a, 0x1b),
                       UINT64_C(0x1111222233334444));
    if (error) {
        return error;
    }
    error = expect_u64("mix2.l", ia64_simd_mix_value(2, a, b),
                       UINT64_C(0x4444888822226666));
    if (error) {
        return error;
    }
    error = expect_u64("unpack2.l", ia64_simd_unpack_value(3, a, b),
                       UINT64_C(0x2222666611115555));
    if (error) {
        return error;
    }
    return expect_u64("unpack2.h", ia64_simd_unpack_value(2, a, b),
                      UINT64_C(0x4444888833337777));
}

static const char *test_saturation(void)
{
    return expect_u64("pack2.uss",
                      ia64_simd_pack_value(1, UINT64_C(0x010000ff0001ffff),
                                           UINT64_C(0x7fff0080007f8000)),
                      UINT64_C(0xff807f00ffff0100));
}

static const char *test_pshladd2_shift_saturation(void)
{
    return expect_u64("pshladd2 shift overflow suppresses add",
                      ia64_simd_pshladd2_value(UINT64_C(0xc0003fffbfff4000),
                                               UINT64_C(0x000100010001ffff), 1),
                      UINT64_C(0x80017fff80007fff));
}

static const char *test_count_zero_and_sum(void)
{
    const char *error;

    error = expect_u64("czx1.r",
                       ia64_simd_czx_value(1, UINT64_C(0x0000000000020201)), 3);
    if (error) {
        return error;
    }
    error = expect_u64("czx1.l",
                       ia64_simd_czx_value(0, UINT64_C(0x0102000000000000)), 2);
    if (error) {
        return error;
    }
    return expect_u64("pshr2.u",
                      ia64_simd_sum_value(UINT64_C(0x0004000300020001),
                                          UINT64_C(0x0008000700060005)),
                      UINT64_C(0x00070003000f000b));
}

int main(void)
{
    static const TestCase tests[] = {
        { "average, compare, and minmax", test_average_compare_minmax },
        { "multiply and SAD", test_multiply_and_sad },
        { "permutations", test_permutations },
        { "saturation", test_saturation },
        { "pshladd2 shift saturation", test_pshladd2_shift_saturation },
        { "count-zero and sum", test_count_zero_and_sum },
    };
    int status = 0;

    printf("TAP version 13\n");
    printf("1..%zu\n", G_N_ELEMENTS(tests));
    for (size_t i = 0; i < G_N_ELEMENTS(tests); i++) {
        const char *error = tests[i].fn();

        if (error) {
            printf("not ok %zu - %s\n# %s\n", i + 1, tests[i].name, error);
            status = 1;
        } else {
            printf("ok %zu - %s\n", i + 1, tests[i].name);
        }
    }
    return status;
}
