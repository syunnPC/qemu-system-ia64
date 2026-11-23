# SPDX-License-Identifier: GPL-2.0-or-later
"""Read the shift, complement and low-32 extraction optimizer paths."""

import re

from Decode.check import expression
from Integer.check import compact, require
from Optimizer import check
from SlotControl.reify import function, recognize

BITOPS = "include/qemu/bitops.h"


def generate(root, source):
    constants = compact(function(source, "do_constant_folding_2"))
    shifts = []
    for opcode in ("shl", "shr"):
        match = re.search(
            rf"caseINDEX_op_{opcode}:if\(type==TCG_TYPE_I32\){{return[^;]+;}}"
            r"return\(uint64_t\)x(<<|>>)\(y&([0-9]+)\);", constants)
        require(match is not None, f"unknown i64 {opcode} constant expression")
        shifts.append(f"  | .{opcode}, x, y => x {'<<<' if match[1] == '<<' else '>>>'} (y &&& {match[2]}#64)")
    match = re.search(r"caseINDEX_op_not:caseINDEX_op_not_vec:return([^;]+);", constants)
    require(match is not None, "unknown NOT constant expression")
    cnot = expression(match[1], {"x": "x"})
    narrow = recognize(check.load(root, BITOPS), "extract64", """
        assert(start >= 0 && length > 0 && length <= 64 - start);
        return (value >> start) & (~0ULL >> (@width@ - length));
    """, width=r"[0-9]+")

    nmasks = recognize(source, "fold_not", """
        TempOptInfo *t1;
        if (fold_const1(ctx, op)) { return true; }
        t1 = arg_info(op->args[1]);
        return fold_masks_zos(ctx, op, @may@, @must@, t1->s_mask);
    """, may=r"[^,]+", must=r"[^,]+")
    def compile_mask(key):
        mask = nmasks[key].replace("t1->z_mask", "may").replace("t1->o_mask", "must")
        return expression(mask, {"may": "m.mayOne", "must": "m.mustOne"})

    recognize(source, "fold_extract", """
        uint64_t z_mask, o_mask, a_mask;
        TempOptInfo *t1 = arg_info(op->args[1]);
        int pos = op->args[2];
        int len = op->args[3];
        if (ti_is_const(t1)) {
            return tcg_opt_gen_movi(ctx, op, op->args[0],
                                    extract64(ti_const_val(t1), pos, len));
        }
        z_mask = extract64(t1->z_mask, pos, len);
        o_mask = extract64(t1->o_mask, pos, len);
        a_mask = pos ? -1 : t1->z_mask ^ z_mask;
        return fold_masks_zosa(ctx, op, z_mask, o_mask, 0, a_mask);
    """)
    recognize(source, "fold_shift", """
        uint64_t s_mask, z_mask, o_mask;
        TempOptInfo *t1, *t2;
        if (fold_const2(ctx, op) ||
            fold_ix_to_i(ctx, op, 0) ||
            fold_xi_to_x(ctx, op, 0)) {
            return true;
        }
        t1 = arg_info(op->args[1]);
        t2 = arg_info(op->args[2]);
        s_mask = t1->s_mask;
        z_mask = t1->z_mask;
        o_mask = t1->o_mask;
        if (ti_is_const(t2)) {
            int sh = ti_const_val(t2);
            z_mask = do_constant_folding(op->opc, ctx->type, z_mask, sh);
            o_mask = do_constant_folding(op->opc, ctx->type, o_mask, sh);
            s_mask = do_constant_folding(op->opc, ctx->type, s_mask, sh);
            return fold_masks_zos(ctx, op, z_mask, o_mask, s_mask);
        }
        switch (op->opc) {
        case INDEX_op_sar:
            return fold_masks_s(ctx, op, s_mask);
        case INDEX_op_shr:
            if (~z_mask & -s_mask) {
                return fold_masks_s(ctx, op, s_mask);
            }
            break;
        default:
            break;
        }
        return finish_folding(ctx, op);
    """)
    recognize(source, "fold_const1", """
        if (arg_is_const(op->args[1])) {
            uint64_t t = arg_const_val(op->args[1]);
            t = do_constant_folding(op->opc, ctx->type, t, 0);
            return tcg_opt_gen_movi(ctx, op, op->args[0], t);
        }
        return false;
    """)
    recognize(source, "fold_ix_to_i", """
        if (arg_is_const_val(op->args[1], i)) {
            return tcg_opt_gen_movi(ctx, op, op->args[0], i);
        }
        return false;
    """)
    recognize(source, "fold_masks_s", "return fold_masks_zosa(ctx, op, -1, 0, s_mask, -1);")
    dispatch = compact(function(source, "tcg_optimize"))
    paths = {
        "extract": "caseINDEX_op_extract:done=fold_extract(&ctx,op);break;",
        "not": "caseINDEX_op_not:caseINDEX_op_not_vec:done=fold_not(&ctx,op);break;",
        "shl/shr": "caseINDEX_op_shl:caseINDEX_op_shr:done=fold_shift(&ctx,op);break;",
    }
    for name, body in paths.items():
        require(dispatch.count(body) == 1, f"changed optimizer dispatch for {name}")
    return f"""
namespace IA64Optimizer
local notation "Word" => BitVec 64

def cExtraConstant : Extra → Word → Word → Word
  | .complement, x, _ => {cnot}
  | .low len, x, _ => x &&& (~~~0#64 >>> ({narrow['width']}#64 - len.zeroExtend 64))
{chr(10).join(shifts)}

def extraMasks (k : Extra) (m : Masks) (count : Word) : Masks :=
  match k with
  | .complement => ⟨{compile_mask('may')}, {compile_mask('must')}⟩
  | k => ⟨cExtraConstant k m.mayOne count, cExtraConstant k m.mustOne count⟩

def extraAffected (k : Extra) (m : Masks) : Word :=
  match k with
  | .low len => m.mayOne ^^^ cExtraConstant (.low len) m.mayOne 0
  | _ => ~~~0#64

def recordExtra (k : Extra) (m : Masks) (affected : Word) : Rewrite :=
  if m.known then literal m.mustOne
  else if affected = 0 then identity m
  else ⟨k.expression, m⟩

def unaryRewrite (k : Extra) (m : Masks) : Rewrite :=
  if m.known then literal (cExtraConstant k m.mayOne 0)
  else recordExtra k (extraMasks k m 0) (extraAffected k m)

def shiftRewrite (k : Extra) (x y : Masks) : Rewrite :=
  if x.known && y.known then literal (cExtraConstant k x.mayOne y.mayOne)
  else if isConstant x 0 then literal 0
  else if isConstant y 0 then identity x
  else if y.known then recordExtra k (extraMasks k x y.mayOne) (~~~0#64)
  else ⟨k.expression, Masks.unknown⟩

def extraRewrite (k : Extra) : Masks → Masks → Rewrite :=
  match k with
  | .complement | .low _ => fun x _ => unaryRewrite k x
  | .shl | .shr => shiftRewrite k

end IA64Optimizer
"""
