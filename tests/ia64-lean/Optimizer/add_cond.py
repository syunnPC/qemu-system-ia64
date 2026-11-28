# SPDX-License-Identifier: GPL-2.0-or-later

from Integer.check import compact, require
from SlotControl.reify import function, recognize


def check_source(root, optimizer):
    translator = (root / "target/ia64/translate/translate.c").read_text(encoding="utf-8")
    branchless = compact(function(translator, "ia64_gen_predicated_integer"))
    require(compact("""
        case IA64_OP_ADD:
            tcg_gen_add_i64(result, ia64_gr_src(op->source1), ia64_gr_src(op->source2));
            break;
    """) in branchless, "conditional ADD value emission changed")
    require(compact("""
        tcg_gen_movcond_i64(TCG_COND_NE, cpu_gr[op->destination], cpu_pr[insn->qp],
                           tcg_constant_i64(0), result, cpu_gr[op->destination]);
    """) in branchless, "conditional ADD destination emission changed")
    generic = (root / "tcg/tcg-op.c").read_text(encoding="utf-8")
    recognize(generic, "tcg_gen_movcond_i64", """
        if (cond == TCG_COND_ALWAYS) {
            tcg_gen_mov_i64(ret, v1);
        } else if (cond == TCG_COND_NEVER) {
            tcg_gen_mov_i64(ret, v2);
        } else {
            tcg_gen_op6i_i64(INDEX_op_movcond, ret, c1, c2, v1, v2, cond);
        }
    """)
    recognize(optimizer, "pref_commutative", """
        return !ti_is_const(ti) ? 0 : ti_const_val(ti) ? 3 : 2;
    """)
    recognize(optimizer, "swap_commutative", """
        TCGArg a1 = *p1, a2 = *p2;
        int sum = 0;
        sum += pref_commutative(arg_info(a1));
        sum -= pref_commutative(arg_info(a2));
        if (sum > 0 || (sum == 0 && dest == a2)) {
            *p1 = a2;
            *p2 = a1;
            return true;
        }
        return false;
    """)
    recognize(optimizer, "copy_propagate", """
        for (int i = nb_oargs; i < nb_oargs + nb_iargs; i++) {
            TCGTemp *ts = arg_temp(op->args[i]);
            if (ts_is_copy(ts)) {
                op->args[i] = temp_arg(find_better_copy(ts));
            }
        }
    """)
    fold = compact(function(optimizer, "fold_movcond"))
    require(compact("""
        if (args_are_copies(op->args[3], op->args[4])) {
            return tcg_opt_gen_mov(ctx, op, op->args[0], op->args[3]);
        }
        if (swap_commutative(op->args[0], &op->args[4], &op->args[3])) {
            op->args[5] = tcg_invert_cond(op->args[5]);
        }
    """) in fold, "MOVCOND copy or comparison polarity changed")
    require(compact("""
        i = do_constant_folding_cond1(ctx, op, NO_DEST, &op->args[1],
                                      &op->args[2], &op->args[5]);
        if (i >= 0) {
            return tcg_opt_gen_mov(ctx, op, op->args[0], op->args[4 - i]);
        }
    """) in fold, "MOVCOND constant condition selection changed")

    require(compact("""
        if (tv == 1 && fv == 0) {
            op->opc = INDEX_op_setcond;
            op->args[3] = cond;
        } else if (fv == 1 && tv == 0) {
            op->opc = INDEX_op_setcond;
            op->args[3] = tcg_invert_cond(cond);
        } else if (tv == -1 && fv == 0) {
            op->opc = INDEX_op_negsetcond;
            op->args[3] = cond;
        } else if (fv == -1 && tv == 0) {
            op->opc = INDEX_op_negsetcond;
            op->args[3] = tcg_invert_cond(cond);
        }
    """) in fold, "MOVCOND constant arm lowering changed")
    recognize(optimizer, "cmp_better_copy", "return a->kind < b->kind ? b : a;")
    recognize(optimizer, "find_better_copy", """
        TCGTemp *i, *ret;
        if (temp_readonly(ts)) { return ts; }
        ret = ts;
        for (i = ts_info(ts)->next_copy; i != ts; i = ts_info(i)->next_copy) {
            ret = cmp_better_copy(ret, i);
        }
        return ret;
    """)
    condition = (root / "include/tcg/tcg-cond.h").read_text(encoding="utf-8")
    require("TCG_COND_EQ=8|0|0|0,TCG_COND_NE=8|0|0|1," in compact(condition),
            "TCG equality conditions changed assignment")
    recognize(condition, "tcg_invert_cond", "return (TCGCond)(c ^ 1);")
