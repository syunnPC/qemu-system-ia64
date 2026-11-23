#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Lift the whole standalone decoder and prove selected complete SDM forms."""

import argparse
import gc
import json
import os
import re
from random import Random
from pathlib import Path
import shlex
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))

from forms import (ALU, ATOMICS16, COMPARE, COMPARE_ALIASES, FP_LOADS, FP_LOGIC,
                   INTEGER_BITFIELD_ALIASES, INTEGER_BITFIELDS, INTEGER_FEATURES,
                   INTEGER_MAJOR_SEVEN, INTEGER_MEMORY, INTEGER_MISC, INTEGER_TESTS,
                   INTEGER_TEST_UPDATE_ALIASES, MULTIMEDIA, REFUTATIONS)
from cast import Array, Frame, Machine, Pointer, Ref, Unsupported, need
from Decode.check import ROOT, c_function, get_match, run_lean, uncomment
from model import feature_masks, lean_gate

UNIT_NAMES = ("RESERVED", "M", "I", "B", "F", "L", "X")
OPERANDS = ("r1", "r2", "r3", "p1", "p2", "b1", "b2", "sf", "fp_precision", "imm")
FLAGS = (
    "hint_m_reg_increment", "reg_base_update", "imm_base_update", "mem_acquire",
    "mem_release", "compare_unc", "clear_p2_before_predicate", "check_fp",
    "fp_load_speculative", "fp_load_advanced", "fp_load_check", "fp_load_check_clear",
    "probe_fault", "probe_imm", "placement_illegal", "reserved_field",
)
METADATA = ("unit", "raw", "address", "slot", "qp")
FORMS = (ALU | COMPARE | MULTIMEDIA | INTEGER_MEMORY | ATOMICS16 | FP_LOADS |
         INTEGER_MAJOR_SEVEN | INTEGER_BITFIELDS | INTEGER_TESTS | INTEGER_MISC | FP_LOGIC)
REFUTED_FORMS = {name: witness.form for name, witness in REFUTATIONS.items()}
REJECTION_CHECK = {
    "CONSTANT_ZERO_VIOLATION": "cConstantZeroIllegal",
    "RESERVED_IF_QP": "cReservedIfQp",
}


def get_ast(root, work):
    unit = work / "decoder.c"
    unit.write_text('''#include <limits.h>
#include "target/ia64/decoder.c"
#include "target/ia64/decode/decode.c"
_Static_assert(CHAR_BIT == 8 && sizeof(short) == 2 && sizeof(int) == 4 &&
               sizeof(long) == 8 && sizeof(long long) == 8 && sizeof(void *) == 8,
               "symbolic execution requires the LP64 C ABI");
''', encoding="utf-8")
    clang = shlex.split(os.environ.get("CLANG", "clang"))
    text = subprocess.run(
        [*clang, "-std=c11", "-I", str(root), "-fsyntax-only", "-Xclang", "-ast-dump=json",
         str(unit)],
        check=True, text=True, capture_output=True,
    ).stdout
    return json.loads(text)


def check_model_route(root):
    text = uncomment((root / "target/ia64/translate/translate.c").read_text(encoding="utf-8"))
    params, code = c_function(text, "ia64_decode_insn_for_model")
    get_match(r"CPUIA64State\s*\*env,\s*IA64SlotUnit\s+unit,\s*uint64_t\s+raw,"
              r"\s*uint64_t\s+address,\s*uint8_t\s+slot", params,
              "decode model parameters", full=True)
    get_match(r"\s*Ia64Instruction insn\s*=\s*ia64_decode_insn\(unit, raw, address, slot\);"
              r"\s*return insn;\s*", code, "decode model path", full=True)
    _, decode_route = c_function(text, "ia64_tr_translate_insn")
    route = re.sub(r"\s+", "", decode_route)
    get_match(re.escape(
        "template_code=ia64_bundle_template_code(low);"
        "template_info=ia64_template_info(template_code);"
        "ctx->restart.exit_after_bundle=false;db->pc_next=bundle_ip+16;"
        "if(!template_info->defined){"
        "ia64_gen_check_instruction_debug(ctx,bundle_ip,ctx->restart.start_slot);"
        "ia64_gen_raise_exception(IA64_EXCP_RESERVED_TEMPLATE,bundle_ip,template_code,"
        "ctx->restart.start_slot);db->is_jmp=DISAS_NORETURN;return;}"
        + "".join(f"slots[{i}]=ia64_bundle_slot(low,high,{i});" for i in range(3))
        + "if((template_code==4||template_code==5)&&ctx->restart.start_slot==2){"
        "ia64_gen_check_instruction_debug(ctx,bundle_ip,2);"
        "ia64_gen_raise_exception(IA64_EXCP_ILLEGAL,bundle_ip,slots[2],2);"
        "db->is_jmp=DISAS_NORETURN;return;}"
        "skip_x_slot=false;for(slot=ctx->restart.start_slot;slot<3;slot++){"
        "if(skip_x_slot&&slot==2){break;}"
        "decoded[slot]=ia64_decode_insn_for_model("
        "ctx->env,template_info->units[slot],slots[slot],bundle_ip,slot);"
        "ia64_apply_mlx_long_fixup(template_code,slots,slot,&decoded[slot],&skip_x_slot);}"
    ), route, "TCG bundle decode route")
    return feature_masks(root, text, ATOMICS16, INTEGER_FEATURES, FORMS.keys() | {"NOP"})


def observation_fields(machine, insn):
    fields = insn.fields
    need(fields.keys() == set((*METADATA, *FLAGS, "ctx", "opcode", "operands",
                               "encoding_class", "pred_update", "valid")),
         "decoded instruction has changed fields")
    operands = fields["operands"].fields
    need(operands.keys() == {"decoder"}, "unexpected active operand union member")
    operands = operands["decoder"].fields
    need(operands.keys() == set(OPERANDS), "decoder operand view has changed fields")
    words = ((1,), *(fields[name] for name in METADATA), *(operands[name] for name in OPERANDS),
             fields["encoding_class"], fields["pred_update"], *(fields[name] for name in FLAGS),
             fields["valid"], (machine.truth(fields["ctx"]),))
    return words


def pack(words):
    result = tuple(bit for word in reversed(words) for bit in word)
    need(len(result) == 395, "decoded observation has changed widths")
    return result


def selected(machine, insn, opcode):
    return machine.bits.eq(insn.fields["opcode"], machine.as_int(machine.enums[f"IA64_OP_{opcode}"], "enum Ia64Opcode"))


def check_adapter(root, work, machine, decoded):
    bits = machine.bits
    fields = (decoded.fields["opcode"], *observation_fields(machine, decoded))
    rng = Random(0x1A64)

    def sample(guard):
        values = {"unit": rng.getrandbits(3), "raw": rng.getrandbits(41),
                  "address": rng.getrandbits(64), "slot": rng.getrandbits(8)}
        while guard > 1:
            (name, bit), lo, hi = bits.nodes[guard]
            take_one = bool(hi) and (not lo or rng.randrange(2) == 1)
            values[name] = (values[name] & ~(1 << bit)) | (int(take_one) << bit)
            guard = hi if take_one else lo
        return values

    def bit_value(bit, sample):
        while bit > 1:
            (name, index), lo, hi = bits.nodes[bit]
            bit = hi if sample[name] & (1 << index) else lo
        return bit

    samples = []
    for name in sorted(machine.enums):
        if name.startswith("IA64_OP_"):
            guard = selected(machine, decoded, name.removeprefix("IA64_OP_"))
            if guard:
                samples.append(sample(guard))
    samples.extend(sample(1) for _ in range(32))
    samples.extend(dict(unit=machine.enums[f"IA64_UNIT_{'I' if witness.slot == 1 else 'M'}"],
                        raw=witness.raw, address=0, slot=witness.slot)
                   for witness in REFUTATIONS.values())
    expected = [
        [sum(bit_value(bit, values) << i for i, bit in enumerate(field)) for field in fields]
        for values in samples
    ]
    stdin = "".join(f"{v['unit']} {v['raw']:x} {v['address']:x} {v['slot']}\n" for v in samples)
    executable = work / "observe"
    cc = shlex.split(os.environ.get("CLANG", "clang"))
    subprocess.run([*cc, "-std=c11", "-O2", "-I", str(root),
                    str(HERE / "observe.c"), str(root / "target/ia64/decode/decode.c"),
                    "-o", str(executable)], check=True)
    actual = subprocess.run([str(executable)], input=stdin, text=True,
                            stdout=subprocess.PIPE, check=True).stdout.splitlines()
    for case, wanted, line in zip(samples, expected, actual, strict=True):
        need([int(x) for x in line.split()] == wanted,
             f"native C and symbolic adapter disagree for {case}")


def lift_definition(bits, name, inputs, value, result_type=None):
    # Emit the source-derived decision DAG. Lean compares this term to
    # independent SDM fields and selectors in Spec.lean.
    roots = [value] if isinstance(value, int) else value
    reachable = set()
    pending = list(roots)
    while pending:
        decision = pending.pop()
        if decision > 1 and decision not in reachable:
            _, lo, hi = bits.nodes[decision]
            if (lo, hi) != (0, 1):
                reachable.add(decision)
                pending.extend((lo, hi))

    def term(decision):
        if decision < 2:
            return ("false", "true")[decision]
        (name, index), lo, hi = bits.nodes[decision]
        if (lo, hi) == (0, 1):
            return f"{name}.getLsbD {index}"
        return f"d{decision}"

    declarations = []
    for decision in sorted(reachable):
        (var, index), lo, hi = bits.nodes[decision]
        declarations.append(f"  let d{decision} := bif {var}.getLsbD {index} "
                            f"then {term(hi)} else {term(lo)}")
    if isinstance(value, int):
        result = term(value)
        value_type = "Bool"
    else:
        parts, pos = [], len(value) - 1
        while pos >= 0:
            bit = value[pos]
            if bit < 2:
                end = pos
                while pos >= 0 and value[pos] < 2:
                    pos -= 1
                chunk = bits.number(value[pos + 1:end + 1])
                parts.append(f"({chunk} : BitVec {end - pos})")
            else:
                variable, low, high = bits.nodes[bit]
                if (low, high) == (0, 1):
                    source, top = variable
                    base = top
                    while pos > 0 and value[pos - 1] > 1:
                        if bits.nodes[value[pos - 1]] != ((source, base - 1), 0, 1):
                            break
                        base -= 1
                        pos -= 1
                    parts.append(f"({source}.extractLsb' {base} {top - base + 1})")
                else:
                    end = pos
                    while pos > 0 and value[pos - 1] == bit:
                        pos -= 1
                    width = end - pos + 1
                    parts.append(f"(bif {term(bit)} then ({(1 << width) - 1} : BitVec {width}) else 0)")
                pos -= 1
        result = " ++ ".join(parts)
        value_type = f"BitVec {len(value)}"
    header = f"def {name} {inputs} : {result_type or value_type} :="
    return "\n".join((header, *declarations, "  " + result))


def ref(root):
    return Pointer(((1, Ref(root)),))


def layouts(machine):
    b = machine.bits
    low = b.word("low", 64)
    high = b.word("high", 64)
    n = b.word("index", 2)
    raw = machine.call("ia64_bundle_slot", [low, high, b.cast(n, 32)], Frame(1, {}))
    template = machine.call("ia64_bundle_template_code", [low], Frame(1, {}))
    code = b.word("code", 5)
    frame = Frame(1, {})
    info = machine.call("ia64_template_info", [b.cast(code, 8)], frame)
    valid = machine.truth(machine.read(machine.member(info, "defined"), frame))
    units = machine.member(info, "units")
    stops = machine.member(info, "stop_after")
    row = machine.as_int(machine.enums["IA64_UNIT_RESERVED"], "enum IA64SlotUnit")
    stop = 0
    for index in range(3):
        selector = b.eq(n, b.constant(index, 2))
        entry = b.constant(index, 32)
        src = machine.read(machine.index(units, entry, frame), frame)
        row = b.select(selector, src, row)
        at_stop = machine.truth(machine.read(machine.index(stops, entry, frame), frame))
        stop = b.either(stop, b.both(selector, at_stop))
    for bit in (*template[5:], *row[3:]):
        need(bit == 0, "decoder template or unit wider than architectural field")
    return [
        lift_definition(b, "cSlot", "(low high : Word) (index : BitVec 2)", raw),
        lift_definition(b, "cTemplate", "(low : Word)", template),
        lift_definition(b, "cDefined", "(code : BitVec 5)", valid),
        lift_definition(b, "cUnit", "(code : BitVec 5) (index : BitVec 2)", row[:3]),
        lift_definition(b, "cStop", "(code : BitVec 5) (index : BitVec 2)", stop),
    ]


def decode(machine):
    b = machine.bits
    unit = b.word("unit", 3)
    raw = b.word("raw", 41)
    address = b.word("address", 64)
    slot = b.word("slot", 8)
    decoded = []
    for code in range(8):
        result = machine.call("ia64_decode_insn",
                              [b.constant(code, 32), b.cast(raw, 64), address, slot],
                              Frame(1, {}))
        decoded.append(result)
    complete = machine.combine((b.eq(unit, b.constant(i, 3)), v) for i, v in enumerate(decoded))
    return complete


def fixup(machine, complete):
    b = machine.bits
    code, slot = b.word("code", 5), b.word("slot", 8)
    l, x = b.word("l", 41), b.word("x", 41)
    raw = b.word("raw", 41)
    paired = b.either(b.eq(code, b.constant(4, 5)), b.eq(code, b.constant(5, 5)))
    is_one = b.eq(slot, b.constant(1, 8))
    ordinary = b.neg(b.both(paired, is_one))

    def apply_fixup(insn, predicate, chosen_slot, slots):
        frame = Frame(predicate, {"slots": Array(tuple(b.cast(s, 64) for s in slots)),
                                  "instruction": insn, "skip": (0,)})
        machine.call("ia64_apply_mlx_long_fixup",
                     [b.cast(code, 8), ref("slots"), b.cast(chosen_slot, 32),
                      ref("instruction"), ref("skip")], frame)
        return frame.ram["instruction"], frame.ram["skip"][0]

    # Everywhere except an MLX bundle's slot 1, actual fixup must leave
    # the complete decoder result untouched and keep slot 2 eligible.
    after, skip = apply_fixup(complete, ordinary, slot, (raw, l, x))
    unchanged = b.all((b.eq(pack(observation_fields(machine, complete)), pack(observation_fields(machine, after))),
                       b.eq(complete.fields["opcode"], after.fields["opcode"]), b.neg(skip)))
    check_ordinary = lift_definition(
        b, "cOrdinaryFixup",
        "(code : BitVec 5) (unit : UnitCode) (raw l x : Slot) (address : Word) (slot : BitVec 8)",
        unchanged,
    )

    # The L candidate receives the actual L bits (renamed below); the
    # X bits are separate and are always 41-bit ISA slots.
    bounded_slot = b.constant(1, 8)
    # Recreate the L candidate for fixed physical slot 1.
    candidate = machine.call("ia64_decode_insn",
                             [b.constant(machine.enums["IA64_UNIT_L"], 32), b.cast(raw, 64),
                              b.word("address", 64), bounded_slot], Frame(1, {}))
    pair_result, skip = apply_fixup(candidate, paired, bounded_slot, (l, raw, x))
    no_ordinary = b.neg(b.any(selected(machine, pair_result, opcode) for opcode in FORMS))
    check_pair = lift_definition(b, "cMlxNoOrdinary",
                                 "(code : BitVec 5) (raw l x : Slot) (address : Word)", no_ordinary)
    need(after == complete, "ordinary MLX fixup changed the source record")
    check_skip = lift_definition(b, "cPairSkip",
                                 "(code : BitVec 5) (raw l x : Slot) (address : Word)", skip)
    return [check_ordinary, check_pair, check_skip], pair_result


def integer_seven_partition(machine, complete):
    b = machine.bits
    classes = complete.fields["encoding_class"]
    names = ("DEFINED", "RESERVED_IF_QP", "CONSTANT_ZERO_VIOLATION")
    enc = {kind: b.eq(classes, machine.as_int(machine.enums[f"IA64_ENCODING_{kind}"],
                                             "enum IA64EncodingClass")) for kind in names}
    invalid = selected(machine, complete, "ILLEGAL")
    tags = b.any(selected(machine, complete, name) for name in INTEGER_MAJOR_SEVEN)
    states = [b.both(tags, enc["DEFINED"]),
              b.both(invalid, enc["RESERVED_IF_QP"]),
              b.both(invalid, enc["CONSTANT_ZERO_VIOLATION"])]
    unit = machine.enums["IA64_UNIT_I"]
    domain = b.both(b.eq(b.word("unit", 3), b.constant(unit, 3)),
                    b.eq(b.word("raw", 41)[37:41], b.constant(7, 4)))
    src = [lift_definition(b, "cIntegerSevenStatus",
                           "(unit : UnitCode) (raw : Slot) (address : Word) (slot : BitVec 8)",
                           tuple(b.both(domain, state) for state in states))]
    rows = "\n    ∨ ".join(f"aEncoding ({form}) raw" for form in INTEGER_MAJOR_SEVEN.values())
    src.append(f"""
def integerSevenAssigned (raw : Slot) : Prop :=
    {rows}

instance (raw : Slot) : Decidable (integerSevenAssigned raw) := by
  unfold integerSevenAssigned
  infer_instance

-- Major seven uses the I9 constant-zero convention before its purple
-- cell default. Purple cells and unassigned MUX1 types are Reserved if QP.
theorem integerSevenPartition (raw : Slot) (address : Word) (slot : BitVec 8)
    (seven : raw.extractLsb' 37 4 = 7) :
    cIntegerSevenStatus iUnit raw address slot =
      (if integerSevenAssigned raw then 1 else
       if iMajorSeven 1 raw ∧ (iCell 1 1 2 raw ∨ iCell 1 1 3 raw) ∧
           raw.extractLsb' 13 7 ≠ 0 then 4 else 2) := by
  dsimp only [cIntegerSevenStatus, integerSevenAssigned, aEncoding, iMajorSeven, iCell,
             iSevenFields, mux1Type, iShiftCells, iUnit]
  bv_decide
""")
    return src


STRUCTURAL = """
theorem bundleSlot_matches (low high : Word) (index : BitVec 2) :
    cSlot low high index = (slotBits low high index).zeroExtend 64 := by
  simp only [cSlot, slotBits] <;> bv_decide

theorem bundleTemplate_matches (low : Word) :
    cTemplate low = (templateBits low).zeroExtend 8 := by
  simp only [cTemplate, templateBits] <;> bv_decide

theorem definedTemplate_matches (code : BitVec 5) :
    cDefined code = validTemplate code := by
  simp only [cDefined, validTemplate] <;> bv_decide

theorem slotUnit_matches (code : BitVec 5) (index : BitVec 2) :
    cUnit code index = templateUnit reservedUnit mUnit iUnit bUnit fUnit lUnit xUnit code index := by
  simp only [cUnit, templateUnit, reservedUnit, mUnit, iUnit, bUnit, fUnit, lUnit, xUnit]
  bv_decide

theorem ordinaryFixup_matches (code : BitVec 5) (unit : UnitCode) (raw l x : Slot)
    (address : Word) (slot : BitVec 8)
    (ordinary : (code ≠ 4 ∧ code ≠ 5) ∨ slot ≠ 1) :
    cOrdinaryFixup code unit raw l x address slot = true := by
  simp only [cOrdinaryFixup] <;> bv_decide

theorem mlxHasNoOrdinary (code : BitVec 5) (raw l x : Slot) (address : Word)
    (paired : code = 4 ∨ code = 5) : cMlxNoOrdinary code raw l x address = true := by
  simp only [cMlxNoOrdinary] <;> bv_decide
"""


def generate(root, work, emit_opcode=None):
    masks = check_model_route(root)
    machine = Machine(get_ast(root, work))
    ucode = [machine.enums[f"IA64_UNIT_{name}"] for name in UNIT_NAMES]
    need(len(set(ucode)) == len(UNIT_NAMES) and all(0 <= code < 8 for code in ucode),
         "decoder slot unit enum changed width or aliases")
    forms = FORMS | REFUTED_FORMS | {"NOP": None}
    if emit_opcode is not None:
        forms = {opcode: forms[opcode] for opcode in emit_opcode}
    src = [(HERE / filename).read_text(encoding="utf-8")
           for filename in ("Spec.lean", "Compare.lean.inc")]
    if "NOP" in forms:
        src.append((HERE / "NopSpec.lean.inc").read_text(encoding="utf-8"))
    src += ["namespace FullDecodeImplementation",
           "open FullDecodeSpec", "set_option maxRecDepth 16384",
           "set_option maxHeartbeats 0", "set_option exponentiation.threshold 512",
           "set_option linter.unusedVariables false"]
    for name, code in zip(UNIT_NAMES, ucode, strict=True):
        src.append(f"def {name.lower()}Unit : UnitCode := {code}")
    layout = layouts(machine)
    complete = decode(machine)
    check_adapter(root, work, machine, complete)
    mlx_projections, pair_result = fixup(machine, complete)
    src += layout + mlx_projections + [STRUCTURAL, (HERE / "Bundle.lean.inc").read_text()]
    src += [lean_gate(name, mask) for name, mask in masks.items()]
    src += integer_seven_partition(machine, complete)
    b, image = machine.bits, pack(observation_fields(machine, complete))
    args = "(unit : UnitCode) (raw : Slot) (address : Word) (slot : BitVec 8)"
    call = "unit raw address slot"
    aliases = b.any(
        selected(machine, insn, opcode)
        for insn in (complete, pair_result) for opcode in COMPARE_ALIASES
    )
    src.append(lift_definition(b, "cCompareAliasTag",
                               args + " (code : BitVec 5) (l x : Slot)", aliases))
    alias_tests = b.any(
        selected(machine, insn, opcode)
        for insn in (complete, pair_result)
        for opcode in INTEGER_BITFIELD_ALIASES + INTEGER_TEST_UPDATE_ALIASES
    )
    src.append(lift_definition(b, "cBitfieldAliasTag",
                               args + " (code : BitVec 5) (l x : Slot)", alias_tests))
    for kind, symbol in REJECTION_CHECK.items():
        encoding = machine.as_int(machine.enums[f"IA64_ENCODING_{kind}"], "enum IA64EncodingClass")
        rejects = b.both(selected(machine, complete, "ILLEGAL"),
                         b.eq(complete.fields["encoding_class"], encoding))
        src.append(lift_definition(b, symbol, args, rejects))
    src.append(f"""
theorem compareAliasesHaveNoTag {args} (code : BitVec 5) (l x : Slot) :
    cCompareAliasTag {call} code l x = false := by
  dsimp only [cCompareAliasTag] <;> bv_decide

theorem bitfieldAliasesHaveNoTag {args} (code : BitVec 5) (l x : Slot) :
    cBitfieldAliasTag {call} code l x = false := by
  dsimp only [cBitfieldAliasTag] <;> bv_decide

theorem malformedA7 {args}
    (a : aUnit mUnit iUnit unit)
    (major : raw.extractLsb' 37 4 = 12 ∨ raw.extractLsb' 37 4 = 13 ∨
        raw.extractLsb' 37 4 = 14)
    (registerForm : raw.extractLsb' 34 2 < 2)
    (tb : raw.extractLsb' 36 1 = 1)
    (nonzero : raw.extractLsb' 13 7 ≠ 0) :
    cConstantZeroIllegal {call} = true := by
  dsimp only [cConstantZeroIllegal]
  dsimp only [aUnit, mUnit, iUnit] at a
  bv_decide

theorem malformedI30 (raw : Slot) (address : Word) (slot : BitVec 8)
    (major : raw.extractLsb' 37 4 = 5) (x2 : raw.extractLsb' 34 2 = 0)
    (family : raw.getLsbD 13 = true ∧ raw.getLsbD 19 = true)
    (nonzero : raw.extractLsb' 20 7 ≠ 0) :
    cConstantZeroIllegal iUnit raw address slot = true := by
  dsimp only [cConstantZeroIllegal, iUnit]
  bv_decide
""")
    for opcode, form in forms.items():
        name = f"c{opcode}"
        gate, operands = f"p{opcode}", f"o{opcode}"
        presence = selected(machine, complete, opcode)
        value = tuple(b.restrict(bit, presence) for bit in image)
        src.append(lift_definition(b, gate, args, presence))
        src.append(lift_definition(b, operands, args, value, "Observation"))
        src.append(f"def {name} {args} : Observation :=\n"
                   f"  bif {gate} {call} then {operands} {call} else 0")
        pair = f"cMlx{opcode}"
        ptag = selected(machine, pair_result, opcode)
        pair_image = pack(observation_fields(machine, pair_result))
        paired_args = "(code : BitVec 5) (raw l x : Slot) (address : Word)"
        src.append(lift_definition(b, pair, paired_args,
                                   tuple(b.both(ptag, bit) for bit in pair_image), "Observation"))
        if opcode == "NOP":
            src.append((HERE / "NopProof.lean.inc").read_text(encoding="utf-8"))
            continue
        if opcode in REFUTED_FORMS:
            witness = REFUTATIONS[opcode]
            raw = witness.raw
            if witness.rejected_as:
                bundle = raw << (5 + 41 * witness.slot)
                low, high = bundle & ((1 << 64) - 1), bundle >> 64
                wire = f"{low} {high} 0 {witness.slot}"
                src.append(f"""
def bundle{opcode} := cBundle {name} {pair}

theorem {opcode}_observed :
    bundle{opcode} {wire} = 0 := by decide

theorem {opcode}_classification :
    {REJECTION_CHECK[witness.rejected_as]} iUnit {raw} 0 {witness.slot} = true := by decide

theorem {opcode}_refuted :
    ¬ BundleContract reservedUnit mUnit iUnit bUnit fUnit lUnit xUnit ({form}) bundle{opcode} := by
  intro complete
  have actual := (complete {wire}).1
  rw [{opcode}_observed] at actual
  exact (by decide : (0 : Observation) ≠
    aBundle reservedUnit mUnit iUnit bUnit fUnit lUnit xUnit ({form}) {wire}) actual
""")
                continue
            low = raw << 5
            src.append(f"""
def bundle{opcode} := cBundle {name} {pair}

theorem {opcode}_observed :
    bundle{opcode} {low} 0 0 0 = aRecord ({form}) mUnit {raw} 0 0 := by
  decide

theorem {opcode}_classification :
    (bundle{opcode} {low} 0 0 0).getLsbD 1 = true ∧
    (bundle{opcode} {low} 0 0 0).extractLsb' 2 2 = 0 ∧
    (bundle{opcode} {low} 0 0 0).extractLsb' 50 32 = 0 := by
  rw [{opcode}_observed]
  decide

theorem {opcode}_refuted :
    ¬ BundleContract reservedUnit mUnit iUnit bUnit fUnit lUnit xUnit ({form}) bundle{opcode} := by
  intro complete
  have wire := (complete {low} 0 0 0).1
  rw [{opcode}_observed] at wire
  exact (by decide : aRecord ({form}) mUnit {raw} 0 0 ≠
    aBundle reservedUnit mUnit iUnit bUnit fUnit lUnit xUnit ({form}) {low} 0 0 0) wire
""")
            continue
        src.append(f"""
theorem {opcode}_pair {paired_args} (_paired : code = 4 ∨ code = 5) :
    {pair} code raw l x address = 0 := by
  dsimp only [{pair}] <;> bv_decide
""")
        major_seven_defs = (
            "iMajorSeven, iCell, iSevenFields, mux1Type, iShiftCells, iRegisterCount, "
            "iShiftR2, iShiftR3, iShiftImmediate, multiplyShiftCount, "
            if opcode in INTEGER_MAJOR_SEVEN else ""
        )
        bitfield_defs = "iBitfieldEncoding, bitfieldImm, packBitfield, " if opcode in INTEGER_BITFIELDS else ""
        predicate_defs = "iTestEncoding, iTestParallel, " if opcode in INTEGER_TESTS else ""
        misc_defs = "iMiscEncoding, iMiscImm, " if opcode in INTEGER_MISC else ""
        fp_defs = "fpMergeAliasEncoding, " if opcode in FP_LOGIC else ""
        src.append(f"""
theorem {opcode}_matches {args} :
    {name} {call} = aOpcode mUnit iUnit fUnit ({form}) {call} := by
  dsimp only [{name}, {gate}, {operands}, aOpcode, formUnit, aUnit, aEncoding, aRecord, aRecordTail,
             compareEncoding, hasExtension, fpLoadRow, aR1, aR2, aR3, aPred, aUnc, aPredUpdate,
             aImm, aCheckFp, aStatusField, reg,
             imm8, imm14, imm22, imm9, inc3, updateByRegister, updateByImmediate,
             memAcquire, memRelease, fpLoadAttributes, {major_seven_defs}{bitfield_defs}{predicate_defs}{misc_defs}
             {fp_defs}mUnit, iUnit, fUnit]
  bv_decide

def bundle{opcode} := cBundle {name} {pair}

theorem {opcode}_bundle_matches (low high address : Word) (index : BitVec 2) :
    bundle{opcode} low high address index =
      aBundle reservedUnit mUnit iUnit bUnit fUnit lUnit xUnit ({form}) low high address index :=
  cBundle_matches ({form}) {name} {pair} {opcode}_matches {opcode}_pair low high address index

theorem {opcode}_complete :
    BundleContract reservedUnit mUnit iUnit bUnit fUnit lUnit xUnit ({form}) bundle{opcode} := by
  intro low high address index
  rw [{opcode}_bundle_matches]
  exact ⟨rfl, aBundle_present _ _ _ _ _ _ _ _ _ _ _ _⟩
""")
        if opcode in ATOMICS16:
            src.append(f"""
def aoFault{opcode} (low high address : Word) (index : BitVec 2) (cpuid4 : Word) (qualifies : Bool) :
    Bool := (bundle{opcode} low high address index).getLsbD 394 && cAoReject cpuid4 qualifies

theorem {opcode}_withFeature :
    AoBundleContract reservedUnit mUnit iUnit bUnit fUnit lUnit xUnit ({form})
      bundle{opcode} aoFault{opcode} := by
  refine ⟨{opcode}_complete, ?_⟩
  intro low high address index cpuid4 qualifies
  rw [aoFault{opcode}, {opcode}_bundle_matches, aoReject_matches]
""")
        if opcode in INTEGER_FEATURES:
            feature = INTEGER_FEATURES[opcode]
            src.append(f"""
def integerFeatureFault{opcode} (low high address : Word) (index : BitVec 2)
    (cpuid4 : Word) (qualifies : Bool) : Bool :=
  (bundle{opcode} low high address index).getLsbD 394 && c{feature.capitalize()}Reject cpuid4 qualifies

theorem {opcode}_withFeature :
    IntegerFeatureBundleContract reservedUnit mUnit iUnit bUnit fUnit lUnit xUnit ({form})
      .{feature.lower()} bundle{opcode} integerFeatureFault{opcode} := by
  refine ⟨{opcode}_complete, ?_⟩
  intro low high address index cpuid4 qualifies
  rw [integerFeatureFault{opcode}, {opcode}_bundle_matches, {feature.lower()}Reject_matches]
""")
    src.append("end FullDecodeImplementation")
    return "\n\n".join(src) + "\n", (len(machine.called), len(machine.blocks))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=ROOT)
    parser.add_argument("--emit-lean", type=Path)
    parser.add_argument("--emit-opcode", action="append",
                        choices=sorted(FORMS.keys() | REFUTED_FORMS.keys() | {"NOP"}),
                        help="emit selected opcodes from the whole lifted decoder")
    parser.add_argument("--coverage", action="store_true")
    options = parser.parse_args()
    if options.emit_opcode and not options.emit_lean:
        parser.error("--emit-opcode requires --emit-lean")
    try:
        with tempfile.TemporaryDirectory(prefix="ia64-full-decode-lean-") as tmp:
            work = Path(tmp)
            text, (functions, blocks) = generate(options.source_root.resolve(), work, options.emit_opcode)
            gc.collect()
            if options.emit_lean:
                options.emit_lean.write_text(text, encoding="utf-8")
                return
            proof = work / "Decode.lean"
            proof.write_text(text, encoding="utf-8")
            # The combined decoder proof needs a larger Lean worker stack.
            run_lean(work, proof, "-s", "32768", "-DwarningAsError=true")
            if options.coverage:
                coverage = {f"decode:{opcode}": "complete bundle, selector and operand proof"
                            for opcode in FORMS}
                coverage["decode:NOP"] = "assigned M/I/B/F/L+X NOP and white B-unit encoding refinement"
                print(json.dumps(coverage, sort_keys=True))
            else:
                print(f"IA-64 decoder lifted {functions} functions and "
                      f"{blocks} statement sites. Exact proofs for {len(FORMS)} opcodes.")
                print("IA-64 NOP decoder matches all assigned short and long NOP and white B-unit forms.")
                for opcode, witness in REFUTATIONS.items():
                    if witness.rejected_as == "CONSTANT_ZERO_VIOLATION":
                        detail = ("SDM Vol. 3 §4.3.2.5 (I14) leaves bit 13 an ignored white field. "
                                  "QEMU reports a constant-zero violation.")
                    elif witness.rejected_as == "RESERVED_IF_QP":
                        detail = ("SDM Vol. 3 §4.3.8 (I29) leaves bit 36 an ignored white field. "
                                  "QEMU reports a predicate-qualified reserved encoding.")
                    else:
                        detail = (f"SDM Vol. 3 Table {witness.table} marks the encoding reserved when "
                                  f"PR[qp] is true. QEMU emits a valid {opcode} record.")
                    print(f"COUNTEREXAMPLE decode:{opcode} MII slot {witness.slot} "
                          f"raw=0x{witness.raw:x}. {detail}")
    except (ValueError, OSError, subprocess.CalledProcessError) as exc:
        parser.exit(1, f"Full IA-64 decode check failed: {exc}\n")


if __name__ == "__main__":
    main()
