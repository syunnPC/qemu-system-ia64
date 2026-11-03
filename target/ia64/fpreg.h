/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * IA-64 floating-point register representation helpers.
 */

#ifndef TARGET_IA64_FPREG_H
#define TARGET_IA64_FPREG_H

#include <stdbool.h>
#include <stdint.h>

typedef struct CPUArchState CPUIA64State;

#define IA64_FP_REG_INTEGER_EXP 0x1003e
#define IA64_FP_REG_NATVAL_EXP  0x1fffe
#define IA64_FP_REG_SPECIAL_EXP 0x1ffff
#define IA64_FP_SIGNIFICAND_INTEGER_BIT (1ULL << 63)
#define IA64_FP_SPILL_EXP_SIGN_MASK 0x3ffffULL

/*
 * Keep the padding in floatx80 initialized.  Building the value with the
 * generic positional compound literal can make the host compiler assemble
 * the padded return value through overlapping stack stores and loads.
 */
static inline floatx80 ia64_make_floatx80(uint16_t exp, uint64_t mant)
{
    floatx80 value = { 0 };

    value.low = mant;
    value.high = exp;
    return value;
}

/*
 * These tag operations sit on every floating-point helper path.  Keep them
 * inline even though the full spill/fill conversion lives in fpreg.c; putting
 * these operations behind an out-of-line ABI measurably increases the cost of
 * ordinary FP instructions.
 *
 * Include cpu.h before this header so CPUIA64State is complete.
 */
static inline uint64_t ia64_fpreg_tag_bit(unsigned reg)
{
    return 1ULL << (reg % 64);
}

static inline void ia64_fpreg_mark_written(CPUIA64State *env, unsigned reg)
{
    if (reg <= 1) {
        return;
    }

    env->psr |= reg >= 32 ? IA64_PSR_MFH : IA64_PSR_MFL;
    if (reg >= 32) {
        env->fp.rotating_fr_live = true;
    }
}

static inline void ia64_fpreg_clear_tags(CPUIA64State *env, unsigned reg)
{
    uint64_t mask = ~ia64_fpreg_tag_bit(reg);

    env->fp.fr_nat[reg / 64] &= mask;
    env->fp.fr_sig[reg / 64] &= mask;
    env->fp.fr_ext_valid[reg / 64] &= mask;
    env->fp.fr_int_origin[reg / 64] &= mask;
    env->fp.fr_int_value[reg] = 0;
}

static inline bool ia64_fpreg_is_nat(const CPUIA64State *env, unsigned reg)
{
    return reg > 1 && reg < IA64_FR_COUNT &&
           ((env->fp.fr_nat[reg / 64] >> (reg % 64)) & 1);
}

static inline bool ia64_fpreg_is_integer(const CPUIA64State *env, unsigned reg)
{
    return reg > 1 && reg < IA64_FR_COUNT &&
           ((env->fp.fr_sig[reg / 64] >> (reg % 64)) & 1);
}

static inline void ia64_fpreg_from_binary64(CPUIA64State *env, unsigned reg,
                                            uint64_t value)
{
    g_assert(reg < IA64_FR_COUNT);
    if (reg <= 1) {
        return;
    }

    ia64_fpreg_clear_tags(env, reg);
    env->fp.fr[reg] = value;
    ia64_fpreg_mark_written(env, reg);
}

/*
 * Convert between the internal tagged register cache and the architected
 * 128-bit spill/fill representation.  Bits above bit 17 of high are reserved
 * and are ignored on fill, as required by the spill format.
 */
void ia64_fpreg_to_spill(const CPUIA64State *env, unsigned reg, uint64_t *low,
                         uint64_t *high);
void ia64_fpreg_from_spill(CPUIA64State *env, unsigned reg, uint64_t low,
                           uint64_t high);

/* Exact extended-format state is retained separately from the float64 cache. */
bool ia64_fpreg_get_extended(const CPUIA64State *env, unsigned reg, bool *sign,
                             uint32_t *exp, uint64_t *mant);

/* IEEE interchange helpers used by setf/getf and the pure unit test. */
void ia64_fpreg_from_binary32(CPUIA64State *env, unsigned reg, uint32_t value);
uint32_t ia64_fpreg_to_binary32(const CPUIA64State *env, unsigned reg);
uint64_t ia64_fpreg_to_binary64(const CPUIA64State *env, unsigned reg);

#endif
