# SPDX-License-Identifier: GPL-2.0-or-later
"""Lift the scalar optimizer's constant, copy and mask rewrites."""

import re

from Decode.check import expression
from Integer.check import compact, require
from SlotControl.reify import fixed, function, recognize


OPS = ("add", "sub", "and", "or", "xor")
INTEGER = r"-?(?:[0-9]+|0x[0-9a-fA-F]+)"
EXPR = r"[^;]+"

# Complete helper bodies delimit the modeled i64 contexts. In particular,
# mov elimination uses a known-copy relation, and argument copying and host
# operation support remain inputs to the local rewrite.
HELPERS = {
    "ti_is_const": "c1b258e736398fb6db28f754c580b2889ed98280487342d9cbe9c6e84dc9dd3a",
    "ti_const_val": "48a9196d244cd9e68df952eb3e8ee4015cabf48a3d753865715f47e5cffd646f",
    "fold_const2": "c304ac182574735596bc83eb41fa2156ba72ec4d9366a7b1ebc390f3c148d21a",
    "fold_const2_commutative": "0271e3aeaaf53d864d1f478088f47194a75dc6b737fc91f4f744a6827f29e910",
    "fold_masks_zosa_int": "0ce0f3f3d0117ff1ec16db5f5a3280eb5710b4e4b83b5c3aca8a1d971314d40e",
    "fold_masks_zosa": "74ae77df3e84bd078a11281a765cd20616e2187fcfe84ab6ac428a24a73f015f",
    "fold_masks_zos": "e1b2a2cd74d8a4d59c95b54e8807cb9c713bd4f403f8bd9ca5626709d912b0f9",
    "fold_masks_z": "a567134d988f6481bf5241186ca93966f89c6a662d9d34b880d45011c82ddbf6",
    "fold_to_not": "d68adfad83783baddc03156b4bc52040ce1a51e90cb36711c2ea35086f7187aa",
    "fold_neg_no_const": "e3266b752d4f73a2e9e58c44c694e10fc5858a9010ee7ee76e77e98342beaeea",
    "fold_sub_to_neg": "aeb3c0a8cc845a41a4bed76d81fa87e571efb47392084997b93036cbd61d31aa",
    "fold_not": "80b2349c4a2f37f557e2e03aa1d17a974494359f4564ad2f85ed3a7732deeceb",
    "fold_xi_to_x": "bb3387c2f9dc940bb0134929e89e2eeef847b8c2ddc3d2f5bafde4879060bb06",
    "fold_xi_to_not": "af196bdab437725c84377144b0677fd800bc3c0f3c182102ff8fbecc2433b83a",
    "fold_xx_to_x": "614d4855e1d8d2b74d53f9188755160708de1cb321565f9d5f5556d5f6d6c354",
    "fold_xx_to_i": "fb80a3bc0bb9b15341d00d098412e2982f46ad5dd606f3c553131cf4e90e2410",
    "tcg_opt_gen_mov": "224361db6f336f3872e4c16e5f316db284f40a5144806fd4e59f90306e991a1e",
    "tcg_opt_gen_movi": "8ec78cf4a1caea9dc590f3d2bac670225620e7a6c32f8137fbbc8ec62c595660",
    "arg_new_constant": "a4c2e2d0606b5b27965bc6ca9862bed087be65c2a316cff24d8aeedfe11c8895",
    "args_are_copies": "b3e88e0584aa57d66fc25b9db58d5239389971ea0b8559116c128abf30c86a58",
    "swap_commutative": "cfa20648c2b2645f79877b1f88e8e0bacf786ee83d5765e0f45b8b8bc3323f41",
    "finish_folding": "2f263a13700e348e57aa448b3aa38fb920da28d1d2fc1a8cf72f7c19a2536ddd",
    "init_ts_info": "a6b1cf11738bea122caa0e3be76cde24d6ed699cba4b5251b5443e76e9f9344e",
}


def constant_operations(source):
    body = compact(function(source, "do_constant_folding_2"))
    require(body.startswith("uint64_tl64,h64;switch(op){"),
            "changed constant folder before its opcode switch")
    output = []
    for op in OPS:
        match = re.search(rf"caseINDEX_op_{op}:(?:caseINDEX_op_{op}_vec:)?return([^;]+);", body)
        require(match is not None, f"unknown constant TCG {op} expression")
        output.append(f"  | .{op} => {expression(match[1], {'x': 'left', 'y': 'right'})}")
    recognize(source, "do_constant_folding", """
        uint64_t res = do_constant_folding_2(op, type, x, y);
        if (type == TCG_TYPE_I32) {
            res = (int32_t)res;
        }
        return res;
    """)
    return "def cConstant (op : Opcode) (left right : Word) : Word :=\n  match op with\n" + "\n".join(output)


MASK_FIELDS = {"t1->z_mask": "f.x.mayOne", "t2->z_mask": "f.y.mayOne",
               "t1->o_mask": "f.x.mustOne", "t2->o_mask": "f.y.mustOne"}


def mask_functions(op, fields, changed=True):
    def lift(key):
        src = fields[key]
        for idx, (name, _) in enumerate(MASK_FIELDS.items()):
            src = src.replace(name, f"m{idx}")
        names = {f"m{idx}": value for idx, value in enumerate(MASK_FIELDS.values())}
        return expression(src, names)

    parts = [f"def {op}Result (f : Info) : Masks := ⟨{lift('may')}, {lift('ones')}⟩"]
    if changed:
        parts.append(f"def {op}Affected (f : Info) : Word := {lift('affected')}")
    return "\n".join(parts)


def word_literal(text):
    return f"{int(text, 0) % (1 << 64)}#64"


def arithmetic(source):
    add = recognize(source, "fold_add", """
        if (fold_const2_commutative(ctx, op) || fold_xi_to_x(ctx, op, @zero@)) {
            return true;
        }
        return finish_folding(ctx, op);
    """, zero=INTEGER)
    sub = recognize(source, "fold_sub", """
        if (fold_const2(ctx, op) || fold_xx_to_i(ctx, op, @copies@) ||
            fold_xi_to_x(ctx, op, @zero@) || fold_sub_to_neg(ctx, op)) {
            return true;
        }
        if (arg_is_const(op->args[2])) {
            uint64_t val = arg_const_val(op->args[2]);
            op->opc = INDEX_op_add;
            op->args[2] = arg_new_constant(ctx, @immediate@);
        }
        return finish_folding(ctx, op);
    """, copies=INTEGER, zero=INTEGER, immediate=EXPR)
    source_imm = sub["immediate"]
    if source_imm.startswith("-"):
        source_imm = "0" + source_imm
    imm = expression(source_imm, {"val": "f.y.mayOne"})
    return f"""
def foldAdd (f : Info) : Rewrite :=
  if constOperands f then bothConstants .add f
  else if isConstant f.y {word_literal(add['zero'])} then identity f.x
  else keep .add Masks.unknown

def foldSub (f : Info) : Rewrite :=
  if constOperands f then bothConstants .sub f
  else if f.copies then literal {word_literal(sub['copies'])}
  else if isConstant f.y {word_literal(sub['zero'])} then identity f.x
  else if isConstant f.x 0 then ⟨.negSecond, Masks.unknown⟩
  else if f.y.known then ⟨.addImmediate {imm}, Masks.unknown⟩
  else keep .sub Masks.unknown
"""


def bitwise(source):
    conjunction = recognize(source, "fold_and", """
        uint64_t z_mask, o_mask, s_mask, a_mask;
        TempOptInfo *t1, *t2;
        if (fold_const2_commutative(ctx, op)) {
            return true;
        }
        t1 = arg_info(op->args[1]);
        t2 = arg_info(op->args[2]);
        z_mask = @may@;
        o_mask = @ones@;
        s_mask = t1->s_mask & t2->s_mask;
        a_mask = @affected@;
        if (!fold_masks_zosa_int(ctx, op, z_mask, o_mask, s_mask, a_mask)) {
            if (op->opc == INDEX_op_and && ti_is_const(t2)) {
                uint64_t val = ti_const_val(t2);
                if (!(val & (val + 1))) {
                    unsigned len = ctz64(~val);
                    if (TCG_TARGET_extract_valid(ctx->type, 0, len)) {
                        op->opc = INDEX_op_extract;
                        op->args[2] = 0;
                        op->args[3] = len;
                    }
                }
            } else {
                fold_xx_to_x(ctx, op);
            }
        }
        return true;
    """, may=EXPR, ones=EXPR, affected=EXPR)
    disjunction = recognize(source, "fold_or", """
        uint64_t z_mask, o_mask, s_mask, a_mask;
        TempOptInfo *t1, *t2;
        if (fold_const2_commutative(ctx, op) || fold_xi_to_x(ctx, op, @zero@) ||
            fold_xx_to_x(ctx, op)) {
            return true;
        }
        t1 = arg_info(op->args[1]);
        t2 = arg_info(op->args[2]);
        z_mask = @may@;
        o_mask = @ones@;
        s_mask = t1->s_mask & t2->s_mask;
        a_mask = @affected@;
        return fold_masks_zosa(ctx, op, z_mask, o_mask, s_mask, a_mask);
    """, may=EXPR, ones=EXPR, affected=EXPR, zero=INTEGER)
    exclusive = recognize(source, "fold_xor", """
        uint64_t z_mask, o_mask, s_mask;
        TempOptInfo *t1, *t2;
        if (fold_const2_commutative(ctx, op) || fold_xx_to_i(ctx, op, @copies@) ||
            fold_xi_to_x(ctx, op, @zero@) || fold_xi_to_not(ctx, op, @complement@)) {
            return true;
        }
        t1 = arg_info(op->args[1]);
        t2 = arg_info(op->args[2]);
        z_mask = @may@;
        o_mask = @ones@;
        s_mask = t1->s_mask & t2->s_mask;
        return fold_masks_zos(ctx, op, z_mask, o_mask, s_mask);
    """, may=EXPR, ones=EXPR, zero=INTEGER, copies=INTEGER, complement=INTEGER)
    return "\n\n".join([
        mask_functions("and", conjunction), mask_functions("or", disjunction),
        mask_functions("xor", exclusive, changed=False),
        f"""
def foldAnd (f : Info) : Rewrite :=
  if constOperands f then bothConstants .and f
  else if (andResult f).known then literal (andResult f).mustOne
  else if andAffected f = 0 then identity f.x
  else if f.y.known then
    if f.y.mayOne &&& (f.y.mayOne + 1) = 0 ∧ f.extractSupported then
      ⟨.extractFirst f.extractLength, andResult f⟩
    else keep .and (andResult f)
  else if f.copies then identity f.x
  else keep .and (andResult f)

def foldOr (f : Info) : Rewrite :=
  if constOperands f then bothConstants .or f
  else if isConstant f.y {word_literal(disjunction['zero'])} then identity f.x
  else if f.copies then identity f.x
  else if (orResult f).known then literal (orResult f).mustOne
  else if orAffected f = 0 then identity f.x
  else keep .or (orResult f)

def foldXor (f : Info) : Rewrite :=
  if constOperands f then bothConstants .xor f
  else if f.copies then literal {word_literal(exclusive['copies'])}
  else if isConstant f.y {word_literal(exclusive['zero'])} then identity f.x
  else if isConstant f.y {word_literal(exclusive['complement'])} && f.hasNot then
    let negated : Masks := ⟨~~~f.x.mustOne, ~~~f.x.mayOne⟩
    ⟨.complementFirst, negated⟩
  else if (xorResult f).known then literal (xorResult f).mustOne
  else keep .xor (xorResult f)
""",
    ])


def generate(source, utils):
    for name, digest in HELPERS.items():
        fixed(source, name, digest)
    optimizer = compact(function(source, "tcg_optimize"))
    for op in OPS:
        require(re.search(rf"caseINDEX_op_{op}:(?:caseINDEX_op_{op}_vec:)?"
                          rf"done=fold_{op}\(&ctx,op\);break;", optimizer),
                f"TCG optimize dispatch no longer selects fold_{op}")
    recognize(utils, "ctz64", "return val ? __builtin_ctzll(val) : 64;")
    return "\n\n".join([
        "namespace IA64Optimizer\nlocal notation \"Word\" => BitVec 64",
        constant_operations(source),
        """def constOperands (f : Info) : Bool := f.x.known && f.y.known
def isConstant (m : Masks) (value : Word) : Bool := m.known && m.mayOne == value
def bothConstants (op : Opcode) (f : Info) : Rewrite :=
  literal (cConstant op f.x.mayOne f.y.mayOne)
""",
        arithmetic(source), bitwise(source), "end IA64Optimizer",
    ])
