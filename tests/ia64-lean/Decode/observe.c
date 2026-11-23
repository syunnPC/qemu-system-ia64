/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Project decoder fields for the Lean encoding checks.
 */

#include <inttypes.h>
#include <stdio.h>

#include "target/ia64/decode/decode.h"

static const char *opcode_name(Ia64Opcode opcode)
{
    switch (opcode) {
#define IA64_OPCODE(name, ...) case IA64_OP_##name: return #name;
#include "target/ia64/decode/opcode.inc"
#undef IA64_OPCODE
    default:
        return "UNKNOWN";
    }
}

static const char *encoding_name(Ia64EncodingClass encoding)
{
    switch (encoding) {
    case IA64_ENCODING_DEFINED:
        return "defined";
    case IA64_ENCODING_IGNORED:
        return "ignored";
    case IA64_ENCODING_RESERVED:
        return "reserved";
    case IA64_ENCODING_RESERVED_IF_QP:
        return "reserved_if_qp";
    case IA64_ENCODING_RESERVED_IF_QP_B:
        return "reserved_if_qp_b";
    case IA64_ENCODING_CONSTANT_ZERO_VIOLATION:
        return "constant_zero_violation";
    }
    return "unknown";
}

int main(void)
{
    static const IA64SlotUnit units[256] = {
        ['M'] = IA64_UNIT_M,
        ['I'] = IA64_UNIT_I,
        ['B'] = IA64_UNIT_B,
        ['F'] = IA64_UNIT_F,
        ['X'] = IA64_UNIT_X,
        ['L'] = IA64_UNIT_L,
    };
    char unit;
    unsigned template_code;
    uint64_t raw, left;
    int parsed;

    while ((parsed = scanf(" %c %x %" SCNx64 " %" SCNx64,
                           &unit, &template_code, &raw, &left)) == 4) {
        const uint64_t address = 0x1234567812345670;
        const uint64_t slots[] = { 0, left, raw };
        const unsigned slot = template_code ? 1 : 2;
        bool skip = false;
        Ia64Instruction i = ia64_decode_insn(units[(unsigned char)unit],
                                             template_code ? left : raw,
                                             address, slot);

        if (template_code) {
            ia64_apply_mlx_long_fixup(template_code, slots, slot, &i, &skip);
        }

        printf("%s %s %u %u %u %u %u %u %u %u %u %" PRIu64
               " %u %u %u %u %u %u %u %u %u %u\n",
               opcode_name(i.opcode), encoding_name(i.encoding_class), i.qp,
               i.operands.decoder.r1, i.operands.decoder.r2,
               i.operands.decoder.r3, i.operands.decoder.p1,
               i.operands.decoder.p2, i.operands.decoder.b1,
               i.operands.decoder.b2, i.operands.decoder.sf,
               (uint64_t)i.operands.decoder.imm, i.compare_unc,
               i.reg_base_update, i.imm_base_update, i.mem_acquire,
               i.mem_release, i.check_fp, skip, i.operands.decoder.fp_precision,
               i.pred_update, i.clear_p2_before_predicate);
    }

    return parsed == EOF && !ferror(stdin) && !ferror(stdout) ? 0 : 1;
}
