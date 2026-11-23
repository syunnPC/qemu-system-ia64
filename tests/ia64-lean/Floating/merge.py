# SPDX-License-Identifier: GPL-2.0-or-later
"""Extract the guarded fmerge value paths and the register-cache transitions."""

import re

from check import get_match


def expect(actual, expected, context):
    get_match(re.escape(expected), actual, context, full=True)


def validate_cache(fpreg, header, translate, fp, body, cpu):
    for name, index in (("ZERO", 0), ("ONE", 1)):
        get_match(rf"\bIA64_FR_{name}_INDEX\s*=\s*{index}\s*,", cpu, f"fixed f{index} index")
    expect(body(translate, "ia64_fr_is_writable"), "returnreg>1;", "TCG FP destination")
    expect(body(translate, "ia64_gen_fr_mov"),
           "if(ia64_fr_is_writable(reg)){tcg_gen_mov_i64(cpu_fr[reg],value);"
           "ia64_gen_fr_nat_clear(reg);ia64_gen_fr_sig_clear(reg);ia64_gen_fr_ext_clear(reg);"
           "ia64_gen_fr_mark_written(ctx,reg);}", "TCG binary result write")
    for name, fields in (("nat", ("nat",)), ("sig", ("sig", "int_origin")),
                         ("ext", ("ext_valid",))):
        masks = "".join(f"tcg_gen_andi_i64(cpu_fr_{field}[reg/64],cpu_fr_{field}[reg/64],"
                        "~(1ULL<<(reg%64)));" for field in fields)
        expect(body(translate, f"ia64_gen_fr_{name}_clear"), "if(reg<=1){return;}" + masks,
               f"TCG {name} tag clear")
    expect(body(translate, "ia64_gen_fr_special_read"),
           "TCGv_i64tags;if(reg<=1){returntcg_constant_i64(0);}"
           "if(ctx->fp.format[reg]!=IA64_FR_FORMAT_UNKNOWN){"
           "returntcg_constant_i64(ctx->fp.format[reg]!=IA64_FR_FORMAT_BINARY);}"
           "tags=tcg_temp_new_i64();"
           "tcg_gen_or_i64(tags,cpu_fr_nat[reg/64],cpu_fr_sig[reg/64]);"
           "tcg_gen_or_i64(tags,tags,cpu_fr_ext_valid[reg/64]);"
           "tcg_gen_or_i64(tags,tags,cpu_fr_int_origin[reg/64]);"
           "tcg_gen_shri_i64(tags,tags,reg%64);tcg_gen_andi_i64(tags,tags,1);returntags;",
           "TCG binary source eligibility")
    expect(body(fpreg, "ia64_fpreg_get_extended"),
           "if(reg<=1||reg>=IA64_FR_COUNT||"
           "!((env->fp.fr_ext_valid[reg/64]>>(reg%64))&1)){returnfalse;}"
           "*sign=(env->fp.fr_ext_sign[reg/64]>>(reg%64))&1;"
           "*exp=env->fp.fr_ext_exp[reg];*mant=env->fp.fr_ext_mant[reg];returntrue;",
           "authoritative extended FP cache")
    for name, tag in (("nat", "nat"), ("integer", "sig")):
        expect(body(header, f"ia64_fpreg_is_{name}"),
               f"returnreg>1&&reg<IA64_FR_COUNT&&((env->fp.fr_{tag}[reg/64]>>(reg%64))&1);",
               f"FP cache {name} read")
    expect(body(fpreg, "ia64_fpreg_to_spill"),
           "uint32_texp;boolsign;g_assert(reg<IA64_FR_COUNT);"
           "if(ia64_fpreg_is_nat(env,reg)){*low=0;exp=IA64_FP_REG_NATVAL_EXP;sign=false;}"
           "elseif(ia64_fpreg_get_extended(env,reg,&sign,&exp,low)){}"
           "elseif(ia64_fpreg_is_integer(env,reg)){*low=env->fp.fr[reg];"
           "exp=IA64_FP_REG_INTEGER_EXP;sign=false;}"
           "else{binary64_to_register_format(reg==IA64_FR_ZERO_INDEX?0:reg=="
           "IA64_FR_ONE_INDEX?IA64_FR_ONE:env->fp.fr[reg],low,&exp,&sign);}"
           "*high=(exp&0x1ffff)|((uint64_t)sign<<17);",
           "FP cache spill and fixed sources")
    expect(body(fpreg, "ia64_fpreg_from_spill"),
           "uint64_tcompact;uint32_texp;boolsign;uint64_tbit;g_assert(reg<IA64_FR_COUNT);"
           "if(reg<=1){return;}high&=IA64_FP_SPILL_EXP_SIGN_MASK;exp=high&0x1ffff;"
           "sign=(high>>17)&1;bit=ia64_fpreg_tag_bit(reg);ia64_fpreg_clear_tags(env,reg);"
           "if(!sign&&exp==IA64_FP_REG_NATVAL_EXP&&low==0){env->fp.fr[reg]=0;"
           "env->fp.fr_nat[reg/64]|=bit;}"
           "elseif(!sign&&exp==IA64_FP_REG_INTEGER_EXP){env->fp.fr[reg]=low;"
           "env->fp.fr_sig[reg/64]|=bit;env->fp.fr_int_value[reg]=low;"
           "env->fp.fr_int_origin[reg/64]|=bit;}"
           "elseif(register_format_to_binary64_exact(low,exp,sign,&compact)){"
           "env->fp.fr[reg]=compact;}"
           "else{env->fp.fr[reg]=0;env->fp.fr_ext_mant[reg]=low;env->fp.fr_ext_exp[reg]=exp;"
           "if(sign){env->fp.fr_ext_sign[reg/64]|=bit;}else{env->fp.fr_ext_sign[reg/64]&=~bit;}"
           "env->fp.fr_ext_valid[reg/64]|=bit;}"
           "ia64_fpreg_mark_written(env,reg);", "FP cache fill state")
    expect(body(header, "ia64_fpreg_tag_bit"), "return1ULL<<(reg%64);", "FP register tag position")
    expect(body(header, "ia64_fpreg_clear_tags"),
           "uint64_tmask=~ia64_fpreg_tag_bit(reg);env->fp.fr_nat[reg/64]&=mask;"
           "env->fp.fr_sig[reg/64]&=mask;env->fp.fr_ext_valid[reg/64]&=mask;"
           "env->fp.fr_int_origin[reg/64]&=mask;env->fp.fr_int_value[reg]=0;",
           "old FP cache representation removal")
    expect(body(fp, "ia64_fr_nat_get"), "returnia64_fpreg_is_nat(env,reg);", "helper NaT test")
    expect(body(fp, "ia64_fr_write_nat"),
           "if(unlikely(env->fp.transaction.active)){ia64_fp_backup_fr(env,reg);}"
           "ia64_fpreg_from_spill(env,reg,0,IA64_FP_REG_NATVAL_EXP);", "helper NaT write")


def tcg_merge(fpgen, body, word):
    helper = []
    for number, suffix in enumerate(("ns", "s", "se")):
        pattern = (f"caseIA64_OP_FMERGE{'_' + suffix.upper() if number else ''}:"
                   f"ia64_gen_fmerge(insn,{number});break;")
        get_match(re.escape(pattern), body(fpgen, "ia64_gen_fp"), "fmerge TCG opcode dispatch")
        helper.append(f"gen_helper_fmerge_{suffix}(tcg_env,tcg_constant_i32(op->destination),"
                      "tcg_constant_i32(op->source1),tcg_constant_i32(op->source2));")
    expect(body(fpgen, "ia64_gen_fmerge_helper"),
           f"if(form==0){{{helper[0]}}}elseif(form==1){{{helper[1]}}}else{{{helper[2]}}}",
           "fmerge helper dispatch")
    exp_mask, exp_zero = get_match(
        r"constuint64_texp_mask=([^;]+);TCGv_i64exponent;"
        r"if\(reg==IA64_FR_ONE_INDEX\)\{return;\}g_assert\(reg>IA64_FR_ONE_INDEX\);"
        r"exponent=tcg_temp_new_i64\(\);"
        r"tcg_gen_andi_i64\(exponent,ia64_fr_binary_src\(reg\),exp_mask\);"
        r"tcg_gen_brcondi_i64\(TCG_COND_EQ,exponent,([^,]+),slow\);"
        r"tcg_gen_brcondi_i64\(TCG_COND_EQ,exponent,exp_mask,slow\);",
        body(fpgen, "ia64_gen_fmerge_require_binary_normal"), "fmerge binary64 normal gate",
        full=True,
    ).groups()
    code = body(fpgen, "ia64_gen_fmerge")
    prefix = (
        "constIA64FloatingOperands*op=&insn->operands.floating;"
        "TCGv_i64special;TCGv_i64left;TCGv_i64right;TCGv_i64result;"
        "TCGLabel*slow;TCGLabel*done;uint64_tleft_mask;if(op->destination<=1){return;}"
        "if(form==2&&(op->source1==IA64_FR_ZERO_INDEX||op->source2==IA64_FR_ZERO_INDEX)){"
        "ia64_gen_fmerge_helper(op,form);return;}slow=NULL;done=NULL;"
        "if(op->source1>1||op->source2>1){slow=gen_new_label();done=gen_new_label();"
        "if(op->source1<=1){special=ia64_gen_fr_special_read(insn->ctx,op->source2);}"
        "elseif(op->source2<=1){special=ia64_gen_fr_special_read(insn->ctx,op->source1);}"
        "else{special=tcg_temp_new_i64();tcg_gen_or_i64(special,"
        "ia64_gen_fr_special_read(insn->ctx,op->source1),"
        "ia64_gen_fr_special_read(insn->ctx,op->source2));}"
        "tcg_gen_brcondi_i64(TCG_COND_NE,special,0,slow);"
        "if(form==2){ia64_gen_fmerge_require_binary_normal(op->source1,slow);"
        "ia64_gen_fmerge_require_binary_normal(op->source2,slow);}}"
    )
    suffix = (
        "left=tcg_temp_new_i64();right=tcg_temp_new_i64();result=tcg_temp_new_i64();"
        "if(form==0){tcg_gen_not_i64(left,ia64_fr_binary_src(op->source1));"
        "tcg_gen_andi_i64(left,left,left_mask);}else{"
        "tcg_gen_andi_i64(left,ia64_fr_binary_src(op->source1),left_mask);}"
        "tcg_gen_andi_i64(right,ia64_fr_binary_src(op->source2),~left_mask);"
        "tcg_gen_or_i64(result,left,right);ia64_gen_fr_mov(insn->ctx,op->destination,result);"
        "if(op->source1<=1&&op->source2<=1){return;}"
        "tcg_gen_br(done);gen_set_label(slow);ia64_gen_fmerge_helper(op,form);gen_set_label(done);"
    )
    wide, narrow = get_match(
        re.escape(prefix) + r"left_mask=form==2\?(UINT64_C\([^)]+\)):(UINT64_C\([^)]+\));"
        + re.escape(suffix), code, "fmerge direct TCG path", full=True,
    ).groups()
    return f"""
def cBinaryNormal (raw : Word) : Prop :=
  (raw &&& {word(exp_mask, {})}) ≠ {word(exp_zero, {})} ∧
    (raw &&& {word(exp_mask, {})}) ≠ {word(exp_mask, {})}

instance (raw : Word) : Decidable (cBinaryNormal raw) := by unfold cBinaryNormal; infer_instance

def cDirectMerge (form : MergeForm) (a b : Word) : Word :=
  let mask := if form = .signExp then {word(wide, {})} else {word(narrow, {})}
  let lhs := if form = .negativeSign then ~~~a else a
  (lhs &&& mask) ||| (b &&& ~~~mask)
"""


def helper_merge(fp, wrapper, fpreg_header, body, macro, word):
    equations = []
    for mode, suffix in (("negativeSign", "ns"), ("sign", "s"), ("signExp", "se")):
        function = f"ia64_fp_fmerge_{suffix}"
        expect(body(wrapper, f"helper_fmerge_{suffix}"), f"{function}(arg0,arg1,arg2,arg3);",
               f"TCG wrapper {suffix}")
        high, = get_match(
            r"uint64_tlow2;uint64_thigh2;uint64_tlow3;uint64_thigh3;"
            r"if\(ia64_fr_nat_get\(env,r2\)\|\|ia64_fr_nat_get\(env,r3\)\)"
            r"\{ia64_fr_write_nat\(env,r1\);return;\}"
            r"ia64_fpreg_to_spill\(env,r2,&low2,&high2\);"
            r"ia64_fpreg_to_spill\(env,r3,&low3,&high3\);"
            r"ia64_fpreg_from_spill\(env,r1,low3,(.+)\);",
            body(fp, function), function, full=True,
        ).groups()
        equations.append(f"  | .{mode} => {word(high, {'high2': 'high2', 'high3': 'high3'})} ++ low3")
    nat = word(macro(fpreg_header, "IA64_FP_REG_NATVAL_EXP"), {})
    integer = word(macro(fpreg_header, "IA64_FP_REG_INTEGER_EXP"), {})
    branches = "\n".join(equations)
    return f"""
def cNatExp : BitVec 17 := ({nat} : Word).extractLsb' 0 17
def cIntegerExp : BitVec 17 := ({integer} : Word).extractLsb' 0 17

def cSplice (form : MergeForm) (a b : BitVec 128) : BitVec 128 :=
  let high2 := a.extractLsb' 64 64
  let high3 := b.extractLsb' 64 64
  let low3 := b.extractLsb' 0 64
  match form with
{branches}
"""
