# SPDX-License-Identifier: GPL-2.0-or-later
"""Extract the direct TCG and helper significand paths for F9 operations."""

import re

from check import get_match
from merge import expect

LOGICAL = (("FAND", "and", "band"), ("FANDCM", "andcm", "bandComplement"),
           ("FOR", "or", "bor"), ("FXOR", "xor", "bxor"))
MIX = (("FMIX_LR", "mixLR"), ("FMIX_R", "mixR"), ("FMIX_L", "mixL"))
OPERATIONS = {
    "and": "a &&& b", "andc": "a &&& ~~~b", "or": "a ||| b", "xor": "a ^^^ b",
}
HELPER_NAT = "if(ia64_fr_write_nat_if_any2(env,r1,r2,r3)){return;}"
TCG_ARGS = ("tcg_env,tcg_constant_i32(op->destination),"
            "tcg_constant_i32(op->source1),tcg_constant_i32(op->source2)")
TCG_ENTRY = (
    "constIA64FloatingOperands*op=&insn->operands.floating;TCGv_i64left,right,value;"
    "TCGLabel*slow,*done;if(op->destination<=1){return;}slow=gen_new_label();"
    "done=gen_new_label();value=tcg_temp_new_i64();"
    "ia64_gen_fp_bitwise_operands(insn,slow,&left,&right);"
)
TCG_EXIT = (
    "ia64_gen_fr_mov_sig(insn->ctx,op->destination,value);"
    "tcg_gen_br(done);gen_set_label(slow);"
)


def validate_routes(translate, fpgen, fp, body):
    expect(body(translate, "ia64_gen_fr_fmov_slow_read"),
           "TCGv_i64tags;TCGv_i64orphan_origin;"
           "if(reg<=1){returntcg_constant_i64(0);}"
           "if(ctx->fp.format[reg]!=IA64_FR_FORMAT_UNKNOWN){"
           "returntcg_constant_i64(ctx->fp.format[reg]==IA64_FR_FORMAT_NAT);}"
           "tags=tcg_temp_new_i64();orphan_origin=tcg_temp_new_i64();"
           "tcg_gen_andc_i64(orphan_origin,cpu_fr_int_origin[reg/64],cpu_fr_sig[reg/64]);"
           "tcg_gen_or_i64(tags,cpu_fr_nat[reg/64],cpu_fr_ext_valid[reg/64]);"
           "tcg_gen_or_i64(tags,tags,orphan_origin);tcg_gen_shri_i64(tags,tags,reg%64);"
           "tcg_gen_andi_i64(tags,tags,1);returntags;",
           "logical helper gate for extended, NaT and orphan tags")
    expect(body(translate, "ia64_gen_fr_sig_read"),
           "TCGv_i64bit;if(reg<=1){returntcg_constant_i64(1);}"
           "if(ctx->fp.format[reg]!=IA64_FR_FORMAT_UNKNOWN){"
           "returntcg_constant_i64(ctx->fp.format[reg]==IA64_FR_FORMAT_SIG);}"
           "bit=tcg_temp_new_i64();tcg_gen_shri_i64(bit,cpu_fr_sig[reg/64],reg%64);"
           "tcg_gen_andi_i64(bit,bit,1);returnbit;", "paired significand cache selection")
    expect(body(translate, "ia64_gen_fr_mov_sig"),
           "if(ia64_fr_is_writable(reg)){tcg_gen_mov_i64(cpu_fr[reg],value);"
           "ia64_gen_fr_nat_clear(reg);ia64_gen_fr_sig_set(reg);"
           "ia64_gen_fr_ext_clear(reg);ia64_gen_fr_mark_written(ctx,reg);}",
           "TCG integer result write")
    expect(body(translate, "ia64_gen_fr_sig_set"),
           "if(reg<=1){return;}tcg_gen_ori_i64(cpu_fr_sig[reg/64],cpu_fr_sig[reg/64],"
           "1ULL<<(reg%64));tcg_gen_ori_i64(cpu_fr_int_origin[reg/64],"
           "cpu_fr_int_origin[reg/64],1ULL<<(reg%64));"
           "tcg_gen_st_i64(cpu_fr[reg],tcg_env,offsetof(CPUIA64State,fp.fr_int_value[reg]));",
           "TCG paired significand and integer-origin tag write")
    expect(body(fp, "ia64_fr_write_sig"),
           "uint64_tbit;if(unlikely(env->fp.transaction.active)){ia64_fp_backup_fr(env,reg);}"
           "if(reg<=1){return;}bit=ia64_fpreg_tag_bit(reg);ia64_fpreg_clear_tags(env,reg);"
           "env->fp.fr[reg]=value;env->fp.fr_sig[reg/64]|=bit;"
           "env->fp.fr_int_value[reg]=value;env->fp.fr_int_origin[reg/64]|=bit;"
           "ia64_fpreg_mark_written(env,reg);", "helper integer result write")
    expect(body(fp, "ia64_fr_nat_any2"),
           "returnia64_fr_nat_get(env,reg1)||ia64_fr_nat_get(env,reg2);", "binary helper NaT test")
    expect(body(fp, "ia64_fr_write_nat_if_any2"),
           "if(!ia64_fr_nat_any2(env,src1,src2)){returnfalse;}"
           "ia64_fr_write_nat(env,dst);returntrue;", "binary helper NaT result")
    expect(body(fpgen, "ia64_gen_fp_bitwise_operands"),
           "constIA64FloatingOperands*op=&insn->operands.floating;"
           "TCGv_i64special=tcg_temp_new_i64();"
           "tcg_gen_or_i64(special,ia64_gen_fr_fmov_slow_read(insn->ctx,op->source1),"
           "ia64_gen_fr_fmov_slow_read(insn->ctx,op->source2));"
           "tcg_gen_brcondi_i64(TCG_COND_NE,special,0,slow);"
           "*left=ia64_gen_fp_significand(insn,op->source1);"
           "*right=op->source1==op->source2?*left:ia64_gen_fp_significand(insn,op->source2);",
           "binary logical operand selection and identical source reuse")
    projection = body(fpgen, "ia64_gen_fp_significand")
    expect(projection[:projection.index("exponent=tcg_temp_new_i64();")],
           "TCGv_i64value,exponent;value=tcg_temp_new_i64();"
           "if(reg<=1){tcg_gen_mov_i64(value,ia64_fr_significand_src(reg));returnvalue;}",
           "TCG fixed significand read")
    get_match(re.escape(
        "tcg_gen_or_i64(value,value,exponent);"
        "tcg_gen_movcond_i64(TCG_COND_NE,value,ia64_gen_fr_sig_read(insn->ctx,reg),"
        "tcg_constant_i64(0),ia64_fr_significand_src(reg),value);returnvalue;"
    ) + "$", projection, "TCG tagged significand bypass")


def significand_read(translate, fp, header, body, macro, word):
    zero, one = get_match(
        r"if\(reg==0\)\{returntcg_constant_i64\(([^;]+)\);\}"
        r"if\(reg==1\)\{returntcg_constant_i64\(([^;]+)\);\}returncpu_fr\[reg\];",
        body(translate, "ia64_fr_significand_src"), "TCG fixed significand constants",
        full=True,
    ).groups()
    helper_zero, helper_one, mantissa, test, integer_bit = get_match(
        r"uint64_tbit;uint64_tvalue;uint64_tsignificand;"
        r"if\(reg==IA64_FR_ZERO_INDEX\)\{return([^;]+);\}"
        r"if\(reg==IA64_FR_ONE_INDEX\)\{return([^;]+);\}"
        r"bit=ia64_fpreg_tag_bit\(reg\);"
        r"if\(env->fp.fr_nat\[reg/64\]&bit\)\{return0;\}"
        r"if\(env->fp.fr_ext_valid\[reg/64\]&bit\)\{returnenv->fp.fr_ext_mant\[reg\];\}"
        r"if\(env->fp.fr_sig\[reg/64\]&bit\)\{returnenv->fp.fr\[reg\];\}"
        r"value=env->fp.fr\[reg\];significand=([^;]+);if\((.+)!=0\)\{"
        r"significand\|=([^;]+);\}returnsignificand;",
        body(fp, "ia64_fr_significand"), "helper canonical significand read", full=True,
    ).groups()
    integer_name = "IA64_FP_SIGNIFICAND_INTEGER_BIT"
    names = {integer_name: word(macro(header, integer_name), {}), "value": "raw"}
    return f"""
def cFastSigZero : Word := {word(zero, {})}
def cFastSigOne : Word := {word(one, {})}
def cHelperSigZero : Word := {word(helper_zero, names)}
def cHelperSigOne : Word := {word(helper_one, names)}

def cHelperBinarySig (raw : Word) : Word :=
  {word(mantissa, names)} ||| (if {word(test, names)} = 0 then 0 else {word(integer_bit, names)})
"""


def logical(fpgen, fp, wrapper, body, word):
    cases = []
    helpers = []
    for number, (opcode, helper, mode) in enumerate(LOGICAL):
        get_match(re.escape(f"caseIA64_OP_{opcode}:ia64_gen_fp_logical(insn,{number});break;"),
                  body(fpgen, "ia64_gen_fp"), f"F9 dispatch for {opcode}")
        expect(body(wrapper, f"helper_flogical_{helper}"),
               f"ia64_fp_flogical_{helper}(arg0,arg1,arg2,arg3);", f"TCG wrapper for {opcode}")
        rhs = get_match(re.escape(HELPER_NAT) + r"ia64_fr_write_sig\(env,r1,(.+)\);",
                        body(fp, f"ia64_fp_flogical_{helper}"), f"helper payload for {opcode}",
                        full=True)[1]
        rhs = rhs.replace("ia64_fr_significand(env,r2)", "a").replace("ia64_fr_significand(env,r3)", "b")
        helpers.append(f"  | .{mode} => {word(rhs, {'a': 'a', 'b': 'b'})}")
        cases.append(rf"case{number}:tcg_gen_({'|'.join(OPERATIONS)})_i64\(value,left,right\);break;")
    dispatcher = "".join(f"case{i}:gen_helper_flogical_{helper}({TCG_ARGS});break;"
                         for i, (_, helper, _) in enumerate(LOGICAL))
    unreachable = "default:g_assert_not_reached();}"
    selected = get_match(re.escape(TCG_ENTRY + "switch(mode){") + "".join(cases)
                         + re.escape(unreachable + TCG_EXIT + "switch(mode){"
                                     + dispatcher + unreachable + "gen_set_label(done);"),
                         body(fpgen, "ia64_gen_fp_logical"), "direct logical TCG operations", full=True)
    tcg_cases = "\n".join(f"  | .{mode} => {OPERATIONS[operation]}"
                          for (_, _, mode), operation in zip(LOGICAL, selected.groups(), strict=True))
    return tcg_cases, "\n".join(helpers)


def mix(fpgen, fp, wrapper, body, word):
    for number, (opcode, _) in enumerate(MIX):
        get_match(re.escape(f"caseIA64_OP_{opcode}:ia64_gen_fp_mix(insn,{number});break;"),
                  body(fpgen, "ia64_gen_fp"), f"F9 dispatch for {opcode}")
    expect(body(wrapper, "helper_fmix"), "ia64_fp_fmix(arg0,arg1,arg2,arg3,arg4);", "TCG mix wrapper")
    count, offset, width, mask = get_match(
        re.escape(TCG_ENTRY) +
        r"if\(form==2\)\{tcg_gen_shri_i64\(value,right,(\d+)\);\}"
        r"else\{tcg_gen_ext32u_i64\(value,right\);\}"
        r"if\(form==1\)\{tcg_gen_deposit_i64\(value,value,left,(\d+),(\d+)\);\}"
        r"else\{tcg_gen_andi_i64\(left,left,([^;]+)\);tcg_gen_or_i64\(value,value,left\);\}"
        + re.escape(TCG_EXIT + f"gen_helper_fmix({TCG_ARGS},tcg_constant_i32(form));gen_set_label(done);"),
        body(fpgen, "ia64_gen_fp_mix"), "direct mix TCG operations", full=True,
    ).groups()
    offset, width = int(offset), int(width)
    if not (int(count) < 64 and 0 <= offset < 64 and 1 <= width <= 64 - offset):
        raise ValueError("invalid TCG mix shift or deposit range")
    deposit_mask = ((1 << width) - 1) << offset
    hi_r, lo_r, hi_l, lo_l, lo_lr, packed = get_match(
        re.escape("uint64_tf2;uint64_tf3;uint32_thi;uint32_tlo;" + HELPER_NAT +
                  "f2=ia64_fr_significand(env,r2);f3=ia64_fr_significand(env,r3);") +
        r"if\(form==1\)\{hi=([^;]+);lo=([^;]+);\}else\{hi=([^;]+);"
        r"lo=form==2\?([^:]+):([^;]+);\}ia64_fr_write_sig\(env,r1,(.+)\);",
        body(fp, "ia64_fp_fmix"), "helper bitmix payload", full=True,
    ).groups()
    mapped = {"f2": "a", "f3": "b"}
    masks = word(mask, {})
    tcg_modes = [
        f"  | .mixLR => (b.extractLsb' 0 32).zeroExtend 64 ||| (a &&& {masks})",
        f"  | .mixR => (((b.extractLsb' 0 32).zeroExtend 64) &&& ~~~({deposit_mask} : Word))"
        f" ||| ((a <<< ({offset} : Word)) &&& ({deposit_mask} : Word))",
        f"  | .mixL => (b >>> ({count} : Word)) ||| (a &&& {masks})",
    ]
    helper_modes = []
    for (_, mode), high, low in zip(MIX, (hi_l, hi_r, hi_l), (lo_lr, lo_r, lo_l), strict=True):
        bindings = {part: f"(({word(expression, mapped)}).extractLsb' 0 32).zeroExtend 64"
                    for part, expression in (("hi", high), ("lo", low))}
        helper_modes.append(f"  | .{mode} => {word(packed, bindings)}")
    return "\n".join(tcg_modes), "\n".join(helper_modes)


def generate_bitwise(translate, fpgen, fp, wrapper, header, body, macro, word):
    validate_routes(translate, fpgen, fp, body)
    tcg_logic, helper_logic = logical(fpgen, fp, wrapper, body, word)
    tcg_mix, helper_mix = mix(fpgen, fp, wrapper, body, word)
    return significand_read(translate, fp, header, body, macro, word) + f"""
def cTcgBits (form : BitwiseForm) (a b : Word) : Word :=
  match form with
{tcg_logic}
{tcg_mix}

def cHelperBits (form : BitwiseForm) (a b : Word) : Word :=
  match form with
{helper_logic}
{helper_mix}
"""
