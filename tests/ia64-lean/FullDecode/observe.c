/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Compare a decoded record from native C with the symbolic C adapter.
 */

#include <inttypes.h>
#include <stdio.h>

#include "target/ia64/decode/decode.h"

int main(void)
{
    unsigned unit, slot;
    uint64_t raw, address;
    int parsed;

    while ((parsed = scanf("%u %" SCNx64 " %" SCNx64 " %u",
                           &unit, &raw, &address, &slot)) == 4) {
        Ia64Instruction insn = ia64_decode_insn(unit, raw, address, slot);
        const IA64DecodedOperands *op = &insn.operands.decoder;
        const uint64_t values[] = {
            insn.opcode, 1, insn.unit, insn.raw, insn.address, insn.slot,
            insn.qp, op->r1, op->r2, op->r3, op->p1, op->p2, op->b1,
            op->b2, op->sf, op->fp_precision, (uint64_t)op->imm,
            insn.encoding_class, insn.pred_update, insn.hint_m_reg_increment,
            insn.reg_base_update, insn.imm_base_update, insn.mem_acquire,
            insn.mem_release, insn.compare_unc, insn.clear_p2_before_predicate,
            insn.check_fp, insn.fp_load_speculative, insn.fp_load_advanced,
            insn.fp_load_check, insn.fp_load_check_clear, insn.probe_fault,
            insn.probe_imm, insn.placement_illegal, insn.reserved_field,
            insn.valid, insn.ctx != NULL,
        };
        for (unsigned i = 0; i < sizeof(values) / sizeof(values[0]); i++) {
            printf("%s%" PRIu64, i == 0 ? "" : " ", values[i]);
        }
        putchar('\n');
    }

    return parsed == EOF && !ferror(stdin) && !ferror(stdout) ? 0 : 1;
}
