# SPDX-License-Identifier: GPL-2.0-or-later
"""Project the translator's predicate-qualified instruction feature gates."""

import re

from Decode.check import c_function, get_match, uncomment
from cast import need


def compact_body(source, name):
    body, = get_match(
        rf"^(?:static\s+)?\w+\s+\*?{name}\s*\([^{{}};]*\)\s*\{{(.*?)^\}}",
        source, name,
    ).groups()
    return re.sub(r"\s+", "", body)


def feature_masks(root, translator, gated_forms, integer_features, covered):
    _, atomic_body = c_function(translator, "ia64_insn_needs_16byte_atomics")
    cases, = get_match(
        r"\s*switch\s*\(insn->opcode\)\s*\{"
        r"((?:\s*case\s+IA64_OP_\w+:)+)"
        r"\s*return\s+true;\s*default:\s*return\s+false;\s*\}\s*",
        atomic_body, "16-byte atomics", full=True,
    ).groups()
    observed = set(re.findall(r"case\s+IA64_OP_(\w+):", cases))
    need(observed == set(gated_forms), f"unexpected CPUID.ao opcode set {sorted(observed)}")
    feature_cases, = get_match(
        r"switch\(insn->opcode\)\{"
        r"((?:(?:caseIA64_OP_\w+:)+returnIA64_CPUID4_\w+;)*)"
        r"default:return0;\}",
        compact_body(translator, "ia64_insn_required_integer_feature"),
        "optional integer features", full=True,
    ).groups()
    required = {
        opcode: bit
        for tags, bit in re.findall(r"((?:caseIA64_OP_\w+:)+)returnIA64_CPUID4_(\w+);",
                                    feature_cases)
        for opcode in re.findall(r"caseIA64_OP_(\w+):", tags)
    }
    need({op: bit for op, bit in required.items() if op in covered} == integer_features,
         "unexpected required features for covered integer opcodes")
    dispatch = compact_body(translator, "ia64_gen_insn")
    need(dispatch.startswith(
        "TCGLabel*skip=NULL;IA64PrepareResultprepare;IA64GenResultresult;"
        "IA64GeneratorKindgenerator=ia64_insn_generator(insn);constbooltrack_psr_suppression="
        "ctx->restart.track_psr_suppression;"
        "if(ia64_gen_predicated_integer(ctx,insn)){"
        "ia64_gen_note_successful_bundle(ctx,insn->address,record_iipa,track_psr_suppression);"
        "returnfalse;}prepare=ia64_gen_prepare_insn(ctx,insn,&skip);"
    ), "unrecognized entry to IA-64 instruction preparation")
    fast_path = compact_body(translator, "ia64_gen_predicated_integer")
    fast_cases, = get_match(
        r"switch\(insn->opcode\)\{((?:(?:caseIA64_OP_\w+:)+"
        r"immediate=(?:true|false);break;)+)default:returnfalse;\}",
        fast_path, "predicated fast-path opcode selection",
    ).groups()
    fast = set(re.findall(r"caseIA64_OP_(\w+):", fast_cases))
    need(not fast.intersection(observed | integer_features.keys()),
         "an optional opcode bypasses its model gate")
    prepare = compact_body(translator, "ia64_gen_prepare_insn")
    predicate = (
        "if(ia64_current_insn_cannot_execute(ctx,insn)){"
        "*predicate_skip=NULL;returnIA64_PREPARE_NULLIFIED;}"
        "skip=ia64_gen_predicate_skip(ctx,insn,qp_value);*predicate_skip=skip;"
    )
    atomic_gate = (
        "if(ia64_insn_needs_16byte_atomics(insn)&&"
        "!(ia64_env_cpu_class(ctx->env)->cpuid_features&IA64_CPUID4_AO)){"
        "ia64_gen_raise_exception(IA64_EXCP_ILLEGAL,insn->address,insn->raw,insn->slot);"
        "if(skip==NULL){returnIA64_PREPARE_NORETURN;}"
        "ia64_gen_predicate_end(skip);returnIA64_PREPARE_COMPLETE;}"
    )
    integer_gate = (
        "required_feature=ia64_insn_required_integer_feature(insn);"
        "if(required_feature!=0&&"
        "!(ia64_env_cpu_class(ctx->env)->cpuid_features&required_feature)){"
        "ia64_gen_raise_exception(IA64_EXCP_ILLEGAL,insn->address,insn->raw,insn->slot);"
        "if(skip==NULL){returnIA64_PREPARE_NORETURN;}"
        "ia64_gen_predicate_end(skip);returnIA64_PREPARE_COMPLETE;}"
    )
    for gate in (atomic_gate, integer_gate):
        need(prepare.count(predicate) == 1 and prepare.count(gate) == 1
             and prepare.index(predicate) < prepare.index(gate), "unrecognized CPUID instruction gate")
    qualifiers = (
        "qp_known=ia64_predicate_known(ctx,insn->qp);"
        "ctx->reg.current_qp_known=qp_known!=IA64_PREDICATE_UNKNOWN;"
        "ctx->reg.current_qp_value=qp_known==IA64_PREDICATE_ONE;"
        "if(qp_known==IA64_PREDICATE_ONE){qp_value=tcg_constant_i64(1);}"
        "elseif(qp_known==IA64_PREDICATE_ZERO){qp_value=tcg_constant_i64(0);}"
        "else{qp_value=cpu_pr[insn->qp];}"
    )
    need(prepare.count(qualifiers) == 1 and prepare.index(qualifiers) < prepare.index(predicate),
         "unrecognized resolved qualifying predicate")
    skip = compact_body(translator, "ia64_gen_predicate_skip")
    need(skip == (
        "TCGLabel*skip;"
        "if(insn->qp==0||insn->opcode==IA64_OP_BR_WEXIT||insn->opcode==IA64_OP_BR_WTOP){"
        "returnNULL;}if(ctx->reg.current_qp_known&&ctx->reg.current_qp_value){returnNULL;}"
        "skip=gen_new_label();if(ctx->reg.current_qp_known){tcg_gen_br(skip);}"
        "else{tcg_gen_brcondi_i64(TCG_COND_EQ,qp_value,0,skip);}returnskip;"
    ), "unrecognized predicate skip projection")
    need(compact_body(translator, "ia64_gen_predicate_end") ==
         "if(skip!=NULL){gen_set_label(skip);}", "cannot locate predicate skip target")
    need(compact_body(translator, "ia64_predicate_nullifies_insn") ==
         "returninsn->opcode!=IA64_OP_BR_WEXIT&&insn->opcode!=IA64_OP_BR_WTOP;",
         "unrecognized predicate nullification eligibility")
    need(compact_body(translator, "ia64_current_insn_cannot_execute") ==
         "returnia64_predicate_nullifies_insn(insn)&&ctx->reg.current_qp_known&&"
         "!ctx->reg.current_qp_value;", "unrecognized predicate nullification")
    system = uncomment((root / "target/ia64/arch/system.c").read_text(encoding="utf-8"))
    read_cpuid = compact_body(system, "ia64_system_read_cpuid")
    need(read_cpuid.startswith("IA64CPUClass*icc=ia64_env_cpu_class(env);index&=0xff;switch(index){")
         and read_cpuid.count("case4:returnicc->cpuid_features;") == 1,
         "CPU feature word differs from the architectural CPUID[4] read")
    cpu = uncomment((root / "target/ia64/cpu.h").read_text(encoding="utf-8"))
    masks = {}
    for name in sorted({"AO", *integer_features.values()}):
        bit, = get_match(rf"^#define[ \t]+IA64_CPUID4_{name}[ \t]+\(1ULL\s*<<\s*([0-9]+)\)[ \t]*$",
                         cpu, f"CPUID.{name.lower()} position").groups()
        need(0 <= int(bit) < 64, f"CPUID.{name.lower()} mask is outside the feature word")
        masks[name] = 1 << int(bit)
    return masks


def lean_gate(name, mask):
    symbol = name.capitalize()
    reference = ("FullDecodeSpec.aoUnsupported" if name == "AO" else
                 f"FullDecodeSpec.integerUnsupported .{name.lower()}")
    return f"""
def c{symbol}Reject (cpuid4 : Word) (qualifies : Bool) : Bool :=
  qualifies && decide ((cpuid4 &&& {mask}) = 0)

theorem {name.lower()}Reject_matches (cpuid4 : Word) (qualifies : Bool) :
    c{symbol}Reject cpuid4 qualifies = {reference} cpuid4 qualifies := by
  dsimp only [c{symbol}Reject, FullDecodeSpec.aoUnsupported, FullDecodeSpec.integerUnsupported]
  bv_decide
"""
