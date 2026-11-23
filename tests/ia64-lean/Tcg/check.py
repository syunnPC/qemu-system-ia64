#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Compose source-lifted TCG scalar values with register/NaT step effects."""

import argparse
from hashlib import sha256
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))
from Integer import check as integer
from Tcg.flow import check_flows


# These complete bodies delimit the normalized control/bit effects in
# Execution.lean.inc. Value expressions and the predication condition are
# lifted separately, so changing an operation reaches the Lean theorem.
GUARDS = {
    "translate/translate.c": {
        "ia64_gen_prepare_insn": "0e4c08174cef80aa042f7113ee5322250e92b3da80876b4cc48dcae3062ae703",
        "ia64_gen_check_gr_in_frame": "4b9ca805eec9cdc795b6bad464a631a2df1fe0d1dbb570268fb29b134db04485",
        "ia64_gen_insn": "340706a890cd42d117ccc06055de41fd16c333d5294b2912fc5e189ad229ac6d",
        "ia64_predicate_known": "9df0ecddb5214a7a463839972a3cb5ffe987ad8cfca97b13dd5f7d9cc9e10a03",
        "ia64_current_insn_cannot_execute": "61006930746a864aeaa5283e4dc9d318b6685350e980d33d375387fafe06a6b8",
        "ia64_gen_note_stacked_gr_write": "0f6d9ad83a04614eff929c5223b1d63831fc23ac1bebecc3a9e2f1ea010906cb",
        "ia64_insn_nat_known": "6092743aaa45bdb579a7379c06aa66dd7bbfaae5fa68844d63b1ff9025cfc530",
        "ia64_nat_known": "8f49c86bb608b848edb1db827fced9ce54130df7e9f9bb9b63d482ff6d49f908",
        "ia64_gen_gr_nat_read": "a4147dc7899d6eb6b21907930c617c4aa6cfe15418d6a2f0d45eaf918b97023c",
        "ia64_gen_gr_nat_from_1": "1744bbd0d5d9afff02126243eafd974ba68895acea9c419bb93692f26deb4466",
        "ia64_gen_gr_nat_from_2": "e9b8e06c5ab75cb4667a879266ccdd8c75faea213973eb8845d2ef9a4493d015",
        "ia64_gen_gr_nat_or_from_1": "64c63d52de8ca1228813111fb223806e52d78b4164fd19972546c80c018c6141",
        "ia64_gen_gr_nat_clear": "88d9a97e1c3bebefc21a1b185102f57e5f7376b32ee9959f7cb41ec249c7158f",
        "ia64_gen_gr_nat_set": "04253370595722eaaf2a61014d898133b32534f4946ba6d3cf9acf0b1152d844",
        "ia64_gen_gr_nat_assign_masked": "0fd43dcbef5631a3f1b50a9af5f398477537ed3fe1e888cb1b4088f55a8b8025",
        "ia64_gen_gr_nat_assign": "110470c6a770ea67a4a3251735df184bd43eb4a9b29cb95c2815aa50e863bc4d",
        "ia64_gen_dispatch": "3bc4394b720eecbf04ab698867bd569d144c6b7f4d41adce9a1d07e495e2755b",
        "ia64_gen_raise_exception": "be4f82afe4605984a398263e0b2ac956e2e511b313488836e1cbbba33a32b319",
        "ia64_gen_predicate_end": "8a56c902008952a0a4f85b63798184dc69f2456295c8843a29cc842b7bdcb73f",
    },
    "translate/gen-integer.c": {
        "ia64_gen_integer_nat_from_1": "99ffd328a6d38a1811700be37435147dd31a58d0f2fb8e09fda636a79f4bc8a0",
        "ia64_gen_integer_nat_from_2": "2ee4ead5a088f104a76fd99063b10f5934dfea14cf2e9a078f13ff1a9047f2c7",
    },
}
TRANSLATE = "translate/translate.c"
GEN_INTEGER = "translate/gen-integer.c"

OPCODES = integer.OPERANDS
SHIFT_IMM = {"SHL_IMM", "SHRU_IMM", "SHR_IMM", "SHRP_IMM"}

require = integer.require
function = integer.function
compact = integer.compact
switch_cases = lambda body: integer.cases(integer.switches(body)[0][2])


def source(root, relative):
    return integer.stripped((root / "target/ia64" / relative).read_text(encoding="utf-8"))


def known_routes(sources, cpu):
    require(re.search(r"#define\s+IA64_STACKED_GR_BASE\s+32\b", cpu),
            "The TCG view no longer uses a static 32-register window")
    for path, functions in GUARDS.items():
        for name, digest in functions.items():
            checked = compact(function(sources[path], name)).encode()
            require(sha256(checked).hexdigest() == digest, f"Changed scalar control path {name}")
    translator = sources[TRANSLATE]
    require(compact(function(translator, "ia64_gr_src")) ==
            "returnreg==0?tcg_constant_i64(0):cpu_gr[reg];",
            "Unexpected GR0/data register selector")
    require(compact(function(translator, "ia64_predicate_nullifies_insn")) ==
            "returninsn->opcode!=IA64_OP_BR_WEXIT&&insn->opcode!=IA64_OP_BR_WTOP;",
            "Unexpected ALU predication classification")
    required_features = set(switch_cases(function(translator, "ia64_insn_required_integer_feature")))
    require(required_features == {"CLZ", "MPY4", "MPYSHL4", "default"},
            "Unexpected CPUID feature-dependent scalar forms")
    return required_features - {"default"}


def lift_preparation(translator):
    branch = compact(function(translator, "ia64_gen_predicate_skip"))
    match = re.fullmatch(
        "TCGLabel\\*skip;"
        "if\\(insn->qp==0\\|\\|insn->opcode==IA64_OP_BR_WEXIT"
        "\\|\\|insn->opcode==IA64_OP_BR_WTOP\\)\\{returnNULL;\\}"
        "if\\(ctx->reg.current_qp_known&&ctx->reg.current_qp_value\\)"
        "\\{returnNULL;\\}"
        "skip=gen_new_label\\(\\);"
        "if\\(ctx->reg.current_qp_known\\)\\{tcg_gen_br\\(skip\\);\\}"
        "else\\{tcg_gen_brcondi_i64\\(TCG_COND_(EQ|NE),qp_value,0,skip\\);\\}"
        "returnskip;",
        branch,
    )
    require(match is not None, "Unsupported predicate skip emitter")
    # The dynamic branch is emitted only if no compile-time predicate
    # truth is known. Its condition controls reaching the instruction body.
    dynamic = "s.pr i.qp" if match[1] == "EQ" else "!(s.pr i.qp)"
    return f"""def sourceQualified (s : State α) (i : Slot) (f : Facts) : Bool :=
  if i.qp = 0 || f.predicate.isSome then sourcePredicate s i f
  else {dynamic}

def prepareProjected (s : State α) (i : Slot) (f : Facts) : Decision :=
  if i.malformed then .fault .illegal
  else if !sourceQualified s i f then .nullified
  else if i.illegalPlacement then .fault .illegal
  else if i.reservedField then .fault .reservedField
  else if i.missingFeature then .fault .illegal
  else if !sourceFrame s i f then .fault .illegal
  else .execute

theorem sourceQualified_eq (s : State α) (i : Slot) (f : Facts) (ok : Sound s i f) :
    sourceQualified s i f = SDM.qualified s i.qp := by
  unfold sourceQualified
  split
  · exact sourcePredicate_eq _ _ _ ok
  · rename_i dynamic
    have notZero : i.qp ≠ 0 := by
      intro isZero
      simp [isZero] at dynamic
    simp [SDM.qualified, notZero]

theorem prepareProjected_eq (s : State α) (i : Slot) (f : Facts) (ok : Sound s i f) :
    prepareProjected s i f = SDM.prepare s i := by
  simp only [prepareProjected, SDM.prepare, sourceQualified_eq _ _ _ ok, sourceFrame_eq _ _ _ ok]
  cases i.missingFeature <;> cases SDM.legalTarget s i.dst <;> simp
"""


def nat_routes(generator, opcodes):
    main = switch_cases(function(generator, "ia64_gen_integer"))
    routed = switch_cases(function(generator, "ia64_gen_native_integer_nat"))
    result = {}
    for opcode in opcodes:
        if opcode in ("MOVL", "ADDP4", "ADDP4_IMM"):
            body = main[opcode]
        else:
            body = routed[opcode]
        call = re.findall(
            r"\b(ia64_gen_(?:(?:integer|gr)_nat_from_[12]|gr_nat_clear))\(([^;]+)\);",
            body,
        )
        require(len(call) == 1, f"Unrecognized scalar NaT operation for {opcode}")
        name, arguments = map(compact, call[0])
        if name == "ia64_gen_gr_nat_clear":
            require(arguments == "insn,op->destination", f"Unexpected clear operands for {opcode}")
            expression = "false"
        elif name.endswith("_1"):
            require(arguments in {"insn,op->destination,op->source1",
                                  "insn,op->destination,op->source2"},
                    f"Unexpected copy operands for {opcode}")
            expression = f"sourceCopy s f i.dst i.r{'2' if arguments.endswith('1') else '3'}"
        else:
            require(arguments == "insn,op->destination,op->source1,op->source2",
                    f"Unexpected NaT-OR operands for {opcode}")
            expression = "sourcePair s f i.dst i.r2 i.r3"
        if opcode not in ("MOVL", "ADDP4", "ADDP4_IMM"):
            require(compact(body) == f"{name}({arguments});break;",
                    f"Unexpected ordinary NaT case for {opcode}")
        result[opcode] = expression
    return result


def ordinary_theorems(values, nat, affected):
    for opcode, operands in OPCODES.items():
        prefix = f"theorem ordinaryValue_{opcode} (x y old imm : Word)"
        proof = next(value for value in values if value.startswith(prefix))
        claim = proof.split(" := by", 1)[0].rsplit(") : ", 1)[1]
        field_form = opcode in integer.BITFIELD
        actual = claim.rsplit(" = (SDM." if field_form else " = SDM.", 1)[0]
        require(":= by" not in actual and actual, f"Missing source ALU expression {opcode}")
        counts = " (count : i.imm < 64#64)" if opcode in SHIFT_IMM else ""
        call_count = " count" if counts else ""
        fields = ""
        let_fields = ""
        value_equality = "value"
        form_args = ""
        packed_args = ""
        if field_form:
            packed, separator, actual = actual.partition("; ")
            require(separator and packed.startswith("let imm := "),
                    f"Missing decoded field packing for {opcode}")
            fields = " (pos len fill : Word)"
            form_args = " pos len fill"
            bound = 16 if opcode == "DEP" else 64
            counts = (" (legalPos : pos < 64#64)"
                      f" (legalLen : 1#64 ≤ len ∧ len ≤ {bound}#64)")
            call_count = " pos len fill legalPos legalLen"
            packed_args = " pos len fill legalPos legalLen"
            if opcode in {"DEP_IMM", "DEPZ_IMM"}:
                counts += f" (legalFill : fill < {2 if opcode == 'DEP_IMM' else 256}#64)"
                call_count += " legalFill"
                packed_args += " legalFill"
            counts += f" (encoding : i.imm = {packed.removeprefix('let imm := ')})"
            value_equality = "by\n    rw [encoding]\n    exact value"
            let_fields = " encoding"
        unused = {"x", "y", "imm"} - set(operands.split())
        ignored = [f"_{name}" if name in unused else name for name in ("x", "y", "imm")]
        # Unaffected opcodes have no optional CPUID gate in the actual
        # translator. The Slot normalizer supplies the absence of a gate.
        normalized = "" if opcode in affected else " (_feature : i.missingFeature = false)"
        source_count = packed_args + let_fields if field_form else call_count
        op_spec = f"(alu_{opcode}{form_args})"
        substitutions = {"x": "(SDM.value s i.r2)", "y": "(SDM.value s i.r3)", "imm": "i.imm"}
        sd_operands = " ".join(substitutions.get(v, v) for v in operands.split())
        yield f"""def alu_{opcode}{fields} : Alu :=
  ⟨fun {' '.join(ignored)} => IA64Integer.SDM.{opcode} {operands},
   {str('x' in operands.split()).lower()}, {str('y' in operands.split()).lower()}⟩

def sourceValue_{opcode} (x y old imm : Word) : Word := {actual}

def sourceBody_{opcode} (s : State α) (i : Slot) (f : Facts) : State α :=
  sourceCommit s f i.dst
    (sourceValue_{opcode} (SDM.value s i.r2) (SDM.value s i.r3) (s.gr i.dst) i.imm)
    ({nat[opcode]})

theorem sourceBody_{opcode}_eq (s : State α) (i : Slot) (f : Facts) (ok : Sound s i f){fields}{counts} :
    sourceBody_{opcode} s i f = SDM.operate s i {op_spec} := by
  have value := IA64Integer.ordinaryValue_{opcode}
    (SDM.value s i.r2) (SDM.value s i.r3) (s.gr i.dst) i.imm{call_count}
  have lifted : sourceValue_{opcode} (SDM.value s i.r2) (SDM.value s i.r3) (s.gr i.dst) i.imm =
      IA64Integer.SDM.{opcode} {sd_operands} := {value_equality}
  have copied : ∀ src, sourceCopy s f i.dst src = SDM.nat s src :=
    fun src => sourceCopy_eq s i f i.dst src ok
  have both : sourcePair s f i.dst i.r2 i.r3 = (SDM.nat s i.r2 || SDM.nat s i.r3) :=
    sourcePair_eq s i f i.dst i.r2 i.r3 ok
  simp [sourceBody_{opcode}, SDM.operate, alu_{opcode}, lifted,
        sourceCommit_eq _ i _ _ _ _ ok, copied, both, IA64Integer.SDM.sourceNaT]

theorem ordinaryStep_{opcode} (s : State α) (i : Slot) (f : Facts) (ok : Sound s i f){fields}
    (_defined : SDM.definedSources s i {op_spec}){normalized}{counts} :
    finish s i (prepareProjected s i f) (fun s i => sourceBody_{opcode} s i f) =
      sdStep s i {op_spec} :=
  compose _ _ _ (fun s i => prepareProjected s i f) _
    (prepareProjected_eq s i f ok) (fun _ => sourceBody_{opcode}_eq s i f ok{source_count})
"""


def fast_theorems(values, nats, supported):
    for opcode in sorted(supported):
        vp = next(term for term in values if term.startswith(f"theorem predicatedValue_{opcode} "))
        value = vp.split(":\n", 1)[1].split(" =\n", 1)[0].strip()
        np = next(term for term in nats if term.startswith(f"theorem predicatedNaT_{opcode}\n"))
        packed = np.split("    let newFact := ", 1)[1].split(" =\n", 1)[0].strip()
        yield f"""def sourceFastValue_{opcode} (x y old imm predicate : Word) : Word := {value}

def sourceFastNaT_{opcode}
    (natWord dst r2 r3 predicate oldFact r2Fact r3Fact : Word) : Word :=
  let newFact := {packed}

def sourceFastStep_{opcode} (s : State α) (i : Slot) (f : Facts) : Outcome α :=
  let predicate := IA64Integer.SDM.natBit (s.pr i.qp)
  sourceFastOutcome s i
    (sourceFastValue_{opcode} (SDM.value s i.r2) (SDM.value s i.r3) (s.gr i.dst) i.imm predicate)
    (sourceFastNaT_{opcode} (s.natWord 0) (regWord i.dst) (regWord i.r2) (regWord i.r3) predicate
      (factWord (sourceKnownNat f i.dst)) (factWord (sourceKnownNat f i.r2))
      (factWord (sourceKnownNat f i.r3)))
    (s.pr i.qp)

theorem fastStep_{opcode} (s : State α) (i : Slot) (f : Facts) (ok : Sound s i f)
    (guards : FastApplicability i f) : sourceFastStep_{opcode} s i f = sdStep s i alu_{opcode} := by
  let qp := IA64Integer.SDM.natBit (s.pr i.qp)
  have value := IA64Integer.predicatedValue_{opcode}
    (SDM.value s i.r2) (SDM.value s i.r3) (s.gr i.dst) i.imm qp
  change sourceFastValue_{opcode} (SDM.value s i.r2) (SDM.value s i.r3)
    (s.gr i.dst) i.imm qp = _ at value
  have nat := IA64Integer.predicatedNaT_{opcode} (s.natWord 0)
    (regWord i.dst) (regWord i.r2) (regWord i.r3) qp
    (factWord (sourceKnownNat f i.dst)) (factWord (sourceKnownNat f i.r2))
    (factWord (sourceKnownNat f i.r3))
    (regWord_nonzero i.dst guards.dstNonzero)
    (regWord_bound i.dst guards.dstStatic) (regWord_bound i.r2 guards.r2Static)
    (regWord_bound i.r3 guards.r3Static)
    (sound_static_fact s i f i.dst guards.dstStatic ok)
    (sound_static_fact s i f i.r2 guards.r2Static ok)
    (sound_static_fact s i f i.r3 guards.r3Static ok)
  change sourceFastNaT_{opcode} (s.natWord 0) (regWord i.dst) (regWord i.r2)
    (regWord i.r3) qp (factWord (sourceKnownNat f i.dst))
    (factWord (sourceKnownNat f i.r2)) (factWord (sourceKnownNat f i.r3)) = _ at nat
  dsimp only [qp] at value nat
  simp only [predWord] at value nat
  rw [static_nat s i.r2 guards.r2Static, static_nat s i.r3 guards.r3Static] at nat
  dsimp only [sourceFastStep_{opcode}]
  rw [value, nat]
  exact fastEffectsCompose s i f alu_{opcode} guards
"""


def generate(root, overrides=None):
    overrides = overrides or {}
    sources = {name: overrides.get(name, source(root, name)) for name in GUARDS}
    translator = sources[TRANSLATE]
    generator = sources[GEN_INTEGER]
    affected = known_routes(sources, source(root, "cpu.h"))
    projected = lift_preparation(translator)
    opcode_source = source(root, "decode/opcode.inc")
    flow_checks = check_flows(translator, overrides.get("cpu.c", source(root, "cpu.c")),
                              source(root, "translate/translate.h"), opcode_source, OPCODES)
    nats = nat_routes(generator, OPCODES)
    values, fast = integer.generate_value_proofs(generator, translator, source(root, "decode/decode.c"))
    nats_fast = integer.generate_fast_nat_proofs(translator, opcode_source)
    used = [value for value in values if not value.startswith("theorem ordinaryValue_") or
            value.split()[1].removeprefix("ordinaryValue_") in OPCODES]
    # Build self-contained generated Lean, keeping the independently
    # authored state/SDM rules separate from the lifted C expressions.
    reference = (integer.HERE / "Spec.lean").read_text(encoding="utf-8")
    state = (HERE / "Spec.lean").read_text(encoding="utf-8").replace("import Integer.Spec", "")
    execution = (HERE / "Execution.lean.inc").read_text(encoding="utf-8")
    facts = (HERE / "Facts.lean.inc").read_text(encoding="utf-8")
    body = "\n\n".join(["namespace IA64Tcg", "open IA64Integer",
                        'local notation "Word" => BitVec 64',
                        "set_option linter.unusedVariables false",
                        "set_option maxHeartbeats 2000000", projected,
                        *ordinary_theorems(used, nats, affected),
                        *fast_theorems(used, nats_fast, fast), "end IA64Tcg"])
    return "\n\n".join([reference, integer.wrap_generated(used + nats_fast), state,
                         execution, facts, flow_checks, body])


def run_lean(code):
    with tempfile.TemporaryDirectory(prefix="ia64-lean-tcg-") as temp:
        module = Path(temp) / "Proofs.lean"
        module.write_text(code, encoding="utf-8")
        return subprocess.run(["lake", "env", "lean", "-DwarningAsError=true", str(module)],
                              cwd=HERE.parent, capture_output=True, text=True, check=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=integer.TARGET.parents[1])
    parser.add_argument("--emit-lean", type=Path)
    parser.add_argument("--coverage", action="store_true")
    parser.add_argument("--mutation-test", action="store_true")
    args = parser.parse_args()
    code = generate(args.source_root)
    if args.coverage:
        print(json.dumps({
            f"tcg:{opcode}": "Scalar GR, packed NaT, dirty, predicate and fault-request step projection"
            for opcode in OPCODES
        }, sort_keys=True))
        return
    if args.emit_lean:
        args.emit_lean.write_text(code, encoding="utf-8")
        return
    result = run_lean(code)
    require(result.returncode == 0, result.stdout + result.stderr)
    print(f"TCG: source-bound scalar step projections passed for {len(OPCODES)} ordinary "
          "and all 10 branchless predicated forms")
    print("TCG: packed NaT, predicate, dirty and frame facts hold along covered scalar traces")
    if args.mutation_test:
        translate = source(args.source_root, TRANSLATE)
        mutant, count = re.subn(r"TCG_COND_EQ(?=, qp_value, 0, skip\))", "TCG_COND_NE", translate)
        require(count == 1, "Missing predicate skip mutation")
        source_mutant = generate(args.source_root, {TRANSLATE: mutant})
        prefix = source_mutant.split("\ndef alu_", 1)[0]
        result = run_lean(prefix + "\nend IA64Tcg\n")
        require(result.returncode != 0 and "unsolved goals" in result.stdout,
                "The opposite dynamic qualification unexpectedly passed:\n"
                + result.stdout + result.stderr)
        print("TCG: Lean rejected reversing the emitted dynamic predicate skip")
        cpu = source(args.source_root, "cpu.c")
        mutant, count = re.subn(r"(cpu->env\.nat\[0\]) \| (cpu->env\.nat\[1\])",
                                r"\1 & \2", cpu)
        require(count == 1, "Missing native TB NaT key mutation")
        source_mutant = generate(args.source_root, {"cpu.c": mutant})
        result = run_lean(source_mutant.split("\nnamespace IA64Tcg\n\nopen IA64Integer", 1)[0])
        require(result.returncode != 0 and "counterexample" in result.stdout.lower(),
                "The unsound NaT key unexpectedly passed:\n" + result.stdout + result.stderr)
        print("TCG: Lean rejected replacing the TB NaT-key disjunction with conjunction")


if __name__ == "__main__":
    try:
        main()
    except ValueError as error:
        raise SystemExit(str(error)) from None
