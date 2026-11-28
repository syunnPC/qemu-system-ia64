# SPDX-License-Identifier: GPL-2.0-or-later
"""Lift F2 product, carry addition, routing and integer result selection."""

import re

from check import get_match
from merge import expect


XMA = (
    ("XMA_L", "low"),
    ("XMA_H", "signedHigh"),
    ("XMA_HU", "unsignedHigh"),
    ("XMPY_HU", "unsignedMultiply"),
)


def lift_tcg(fpgen, translate, tcg, body):
    emitter = body(fpgen, "ia64_gen_xma")
    prefix, arithmetic = emitter.split("if(mode==1){", 1)
    expect(prefix,
           "constIA64FloatingOperands*op=&insn->operands.floating;"
           "uint64_tsig_mask[2]={0,0};TCGv_i64low=tcg_temp_new_i64();"
           "TCGv_i64high=tcg_temp_new_i64();TCGv_i64missing=NULL;"
           "TCGLabel*fast=NULL;TCGLabel*done=NULL;"
           "if(op->source2>1){sig_mask[op->source2/64]|=1ULL<<(op->source2%64);}"
           "if(op->auxiliary1>1){sig_mask[op->auxiliary1/64]|=1ULL<<(op->auxiliary1%64);}"
           "if(mode!=3){if(op->source1>1){"
           "sig_mask[op->source1/64]|=1ULL<<(op->source1%64);}}"
           "for(uint32_tword=0;word<ARRAY_SIZE(sig_mask);word++){if(sig_mask[word]!=0){"
           "TCGv_i64bits=tcg_temp_new_i64();tcg_gen_xori_i64(bits,"
           "ia64_gen_fr_sig_mask_read(insn->ctx,word,sig_mask[word]),sig_mask[word]);"
           "if(missing==NULL){missing=bits;}else{tcg_gen_or_i64(missing,missing,bits);}}}"
           "if(missing!=NULL){fast=gen_new_label();done=gen_new_label();"
           "tcg_gen_brcondi_i64(TCG_COND_EQ,missing,0,fast);"
           "gen_helper_xma(tcg_env,tcg_constant_i32(op->destination),"
           "tcg_constant_i32(mode==3?0:op->source1),tcg_constant_i32(op->source2),"
           "tcg_constant_i32(op->auxiliary1),tcg_constant_i32(mode));"
           "tcg_gen_br(done);gen_set_label(fast);}", "F2 source tag and helper gate")
    multiply_args = re.escape("(low,high,ia64_fr_significand_src(op->source2),"
                              "ia64_fr_significand_src(op->auxiliary1));}")
    signed, unsigned, upper_addend, first, second = get_match(
        r"tcg_gen_mul([us])2_i64" + multiply_args + r"else\{tcg_gen_mul([us])2_i64" +
        multiply_args +
        re.escape("if(mode!=3){tcg_gen_add2_i64(low,high,low,high,"
                  "ia64_fr_significand_src(op->source1),tcg_constant_i64(") +
        r"(\d+)\)\);\}ia64_gen_fr_mov_sig\(insn->ctx,op->destination,"
        r"mode==0\?(low|high):(low|high)\);if\(done!=NULL\)\{gen_set_label\(done\);\}",
        arithmetic, "F2 TCG arithmetic, addend and result word", full=True,
    ).groups()
    if not 0 <= int(upper_addend) < 2**64:
        raise ValueError("invalid upper addition operand in F2 TCG")
    expect(body(translate, "ia64_gen_fr_sig_mask_read"),
           "TCGv_i64bits;uint64_tpending=mask;uint64_tknown=0;"
           "g_assert(word<ARRAY_SIZE(cpu_fr_sig));while(pending){unsignedbit=ctz64(pending);"
           "unsignedformat=ctx->fp.format[word*64+bit];pending&=pending-1;"
           "if(format!=IA64_FR_FORMAT_UNKNOWN){mask&=~(UINT64_C(1)<<bit);"
           "if(format==IA64_FR_FORMAT_SIG){known|=UINT64_C(1)<<bit;}}}"
           "if(!mask){returntcg_constant_i64(known);}bits=tcg_temp_new_i64();"
           "tcg_gen_andi_i64(bits,cpu_fr_sig[word],mask);tcg_gen_ori_i64(bits,bits,known);returnbits;",
           "F2 constant-folded and runtime significand bit merge")
    carry, = get_match(
        re.escape("if(tcg_op_supported(INDEX_op_addci,TCG_TYPE_REG,0)){"
                  "TCGv_i64t0=tcg_temp_ebb_new_i64();tcg_gen_op3_i64(INDEX_op_addco,t0,al,bl);"
                  "tcg_gen_op3_i64(INDEX_op_addci,rh,ah,bh);tcg_gen_mov_i64(rl,t0);"
                  "tcg_temp_free_i64(t0);}else{TCGv_i64t0=tcg_temp_ebb_new_i64();"
                  "TCGv_i64t1=tcg_temp_ebb_new_i64();tcg_gen_add_i64(t0,al,bl);"
                  "tcg_gen_setcond_i64(TCG_COND_") +
        r"(LTU|GTU)" + re.escape(
            ",t1,t0,al);tcg_gen_add_i64(rh,ah,bh);tcg_gen_add_i64(rh,rh,t1);"
            "tcg_gen_mov_i64(rl,t0);tcg_temp_free_i64(t0);tcg_temp_free_i64(t1);}"),
        body(tcg, "tcg_gen_add2_i64"), "two-word TCG addition lowering", full=True,
    ).groups()
    compare = "<" if carry == "LTU" else ">"
    return f"""
def cTcgXmaAdd (nativeCarry : Bool) (low high addend : Word) : Word × Word :=
  let carryAddend : Word := {upper_addend}
  if nativeCarry then
    let sum := low.zeroExtend 65 + addend.zeroExtend 65
    (sum.setWidth 64, high + carryAddend + (sum >>> 64).setWidth 64)
  else
    let sum := low + addend
    (sum, high + carryAddend + if sum {compare} low then 1 else 0)

def cTcgXmaPayload (mode : Nat) (nativeCarry : Bool) (a b addend : Word) : Word :=
  let signed := if mode = 1 then {str(signed == 's').lower()} else {str(unsigned == 's').lower()}
  let product : BitVec 128 :=
    if signed then a.signExtend 128 * b.signExtend 128 else a.zeroExtend 128 * b.zeroExtend 128
  let low := product.extractLsb' 0 64
  let high := product.extractLsb' 64 64
  let (low, high) :=
    if mode = 3 then (low, high)
    else cTcgXmaAdd nativeCarry low high addend
  if mode = 0 then {first} else {second}
"""


def lift_helper(fp, wrapper, helpers, body):
    expect(body(wrapper, "helper_xma"), "ia64_fp_xma(arg0,arg1,arg2,arg3,arg4,arg5);",
           "F2 helper ABI argument order")
    get_match(r"DEF_HELPER_6\(xma,\s*void,\s*env,\s*i32,\s*i32,\s*i32,\s*i32,\s*i32\)",
              helpers, "F2 TCG helper declaration")
    expect(body(fp, "ia64_fr_nat_any3"),
           "returnia64_fr_nat_any2(env,reg1,reg2)||ia64_fr_nat_get(env,reg3);",
           "three-input F2 NaT test")
    expect(body(fp, "ia64_fr_write_nat_if_any3"),
           "if(!ia64_fr_nat_any3(env,src1,src2,src3)){returnfalse;}"
           "ia64_fr_write_nat(env,dst);returntrue;", "F2 NaT result and early return")
    signed_shift, unsigned_shift = get_match(
        re.escape("uint64_ta;uint64_tb;uint64_taddend;__uint128_tresult;"
                  "if(ia64_fr_write_nat_if_any3(env,r1,r2,r3,r4)){return;}"
                  "a=ia64_fr_significand(env,r3);b=ia64_fr_significand(env,r4);"
                  "addend=ia64_fr_significand(env,r2);if(mode==1){"
                  "result=(__int128)(int64_t)a*(__int128)(int64_t)b+addend;"
                  "ia64_fr_write_sig(env,r1,result>>") + r"(\d+)" +
        re.escape(");}else{result=(__uint128_t)a*b;if(mode!=3){result+=addend;}"
                  "ia64_fr_write_sig(env,r1,mode==0?(uint64_t)result:result>>") + r"(\d+)\);\}",
        body(fp, "ia64_fp_xma"), "F2 C casts, multiply-add and selected result word", full=True,
    ).groups()
    if any(int(shift) >= 128 for shift in (signed_shift, unsigned_shift)):
        raise ValueError("invalid 128-bit C shift in F2 helper")
    return f"""
def cHelperXmaPayload (mode : Nat) (a b addend : Word) : Word :=
  if mode = 1 then
    let sum : BitVec 128 := a.signExtend 128 * b.signExtend 128 + addend.zeroExtend 128
    (sum >>> {signed_shift}).setWidth 64
  else
    let product : BitVec 128 := a.zeroExtend 128 * b.zeroExtend 128
    let sum := if mode = 3 then product else product + addend.zeroExtend 128
    if mode = 0 then sum.setWidth 64 else (sum >>> {unsigned_shift}).setWidth 64
"""


def generate_xma(translate, fpgen, fp, wrapper, helpers, operands, tcg, body):
    get_match(r"typedef struct IA64FloatingOperands\s*\{\s*uint8_t destination;"
              r"\s*uint8_t source1;\s*uint8_t source2;\s*uint8_t auxiliary1;",
              operands, "F2 floating operand view")
    dispatch = body(fpgen, "ia64_gen_fp")
    modes = []
    for opcode, form in XMA:
        mode = get_match(rf"caseIA64_OP_{opcode}:ia64_gen_xma\(insn,(\d+)\);break;",
                         dispatch, f"F2 TCG dispatch for {opcode}")[1]
        modes.append(f"  | .{form} => {mode}")
    return ("\ndef cXmaMode : XmaForm → Nat\n" + "\n".join(modes) + "\n"
            + lift_tcg(fpgen, translate, tcg, body) + lift_helper(fp, wrapper, helpers, body))
