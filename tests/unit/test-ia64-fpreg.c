/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Pure unit tests for the IA-64 floating-point register representation.
 */

#include "qemu/osdep.h"
#include "target/ia64/cpu.h"
#include "target/ia64/fpreg.h"

typedef const char *(*TestFn)(void);

typedef struct TestCase {
    const char *name;
    TestFn fn;
} TestCase;

typedef struct Binary32Case {
    uint32_t value;
    uint64_t low;
    uint64_t high;
} Binary32Case;

typedef struct Binary64Case {
    uint64_t value;
    uint64_t low;
    uint64_t high;
} Binary64Case;

static char failure[192];

static void reset_env(CPUIA64State *env)
{
    memset(env, 0, sizeof(*env));
    env->fp.fr[1] = IA64_FR_ONE;
    set_float_2nan_prop_rule(float_2nan_prop_ab, &env->fp.fp_status);
    set_float_3nan_prop_rule(float_3nan_prop_abc, &env->fp.fp_status);
    set_float_infzeronan_rule(float_infzeronan_dnan_never, &env->fp.fp_status);
    set_float_default_nan_pattern(0b11000000, &env->fp.fp_status);
}

static const char *fail_u64(const char *label, uint64_t actual,
                            uint64_t expected)
{
    snprintf(failure, sizeof(failure),
             "%s: expected %016" PRIx64 " got %016" PRIx64, label, expected,
             actual);
    return failure;
}

static const char *expect_spill(const CPUIA64State *env, unsigned reg,
                                uint64_t expected_low, uint64_t expected_high)
{
    uint64_t low;
    uint64_t high;

    ia64_fpreg_to_spill(env, reg, &low, &high);
    if (low != expected_low) {
        return fail_u64("spill low", low, expected_low);
    }
    if (high != expected_high) {
        return fail_u64("spill high", high, expected_high);
    }
    return NULL;
}

static const char *test_constants(void)
{
    CPUIA64State env;
    const char *error;

    reset_env(&env);
    env.fp.fr[0] = UINT64_MAX;
    env.fp.fr[1] = 0;
    ia64_fpreg_from_spill(&env, 0, UINT64_MAX, 0x3ffff);
    ia64_fpreg_from_spill(&env, 1, UINT64_MAX, 0x3ffff);
    if (env.fp.fr[0] != UINT64_MAX || env.fp.fr[1] != 0) {
        return "write to architectural f0/f1 was not ignored";
    }

    error = expect_spill(&env, 0, 0, 0);
    if (error != NULL) {
        return error;
    }
    error = expect_spill(&env, 1, 0x8000000000000000ULL, 0xffff);
    if (error != NULL) {
        return error;
    }
    if (ia64_fpreg_is_nat(&env, 0) || ia64_fpreg_is_nat(&env, 1)) {
        return "architectural constants reported as NaTVal";
    }
    return NULL;
}

static const char *test_binary32(void)
{
    static const Binary32Case cases[] = {
        { 0x00000000, 0x0000000000000000ULL, 0x0000000000000000ULL },
        { 0x80000000, 0x0000000000000000ULL, 0x0000000000020000ULL },
        { 0x3f800000, 0x8000000000000000ULL, 0x000000000000ffffULL },
        { 0x00000001, 0x0000010000000000ULL, 0x000000000000ff81ULL },
        { 0x7f800000, 0x8000000000000000ULL, 0x000000000001ffffULL },
        { 0x7fc12345, 0xc123450000000000ULL, 0x000000000001ffffULL },
    };
    CPUIA64State env;
    unsigned i;

    reset_env(&env);
    for (i = 0; i < G_N_ELEMENTS(cases); i++) {
        const char *error;

        ia64_fpreg_from_binary32(&env, 2, cases[i].value);
        error = expect_spill(&env, 2, cases[i].low, cases[i].high);
        if (error != NULL) {
            return error;
        }
        if (ia64_fpreg_to_binary32(&env, 2) != cases[i].value) {
            return fail_u64("binary32 round-trip",
                            ia64_fpreg_to_binary32(&env, 2), cases[i].value);
        }
    }
    return NULL;
}

static const char *test_binary64(void)
{
    static const Binary64Case cases[] = {
        { 0x0000000000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL },
        { 0x8000000000000000ULL, 0x0000000000000000ULL, 0x0000000000020000ULL },
        { 0x3ff0000000000000ULL, 0x8000000000000000ULL, 0x000000000000ffffULL },
        { 0x0000000000000001ULL, 0x0000000000000800ULL, 0x000000000000fc01ULL },
        { 0x7ff0000000000000ULL, 0x8000000000000000ULL, 0x000000000001ffffULL },
        { 0x7ff8123456789abcULL, 0xc091a2b3c4d5e000ULL, 0x000000000001ffffULL },
    };
    CPUIA64State env;
    unsigned i;

    reset_env(&env);
    for (i = 0; i < G_N_ELEMENTS(cases); i++) {
        const char *error;

        ia64_fpreg_from_binary64(&env, 2, cases[i].value);
        error = expect_spill(&env, 2, cases[i].low, cases[i].high);
        if (error != NULL) {
            return error;
        }
        if (ia64_fpreg_to_binary64(&env, 2) != cases[i].value) {
            return fail_u64("binary64 round-trip",
                            ia64_fpreg_to_binary64(&env, 2), cases[i].value);
        }
    }
    return NULL;
}

static const char *test_integer_significand(void)
{
    CPUIA64State env;
    uint64_t value = 0x0123456789abcdefULL;
    const char *error;

    reset_env(&env);
    ia64_fpreg_from_spill(&env, 5, value, IA64_FP_REG_INTEGER_EXP);
    error = expect_spill(&env, 5, value, IA64_FP_REG_INTEGER_EXP);
    if (error != NULL) {
        return error;
    }
    if (!ia64_fpreg_is_integer(&env, 5) ||
        !(env.fp.fr_int_origin[0] & (1ULL << 5)) ||
        env.fp.fr_int_value[5] != value) {
        return "integer-significand tags were not restored";
    }

    /*
     * A normalized-looking payload at the reserved integer exponent is not a
     * compact binary64 fill: ldf.fill must preserve its integer tag.
     */
    value = IA64_FP_SIGNIFICAND_INTEGER_BIT;
    ia64_fpreg_from_spill(&env, 6, value, IA64_FP_REG_INTEGER_EXP);
    error = expect_spill(&env, 6, value, IA64_FP_REG_INTEGER_EXP);
    if (error != NULL) {
        return error;
    }
    if (!ia64_fpreg_is_integer(&env, 6) ||
        (env.fp.fr_ext_valid[0] & (1ULL << 6))) {
        return "integer exponent was incorrectly compacted";
    }
    return NULL;
}

static const char *test_extended(void)
{
    CPUIA64State env;
    const uint64_t low = 0x8123456789abcdefULL;
    const uint32_t exp = 0x12345;
    const uint64_t high = exp | (1ULL << 17);
    const uint64_t binary64 = (1ULL << 63) |
                              (uint64_t)((((exp >> 6) & 0x400) | (exp & 0x3ff)))
                                  << 52 |
                              ((low >> 11) & ((1ULL << 52) - 1));
    uint64_t mant;
    uint32_t observed_exp;
    bool sign;
    const char *error;

    reset_env(&env);
    env.fp.fr[70] = UINT64_MAX;
    ia64_fpreg_from_spill(&env, 70, low, high);
    error = expect_spill(&env, 70, low, high);
    if (error != NULL) {
        return error;
    }
    if (!ia64_fpreg_get_extended(&env, 70, &sign, &observed_exp, &mant) ||
        !sign || observed_exp != exp || mant != low) {
        return "extended register format was not retained exactly";
    }
    if (env.fp.fr[70] != 0) {
        return "extended register retained an unnecessary compact shadow";
    }
    if (ia64_fpreg_to_binary64(&env, 70) != binary64) {
        return "extended register binary64 extraction used the compact shadow";
    }
    return NULL;
}

static const char *test_natval(void)
{
    CPUIA64State env;
    const char *error;

    reset_env(&env);
    ia64_fpreg_from_spill(&env, 12, 0, IA64_FP_REG_NATVAL_EXP);
    error = expect_spill(&env, 12, 0, IA64_FP_REG_NATVAL_EXP);
    if (error != NULL) {
        return error;
    }
    if (!ia64_fpreg_is_nat(&env, 12) || ia64_fpreg_is_integer(&env, 12)) {
        return "NaTVal tags are inconsistent";
    }
    return NULL;
}

static const char *test_spill_fill_round_trip(void)
{
    static const struct {
        uint64_t low;
        uint64_t high;
    } cases[] = {
        { 0x0000000000000000ULL, 0x0000000000020000ULL },
        { 0xfedcba9876543210ULL, IA64_FP_REG_INTEGER_EXP },
        { 0x0000000000000000ULL, IA64_FP_REG_NATVAL_EXP },
        { 0xc123456789abcdefULL, 0x000000000003ffffULL },
        { 0x8123456789abcdefULL, 0x0000000000023456ULL },
    };
    CPUIA64State env;
    unsigned i;

    reset_env(&env);
    for (i = 0; i < G_N_ELEMENTS(cases); i++) {
        const char *error;

        ia64_fpreg_from_spill(&env, 20 + i, cases[i].low, cases[i].high);
        error = expect_spill(&env, 20 + i, cases[i].low, cases[i].high);
        if (error != NULL) {
            return error;
        }
    }
    return NULL;
}

static const char *test_exact_binary64_fill_compaction(void)
{
    static const Binary64Case cases[] = {
        { 0x0000000000000000ULL, 0x0000000000000000ULL, 0x0000000000000000ULL },
        { 0x8000000000000000ULL, 0x0000000000000000ULL, 0x0000000000020000ULL },
        { 0x0010000000000000ULL, 0x8000000000000000ULL, 0x000000000000fc01ULL },
        { 0xbff8000000000000ULL, 0xc000000000000000ULL, 0x000000000002ffffULL },
        { 0x7fefffffffffffffULL, 0xfffffffffffff800ULL, 0x00000000000103feULL },
    };
    CPUIA64State env;
    unsigned i;

    reset_env(&env);
    for (i = 0; i < G_N_ELEMENTS(cases); i++) {
        const unsigned reg = 20 + i;
        const uint64_t bit = ia64_fpreg_tag_bit(reg);
        const char *error;

        ia64_fpreg_from_spill(&env, reg, cases[i].low, cases[i].high);
        error = expect_spill(&env, reg, cases[i].low, cases[i].high);
        if (error != NULL) {
            return error;
        }
        if (env.fp.fr[reg] != cases[i].value) {
            return fail_u64("compact binary64", env.fp.fr[reg], cases[i].value);
        }
        if ((env.fp.fr_nat[reg / 64] | env.fp.fr_sig[reg / 64] |
             env.fp.fr_ext_valid[reg / 64] | env.fp.fr_int_origin[reg / 64]) &
            bit) {
            return "exact binary64 fill retained a representation tag";
        }
    }
    return NULL;
}

static const char *test_noncompact_fill_encodings(void)
{
    static const struct {
        uint64_t low;
        uint64_t high;
    } cases[] = {
        /* Pseudo-zero, arbitrary unnormal, and non-zero discarded bits. */
        { 0x0000000000000000ULL, 0x000000000000fc01ULL },
        { 0x4000000000000000ULL, 0x000000000000ffffULL },
        { 0x8000000000000001ULL, 0x000000000000ffffULL },
        /* Normalized values just outside the binary64 exponent island. */
        { 0x8000000000000000ULL, 0x000000000000fc00ULL },
        { 0x8000000000000000ULL, 0x00000000000103ffULL },
        /* Exact/misaligned binary64 denormals and both IEEE special classes. */
        { 0x0000000000000800ULL, 0x000000000000fc01ULL },
        { 0x7ffffffffffff800ULL, 0x000000000000fc01ULL },
        { 0x0000000000000001ULL, 0x000000000000fc01ULL },
        { 0x8000000000000000ULL, IA64_FP_REG_SPECIAL_EXP },
        { 0xc000000000000000ULL, IA64_FP_REG_SPECIAL_EXP },
        /* A NaT exponent with a payload and a wide-range normal. */
        { 0x0000000000000800ULL, IA64_FP_REG_NATVAL_EXP },
        { 0x8000000000000000ULL, IA64_FP_REG_NATVAL_EXP },
    };
    CPUIA64State env;
    unsigned i;

    reset_env(&env);
    for (i = 0; i < G_N_ELEMENTS(cases); i++) {
        const unsigned reg = 40 + i;
        bool sign;
        uint32_t exp;
        uint64_t mant;
        const char *error;

        ia64_fpreg_from_spill(&env, reg, cases[i].low, cases[i].high);
        error = expect_spill(&env, reg, cases[i].low, cases[i].high);
        if (error != NULL) {
            return error;
        }
        if (!ia64_fpreg_get_extended(&env, reg, &sign, &exp, &mant) ||
            sign != ((cases[i].high >> 17) & 1) ||
            exp != (cases[i].high & 0x1ffff) || mant != cases[i].low) {
            return "non-binary64 fill lost its exact extended metadata";
        }
    }
    return NULL;
}

static const char *test_random_binary64_spill_fill(void)
{
    CPUIA64State env;
    uint64_t state = 0x9e3779b97f4a7c15ULL;
    unsigned compact_count = 0;
    unsigned denormal_count = 0;
    unsigned integer_count = 0;
    unsigned special_count = 0;
    unsigned i;

    reset_env(&env);
    for (i = 0; i < 100000; i++) {
        const unsigned reg = 3;
        const uint64_t bit = ia64_fpreg_tag_bit(reg);
        uint64_t source_low;
        uint64_t source_high;
        uint64_t filled_low;
        uint64_t filled_high;
        uint64_t value;
        uint32_t binary_exp;
        bool sign;

        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        value = state;
        binary_exp = (value >> 52) & 0x7ff;
        sign = value >> 63;

        ia64_fpreg_from_binary64(&env, 2, value);
        ia64_fpreg_to_spill(&env, 2, &source_low, &source_high);
        ia64_fpreg_from_spill(&env, reg, source_low, source_high);
        ia64_fpreg_to_spill(&env, reg, &filled_low, &filled_high);
        if (filled_low != source_low || filled_high != source_high) {
            snprintf(failure, sizeof(failure),
                     "random round-trip %u changed spill pair", i);
            return failure;
        }

        if (binary_exp == 0x7ff) {
            special_count++;
            if (!(env.fp.fr_ext_valid[0] & bit)) {
                return "random IEEE special did not retain extended state";
            }
        } else if (binary_exp == 0 &&
                   (value & UINT64_C(0x000fffffffffffff)) != 0) {
            denormal_count++;
            if (!(env.fp.fr_ext_valid[0] & bit)) {
                return "random IEEE denormal did not retain extended state";
            }
        } else if (!sign &&
                   (source_high & 0x1ffff) == IA64_FP_REG_INTEGER_EXP) {
            integer_count++;
            if (!ia64_fpreg_is_integer(&env, reg)) {
                return "random integer-exponent fill lost its integer tag";
            }
        } else {
            compact_count++;
            if ((env.fp.fr_nat[0] | env.fp.fr_sig[0] | env.fp.fr_ext_valid[0] |
                 env.fp.fr_int_origin[0]) &
                bit) {
                return "random finite binary64 fill was not compact";
            }
            if (env.fp.fr[reg] != value) {
                return fail_u64("random compact binary64", env.fp.fr[reg],
                                value);
            }
        }
    }

    if (compact_count == 0 || denormal_count == 0 || integer_count == 0 ||
        special_count == 0) {
        return "random corpus did not cover every representation class";
    }
    return NULL;
}

static const char *test_random_spill_pairs(void)
{
    CPUIA64State env;
    uint64_t state = 0xd1b54a32d192ed03ULL;
    unsigned i;

    reset_env(&env);
    for (i = 0; i < 100000; i++) {
        uint64_t source_low;
        uint64_t source_high;
        uint64_t filled_low;
        uint64_t filled_high;

        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        source_low = state;
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        source_high = state & IA64_FP_SPILL_EXP_SIGN_MASK;

        ia64_fpreg_from_spill(&env, 4, source_low, source_high);
        ia64_fpreg_to_spill(&env, 4, &filled_low, &filled_high);
        if (filled_low != source_low || filled_high != source_high) {
            snprintf(failure, sizeof(failure),
                     "random spill pair %u did not round-trip", i);
            return failure;
        }
    }
    return NULL;
}

static const char *test_stale_tags(void)
{
    CPUIA64State env;
    const unsigned reg = 9;
    const uint64_t bit = 1ULL << reg;

    reset_env(&env);
    env.fp.fr_nat[0] |= bit;
    env.fp.fr_sig[0] |= bit;
    env.fp.fr_ext_valid[0] |= bit;
    env.fp.fr_int_origin[0] |= bit;
    env.fp.fr_int_value[reg] = UINT64_MAX;

    ia64_fpreg_from_binary64(&env, reg, 0x4000000000000000ULL);
    if ((env.fp.fr_nat[0] | env.fp.fr_sig[0] | env.fp.fr_ext_valid[0] |
         env.fp.fr_int_origin[0]) &
        bit) {
        return "binary64 write left a stale representation tag";
    }
    if (env.fp.fr_int_value[reg] != 0) {
        return "binary64 write left a stale integer value";
    }

    ia64_fpreg_from_spill(&env, reg, 0x8000000000000000ULL, 0xffff);
    if ((env.fp.fr_nat[0] | env.fp.fr_sig[0] | env.fp.fr_ext_valid[0] |
         env.fp.fr_int_origin[0]) &
        bit) {
        return "exact binary64 fill left a stale representation tag";
    }
    if (env.fp.fr[reg] != IA64_FR_ONE) {
        return "exact binary64 fill did not populate the compact cache";
    }

    ia64_fpreg_from_spill(&env, reg, 0x4000000000000000ULL, 0xffff);
    if ((env.fp.fr_nat[0] | env.fp.fr_sig[0] | env.fp.fr_int_origin[0]) & bit) {
        return "unnormal fill left an incompatible representation tag";
    }
    if (!(env.fp.fr_ext_valid[0] & bit)) {
        return "unnormal fill did not set its extended-valid tag";
    }
    return NULL;
}

int main(void)
{
    static const TestCase tests[] = {
        { "architectural f0/f1", test_constants },
        { "binary32 formats", test_binary32 },
        { "binary64 formats", test_binary64 },
        { "integer significand", test_integer_significand },
        { "extended format", test_extended },
        { "NaTVal", test_natval },
        { "spill fill round-trip", test_spill_fill_round_trip },
        { "exact binary64 fill compaction",
          test_exact_binary64_fill_compaction },
        { "non-compact fill encodings", test_noncompact_fill_encodings },
        { "random binary64 spill fill", test_random_binary64_spill_fill },
        { "random architected spill pairs", test_random_spill_pairs },
        { "stale tag clearing", test_stale_tags },
    };
    unsigned i;
    int status = 0;

    printf("TAP version 13\n");
    printf("1..%zu\n", G_N_ELEMENTS(tests));

    for (i = 0; i < G_N_ELEMENTS(tests); i++) {
        const char *error = tests[i].fn();

        if (error == NULL) {
            printf("ok %u - %s\n", i + 1, tests[i].name);
        } else {
            status = 1;
            printf("not ok %u - %s\n", i + 1, tests[i].name);
            printf("# %s\n", error);
        }
    }
    return status;
}
