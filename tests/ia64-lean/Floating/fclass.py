# SPDX-License-Identifier: GPL-2.0-or-later
"""Lift FCLASS class tests and check its predicate/disabled-FP route."""

import re

from check import get_match
from merge import expect


def outer_split(expr, token):
    depth = 0
    for index, char in enumerate(expr):
        if char == "(":
            depth += 1
        elif char == ")":
            depth -= 1
        elif depth == 0 and expr.startswith(token, index):
            return expr[:index], expr[index + len(token):]
    return None


def grouped(expr):
    if not (expr.startswith("(") and expr.endswith(")")):
        return False
    depth = 0
    for index, char in enumerate(expr):
        if char == "(":
            depth += 1
        elif char == ")":
            depth -= 1
            if depth == 0:
                return index == len(expr) - 1
    return False


def boolean(expr, word, words, names):
    while grouped(expr):
        expr = expr[1:-1]
    if expr in names:
        return expr
    for token in ("||", "&&", "==", "!="):
        split = outer_split(expr, token)
        if split:
            left, right = split
            if token in ("||", "&&"):
                a = boolean(left, word, words, names)
                b = boolean(right, word, words, names)
                return f"({a} {token} {b})"
            comparison = "=" if token == "==" else "≠"
            return f"(decide ({word(left, words)} {comparison} {word(right, words)}))"
    if expr.startswith("!"):
        return f"(!{boolean(expr[1:], word, words, names)})"
    return f"(decide ({word(expr, words)} ≠ 0))"


def lift_helper(fp, wrapper, header, cpu_header, body, macro, word):
    expect(body(wrapper, "helper_fclass"), "ia64_fp_fclass(arg0,arg1,arg2,arg3,arg4);",
           "TCG FCLASS helper wrapper")
    get_match(r"\bIA64_PR_TRUE\s*=\s*0\s*,", cpu_header, "constant true predicate")
    fields = ("is_neg", "has_integer_bit", "is_zero", "is_inf", "is_qnan",
              "is_snan", "is_unorm", "is_normal", "sign_match", "type_match", "member")
    prefix = "uint64_tmant;uint64_thigh;uint32_texp;boolis_neg;boolis_nat=ia64_fr_nat_get(env,f2);"
    prefix += "".join(f"bool{field};" for field in ("is_zero", "is_unorm", "is_inf", "is_qnan",
                                                   "is_snan", "is_normal", "has_integer_bit",
                                                   "sign_match", "type_match", "member"))
    prefix += "ia64_fpreg_to_spill(env,f2,&mant,&high);"
    result = get_match(
        re.escape(prefix) + r"exp=([^;]+);" + "".join(f"{field}=([^;]+);" for field in fields)
        + r"if\((.+)\)\{if\(p1\)\{env->pr\[p1\]=0;\}if\(p2\)\{env->pr\[p2\]=0;\}\}"
        + re.escape("else{if(p1){env->pr[p1]=member?1:0;}if(p2){env->pr[p2]=member?0:1;}}"
                    "env->pr[IA64_PR_TRUE]=1;"),
        body(fp, "ia64_fp_fclass"), "FCLASS helper classifiers and writes", full=True,
    ).groups()
    exponent, *conditions, miss = result
    words = {name: name for name in ("high", "mant", "exp", "fclass9")}
    for name in ("IA64_FP_SIGNIFICAND_INTEGER_BIT", "IA64_FP_REG_SPECIAL_EXP"):
        words[name] = word(macro(header, name), {})
    values = ["def cFclassDecision (mant high : Word) (is_nat : Bool) (mask : BitVec 9) : Bool × Bool :=",
              "  let fclass9 := mask.zeroExtend 64",
              f"  let exp := ({word(exponent, words)}).extractLsb' 0 32 |>.zeroExtend 64"]
    names = {"is_nat"}
    for field, expr in zip(fields, conditions, strict=True):
        values.append(f"  let {field} := {boolean(expr, word, words, names)}")
        names.add(field)
    values.append(f"  ({boolean(miss, word, words, names)}, member)\n")
    return "\n".join(values)


def fields_f5(decode, body):
    expect(body(decode, "ia64_bits"), "return(value>>low)&((1ULL<<width)-1);",
           "standalone decoder field extraction")
    bits = r"ia64_bits\(raw,(\d+),(\d+)\)"
    qp = get_match(r"\.qp=" + bits + ",", body(decode, "ia64_base_insn"),
                    "base qualifying-predicate extraction").groups()
    fields = get_match(
        re.escape("if(unit==IA64_UNIT_F&&ia64_b_op(raw)==5){"
                  "Ia64Instructioninsn=ia64_base_insn(IA64_OP_FCLASS,unit,raw,address,slot);")
        + r"insn\.operands\.decoder\.p1=" + bits + r";insn\.operands\.decoder\.p2=" + bits
        + r";insn\.operands\.decoder\.r2=" + bits + r";insn\.operands\.decoder\.imm=\("
        + bits + r"<<(\d+)\)\|" + bits + r";insn\.compare_unc=" + bits + r"!=0;returninsn;\}",
        body(decode, "ia64_decode_insn"), "selected F5 decoded instruction fields",
    ).groups()
    p1a, p1b, p2a, p2b, ra, rb, ha, hb, sh, la, lb, ua, ub = map(int, fields)

    def field(a, b):
        a, b = int(a), int(b)
        if a >= 64 or not 1 <= b < 64:
            raise ValueError("F5 source has an undefined C field shift")
        return f"((raw.zeroExtend 64).extractLsb' {a} {b})"

    if sh >= 64:
        raise ValueError("F5 source has an undefined C immediate shift")
    return f"""
def cF5Fields (raw : BitVec 41) : BitVec 35 :=
  let imm := ({field(ha, hb)}.zeroExtend 64 <<< ({sh} : Word)) ||| {field(la, lb)}.zeroExtend 64
  let unc : BitVec 1 := if {field(ua, ub)} = 0 then 0 else 1
  {field(*qp)}.setWidth 6 ++ {field(p1a, p1b)}.setWidth 6 ++ {field(p2a, p2b)}.setWidth 6 ++
    {field(ra, rb)}.setWidth 7 ++ imm.setWidth 9 ++ unc
"""


def prepare_route(translate, fpgen, operands, body):
    get_match(re.escape(
        "caseIA64_OP_FCLASS:gen_helper_fclass(tcg_env,tcg_constant_i32(op->auxiliary1),"
        "tcg_constant_i32(op->auxiliary2),tcg_constant_i32(op->source1),"
        "tcg_constant_i32(op->immediate));break;"
    ), body(fpgen, "ia64_gen_fp"), "FCLASS TCG helper arguments")
    for view, fields in (
        ("Common", ("destination", "source1", "source2", "auxiliary1", "auxiliary2")),
        ("Floating", ("destination", "source1", "source2", "auxiliary1", "auxiliary2")),
        ("Decoded", ("r1", "r2", "r3", "p1", "p2")),
    ):
        get_match(rf"typedef struct IA64{view}Operands\s*\{{"
                  + "".join(rf"\s*uint8_t {field};" for field in fields), operands,
                  f"FCLASS {view} operand view")
    get_match(re.escape("caseIA64_OP_FCLASS:returntrue;"),
              body(translate, "ia64_insn_writes_predicate_pair"), "FCLASS pair tag")
    expect(body(translate, "ia64_compare_has_equal_targets"),
           "returninsn->operands.common.auxiliary1==insn->operands.common.auxiliary2&&"
           "ia64_insn_writes_predicate_pair(insn);", "FCLASS equal destination test")
    expect(body(translate, "ia64_gen_clear_unc_compare_targets"),
           "uint32_ttargets[]={insn->operands.common.auxiliary1,insn->operands.common.auxiliary2};"
           "if(!insn->compare_unc||qp_known==IA64_PREDICATE_ONE){return;}"
           "for(inti=0;i<ARRAY_SIZE(targets);i++){uint32_tp=targets[i];if(p!=0){"
           "if(qp_known==IA64_PREDICATE_ZERO){tcg_gen_movi_i64(cpu_pr[p],0);}"
           "else{tcg_gen_movcond_i64(TCG_COND_EQ,cpu_pr[p],qp_value,tcg_constant_i64(0),"
           "tcg_constant_i64(0),cpu_pr[p]);}ia64_predicate_set_known(ctx,p,"
           "qp_known==IA64_PREDICATE_ZERO?IA64_PREDICATE_ZERO:IA64_PREDICATE_UNKNOWN);}}",
           "FCLASS prediction-guarded .unc clear")
    expect(body(translate, "ia64_predicate_nullifies_insn"),
           "returninsn->opcode!=IA64_OP_BR_WEXIT&&insn->opcode!=IA64_OP_BR_WTOP;",
           "FCLASS predication eligibility")
    # These three functions each have a single actual definition. Use
    # the signature as an anchor because they also appear in callers.
    for name, expected in (
        ("ia64_current_insn_cannot_execute",
         "returnia64_predicate_nullifies_insn(insn)&&ctx->reg.current_qp_known&&!ctx->reg.current_qp_value;"),
        ("ia64_gen_predicate_skip",
         "TCGLabel*skip;if(insn->qp==0||insn->opcode==IA64_OP_BR_WEXIT||insn->opcode==IA64_OP_BR_WTOP)"
         "{returnNULL;}if(ctx->reg.current_qp_known&&ctx->reg.current_qp_value){returnNULL;}"
         "skip=gen_new_label();if(ctx->reg.current_qp_known){tcg_gen_br(skip);}"
         "else{tcg_gen_brcondi_i64(TCG_COND_EQ,qp_value,0,skip);}returnskip;"),
        ("ia64_gen_predicate_end", "if(skip!=NULL){gen_set_label(skip);}"),
    ):
        selected = get_match(rf"^static[^;\n]+\b{name}\(", translate, name)[0]
        start = translate.index(selected, translate.find("static TCGLabel *ia64_gen_predicate_skip"))
        expect(body(translate[start:], name), expected, name)

    prepare = body(translate, "ia64_gen_prepare_insn")
    illegal = "ia64_gen_raise_exception(IA64_EXCP_ILLEGAL,insn->address,insn->raw,insn->slot);"
    phases = (
        "qp_known=ia64_predicate_known(ctx,insn->qp);"
        "ctx->reg.current_qp_known=qp_known!=IA64_PREDICATE_UNKNOWN;"
        "ctx->reg.current_qp_value=qp_known==IA64_PREDICATE_ONE;"
        "if(qp_known==IA64_PREDICATE_ONE){qp_value=tcg_constant_i64(1);}"
        "elseif(qp_known==IA64_PREDICATE_ZERO){qp_value=tcg_constant_i64(0);}"
        "else{qp_value=cpu_pr[insn->qp];}"
        "if((insn->compare_unc||(insn->clear_p2_before_predicate&&"
        "insn->qp==insn->operands.common.auxiliary2))&&insn->qp!=0){"
        "TCGv_i64saved_qp=tcg_temp_new_i64();tcg_gen_mov_i64(saved_qp,qp_value);qp_value=saved_qp;}",
        f"if(insn->compare_unc&&ia64_compare_has_equal_targets(insn)){{{illegal}"
        "returnIA64_PREPARE_NORETURN;}",
        "ia64_gen_clear_unc_compare_targets(ctx,insn,qp_value,qp_known);",
        "if(ia64_current_insn_cannot_execute(ctx,insn)){*predicate_skip=NULL;"
        "returnIA64_PREPARE_NULLIFIED;}skip=ia64_gen_predicate_skip(ctx,insn,qp_value);"
        "*predicate_skip=skip;",
        f"if(ia64_compare_has_equal_targets(insn)){{{illegal}if(skip==NULL)"
        "{returnIA64_PREPARE_NORETURN;}ia64_gen_predicate_end(skip);returnIA64_PREPARE_COMPLETE;}",
        "fp_sets=ia64_insn_fp_read_sets(insn)|ia64_insn_fp_write_sets(insn);"
        "ia64_gen_check_disabled_fp(ctx,insn,fp_sets,skip==NULL);"
        "ia64_gen_sync_rotating_fr(ctx,fp_sets,skip==NULL);returnIA64_PREPARE_DISPATCH;",
    )
    get_match(".*?".join(map(re.escape, phases)) + "$", prepare, "FCLASS preparation precedence")

    get_match(re.escape(
        "caseIA64_OP_FCLASS:returnia64_fp_reg_set(insn->operands.common.source1);"
    ), body(translate, "ia64_insn_fp_read_sets"), "FCLASS FP input bank")
    _, write_set = get_match(r"^(switch\(insn->opcode\)\{)(.*default:return0;\})$",
                             body(translate, "ia64_insn_fp_write_sets"), "FP write sets").groups()
    if "caseIA64_OP_FCLASS:" in write_set:
        raise ValueError("FCLASS unexpectedly acquires an FP destination bank")


def disabled_fp(translate, cpu_header, body, macro, word):
    checks = get_match(
        r"uint64_tdisabled_mask;uint64_tisr_flags;TCGv_i64disabled;TCGv_i64isr;TCGLabel\*done;"
        r"sets&=~ctx->fp.enabled_sets;if\(sets==0\)\{return;\}"
        r"done=gen_new_label\(\);disabled=tcg_temp_new_i64\(\);isr=tcg_temp_new_i64\(\);"
        r"disabled_mask=\(\(sets&(\d+)\)\?(IA64_PSR_\w+):0\)\|\(\(sets&(\d+)\)\?"
        r"(IA64_PSR_\w+):0\);isr_flags=ia64_insn_disabled_fp_isr_flags\(insn\);"
        r"tcg_gen_andi_i64\(disabled,cpu_psr,disabled_mask\);"
        r"tcg_gen_brcondi_i64\(TCG_COND_EQ,disabled,0,done\);"
        r"tcg_gen_shri_i64\(isr,disabled,18\);if\(isr_flags!=0\)"
        r"\{tcg_gen_ori_i64\(isr,isr,isr_flags\);\}"
        r"tcg_gen_st_i64\(isr,tcg_env,offsetof\(CPUIA64State,cr_isr\)\);"
        r"ia64_gen_raise_exception\(IA64_EXCP_DISABLED_FP,insn->address,0,insn->slot\);"
        r"gen_set_label\(done\);if\(unconditional\)\{ctx->fp.enabled_sets\|=sets;\}",
        body(translate, "ia64_gen_check_disabled_fp"), "FCLASS live disabled-FP check", full=True,
    ).groups()
    lo, lo_mask, hi, hi_mask = checks
    psr_low, psr_high = [word(macro(cpu_header, m), {}) for m in (lo_mask, hi_mask)]
    return f"""
def cClassSet (reg : BitVec 7) : BitVec 32 :=
  if 32 ≤ reg then 2 else if 2 ≤ reg then 1 else 0

def cFpDisabledBits (sets : BitVec 32) (psr : Word) (enabledSets : BitVec 32) : Word :=
  let checks := sets &&& ~~~enabledSets
  let mask := (if checks &&& {lo} = 0 then 0 else {psr_low}) |||
              (if checks &&& {hi} = 0 then 0 else {psr_high})
  psr &&& mask

def cClassDisabled (reg : BitVec 7) (psr : Word) (enabledSets : BitVec 32) : Bool :=
  decide (cFpDisabledBits (cClassSet reg) psr enabledSets ≠ 0)
"""


def generate_fclass(translate, fpgen, fp, wrapper, header, cpu_header, decode, operands,
                    body, macro, word):
    prepare_route(translate, fpgen, operands, body)
    return (fields_f5(decode, body)
            + lift_helper(fp, wrapper, header, cpu_header, body, macro, word)
            + disabled_fp(translate, cpu_header, body, macro, word))
