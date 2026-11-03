/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * IA-64 floating-point register representation helpers.
 */

#include "qemu/osdep.h"
#include "cpu.h"
#include "fpreg.h"

#define IA64_FP_SINGLE_EXP_BASE 0x0ff80
#define IA64_FP_SINGLE_FRAC_MASK ((1ULL << 23) - 1)
#define IA64_FP_DOUBLE_EXP_BASE 0x0fc00
#define IA64_FP_DOUBLE_MAX_EXP 0x103fe
#define IA64_FP_DOUBLE_FRAC_MASK ((1ULL << 52) - 1)
#define IA64_FP_DOUBLE_LOW_MASK ((1ULL << 11) - 1)

bool ia64_fpreg_get_extended(const CPUIA64State *env, unsigned reg, bool *sign,
                             uint32_t *exp, uint64_t *mant)
{
    if (reg <= 1 || reg >= IA64_FR_COUNT ||
        !((env->fp.fr_ext_valid[reg / 64] >> (reg % 64)) & 1)) {
        return false;
    }

    *sign = (env->fp.fr_ext_sign[reg / 64] >> (reg % 64)) & 1;
    *exp = env->fp.fr_ext_exp[reg];
    *mant = env->fp.fr_ext_mant[reg];
    return true;
}

static void binary64_to_register_format(uint64_t value, uint64_t *sig,
                                        uint32_t *exp, bool *sign)
{
    uint64_t frac = value & IA64_FP_DOUBLE_FRAC_MASK;
    uint32_t binary_exp = (value >> 52) & 0x7ff;

    *sign = value >> 63;
    if (binary_exp == 0) {
        if (frac == 0) {
            *exp = 0;
            *sig = 0;
        } else {
            *exp = IA64_FP_DOUBLE_EXP_BASE + 1;
            *sig = frac << 11;
        }
    } else if (binary_exp == 0x7ff) {
        *exp = IA64_FP_REG_SPECIAL_EXP;
        *sig = IA64_FP_SIGNIFICAND_INTEGER_BIT | (frac << 11);
    } else {
        *exp = IA64_FP_DOUBLE_EXP_BASE + binary_exp;
        *sig = IA64_FP_SIGNIFICAND_INTEGER_BIT | (frac << 11);
    }
}

static void binary32_to_register_format(uint32_t value, uint64_t *sig,
                                        uint32_t *exp, bool *sign)
{
    uint32_t frac = value & IA64_FP_SINGLE_FRAC_MASK;
    uint32_t binary_exp = (value >> 23) & 0xff;

    *sign = value >> 31;
    if (binary_exp == 0) {
        if (frac == 0) {
            *exp = 0;
            *sig = 0;
        } else {
            *exp = IA64_FP_SINGLE_EXP_BASE + 1;
            *sig = (uint64_t)frac << 40;
        }
    } else if (binary_exp == 0xff) {
        *exp = IA64_FP_REG_SPECIAL_EXP;
        *sig = IA64_FP_SIGNIFICAND_INTEGER_BIT | ((uint64_t)frac << 40);
    } else {
        *exp = IA64_FP_SINGLE_EXP_BASE + binary_exp;
        *sig = IA64_FP_SIGNIFICAND_INTEGER_BIT | ((uint64_t)frac << 40);
    }
}

static uint32_t register_format_to_binary32(uint64_t sig, uint32_t exp,
                                            bool sign)
{
    uint32_t value = (uint32_t)sign << 31;

    if (sig & IA64_FP_SIGNIFICAND_INTEGER_BIT) {
        value |= (((exp >> 9) & 0x80) | (exp & 0x7f)) << 23;
    }
    value |= (sig >> 40) & IA64_FP_SINGLE_FRAC_MASK;
    return value;
}

static uint64_t register_format_to_binary64(uint64_t sig, uint32_t exp,
                                            bool sign)
{
    uint64_t value = (uint64_t)sign << 63;

    if (sig & IA64_FP_SIGNIFICAND_INTEGER_BIT) {
        value |= (uint64_t)(((exp >> 6) & 0x400) | (exp & 0x3ff)) << 52;
    }
    value |= (sig >> 11) & IA64_FP_DOUBLE_FRAC_MASK;
    return value;
}

/*
 * Return a compact binary64 value only when converting it back with
 * binary64_to_register_format() reproduces every architected bit.  Special,
 * wide-range, pseudo-zero, and arbitrary unnormal encodings must retain their
 * exact extended metadata instead.  Keep binary64 denormals extended too:
 * routing them through compact float64 conversions would report a SoftFloat
 * input-denormal event and could change IA-64 exception priority.
 */
static bool register_format_to_binary64_exact(uint64_t sig, uint32_t exp,
                                              bool sign, uint64_t *value)
{
    uint64_t result = (uint64_t)sign << 63;

    if (likely((sig &
                (IA64_FP_SIGNIFICAND_INTEGER_BIT | IA64_FP_DOUBLE_LOW_MASK)) ==
                   IA64_FP_SIGNIFICAND_INTEGER_BIT &&
               (uint32_t)(exp - (IA64_FP_DOUBLE_EXP_BASE + 1)) <=
                   IA64_FP_DOUBLE_MAX_EXP - (IA64_FP_DOUBLE_EXP_BASE + 1))) {
        result |= (uint64_t)(exp - IA64_FP_DOUBLE_EXP_BASE) << 52;
        result |= (sig >> 11) & IA64_FP_DOUBLE_FRAC_MASK;
    } else if (sig == 0 && exp == 0) {
        /* Canonical signed zero. */
    } else {
        return false;
    }

    *value = result;
    return true;
}

void ia64_fpreg_to_spill(const CPUIA64State *env, unsigned reg, uint64_t *low,
                         uint64_t *high)
{
    uint32_t exp;
    bool sign;

    g_assert(reg < IA64_FR_COUNT);

    if (ia64_fpreg_is_nat(env, reg)) {
        *low = 0;
        exp = IA64_FP_REG_NATVAL_EXP;
        sign = false;
    } else if (ia64_fpreg_get_extended(env, reg, &sign, &exp, low)) {
        /* Exact register-format value was retained by fill or arithmetic. */
    } else if (ia64_fpreg_is_integer(env, reg)) {
        *low = env->fp.fr[reg];
        exp = IA64_FP_REG_INTEGER_EXP;
        sign = false;
    } else {
        binary64_to_register_format(reg == IA64_FR_ZERO_INDEX ? 0
                                    : reg == IA64_FR_ONE_INDEX
                                        ? IA64_FR_ONE
                                        : env->fp.fr[reg],
                                    low, &exp, &sign);
    }

    *high = (exp & 0x1ffff) | ((uint64_t)sign << 17);
}

void ia64_fpreg_from_spill(CPUIA64State *env, unsigned reg, uint64_t low,
                           uint64_t high)
{
    uint64_t compact;
    uint32_t exp;
    bool sign;
    uint64_t bit;

    g_assert(reg < IA64_FR_COUNT);
    if (reg <= 1) {
        return;
    }

    high &= IA64_FP_SPILL_EXP_SIGN_MASK;
    exp = high & 0x1ffff;
    sign = (high >> 17) & 1;
    bit = ia64_fpreg_tag_bit(reg);
    ia64_fpreg_clear_tags(env, reg);

    if (!sign && exp == IA64_FP_REG_NATVAL_EXP && low == 0) {
        env->fp.fr[reg] = 0;
        env->fp.fr_nat[reg / 64] |= bit;
    } else if (!sign && exp == IA64_FP_REG_INTEGER_EXP) {
        env->fp.fr[reg] = low;
        env->fp.fr_sig[reg / 64] |= bit;
        env->fp.fr_int_value[reg] = low;
        env->fp.fr_int_origin[reg / 64] |= bit;
    } else if (register_format_to_binary64_exact(low, exp, sign, &compact)) {
        env->fp.fr[reg] = compact;
    } else {
        /*
         * fr_ext_* is authoritative while fr_ext_valid is set.  Computing a
         * rounded binary64 shadow here is both lossy and wasted: every reader
         * checks the representation tag before consulting fr[].
         */
        env->fp.fr[reg] = 0;
        env->fp.fr_ext_mant[reg] = low;
        env->fp.fr_ext_exp[reg] = exp;
        if (sign) {
            env->fp.fr_ext_sign[reg / 64] |= bit;
        } else {
            env->fp.fr_ext_sign[reg / 64] &= ~bit;
        }
        env->fp.fr_ext_valid[reg / 64] |= bit;
    }
    ia64_fpreg_mark_written(env, reg);
}

void ia64_fpreg_from_binary32(CPUIA64State *env, unsigned reg, uint32_t value)
{
    uint64_t sig;
    uint32_t exp;
    bool sign;

    binary32_to_register_format(value, &sig, &exp, &sign);
    ia64_fpreg_from_spill(env, reg, sig,
                          (uint64_t)exp | ((uint64_t)sign << 17));
}

uint32_t ia64_fpreg_to_binary32(const CPUIA64State *env, unsigned reg)
{
    uint64_t low;
    uint64_t high;

    ia64_fpreg_to_spill(env, reg, &low, &high);
    return register_format_to_binary32(low, high & 0x1ffff, (high >> 17) & 1);
}

uint64_t ia64_fpreg_to_binary64(const CPUIA64State *env, unsigned reg)
{
    uint64_t low;
    uint64_t high;

    ia64_fpreg_to_spill(env, reg, &low, &high);
    return register_format_to_binary64(low, high & 0x1ffff, (high >> 17) & 1);
}
