# SPDX-License-Identifier: GPL-2.0-or-later
"""Project the lazy floating-register rebase and its TCG preparation path."""

import re

from check import get_match
from merge import expect


def generate_rotation(fp, translate, wrapper, cpu_header, operands, opcodes, body, macro):
    base = int(macro(fp, "IA64_ROTATING_FR_BASE").strip())
    count = int(macro(cpu_header, "IA64_FR_COUNT").strip()) - base
    if not (0 < base < 128 and 1 < count < 128):
        raise ValueError("invalid rotating FP register range")
    expect(macro(fp, "IA64_ROTATING_FR_COUNT").strip(),
           "(IA64_FR_COUNT - IA64_ROTATING_FR_BASE)", "floating rotation span")
    expect(body(fp, "ia64_sync_rotating_fr"),
           "uint32_tnew_rrb=env->cfm_rrb_fr;"
           "uint32_told_rrb=env->fp.rotating_fr_materialized_rrb;"
           "if(new_rrb!=old_rrb&&env->fp.rotating_fr_live){uint32_tshift="
           "new_rrb>=old_rrb?new_rrb-old_rrb:new_rrb+IA64_ROTATING_FR_COUNT-old_rrb;"
           "ia64_rebase_rotating_fr(env,shift);}env->fp.rotating_fr_materialized_rrb=new_rrb;",
           "floating rotation rename-base difference")
    rebase = (
        "tmp[IA64_ROTATING_FR_COUNT];uint32_ti;"
        "if(shift==1){TYPEfirst=values[IA64_ROTATING_FR_BASE];"
        "memmove(&values[IA64_ROTATING_FR_BASE],&values[IA64_ROTATING_FR_BASE+1],"
        "(IA64_ROTATING_FR_COUNT-1)*sizeof(values[0]));values[IA64_FR_COUNT-1]=first;return;}"
        "if(shift==IA64_ROTATING_FR_COUNT-1){TYPElast=values[IA64_FR_COUNT-1];"
        "memmove(&values[IA64_ROTATING_FR_BASE+1],&values[IA64_ROTATING_FR_BASE],"
        "(IA64_ROTATING_FR_COUNT-1)*sizeof(values[0]));values[IA64_ROTATING_FR_BASE]=last;return;}"
        "for(i=0;i<IA64_ROTATING_FR_COUNT;i++){uint32_tsrc=(i+shift)%IA64_ROTATING_FR_COUNT;"
        "tmp[i]=values[IA64_ROTATING_FR_BASE+src];}"
        "memcpy(&values[IA64_ROTATING_FR_BASE],tmp,sizeof(tmp));"
    )
    for width in (32, 64):
        dtype = f"uint{width}_t"
        expect(body(fp, f"ia64_rebase_rotating_fr_u{width}"),
               dtype + rebase.replace("TYPE", dtype), f"rotation of {width}-bit register cells")
    tag_width, = get_match(
        r"constuint64_tlow_nonrotating=bits\[0\]&UINT32_MAX;"
        r"const__uint128_tmask=\(\(\(__uint128_t\)1<<(\d+)\)-1\);"
        r"__uint128_trotating=\(\(__uint128_t\)bits\[1\]<<32\)\|\(bits\[0\]>>32\);"
        r"rotating=\(\(rotating>>shift\)\|\(rotating<<\(IA64_ROTATING_FR_COUNT-shift\)\)\)&mask;"
        r"bits\[0\]=low_nonrotating\|\(uint64_t\)\(rotating<<32\);bits\[1\]=rotating>>32;",
        body(fp, "ia64_rebase_rotating_fr_bits"), "packed floating register tag rotation",
        full=True,
    ).groups()
    if not 0 < int(tag_width) < 128:
        raise ValueError("invalid tag rotation mask width")
    expect(body(fp, "ia64_rebase_rotating_fr"),
           "constuint64_trotating_word0_mask=UINT64_MAX<<IA64_ROTATING_FR_BASE;"
           "boolhas_ext=(env->fp.fr_ext_valid[0]&rotating_word0_mask)||env->fp.fr_ext_valid[1];"
           "boolhas_int=(env->fp.fr_int_origin[0]&rotating_word0_mask)||env->fp.fr_int_origin[1];"
           "ia64_rebase_rotating_fr_u64(env->fp.fr,shift);"
           "if(has_int){ia64_rebase_rotating_fr_u64(env->fp.fr_int_value,shift);}"
           "if(has_ext){ia64_rebase_rotating_fr_u64(env->fp.fr_ext_mant,shift);"
           "ia64_rebase_rotating_fr_u32(env->fp.fr_ext_exp,shift);}"
           + "".join(f"ia64_rebase_rotating_fr_bits(env->fp.fr_{field},shift);" for field in
                     ("nat", "sig", "ext_sign", "ext_valid", "int_origin"))
           + "env->fp.fr_sig[0]&=~(env->fp.fr_nat[0]&rotating_word0_mask);"
           "env->fp.fr_sig[1]&=~env->fp.fr_nat[1];",
           "floating cache arrays and tag rebase selection")
    expect(body(wrapper, "helper_sync_rotating_fr"), "ia64_sync_rotating_fr(arg0);", "rotation helper wrapper")
    expect(body(translate, "ia64_fp_reg_set"),
           "if(reg>=32){return2;}if(reg>=2){return1;}return0;", "high-bank access selection")
    for view in ("Common", "Floating"):
        get_match(rf"typedef struct IA64{view}Operands\s*\{{\s*uint8_t destination;"
                  r"\s*uint8_t source1;\s*uint8_t source2;", operands, f"{view} operand layout")
    for name, tail in (
        ("read", "sets=ia64_fp_reg_set(insn->operands.common.source1);"
                 "sets|=ia64_fp_reg_set(insn->operands.common.source2);returnsets;"),
        ("write", "returnia64_fp_reg_set(insn->operands.common.destination);"),
    ):
        cases, = get_match(r"((?:caseIA64_OP_\w+:)+)" + re.escape(tail),
                           body(translate, f"ia64_insn_fp_{name}_sets"),
                           f"binary FP {name} bank selectors").groups()
        if not set(opcodes) <= set(re.findall(r"caseIA64_OP_(\w+):", cases)):
            raise ValueError(f"binary FP instruction missing from {name} bank selectors")
    get_match(re.escape(
        "fp_sets=ia64_insn_fp_read_sets(insn)|ia64_insn_fp_write_sets(insn);"
        "ia64_gen_check_disabled_fp(ctx,insn,fp_sets,skip==NULL);"
        "ia64_gen_sync_rotating_fr(ctx,fp_sets,skip==NULL);returnIA64_PREPARE_DISPATCH;"
    ) + "$", body(translate, "ia64_gen_prepare_insn"), "FP preparation before dispatch")
    expect(body(translate, "ia64_gen_sync_rotating_fr"),
           "TCGv_i32current;TCGv_i32materialized;TCGLabel*done;"
           "if(!(sets&2)||ctx->fp.rotating_synced){return;}current=tcg_temp_new_i32();"
           "materialized=tcg_temp_new_i32();done=gen_new_label();"
           "tcg_gen_ld8u_i32(current,tcg_env,offsetof(CPUIA64State,cfm_rrb_fr));"
           "tcg_gen_ld8u_i32(materialized,tcg_env,offsetof(CPUIA64State,fp.rotating_fr_materialized_rrb));"
           "tcg_gen_brcond_i32(TCG_COND_EQ,current,materialized,done);"
           "gen_helper_sync_rotating_fr(tcg_env);gen_set_label(done);"
           "if(unconditional){ctx->fp.rotating_synced=true;}", "TCG entry to floating rotation")

    return f"""
def cRebaseIndex (index old current : BitVec 7) : BitVec 7 :=
  if index < {base} then index else
  let i := index.zeroExtend 9 - {base}
  let o := old.zeroExtend 9
  let n := current.zeroExtend 9
  let delta := if n ≥ o then n - o else n + {count} - o
  let offset :=
    if delta = 1 then (if i = {count - 1} then 0 else i + 1)
    else if delta = {count - 1} then (if i = 0 then {count - 1} else i - 1)
    else (i + delta) % {count}
  (offset + {base}).setWidth 7

def cRebaseTags (tags : BitVec 128) (shift : BitVec 7) : BitVec 128 :=
  let high := (tags.extractLsb' 64 64).zeroExtend 128
  let low := (tags.extractLsb' 0 64).zeroExtend 128
  let window := (high <<< (32 : BitVec 128)) ||| (low >>> (32 : BitVec 128))
  let sh := shift.zeroExtend 128
  let rotated := ((window >>> sh) ||| (window <<< ({count} - sh))) &&& {((1 << int(tag_width)) - 1)}
  let outHigh := (rotated >>> (32 : BitVec 128)).extractLsb' 0 64
  let outLow := (rotated <<< (32 : BitVec 128)).extractLsb' 0 64
  outHigh ++ (outLow ||| ((low.extractLsb' 0 64) &&& 0xffffffff))
"""
