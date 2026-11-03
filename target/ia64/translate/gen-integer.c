/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * IA-64 integer ALU and compare instruction generation.
 */

#include "qemu/osdep.h"

#include "exec/helper-proto.h"
#include "exec/helper-gen.h"
#include "tcg/tcg-op.h"

#include "target/ia64/decode/decode.h"
#include "target/ia64/translate/translate.h"

static void ia64_gen_addp4_result(TCGv_i64 dst, TCGv_i64 src1, TCGv_i64 src3)
{
    TCGv_i64 low = tcg_temp_new_i64();
    TCGv_i64 high = tcg_temp_new_i64();

    tcg_gen_add_i64(low, src1, src3);
    tcg_gen_ext32u_i64(low, low);
    tcg_gen_shri_i64(high, src3, 30);
    tcg_gen_andi_i64(high, high, 3);
    tcg_gen_shli_i64(high, high, 61);
    tcg_gen_or_i64(dst, low, high);
}

static void ia64_gen_native_integer_write(const Ia64Instruction *insn)
{
    const IA64IntegerOperands *op = &insn->operands.integer;
    TCGv_i64 tmp;
    TCGv_i64 tmp2;

    if (op->destination == 0) {
        return;
    }

    switch (insn->opcode) {
    case IA64_OP_ADDS:
    case IA64_OP_ADDL:
        tcg_gen_addi_i64(cpu_gr[op->destination], ia64_gr_src(op->source2),
                         op->immediate);
        break;
    case IA64_OP_SHLADD:
        tmp = tcg_temp_new_i64();
        tcg_gen_shli_i64(tmp, ia64_gr_src(op->source1), op->immediate);
        tcg_gen_add_i64(cpu_gr[op->destination], tmp, ia64_gr_src(op->source2));
        break;
    case IA64_OP_ADD:
        tcg_gen_add_i64(cpu_gr[op->destination], ia64_gr_src(op->source1),
                        ia64_gr_src(op->source2));
        break;
    case IA64_OP_ADD_ONE:
        tmp = tcg_temp_new_i64();
        tcg_gen_add_i64(tmp, ia64_gr_src(op->source1),
                        ia64_gr_src(op->source2));
        tcg_gen_addi_i64(cpu_gr[op->destination], tmp, 1);
        break;
    case IA64_OP_SUB:
        tcg_gen_sub_i64(cpu_gr[op->destination], ia64_gr_src(op->source1),
                        ia64_gr_src(op->source2));
        break;
    case IA64_OP_SUB_ONE:
        tmp = tcg_temp_new_i64();
        tcg_gen_sub_i64(tmp, ia64_gr_src(op->source1),
                        ia64_gr_src(op->source2));
        tcg_gen_subi_i64(cpu_gr[op->destination], tmp, 1);
        break;
    case IA64_OP_AND:
        tcg_gen_and_i64(cpu_gr[op->destination], ia64_gr_src(op->source1),
                        ia64_gr_src(op->source2));
        break;
    case IA64_OP_ANDCM:
        tmp = tcg_temp_new_i64();
        tcg_gen_not_i64(tmp, ia64_gr_src(op->source2));
        tcg_gen_and_i64(cpu_gr[op->destination], ia64_gr_src(op->source1), tmp);
        break;
    case IA64_OP_OR:
        tcg_gen_or_i64(cpu_gr[op->destination], ia64_gr_src(op->source1),
                       ia64_gr_src(op->source2));
        break;
    case IA64_OP_XOR:
        tcg_gen_xor_i64(cpu_gr[op->destination], ia64_gr_src(op->source1),
                        ia64_gr_src(op->source2));
        break;
    case IA64_OP_SHL:
        tmp = tcg_temp_new_i64();
        tcg_gen_andi_i64(tmp, ia64_gr_src(op->source1), 0x3f);
        TCGv_i64 shifted_l = tcg_temp_new_i64();
        tcg_gen_shl_i64(shifted_l, ia64_gr_src(op->source2), tmp);
        tcg_gen_movcond_i64(TCG_COND_GTU, cpu_gr[op->destination],
                            ia64_gr_src(op->source1), tcg_constant_i64(63),
                            tcg_constant_i64(0), shifted_l);
        break;
    case IA64_OP_SHRU:
        tmp = tcg_temp_new_i64();
        tcg_gen_andi_i64(tmp, ia64_gr_src(op->source1), 0x3f);
        TCGv_i64 shifted_ru = tcg_temp_new_i64();
        tcg_gen_shr_i64(shifted_ru, ia64_gr_src(op->source2), tmp);
        tcg_gen_movcond_i64(TCG_COND_GTU, cpu_gr[op->destination],
                            ia64_gr_src(op->source1), tcg_constant_i64(63),
                            tcg_constant_i64(0), shifted_ru);
        break;
    case IA64_OP_SHR:
        tmp = tcg_temp_new_i64();
        tcg_gen_movcond_i64(TCG_COND_GTU, tmp, ia64_gr_src(op->source1),
                            tcg_constant_i64(63), tcg_constant_i64(63),
                            ia64_gr_src(op->source1));
        tcg_gen_sar_i64(cpu_gr[op->destination], ia64_gr_src(op->source2), tmp);
        break;
    case IA64_OP_SHL_IMM:
        tcg_gen_shli_i64(cpu_gr[op->destination], ia64_gr_src(op->source2),
                         op->immediate & 0x3f);
        break;
    case IA64_OP_SHRU_IMM:
        tcg_gen_shri_i64(cpu_gr[op->destination], ia64_gr_src(op->source2),
                         op->immediate & 0x3f);
        break;
    case IA64_OP_SHR_IMM:
        tcg_gen_sari_i64(cpu_gr[op->destination], ia64_gr_src(op->source2),
                         op->immediate & 0x3f);
        break;
    case IA64_OP_DEPZ: {
        const uint64_t pos = op->immediate & 0x3f;
        const uint64_t len = (op->immediate >> 6) & 0x7f;

        tmp = tcg_temp_new_i64();
        tmp2 = tcg_temp_new_i64();
        tcg_gen_movi_i64(tmp, ia64_deposit_mask(pos, len));
        tcg_gen_shli_i64(tmp2, ia64_gr_src(op->source1), pos);
        tcg_gen_and_i64(cpu_gr[op->destination], tmp2, tmp);
        break;
    }
    case IA64_OP_DEPZ_IMM: {
        const uint64_t pos = op->immediate & 0x3f;
        const uint64_t len = (op->immediate >> 6) & 0x7f;
        const int64_t src = (int8_t)((op->immediate >> 13) & 0xff);
        uint64_t mask_value = ia64_deposit_mask(pos, len);

        tcg_gen_movi_i64(cpu_gr[op->destination],
                         (((uint64_t)src << pos) & mask_value));
        break;
    }
    case IA64_OP_EXTRU: {
        const uint64_t pos = op->immediate & 0x3f;
        const uint64_t len =
            ia64_bitfield_effective_len(pos, op->immediate >> 6);

        tmp = tcg_temp_new_i64();
        tmp2 = tcg_temp_new_i64();
        tcg_gen_movi_i64(tmp, ia64_low_mask(len));
        tcg_gen_shri_i64(tmp2, ia64_gr_src(op->source2), pos);
        tcg_gen_and_i64(cpu_gr[op->destination], tmp2, tmp);
        break;
    }
    case IA64_OP_SXT1:
        tcg_gen_ext8s_i64(cpu_gr[op->destination], ia64_gr_src(op->source2));
        break;
    case IA64_OP_SXT2:
        tcg_gen_ext16s_i64(cpu_gr[op->destination], ia64_gr_src(op->source2));
        break;
    case IA64_OP_SXT4:
        tcg_gen_ext32s_i64(cpu_gr[op->destination], ia64_gr_src(op->source2));
        break;
    case IA64_OP_ZXT1:
        tcg_gen_ext8u_i64(cpu_gr[op->destination], ia64_gr_src(op->source2));
        break;
    case IA64_OP_ZXT2:
        tcg_gen_ext16u_i64(cpu_gr[op->destination], ia64_gr_src(op->source2));
        break;
    case IA64_OP_ZXT4:
        tcg_gen_ext32u_i64(cpu_gr[op->destination], ia64_gr_src(op->source2));
        break;
    case IA64_OP_SHRP_IMM:
        if ((op->immediate & 0x3f) == 0) {
            tcg_gen_mov_i64(cpu_gr[op->destination], ia64_gr_src(op->source2));
        } else {
            tmp = tcg_temp_new_i64();
            tmp2 = tcg_temp_new_i64();
            tcg_gen_shri_i64(tmp, ia64_gr_src(op->source2),
                             op->immediate & 0x3f);
            tcg_gen_shli_i64(tmp2, ia64_gr_src(op->source1),
                             64 - (op->immediate & 0x3f));
            tcg_gen_or_i64(cpu_gr[op->destination], tmp, tmp2);
        }
        break;
    case IA64_OP_DEP: {
        TCGv_i64 mask = tcg_temp_new_i64();
        TCGv_i64 pos = tcg_temp_new_i64();
        TCGv_i64 len = tcg_temp_new_i64();
        TCGv_i64 base = tcg_temp_new_i64();
        TCGv_i64 shifted = tcg_temp_new_i64();
        tcg_gen_movi_i64(pos, op->immediate & 0x3f);
        tcg_gen_movi_i64(len, (op->immediate >> 6) & 0x3f);
        tcg_gen_movi_i64(mask, 1);
        tcg_gen_shl_i64(mask, mask, len);
        tcg_gen_subi_i64(mask, mask, 1);
        tcg_gen_shl_i64(mask, mask, pos);
        tcg_gen_not_i64(mask, mask);
        tcg_gen_and_i64(base, ia64_gr_src(op->source2), mask);
        tcg_gen_not_i64(mask, mask);
        tcg_gen_shl_i64(shifted, ia64_gr_src(op->source1), pos);
        tcg_gen_and_i64(shifted, shifted, mask);
        tcg_gen_or_i64(cpu_gr[op->destination], base, shifted);
        break;
    }
    case IA64_OP_DEP_IMM: {
        const uint64_t pos = op->immediate & 0x3f;
        const uint64_t len = (op->immediate >> 6) & 0x7f;
        const uint64_t fill = (op->immediate >> 13) & 1;
        uint64_t mask_value = ia64_deposit_mask(pos, len);

        tmp = tcg_temp_new_i64();
        tmp2 = tcg_temp_new_i64();
        tcg_gen_movi_i64(tmp2, ~mask_value);
        tcg_gen_and_i64(tmp, ia64_gr_src(op->source2), tmp2);
        if (fill) {
            tcg_gen_movi_i64(tmp2, mask_value);
            tcg_gen_or_i64(cpu_gr[op->destination], tmp, tmp2);
        } else {
            tcg_gen_mov_i64(cpu_gr[op->destination], tmp);
        }
        break;
    }
    case IA64_OP_EXTR: {
        TCGv_i64 pos = tcg_temp_new_i64();
        TCGv_i64 len_tcg = tcg_temp_new_i64();
        TCGv_i64 val = tcg_temp_new_i64();
        TCGv_i64 sign_bit = tcg_temp_new_i64();
        const uint64_t pos_imm = op->immediate & 0x3f;
        const uint64_t len_imm =
            ia64_bitfield_effective_len(pos_imm, op->immediate >> 6);
        tmp = tcg_temp_new_i64();
        tcg_gen_movi_i64(pos, pos_imm);
        tcg_gen_movi_i64(len_tcg, len_imm);
        tcg_gen_shr_i64(val, ia64_gr_src(op->source2), pos);
        tcg_gen_movi_i64(tmp, ia64_low_mask(len_imm));
        tcg_gen_and_i64(val, val, tmp);
        tcg_gen_subi_i64(len_tcg, len_tcg, 1);
        tcg_gen_movi_i64(sign_bit, 1);
        tcg_gen_shl_i64(sign_bit, sign_bit, len_tcg);
        tcg_gen_xor_i64(val, val, sign_bit);
        tcg_gen_sub_i64(cpu_gr[op->destination], val, sign_bit);
        break;
    }
    case IA64_OP_SUB_IMM:
        tmp = tcg_temp_new_i64();
        tcg_gen_movi_i64(tmp, op->immediate);
        tcg_gen_sub_i64(cpu_gr[op->destination], tmp, ia64_gr_src(op->source2));
        break;
    case IA64_OP_AND_IMM:
        tmp = tcg_temp_new_i64();
        tcg_gen_movi_i64(tmp, op->immediate);
        tcg_gen_and_i64(cpu_gr[op->destination], tmp, ia64_gr_src(op->source2));
        break;
    case IA64_OP_ANDCM_IMM:
        tmp = tcg_temp_new_i64();
        tmp2 = tcg_temp_new_i64();
        tcg_gen_movi_i64(tmp, op->immediate);
        tcg_gen_not_i64(tmp2, ia64_gr_src(op->source2));
        tcg_gen_and_i64(cpu_gr[op->destination], tmp, tmp2);
        break;
    case IA64_OP_OR_IMM:
        tmp = tcg_temp_new_i64();
        tcg_gen_movi_i64(tmp, op->immediate);
        tcg_gen_or_i64(cpu_gr[op->destination], tmp, ia64_gr_src(op->source2));
        break;
    case IA64_OP_XOR_IMM:
        tmp = tcg_temp_new_i64();
        tcg_gen_movi_i64(tmp, op->immediate);
        tcg_gen_xor_i64(cpu_gr[op->destination], tmp, ia64_gr_src(op->source2));
        break;
    case IA64_OP_SHLADDP4:
        tmp = tcg_temp_new_i64();
        tcg_gen_shli_i64(tmp, ia64_gr_src(op->source1), op->immediate);
        ia64_gen_addp4_result(cpu_gr[op->destination], tmp,
                              ia64_gr_src(op->source2));
        break;
    case IA64_OP_MPY4: {
        TCGv_i64 tmpr2 = tcg_temp_new_i64();
        TCGv_i64 tmpr3 = tcg_temp_new_i64();
        tcg_gen_ext32u_i64(tmpr2, ia64_gr_src(op->source1));
        tcg_gen_ext32u_i64(tmpr3, ia64_gr_src(op->source2));
        tcg_gen_mul_i64(cpu_gr[op->destination], tmpr2, tmpr3);
        break;
    }
    case IA64_OP_MPYSHL4: {
        TCGv_i64 tmpr2 = tcg_temp_new_i64();
        TCGv_i64 tmpr3 = tcg_temp_new_i64();
        TCGv_i64 mul_res = tcg_temp_new_i64();
        tcg_gen_shri_i64(tmpr2, ia64_gr_src(op->source1), 32);
        tcg_gen_ext32u_i64(tmpr3, ia64_gr_src(op->source2));
        tcg_gen_mul_i64(mul_res, tmpr2, tmpr3);
        tcg_gen_shli_i64(cpu_gr[op->destination], mul_res, 32);
        break;
    }
    case IA64_OP_MPYSH: {
        TCGv_i64 tmpr2 = tcg_temp_new_i64();
        TCGv_i64 tmpr3 = tcg_temp_new_i64();
        TCGv_i64 mul_res = tcg_temp_new_i64();
        tcg_gen_ext32s_i64(tmpr2, ia64_gr_src(op->source1));
        tcg_gen_ext32s_i64(tmpr3, ia64_gr_src(op->source2));
        tcg_gen_mul_i64(mul_res, tmpr2, tmpr3);
        tcg_gen_sari_i64(cpu_gr[op->destination], mul_res, 32);
        break;
    }
    case IA64_OP_MPYUH: {
        TCGv_i64 tmpr2 = tcg_temp_new_i64();
        TCGv_i64 tmpr3 = tcg_temp_new_i64();
        TCGv_i64 mul_res = tcg_temp_new_i64();
        tcg_gen_ext32u_i64(tmpr2, ia64_gr_src(op->source1));
        tcg_gen_ext32u_i64(tmpr3, ia64_gr_src(op->source2));
        tcg_gen_mul_i64(mul_res, tmpr2, tmpr3);
        tcg_gen_shri_i64(cpu_gr[op->destination], mul_res, 32);
        break;
    }
    case IA64_OP_MUX: {
        TCGv_i64 r1_orig = tcg_temp_new_i64();
        tcg_gen_mov_i64(r1_orig, ia64_gr_src(op->destination));
        tmp = tcg_temp_new_i64();
        tmp2 = tcg_temp_new_i64();
        tcg_gen_and_i64(tmp, ia64_gr_src(op->source1), r1_orig);
        tcg_gen_not_i64(tmp2, ia64_gr_src(op->source1));
        tcg_gen_and_i64(tmp2, tmp2, ia64_gr_src(op->source2));
        tcg_gen_or_i64(cpu_gr[op->destination], tmp, tmp2);
        break;
    }
    case IA64_OP_POPCNT:
        tcg_gen_ctpop_i64(cpu_gr[op->destination], ia64_gr_src(op->source2));
        break;
    case IA64_OP_CLZ:
        tcg_gen_clzi_i64(cpu_gr[op->destination], ia64_gr_src(op->source2), 64);
        break;
    case IA64_OP_ILLEGAL:
    case IA64_OP_NOP:
    case IA64_OP_BREAK:
    case IA64_OP_CMP_EQ:
    case IA64_OP_CMP_LT:
    case IA64_OP_CMP_LE:
    case IA64_OP_CMP_GT:
    case IA64_OP_CMP_GE:
    case IA64_OP_CMP_LTU:
    case IA64_OP_CMP_LEU:
    case IA64_OP_CMP_GTU:
    case IA64_OP_CMP_GEU:
    case IA64_OP_CMP_NE:
    case IA64_OP_CMP_EQ_AND:
    case IA64_OP_CMP_NE_AND:
    case IA64_OP_CMP_GT_AND:
    case IA64_OP_CMP_LE_AND:
    case IA64_OP_CMP_GE_AND:
    case IA64_OP_CMP_LT_AND:
    case IA64_OP_CMP_EQ_OR:
    case IA64_OP_CMP_NE_OR:
    case IA64_OP_CMP_GT_OR:
    case IA64_OP_CMP_LE_OR:
    case IA64_OP_CMP_GE_OR:
    case IA64_OP_CMP_LT_OR:
    case IA64_OP_CMP_EQ_OR_ANDCM:
    case IA64_OP_CMP_NE_OR_ANDCM:
    case IA64_OP_CMP_GT_OR_ANDCM:
    case IA64_OP_CMP_LE_OR_ANDCM:
    case IA64_OP_CMP_GE_OR_ANDCM:
    case IA64_OP_CMP_LT_OR_ANDCM:
    case IA64_OP_CMP_EQ_IMM:
    case IA64_OP_CMP_LT_IMM:
    case IA64_OP_CMP_EQ_AND_IMM:
    case IA64_OP_CMP_NE_AND_IMM:
    case IA64_OP_CMP_EQ_OR_IMM:
    case IA64_OP_CMP_NE_OR_IMM:
    case IA64_OP_CMP_EQ_OR_ANDCM_IMM:
    case IA64_OP_CMP_NE_OR_ANDCM_IMM:
    case IA64_OP_CMP4_EQ_AND:
    case IA64_OP_CMP4_NE_AND:
    case IA64_OP_CMP4_EQ_OR:
    case IA64_OP_CMP4_NE_OR:
    case IA64_OP_CMP4_EQ_OR_ANDCM:
    case IA64_OP_CMP4_NE_OR_ANDCM:
    case IA64_OP_CMP4_GT_AND:
    case IA64_OP_CMP4_LE_AND:
    case IA64_OP_CMP4_GE_AND:
    case IA64_OP_CMP4_LT_AND:
    case IA64_OP_CMP4_GT_OR:
    case IA64_OP_CMP4_LE_OR:
    case IA64_OP_CMP4_GE_OR:
    case IA64_OP_CMP4_LT_OR:
    case IA64_OP_CMP4_GT_OR_ANDCM:
    case IA64_OP_CMP4_LE_OR_ANDCM:
    case IA64_OP_CMP4_GE_OR_ANDCM:
    case IA64_OP_CMP4_LT_OR_ANDCM:
    case IA64_OP_CMP4_EQ_OR_ANDCM_IMM:
    case IA64_OP_CMP4_NE_OR_ANDCM_IMM:
    case IA64_OP_CMP4_EQ_AND_IMM:
    case IA64_OP_CMP4_NE_AND_IMM:
    case IA64_OP_CMP4_EQ_OR_IMM:
    case IA64_OP_CMP4_NE_OR_IMM:
    case IA64_OP_CMP_LTU_IMM:
    case IA64_OP_LD1:
    case IA64_OP_LD2:
    case IA64_OP_LD4:
    case IA64_OP_LD8:
    case IA64_OP_LD1S:
    case IA64_OP_LD2S:
    case IA64_OP_LD4S:
    case IA64_OP_LD8S:
    case IA64_OP_LD1A:
    case IA64_OP_LD2A:
    case IA64_OP_LD4A:
    case IA64_OP_LD8A:
    case IA64_OP_LD1SA:
    case IA64_OP_LD2SA:
    case IA64_OP_LD4SA:
    case IA64_OP_LD8SA:
    case IA64_OP_LD8FILL:
    case IA64_OP_ST1:
    case IA64_OP_ST2:
    case IA64_OP_ST4:
    case IA64_OP_ST8:
    case IA64_OP_ST1REL:
    case IA64_OP_ST2REL:
    case IA64_OP_ST4REL:
    case IA64_OP_ST8REL:
    case IA64_OP_ST8SPILL:
    case IA64_OP_BR_COND:
    case IA64_OP_BR_INDIRECT:
    case IA64_OP_BR_IA:
    case IA64_OP_BR_CLOOP:
    case IA64_OP_BR_CALL:
    case IA64_OP_BR_CALL_INDIRECT:
    case IA64_OP_BR_RET:
    case IA64_OP_PADD1:
    case IA64_OP_PADD2:
    case IA64_OP_PADD4:
    case IA64_OP_PSUB1:
    case IA64_OP_PSUB2:
    case IA64_OP_PSUB4:
    case IA64_OP_XCHG1:
    case IA64_OP_XCHG2:
    case IA64_OP_XCHG4:
    case IA64_OP_XCHG8:
    case IA64_OP_CMPXCHG1:
    case IA64_OP_CMPXCHG2:
    case IA64_OP_CMPXCHG4:
    case IA64_OP_CMPXCHG8:
    case IA64_OP_CMP8XCHG16:
    case IA64_OP_FETCHADD4:
    case IA64_OP_FETCHADD8:
    case IA64_OP_MOVL:
    case IA64_OP_MOV_BRGR:
    case IA64_OP_MOV_GRBR:
    case IA64_OP_MOV_PRGR:
    case IA64_OP_MOV_GRPR:
    case IA64_OP_MOV_PR_ROT_IMM:
    case IA64_OP_TBIT_Z:
    case IA64_OP_TBIT_NZ:
    case IA64_OP_TBIT_Z_OR_ANDCM:
    case IA64_OP_TBIT_NZ_OR_ANDCM:
    case IA64_OP_TNAT_Z:
    case IA64_OP_TNAT_NZ:
    case IA64_OP_TNAT_NZ_AND:
    case IA64_OP_TF_Z:
    case IA64_OP_TF_NZ:
    case IA64_OP_LD1C_CLR:
    case IA64_OP_LD2C_CLR:
    case IA64_OP_LD4C_CLR:
    case IA64_OP_LD8C_CLR:
    case IA64_OP_LD1C_NC:
    case IA64_OP_LD2C_NC:
    case IA64_OP_LD4C_NC:
    case IA64_OP_LD8C_NC:
    case IA64_OP_HINT_M:
    case IA64_OP_HINT_I:
    case IA64_OP_HINT_B:
    case IA64_OP_HINT_F:
    case IA64_OP_HINT_X:
    case IA64_OP_PTC_E:
    case IA64_OP_CLRRRB:
    case IA64_OP_CLRRRB_PR:
    case IA64_OP_LDFP8:
    case IA64_OP_LDFPD:
    case IA64_OP_LDFPS:
    case IA64_OP_FMERGE_S:
    case IA64_OP_FMERGE_SE:
    case IA64_OP_FMIN:
    case IA64_OP_FMAX:
    case IA64_OP_FAMIN:
    case IA64_OP_FAMAX:
    case IA64_OP_FRCPA:
    case IA64_OP_FPRCPA:
    case IA64_OP_GETF_SIG:
    case IA64_OP_GETF_EXP:
    case IA64_OP_SETF_SIG:
    case IA64_OP_SETF_EXP:
    case IA64_OP_CMP4_EQ:
    case IA64_OP_CMP4_LT:
    case IA64_OP_CMP4_LE:
    case IA64_OP_CMP4_GT:
    case IA64_OP_CMP4_GE:
    case IA64_OP_CMP4_LTU:
    case IA64_OP_CMP4_LEU:
    case IA64_OP_CMP4_GTU:
    case IA64_OP_CMP4_GEU:
    case IA64_OP_CMP4_LT_IMM:
    case IA64_OP_CMP4_LTU_IMM:
        g_assert_not_reached();
    default:
        g_assert_not_reached();
    }
}

static void ia64_gen_integer_nat_from_1(const Ia64Instruction *insn,
                                        uint8_t dst, uint8_t src)
{
    /*
     * These operations write the register value, but copying a register's
     * NaT bit back to the same register is an identity operation.  Avoid the
     * otherwise fairly long extract/clear/insert sequence while retaining
     * the stacked-register dirty bookkeeping associated with the write.
     */
    if (dst == src) {
        ia64_gen_note_stacked_gr_write(insn, dst);
        return;
    }
    ia64_gen_gr_nat_from_1(insn, dst, src);
}

static void ia64_gen_integer_nat_from_2(const Ia64Instruction *insn,
                                        uint8_t dst, uint8_t src1, uint8_t src2)
{
    /* x.NaT | x.NaT is just x.NaT. */
    if (src1 == src2) {
        ia64_gen_integer_nat_from_1(insn, dst, src1);
        return;
    }

    /* Preserve dst.NaT in place and OR in only the non-aliasing input. */
    if (dst == src1) {
        ia64_gen_gr_nat_or_from_1(insn, dst, src2);
        return;
    }
    if (dst == src2) {
        ia64_gen_gr_nat_or_from_1(insn, dst, src1);
        return;
    }

    ia64_gen_gr_nat_from_2(insn, dst, src1, src2);
}

static void ia64_gen_native_integer_nat(const Ia64Instruction *insn)
{
    const IA64IntegerOperands *op = &insn->operands.integer;

    if (op->destination == 0) {
        return;
    }

    switch (insn->opcode) {
    case IA64_OP_ADDS:
    case IA64_OP_ADDL:
    case IA64_OP_SHL_IMM:
    case IA64_OP_SHRU_IMM:
    case IA64_OP_SHR_IMM:
    case IA64_OP_EXTRU:
    case IA64_OP_SXT1:
    case IA64_OP_SXT2:
    case IA64_OP_SXT4:
    case IA64_OP_ZXT1:
    case IA64_OP_ZXT2:
    case IA64_OP_ZXT4:
    case IA64_OP_DEP_IMM:
    case IA64_OP_EXTR:
    case IA64_OP_SUB_IMM:
    case IA64_OP_AND_IMM:
    case IA64_OP_ANDCM_IMM:
    case IA64_OP_OR_IMM:
    case IA64_OP_XOR_IMM:
    case IA64_OP_POPCNT:
    case IA64_OP_CLZ:
        ia64_gen_integer_nat_from_1(insn, op->destination, op->source2);
        break;
    case IA64_OP_DEPZ:
        ia64_gen_integer_nat_from_1(insn, op->destination, op->source1);
        break;
    case IA64_OP_DEPZ_IMM:
        ia64_gen_gr_nat_clear(insn, op->destination);
        break;
    case IA64_OP_SHLADD:
    case IA64_OP_ADD:
    case IA64_OP_ADD_ONE:
    case IA64_OP_SUB:
    case IA64_OP_SUB_ONE:
    case IA64_OP_AND:
    case IA64_OP_ANDCM:
    case IA64_OP_OR:
    case IA64_OP_XOR:
    case IA64_OP_SHL:
    case IA64_OP_SHRU:
    case IA64_OP_SHR:
    case IA64_OP_SHRP_IMM:
    case IA64_OP_DEP:
    case IA64_OP_SHLADDP4:
    case IA64_OP_MPY4:
    case IA64_OP_MPYSHL4:
    case IA64_OP_MPYSH:
    case IA64_OP_MPYUH:
        ia64_gen_integer_nat_from_2(insn, op->destination, op->source1,
                                    op->source2);
        break;
    case IA64_OP_MUX:
        ia64_gen_gr_nat_from_3(insn, op->destination, op->destination,
                               op->source1, op->source2);
        break;
    default:
        g_assert_not_reached();
    }
}

IA64GenResult ia64_gen_integer(DisasContext *ctx, const Ia64Instruction *insn)
{
    const IA64IntegerOperands *op = &insn->operands.integer;
    TCGv_i64 tmp;

    switch (insn->opcode) {
    case IA64_OP_ADDS:
    case IA64_OP_ADDL:
    case IA64_OP_SHLADD:
    case IA64_OP_ADD:
    case IA64_OP_ADD_ONE:
    case IA64_OP_SUB:
    case IA64_OP_SUB_ONE:
    case IA64_OP_AND:
    case IA64_OP_ANDCM:
    case IA64_OP_OR:
    case IA64_OP_XOR:
    case IA64_OP_SUB_IMM:
    case IA64_OP_AND_IMM:
    case IA64_OP_ANDCM_IMM:
    case IA64_OP_OR_IMM:
    case IA64_OP_XOR_IMM:
    case IA64_OP_SHL:
    case IA64_OP_SHRU:
    case IA64_OP_SHR:
    case IA64_OP_SHL_IMM:
    case IA64_OP_SHRU_IMM:
    case IA64_OP_SHR_IMM:
    case IA64_OP_DEPZ:
    case IA64_OP_DEPZ_IMM:
    case IA64_OP_EXTRU:
    case IA64_OP_SHRP_IMM:
    case IA64_OP_DEP:
    case IA64_OP_DEP_IMM:
    case IA64_OP_EXTR:
    case IA64_OP_SXT1:
    case IA64_OP_SXT2:
    case IA64_OP_SXT4:
    case IA64_OP_ZXT1:
    case IA64_OP_ZXT2:
    case IA64_OP_ZXT4:
    case IA64_OP_SHLADDP4:
    case IA64_OP_MPY4:
    case IA64_OP_MPYSHL4:
    case IA64_OP_MPYSH:
    case IA64_OP_MPYUH:
    case IA64_OP_MUX:
    case IA64_OP_POPCNT:
    case IA64_OP_CLZ:
        ia64_gen_native_integer_write(insn);
        ia64_gen_native_integer_nat(insn);
        break;
    case IA64_OP_CMP_EQ:
    case IA64_OP_CMP_LT:
    case IA64_OP_CMP_LE:
    case IA64_OP_CMP_GT:
    case IA64_OP_CMP_GE:
    case IA64_OP_CMP_LTU:
    case IA64_OP_CMP_LEU:
    case IA64_OP_CMP_GTU:
    case IA64_OP_CMP_GEU:
    case IA64_OP_CMP_NE:
    case IA64_OP_CMP_EQ_AND:
    case IA64_OP_CMP_NE_AND:
    case IA64_OP_CMP_GT_AND:
    case IA64_OP_CMP_LE_AND:
    case IA64_OP_CMP_GE_AND:
    case IA64_OP_CMP_LT_AND:
    case IA64_OP_CMP_EQ_OR:
    case IA64_OP_CMP_NE_OR:
    case IA64_OP_CMP_GT_OR:
    case IA64_OP_CMP_LE_OR:
    case IA64_OP_CMP_GE_OR:
    case IA64_OP_CMP_LT_OR:
    case IA64_OP_CMP_EQ_OR_ANDCM:
    case IA64_OP_CMP_NE_OR_ANDCM:
    case IA64_OP_CMP_GT_OR_ANDCM:
    case IA64_OP_CMP_LE_OR_ANDCM:
    case IA64_OP_CMP_GE_OR_ANDCM:
    case IA64_OP_CMP_LT_OR_ANDCM:
    case IA64_OP_CMP_EQ_IMM:
    case IA64_OP_CMP_LT_IMM:
    case IA64_OP_CMP_EQ_AND_IMM:
    case IA64_OP_CMP_NE_AND_IMM:
    case IA64_OP_CMP_EQ_OR_IMM:
    case IA64_OP_CMP_NE_OR_IMM:
    case IA64_OP_CMP_EQ_OR_ANDCM_IMM:
    case IA64_OP_CMP_NE_OR_ANDCM_IMM:
    case IA64_OP_CMP4_EQ_AND:
    case IA64_OP_CMP4_NE_AND:
    case IA64_OP_CMP4_EQ_OR:
    case IA64_OP_CMP4_NE_OR:
    case IA64_OP_CMP4_EQ_OR_ANDCM:
    case IA64_OP_CMP4_NE_OR_ANDCM:
    case IA64_OP_CMP4_GT_AND:
    case IA64_OP_CMP4_LE_AND:
    case IA64_OP_CMP4_GE_AND:
    case IA64_OP_CMP4_LT_AND:
    case IA64_OP_CMP4_GT_OR:
    case IA64_OP_CMP4_LE_OR:
    case IA64_OP_CMP4_GE_OR:
    case IA64_OP_CMP4_LT_OR:
    case IA64_OP_CMP4_GT_OR_ANDCM:
    case IA64_OP_CMP4_LE_OR_ANDCM:
    case IA64_OP_CMP4_GE_OR_ANDCM:
    case IA64_OP_CMP4_LT_OR_ANDCM:
    case IA64_OP_CMP4_EQ_OR_ANDCM_IMM:
    case IA64_OP_CMP4_NE_OR_ANDCM_IMM:
    case IA64_OP_CMP_LTU_IMM:
    case IA64_OP_CMP4_EQ:
    case IA64_OP_CMP4_LT:
    case IA64_OP_CMP4_LE:
    case IA64_OP_CMP4_GT:
    case IA64_OP_CMP4_GE:
    case IA64_OP_CMP4_LTU:
    case IA64_OP_CMP4_LEU:
    case IA64_OP_CMP4_GTU:
    case IA64_OP_CMP4_GEU:
    case IA64_OP_CMP4_EQ_IMM:
    case IA64_OP_CMP4_LT_IMM:
    case IA64_OP_CMP4_LTU_IMM:
    case IA64_OP_CMP4_EQ_AND_IMM:
    case IA64_OP_CMP4_NE_AND_IMM:
    case IA64_OP_CMP4_EQ_OR_IMM:
    case IA64_OP_CMP4_NE_OR_IMM: {
        TCGCond cond;
        bool is_cmp4 = false;
        bool is_cmp_imm = false;
        bool is_signed_cmp4 = false;
        bool is_and = false;
        bool is_or = false;
        bool is_or_andcm = false;
        switch (insn->opcode) {
        case IA64_OP_CMP_EQ:
            cond = TCG_COND_EQ;
            break;
        case IA64_OP_CMP_LT:
            cond = TCG_COND_LT;
            break;
        case IA64_OP_CMP_LE:
            cond = TCG_COND_LE;
            break;
        case IA64_OP_CMP_GT:
            cond = TCG_COND_GT;
            break;
        case IA64_OP_CMP_GE:
            cond = TCG_COND_GE;
            break;
        case IA64_OP_CMP_LTU:
            cond = TCG_COND_LTU;
            break;
        case IA64_OP_CMP_LEU:
            cond = TCG_COND_LEU;
            break;
        case IA64_OP_CMP_GTU:
            cond = TCG_COND_GTU;
            break;
        case IA64_OP_CMP_GEU:
            cond = TCG_COND_GEU;
            break;
        case IA64_OP_CMP_NE:
            cond = TCG_COND_NE;
            break;
        case IA64_OP_CMP_EQ_AND:
            cond = TCG_COND_EQ;
            is_and = true;
            break;
        case IA64_OP_CMP_NE_AND:
            cond = TCG_COND_NE;
            is_and = true;
            break;
        case IA64_OP_CMP_GT_AND:
            cond = TCG_COND_GT;
            is_and = true;
            break;
        case IA64_OP_CMP_LE_AND:
            cond = TCG_COND_LE;
            is_and = true;
            break;
        case IA64_OP_CMP_GE_AND:
            cond = TCG_COND_GE;
            is_and = true;
            break;
        case IA64_OP_CMP_LT_AND:
            cond = TCG_COND_LT;
            is_and = true;
            break;
        case IA64_OP_CMP_EQ_OR:
            cond = TCG_COND_EQ;
            is_or = true;
            break;
        case IA64_OP_CMP_NE_OR:
            cond = TCG_COND_NE;
            is_or = true;
            break;
        case IA64_OP_CMP_GT_OR:
            cond = TCG_COND_GT;
            is_or = true;
            break;
        case IA64_OP_CMP_LE_OR:
            cond = TCG_COND_LE;
            is_or = true;
            break;
        case IA64_OP_CMP_GE_OR:
            cond = TCG_COND_GE;
            is_or = true;
            break;
        case IA64_OP_CMP_LT_OR:
            cond = TCG_COND_LT;
            is_or = true;
            break;
        case IA64_OP_CMP_EQ_OR_ANDCM:
            cond = TCG_COND_EQ;
            is_or_andcm = true;
            break;
        case IA64_OP_CMP_NE_OR_ANDCM:
            cond = TCG_COND_NE;
            is_or_andcm = true;
            break;
        case IA64_OP_CMP_GT_OR_ANDCM:
            cond = TCG_COND_GT;
            is_or_andcm = true;
            break;
        case IA64_OP_CMP_LE_OR_ANDCM:
            cond = TCG_COND_LE;
            is_or_andcm = true;
            break;
        case IA64_OP_CMP_GE_OR_ANDCM:
            cond = TCG_COND_GE;
            is_or_andcm = true;
            break;
        case IA64_OP_CMP_LT_OR_ANDCM:
            cond = TCG_COND_LT;
            is_or_andcm = true;
            break;
        case IA64_OP_CMP_EQ_IMM:
            cond = TCG_COND_EQ;
            is_cmp_imm = true;
            break;
        case IA64_OP_CMP_LT_IMM:
            cond = TCG_COND_LT;
            is_cmp_imm = true;
            break;
        case IA64_OP_CMP_EQ_AND_IMM:
            cond = TCG_COND_EQ;
            is_cmp_imm = true;
            is_and = true;
            break;
        case IA64_OP_CMP_NE_AND_IMM:
            cond = TCG_COND_NE;
            is_cmp_imm = true;
            is_and = true;
            break;
        case IA64_OP_CMP_EQ_OR_IMM:
            cond = TCG_COND_EQ;
            is_cmp_imm = true;
            is_or = true;
            break;
        case IA64_OP_CMP_NE_OR_IMM:
            cond = TCG_COND_NE;
            is_cmp_imm = true;
            is_or = true;
            break;
        case IA64_OP_CMP_EQ_OR_ANDCM_IMM:
            cond = TCG_COND_EQ;
            is_cmp_imm = true;
            is_or_andcm = true;
            break;
        case IA64_OP_CMP_NE_OR_ANDCM_IMM:
            cond = TCG_COND_NE;
            is_cmp_imm = true;
            is_or_andcm = true;
            break;
        case IA64_OP_CMP4_EQ_AND:
            cond = TCG_COND_EQ;
            is_cmp4 = true;
            is_and = true;
            break;
        case IA64_OP_CMP4_NE_AND:
            cond = TCG_COND_NE;
            is_cmp4 = true;
            is_and = true;
            break;
        case IA64_OP_CMP4_EQ_OR:
            cond = TCG_COND_EQ;
            is_cmp4 = true;
            is_or = true;
            break;
        case IA64_OP_CMP4_NE_OR:
            cond = TCG_COND_NE;
            is_cmp4 = true;
            is_or = true;
            break;
        case IA64_OP_CMP4_EQ_OR_ANDCM:
            cond = TCG_COND_EQ;
            is_cmp4 = true;
            is_or_andcm = true;
            break;
        case IA64_OP_CMP4_NE_OR_ANDCM:
            cond = TCG_COND_NE;
            is_cmp4 = true;
            is_or_andcm = true;
            break;
        case IA64_OP_CMP4_GT_AND:
            cond = TCG_COND_GT;
            is_cmp4 = true;
            is_signed_cmp4 = true;
            is_and = true;
            break;
        case IA64_OP_CMP4_LE_AND:
            cond = TCG_COND_LE;
            is_cmp4 = true;
            is_signed_cmp4 = true;
            is_and = true;
            break;
        case IA64_OP_CMP4_GE_AND:
            cond = TCG_COND_GE;
            is_cmp4 = true;
            is_signed_cmp4 = true;
            is_and = true;
            break;
        case IA64_OP_CMP4_LT_AND:
            cond = TCG_COND_LT;
            is_cmp4 = true;
            is_signed_cmp4 = true;
            is_and = true;
            break;
        case IA64_OP_CMP4_GT_OR:
            cond = TCG_COND_GT;
            is_cmp4 = true;
            is_signed_cmp4 = true;
            is_or = true;
            break;
        case IA64_OP_CMP4_LE_OR:
            cond = TCG_COND_LE;
            is_cmp4 = true;
            is_signed_cmp4 = true;
            is_or = true;
            break;
        case IA64_OP_CMP4_GE_OR:
            cond = TCG_COND_GE;
            is_cmp4 = true;
            is_signed_cmp4 = true;
            is_or = true;
            break;
        case IA64_OP_CMP4_LT_OR:
            cond = TCG_COND_LT;
            is_cmp4 = true;
            is_signed_cmp4 = true;
            is_or = true;
            break;
        case IA64_OP_CMP4_GT_OR_ANDCM:
            cond = TCG_COND_GT;
            is_cmp4 = true;
            is_signed_cmp4 = true;
            is_or_andcm = true;
            break;
        case IA64_OP_CMP4_LE_OR_ANDCM:
            cond = TCG_COND_LE;
            is_cmp4 = true;
            is_signed_cmp4 = true;
            is_or_andcm = true;
            break;
        case IA64_OP_CMP4_GE_OR_ANDCM:
            cond = TCG_COND_GE;
            is_cmp4 = true;
            is_signed_cmp4 = true;
            is_or_andcm = true;
            break;
        case IA64_OP_CMP4_LT_OR_ANDCM:
            cond = TCG_COND_LT;
            is_cmp4 = true;
            is_signed_cmp4 = true;
            is_or_andcm = true;
            break;
        case IA64_OP_CMP4_EQ_OR_ANDCM_IMM:
            cond = TCG_COND_EQ;
            is_cmp4 = true;
            is_cmp_imm = true;
            is_or_andcm = true;
            break;
        case IA64_OP_CMP4_NE_OR_ANDCM_IMM:
            cond = TCG_COND_NE;
            is_cmp4 = true;
            is_cmp_imm = true;
            is_or_andcm = true;
            break;
        case IA64_OP_CMP_LTU_IMM:
            cond = TCG_COND_LTU;
            is_cmp_imm = true;
            break;
        case IA64_OP_CMP4_EQ:
            cond = TCG_COND_EQ;
            is_cmp4 = true;
            break;
        case IA64_OP_CMP4_LT:
            cond = TCG_COND_LT;
            is_cmp4 = true;
            is_signed_cmp4 = true;
            break;
        case IA64_OP_CMP4_LE:
            cond = TCG_COND_LE;
            is_cmp4 = true;
            is_signed_cmp4 = true;
            break;
        case IA64_OP_CMP4_GT:
            cond = TCG_COND_GT;
            is_cmp4 = true;
            is_signed_cmp4 = true;
            break;
        case IA64_OP_CMP4_GE:
            cond = TCG_COND_GE;
            is_cmp4 = true;
            is_signed_cmp4 = true;
            break;
        case IA64_OP_CMP4_LTU:
            cond = TCG_COND_LTU;
            is_cmp4 = true;
            break;
        case IA64_OP_CMP4_LEU:
            cond = TCG_COND_LEU;
            is_cmp4 = true;
            break;
        case IA64_OP_CMP4_GTU:
            cond = TCG_COND_GTU;
            is_cmp4 = true;
            break;
        case IA64_OP_CMP4_GEU:
            cond = TCG_COND_GEU;
            is_cmp4 = true;
            break;
        case IA64_OP_CMP4_EQ_IMM:
            cond = TCG_COND_EQ;
            is_cmp4 = true;
            is_cmp_imm = true;
            break;
        case IA64_OP_CMP4_LT_IMM:
            cond = TCG_COND_LT;
            is_cmp4 = true;
            is_cmp_imm = true;
            is_signed_cmp4 = true;
            break;
        case IA64_OP_CMP4_LTU_IMM:
            cond = TCG_COND_LTU;
            is_cmp4 = true;
            is_cmp_imm = true;
            break;
        case IA64_OP_CMP4_EQ_AND_IMM:
            cond = TCG_COND_EQ;
            is_cmp4 = true;
            is_cmp_imm = true;
            is_and = true;
            break;
        case IA64_OP_CMP4_NE_AND_IMM:
            cond = TCG_COND_NE;
            is_cmp4 = true;
            is_cmp_imm = true;
            is_and = true;
            break;
        case IA64_OP_CMP4_EQ_OR_IMM:
            cond = TCG_COND_EQ;
            is_cmp4 = true;
            is_cmp_imm = true;
            is_or = true;
            break;
        case IA64_OP_CMP4_NE_OR_IMM:
            cond = TCG_COND_NE;
            is_cmp4 = true;
            is_cmp_imm = true;
            is_or = true;
            break;
        default:
            cond = TCG_COND_EQ;
            break;
        }
        const bool r2_nat_clear =
            is_cmp_imm || ia64_gr_nat_is_known_clear(insn, op->source1);
        const bool r2_nat_set =
            !is_cmp_imm && ia64_gr_nat_is_known_set(insn, op->source1);
        const bool r3_nat_clear = ia64_gr_nat_is_known_clear(insn, op->source2);
        const bool r3_nat_set = ia64_gr_nat_is_known_set(insn, op->source2);
        const bool nat_clear = r2_nat_clear && r3_nat_clear;
        const bool nat_set = r2_nat_set || r3_nat_set;
        TCGv_i64 src2;
        TCGv_i64 src3;
        TCGLabel *cmp_done = NULL;

        if (nat_set) {
            /* OR forms leave both predicates unchanged on a NaT input. */
            if (!is_or && !is_or_andcm) {
                if (op->predicate1 != 0) {
                    tcg_gen_movi_i64(cpu_pr[op->predicate1], 0);
                }
                if (op->predicate2 != 0) {
                    tcg_gen_movi_i64(cpu_pr[op->predicate2], 0);
                }
            }
            break;
        }
        if (!nat_clear) {
            TCGv_i64 src_nat = NULL;

            cmp_done = gen_new_label();
            if (!r3_nat_clear) {
                src_nat = ia64_gen_gr_nat_read(op->source2);
            }
            if (!r2_nat_clear) {
                TCGv_i64 r2_nat = ia64_gen_gr_nat_read(op->source1);

                if (src_nat == NULL) {
                    src_nat = r2_nat;
                } else {
                    tcg_gen_or_i64(src_nat, src_nat, r2_nat);
                }
            }
            g_assert(src_nat != NULL);
            if (is_or || is_or_andcm) {
                tcg_gen_brcondi_i64(TCG_COND_NE, src_nat, 0, cmp_done);
            } else {
                TCGLabel *no_nat = gen_new_label();

                tcg_gen_brcondi_i64(TCG_COND_EQ, src_nat, 0, no_nat);
                if (op->predicate1 != 0) {
                    tcg_gen_movi_i64(cpu_pr[op->predicate1], 0);
                }
                if (op->predicate2 != 0) {
                    tcg_gen_movi_i64(cpu_pr[op->predicate2], 0);
                }
                tcg_gen_br(cmp_done);
                gen_set_label(no_nat);
            }
        }
        src2 = is_cmp_imm ? tcg_constant_i64(op->immediate)
                          : ia64_gr_src(op->source1);
        src3 = ia64_gr_src(op->source2);
        if (is_cmp4) {
            TCGv_i64 tmp_a = tcg_temp_new_i64();
            TCGv_i64 tmp_b = tcg_temp_new_i64();
            if (is_signed_cmp4) {
                tcg_gen_ext32s_i64(tmp_a, src2);
                tcg_gen_ext32s_i64(tmp_b, src3);
            } else {
                tcg_gen_ext32u_i64(tmp_a, src2);
                tcg_gen_ext32u_i64(tmp_b, src3);
            }
            src2 = tmp_a;
            src3 = tmp_b;
        }
        tmp = tcg_temp_new_i64();
        tcg_gen_setcond_i64(cond, tmp, src2, src3);
        /* Parallel AND/OR compares apply the same result to both targets. */
        if (is_and) {
            if (op->predicate1 != 0) {
                tcg_gen_and_i64(cpu_pr[op->predicate1], cpu_pr[op->predicate1],
                                tmp);
            }
            if (op->predicate2 != 0) {
                tcg_gen_and_i64(cpu_pr[op->predicate2], cpu_pr[op->predicate2],
                                tmp);
            }
        } else if (is_or) {
            if (op->predicate1 != 0) {
                tcg_gen_or_i64(cpu_pr[op->predicate1], cpu_pr[op->predicate1],
                               tmp);
            }
            if (op->predicate2 != 0) {
                tcg_gen_or_i64(cpu_pr[op->predicate2], cpu_pr[op->predicate2],
                               tmp);
            }
        } else if (is_or_andcm) {
            if (op->predicate1 != 0) {
                tcg_gen_or_i64(cpu_pr[op->predicate1], cpu_pr[op->predicate1],
                               tmp);
            }
            if (op->predicate2 != 0) {
                TCGv_i64 not_tmp = tcg_temp_new_i64();
                tcg_gen_xori_i64(not_tmp, tmp, 1);
                tcg_gen_and_i64(cpu_pr[op->predicate2], cpu_pr[op->predicate2],
                                not_tmp);
            }
        } else {
            if (op->predicate1 != 0) {
                tcg_gen_mov_i64(cpu_pr[op->predicate1], tmp);
            }
            if (op->predicate2 != 0) {
                tcg_gen_xori_i64(cpu_pr[op->predicate2], tmp, 1);
            }
        }
        if (cmp_done != NULL) {
            gen_set_label(cmp_done);
        }
        break;
    }
    case IA64_OP_TBIT_Z:
    case IA64_OP_TBIT_NZ:
    case IA64_OP_TBIT_Z_OR_ANDCM:
    case IA64_OP_TBIT_NZ_OR_ANDCM: {
        const bool is_nz = insn->opcode == IA64_OP_TBIT_NZ ||
                           insn->opcode == IA64_OP_TBIT_NZ_OR_ANDCM;
        const bool old_or_andcm = insn->opcode == IA64_OP_TBIT_Z_OR_ANDCM ||
                                  insn->opcode == IA64_OP_TBIT_NZ_OR_ANDCM;
        const bool nat_clear = ia64_gr_nat_is_known_clear(insn, op->source2);
        const bool nat_set = ia64_gr_nat_is_known_set(insn, op->source2);
        TCGv_i64 bit;
        TCGv_i64 cond;
        TCGv_i64 not_cond;
        TCGLabel *tbit_done = NULL;
        Ia64Instruction pred_insn = *insn;

        if (old_or_andcm) {
            pred_insn.pred_update = IA64_PRED_UPDATE_OR_ANDCM;
        }
        if (nat_set) {
            if (pred_insn.pred_update == IA64_PRED_UPDATE_OR ||
                pred_insn.pred_update == IA64_PRED_UPDATE_OR_ANDCM) {
                break;
            }
            if (op->predicate1 != 0) {
                tcg_gen_movi_i64(cpu_pr[op->predicate1], 0);
            }
            if (op->predicate2 != 0) {
                tcg_gen_movi_i64(cpu_pr[op->predicate2], 0);
            }
            break;
        }
        bit = tcg_temp_new_i64();
        cond = tcg_temp_new_i64();
        not_cond = tcg_temp_new_i64();
        if (!nat_clear) {
            TCGv_i64 src_nat = ia64_gen_gr_nat_read(op->source2);

            tbit_done = gen_new_label();
            if (pred_insn.pred_update == IA64_PRED_UPDATE_OR ||
                pred_insn.pred_update == IA64_PRED_UPDATE_OR_ANDCM) {
                tcg_gen_brcondi_i64(TCG_COND_NE, src_nat, 0, tbit_done);
            } else {
                TCGLabel *no_nat = gen_new_label();

                tcg_gen_brcondi_i64(TCG_COND_EQ, src_nat, 0, no_nat);
                if (op->predicate1 != 0) {
                    tcg_gen_movi_i64(cpu_pr[op->predicate1], 0);
                }
                if (op->predicate2 != 0) {
                    tcg_gen_movi_i64(cpu_pr[op->predicate2], 0);
                }
                tcg_gen_br(tbit_done);
                gen_set_label(no_nat);
            }
        }

        tcg_gen_mov_i64(bit, ia64_gr_src(op->source2));
        tcg_gen_shri_i64(bit, bit, op->immediate & 0x3f);
        tcg_gen_andi_i64(bit, bit, 1);
        if (is_nz) {
            tcg_gen_mov_i64(cond, bit);
        } else {
            tcg_gen_xori_i64(cond, bit, 1);
        }
        tcg_gen_xori_i64(not_cond, cond, 1);
        ia64_gen_predicate_test_write(&pred_insn, cond, not_cond);
        if (tbit_done != NULL) {
            gen_set_label(tbit_done);
        }
        break;
    }
    case IA64_OP_TNAT_Z:
    case IA64_OP_TNAT_NZ:
    case IA64_OP_TNAT_NZ_AND: {
        const bool is_nz = insn->opcode == IA64_OP_TNAT_NZ ||
                           insn->opcode == IA64_OP_TNAT_NZ_AND;
        const bool nat_clear = ia64_gr_nat_is_known_clear(insn, op->source2);
        const bool nat_set = ia64_gr_nat_is_known_set(insn, op->source2);
        TCGv_i64 cond;
        TCGv_i64 not_cond;
        Ia64Instruction pred_insn = *insn;

        if (nat_clear || nat_set) {
            bool condition = is_nz ? nat_set : nat_clear;

            cond = tcg_constant_i64(condition);
            not_cond = tcg_constant_i64(!condition);
        } else {
            TCGv_i64 natbit = ia64_gen_gr_nat_read(op->source2);

            cond = tcg_temp_new_i64();
            not_cond = tcg_temp_new_i64();
            if (is_nz) {
                tcg_gen_mov_i64(cond, natbit);
            } else {
                tcg_gen_xori_i64(cond, natbit, 1);
            }
            tcg_gen_xori_i64(not_cond, cond, 1);
        }

        if (insn->opcode == IA64_OP_TNAT_NZ_AND) {
            pred_insn.pred_update = IA64_PRED_UPDATE_AND;
        }
        ia64_gen_predicate_test_write(&pred_insn, cond, not_cond);
        break;
    }
    case IA64_OP_TF_Z:
    case IA64_OP_TF_NZ: {
        const bool is_nz = insn->opcode == IA64_OP_TF_NZ;
        const uint64_t features = ia64_env_cpu_class(ctx->env)->cpuid_features;
        const bool bit = extract64(features, op->immediate, 1);
        const bool condition = is_nz ? bit : !bit;

        ia64_gen_predicate_test_write(insn, tcg_constant_i64(condition),
                                      tcg_constant_i64(!condition));
        break;
    }
    case IA64_OP_MOVL:
        if (op->destination != 0) {
            tcg_gen_movi_i64(cpu_gr[op->destination], op->immediate);
            ia64_gen_gr_nat_clear(insn, op->destination);
        }
        break;
    case IA64_OP_ADDP4:
        if (op->destination != 0) {
            ia64_gen_addp4_result(cpu_gr[op->destination],
                                  ia64_gr_src(op->source1),
                                  ia64_gr_src(op->source2));
            ia64_gen_gr_nat_from_2(insn, op->destination, op->source1,
                                   op->source2);
        }
        break;
    case IA64_OP_ADDP4_IMM:
        if (op->destination != 0) {
            TCGv_i64 imm = tcg_constant_i64(op->immediate);
            ia64_gen_addp4_result(cpu_gr[op->destination], imm,
                                  ia64_gr_src(op->source2));
            ia64_gen_gr_nat_from_1(insn, op->destination, op->source2);
        }
        break;
    default:
        return IA64_GEN_UNHANDLED;
    }
    return IA64_GEN_CONTINUE;
}
