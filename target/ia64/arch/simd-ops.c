/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Pure IA-64 packed-integer operations.  This file deliberately has no CPU,
 * translator, helper-ABI, or accelerator dependency.
 */

#include "qemu/osdep.h"
#include "arch/simd-ops.h"

static uint64_t simd_lane(uint64_t value, int index, int bits)
{
    return (value >> (index * bits)) & (((uint64_t)1 << bits) - 1);
}

static int64_t simd_signed_lane(uint64_t value, int index, int bits)
{
    uint64_t lane = simd_lane(value, index, bits);

    return (int64_t)(lane << (64 - bits)) >> (64 - bits);
}

uint64_t ia64_simd_pavg_value(uint32_t op_sel, uint64_t a, uint64_t b)
{
    bool sub = op_sel == 2 || op_sel == 3;
    bool raz = op_sel == 4 || op_sel == 5;
    int bits = (op_sel == 0 || op_sel == 2 || op_sel == 4) ? 8 : 16;
    int lanes = 64 / bits;
    uint64_t mask = ((uint64_t)1 << bits) - 1;
    uint64_t ext_mask = ((uint64_t)1 << (bits + 1)) - 1;
    uint64_t result = 0;

    for (int i = 0; i < lanes; i++) {
        uint64_t left = simd_lane(a, i, bits);
        uint64_t right = simd_lane(b, i, bits);
        uint64_t temp;
        uint64_t lane;

        if (sub) {
            temp = (left - right) & ext_mask;
            lane = ((temp >> 1) | (temp & 1)) & mask;
        } else if (raz) {
            lane = ((left + right + 1) >> 1) & mask;
        } else {
            temp = left + right;
            lane = ((temp >> 1) | (temp & 1)) & mask;
        }
        result |= lane << (i * bits);
    }
    return result;
}

uint64_t ia64_simd_pcmp_value(uint32_t op_sel, uint64_t a, uint64_t b)
{
    int bits;
    int lanes;
    uint64_t mask;
    bool is_gt = (op_sel & 1) != 0;
    uint64_t result = 0;

    switch (op_sel) {
    case 0:
    case 1:
        bits = 8;
        break;
    case 2:
    case 3:
        bits = 16;
        break;
    default:
        bits = 32;
        break;
    }
    lanes = 64 / bits;
    mask = ((uint64_t)1 << bits) - 1;
    for (int i = 0; i < lanes; i++) {
        uint64_t left = simd_lane(a, i, bits);
        uint64_t right = simd_lane(b, i, bits);
        uint64_t lane;

        if (is_gt) {
            lane = simd_signed_lane(a, i, bits) > simd_signed_lane(b, i, bits)
                       ? mask
                       : 0;
        } else {
            lane = left == right ? mask : 0;
        }
        result |= lane << (i * bits);
    }
    return result;
}

uint64_t ia64_simd_pminmax_value(uint32_t op_sel, uint64_t a, uint64_t b)
{
    int bits = op_sel <= 1 ? 8 : 16;
    int lanes = 64 / bits;
    bool is_max = op_sel == 0 || op_sel == 2;
    uint64_t result = 0;

    for (int i = 0; i < lanes; i++) {
        uint64_t left = simd_lane(a, i, bits);
        uint64_t right = simd_lane(b, i, bits);
        uint64_t lane;

        if (bits == 16) {
            int64_t signed_left = simd_signed_lane(a, i, bits);
            int64_t signed_right = simd_signed_lane(b, i, bits);

            lane = is_max ? (signed_left > signed_right ? left : right)
                          : (signed_left < signed_right ? left : right);
        } else {
            lane = is_max ? (left > right ? left : right)
                          : (left < right ? left : right);
        }
        result |= lane << (i * bits);
    }
    return result;
}

static uint64_t simd_pmpy2_result(uint64_t a, uint64_t b, bool right_form)
{
    int first_lane = right_form ? 0 : 1;
    uint64_t result = 0;

    for (int output = 0; output < 2; output++) {
        int lane = first_lane + output * 2;
        int32_t product =
            (int16_t)simd_lane(a, lane, 16) * (int16_t)simd_lane(b, lane, 16);

        result |= (uint64_t)(uint32_t)product << (output * 32);
    }
    return result;
}

uint64_t ia64_simd_pmpy_value(uint32_t op_sel, uint64_t a, uint64_t b,
                              uint32_t shift)
{
    uint64_t result = 0;

    if (op_sel <= 1) {
        return simd_pmpy2_result(a, b, op_sel == 1);
    }
    for (int i = 0; i < 4; i++) {
        uint64_t left = simd_lane(a, i, 16);
        uint64_t right = simd_lane(b, i, 16);
        uint64_t product;

        if (op_sel == 2) {
            int64_t signed_product = (int64_t)(int16_t)left * (int16_t)right;

            product = signed_product >> shift;
        } else {
            product = (left * right) >> shift;
        }
        result |= (product & UINT16_MAX) << (i * 16);
    }
    return result;
}

uint64_t ia64_simd_psad1_value(uint64_t a, uint64_t b)
{
    uint64_t sum = 0;

    for (int i = 0; i < 8; i++) {
        uint64_t left = simd_lane(a, i, 8);
        uint64_t right = simd_lane(b, i, 8);

        sum += left > right ? left - right : right - left;
    }
    return sum;
}

uint64_t ia64_simd_pshladd2_value(uint64_t a, uint64_t b, uint32_t count)
{
    uint64_t result = 0;

    g_assert(count >= 1 && count <= 3);
    for (int i = 0; i < 4; i++) {
        int64_t left = simd_signed_lane(a, i, 16);
        int64_t right = simd_signed_lane(b, i, 16);
        int64_t shifted = left * (INT64_C(1) << count);
        int64_t lane;

        if (shifted > INT16_MAX) {
            lane = INT16_MAX;
        } else if (shifted < INT16_MIN) {
            lane = INT16_MIN;
        } else {
            lane = shifted + right;
            lane = MIN(lane, INT16_MAX);
            lane = MAX(lane, INT16_MIN);
        }
        result |= ((uint64_t)lane & UINT16_MAX) << (i * 16);
    }
    return result;
}

uint64_t ia64_simd_mux_value(uint32_t op_sel, uint64_t value, uint32_t imm)
{
    static const uint8_t mux1_perms[16][8] = {
        [0x0] = { 0, 0, 0, 0, 0, 0, 0, 0 }, /* @brcst */
        [0x8] = { 0, 4, 2, 6, 1, 5, 3, 7 }, /* @mix */
        [0x9] = { 0, 4, 1, 5, 2, 6, 3, 7 }, /* @shuf */
        [0xa] = { 0, 2, 4, 6, 1, 3, 5, 7 }, /* @alt */
        [0xb] = { 7, 6, 5, 4, 3, 2, 1, 0 }, /* @rev */
    };
    uint64_t result = 0;

    if (op_sel == 0) {
        const uint8_t *permutation = mux1_perms[imm & 0xf];

        for (int i = 0; i < 8; i++) {
            result |= simd_lane(value, permutation[i], 8) << (i * 8);
        }
    } else {
        for (int i = 0; i < 4; i++) {
            int lane = (imm >> (i * 2)) & 3;

            result |= simd_lane(value, lane, 16) << (i * 16);
        }
    }
    return result;
}

uint64_t ia64_simd_mix_value(uint32_t op_sel, uint64_t a, uint64_t b)
{
    int bits;
    bool left;
    uint64_t result = 0;

    switch (op_sel) {
    case 0:
    case 1:
        bits = 8;
        left = op_sel == 0;
        break;
    case 2:
    case 3:
        bits = 16;
        left = op_sel == 2;
        break;
    default:
        bits = 32;
        left = op_sel == 4;
        break;
    }
    for (int i = 0; i < 32 / bits; i++) {
        int lane = 2 * i + (left ? 1 : 0);

        result |= simd_lane(a, lane, bits) << ((2 * i + 1) * bits);
        result |= simd_lane(b, lane, bits) << ((2 * i) * bits);
    }
    return result;
}

uint64_t ia64_simd_unpack_value(uint32_t op_sel, uint64_t a, uint64_t b)
{
    int bits;
    bool low;
    int half;
    int base;
    uint64_t result = 0;

    switch (op_sel) {
    case 0:
    case 1:
        bits = 8;
        low = op_sel == 1;
        break;
    case 2:
    case 3:
        bits = 16;
        low = op_sel == 3;
        break;
    default:
        bits = 32;
        low = op_sel == 5;
        break;
    }
    half = 32 / bits;
    base = low ? 0 : half;
    for (int i = 0; i < half; i++) {
        uint64_t left = simd_lane(a, base + i, bits);
        uint64_t right = simd_lane(b, base + i, bits);

        result |= right << ((2 * i) * bits);
        result |= left << ((2 * i + 1) * bits);
    }
    return result;
}

uint64_t ia64_simd_pack_value(uint32_t op_sel, uint64_t a, uint64_t b)
{
    int input_bits;
    int output_bits;
    bool unsigned_saturation;
    int64_t output_max;
    int64_t output_min;
    uint64_t unsigned_max;
    uint64_t sources[2] = { a, b };
    uint64_t result = 0;
    int output_lane = 0;

    switch (op_sel) {
    case 0:
        input_bits = 16;
        output_bits = 8;
        unsigned_saturation = false;
        break;
    case 1:
        input_bits = 16;
        output_bits = 8;
        unsigned_saturation = true;
        break;
    default:
        input_bits = 32;
        output_bits = 16;
        unsigned_saturation = false;
        break;
    }
    output_max = (1LL << (output_bits - 1)) - 1;
    output_min = -(1LL << (output_bits - 1));
    unsigned_max = (1ULL << output_bits) - 1;
    for (int source = 0; source < 2; source++) {
        int lanes = 64 / input_bits;

        for (int i = 0; i < lanes; i++, output_lane++) {
            int64_t value = simd_signed_lane(sources[source], i, input_bits);
            uint64_t lane;

            if (unsigned_saturation) {
                if (value < 0) {
                    value = 0;
                } else if ((uint64_t)value > unsigned_max) {
                    value = unsigned_max;
                }
            } else if (value > output_max) {
                value = output_max;
            } else if (value < output_min) {
                value = output_min;
            }
            lane = (uint64_t)value & ((1ULL << output_bits) - 1);
            result |= lane << (output_lane * output_bits);
        }
    }
    return result;
}

uint64_t ia64_simd_czx_value(uint32_t op_sel, uint64_t value)
{
    int bits = op_sel <= 1 ? 8 : 16;
    int max_count = 64 / bits;
    int index = max_count;

    if (op_sel == 0 || op_sel == 2) {
        for (int i = max_count - 1; i >= 0; i--) {
            if (simd_lane(value, i, bits) == 0) {
                index = max_count - 1 - i;
                break;
            }
        }
    } else {
        for (int i = 0; i < max_count; i++) {
            if (simd_lane(value, i, bits) == 0) {
                index = i;
                break;
            }
        }
    }
    return index;
}

uint64_t ia64_simd_sum_value(uint64_t a, uint64_t b)
{
    uint64_t result = 0;

    for (int i = 0; i < 2; i++) {
        uint64_t left = simd_lane(a, 2 * i, 16) + simd_lane(a, 2 * i + 1, 16);
        uint64_t right = simd_lane(b, 2 * i, 16) + simd_lane(b, 2 * i + 1, 16);

        result |= left << ((2 + i) * 16);
        result |= right << (i * 16);
    }
    return result;
}
