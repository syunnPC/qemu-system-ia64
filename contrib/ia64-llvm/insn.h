/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef IA64_LLVM_INSN_H
#define IA64_LLVM_INSN_H

#include "target/ia64/decode/operand.h"

static inline bool ia64_llvm_insn_supported(const Ia64Instruction *insn)
{
    const IA64MemoryOperands *mem = &insn->operands.memory;
    const IA64IntegerOperands *integer = &insn->operands.integer;

    if (!insn->valid || insn->qp || insn->mem_acquire || insn->mem_release) {
        return false;
    }
    if (insn->encoding_class != IA64_ENCODING_DEFINED) {
        return insn->encoding_class == IA64_ENCODING_IGNORED &&
               insn->opcode == IA64_OP_NOP;
    }

    switch (insn->opcode) {
    case IA64_OP_NOP:
        return true;
    case IA64_OP_LD1:
    case IA64_OP_LD2:
    case IA64_OP_LD4:
    case IA64_OP_LD8:
        return mem->destination &&
               (!(insn->imm_base_update || insn->reg_base_update) ||
                (mem->base && mem->base != mem->destination));
    case IA64_OP_ST1:
    case IA64_OP_ST2:
    case IA64_OP_ST4:
    case IA64_OP_ST8:
        return !insn->reg_base_update && (!insn->imm_base_update || mem->base);
    case IA64_OP_CMP_EQ:
    case IA64_OP_CMP_LT:
    case IA64_OP_CMP_LTU:
    case IA64_OP_CMP_EQ_IMM:
    case IA64_OP_CMP_LT_IMM:
    case IA64_OP_CMP_LTU_IMM:
    case IA64_OP_CMP4_EQ:
    case IA64_OP_CMP4_LT:
    case IA64_OP_CMP4_LTU:
    case IA64_OP_CMP4_EQ_IMM:
    case IA64_OP_CMP4_LT_IMM:
    case IA64_OP_CMP4_LTU_IMM:
        return insn->pred_update == IA64_PRED_UPDATE_NORMAL &&
               integer->predicate1 != integer->predicate2;
    case IA64_OP_ADD:
    case IA64_OP_ADD_ONE:
    case IA64_OP_SUB:
    case IA64_OP_SUB_ONE:
    case IA64_OP_SUB_IMM:
    case IA64_OP_ADDS:
    case IA64_OP_ADDL:
    case IA64_OP_AND:
    case IA64_OP_ANDCM:
    case IA64_OP_OR:
    case IA64_OP_XOR:
    case IA64_OP_AND_IMM:
    case IA64_OP_ANDCM_IMM:
    case IA64_OP_OR_IMM:
    case IA64_OP_XOR_IMM:
    case IA64_OP_ADDP4:
    case IA64_OP_ADDP4_IMM:
    case IA64_OP_SHLADDP4:
    case IA64_OP_SHRP_IMM:
    case IA64_OP_SHLADD:
    case IA64_OP_SHL:
    case IA64_OP_SHR:
    case IA64_OP_SHRU:
    case IA64_OP_SHL_IMM:
    case IA64_OP_SHR_IMM:
    case IA64_OP_SHRU_IMM:
    case IA64_OP_EXTR:
    case IA64_OP_EXTRU:
    case IA64_OP_DEPZ:
    case IA64_OP_DEPZ_IMM:
    case IA64_OP_DEP:
    case IA64_OP_DEP_IMM:
    case IA64_OP_SXT1:
    case IA64_OP_SXT2:
    case IA64_OP_SXT4:
    case IA64_OP_ZXT1:
    case IA64_OP_ZXT2:
    case IA64_OP_ZXT4:
    case IA64_OP_MIX4_L:
    case IA64_OP_MIX4_R:
    case IA64_OP_GETF_SIG:
        return insn->operands.common.destination != 0;
    default:
        return false;
    }
}

#endif
