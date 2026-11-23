# SPDX-License-Identifier: GPL-2.0-or-later
"""Project the FP destination and modified-register PSR markers."""

from check import get_match


def generate_target_write(header, translate, cpu_header, body, macro, word):
    helper_set, helper_high, helper_low, helper_live = get_match(
        r"if\(reg<=1\)\{return;\}env->psr\|=reg>=(\d+)\?"
        r"(IA64_PSR_\w+):(IA64_PSR_\w+);"
        r"if\(reg>=(\d+)\)\{env->fp.rotating_fr_live=true;\}",
        body(header, "ia64_fpreg_mark_written"), "helper FP write markers", full=True,
    ).groups()
    tcg_set, tcg_high_set, tcg_low_set, tcg_reg, tcg_high, tcg_low, tcg_live = get_match(
        r"if\(reg>1\)\{unsignedset=reg>=(\d+)\?(\d+):(\d+);"
        r"if\(!\(ctx->fp.written_sets&set\)\)\{"
        r"tcg_gen_ori_i64\(cpu_psr,cpu_psr,reg>=(\d+)\?"
        r"(IA64_PSR_\w+):(IA64_PSR_\w+)\);\}"
        r"if\(reg>=(\d+)&&!ctx->fp.rotating_live\)\{"
        r"tcg_gen_st8_i32\(tcg_constant_i32\(1\),tcg_env,"
        r"offsetof\(CPUIA64State,fp.rotating_fr_live\)\);\}\}",
        body(translate, "ia64_gen_fr_mark_written"), "TCG FP write markers", full=True,
    ).groups()

    def mask(name):
        return word(macro(cpu_header, name), {})

    for threshold in (helper_set, helper_live, tcg_set, tcg_reg, tcg_live):
        if int(threshold) >= 128:
            raise ValueError("FP bank split is outside the register file")

    return f"""
def cHelperMark (dest : BitVec 7) (psr : Word) (live : Bool) : Word × Bool :=
  (psr ||| (if {helper_set} ≤ dest then {mask(helper_high)} else {mask(helper_low)}),
   live || decide ({helper_live} ≤ dest))

def cTcgSet (dest : BitVec 7) : BitVec 32 :=
  if {tcg_set} ≤ dest then {tcg_high_set} else {tcg_low_set}

def cTcgMark (dest : BitVec 7) (psr : Word) (live : Bool)
    (knownSets : BitVec 32) (knownLive : Bool) : Word × Bool :=
  (if (knownSets &&& cTcgSet dest) = 0 then
     psr ||| (if {tcg_reg} ≤ dest then {mask(tcg_high)} else {mask(tcg_low)})
   else psr,
   if {tcg_live} ≤ dest ∧ knownLive = false then true else live)
"""
