# SPDX-License-Identifier: GPL-2.0-or-later
"""Reuse the scalar emitter lift for register ADD and bind its decoded operands."""

from pathlib import Path
import re

from Integer import check as integer
from SlotControl.reify import fixed, function, numeric_macro, recognize
from Tcg import check as scalar


HERE = Path(__file__).resolve().parent


def lift_operands(decoder, translator, header):
    main = function(decoder, "ia64_decode_insn")
    branch = re.search(
        r"if\s*\(pattern != NULL\) \{\s*Ia64Instruction insn\s*=\s*"
        r"ia64_base_insn\(pattern->opcode, unit, raw, address, slot\);\s*"
        r"insn\.operands\.decoder\.r1 = ia64_bits\(raw, (\d+), 7\);\s*"
        r"insn\.operands\.decoder\.r3 = ia64_bits\(raw, (\d+), 7\);\s*"
        r"if \(pattern->immediate\) \{\s*insn\.operands\.decoder\.imm = ia64_imm8\(raw\);\s*"
        r"\} else \{\s*insn\.operands\.decoder\.r2 = ia64_bits\(raw, (\d+), 7\);\s*"
        r"\}\s*return insn;\s*\}", main)
    integer.require(branch is not None, "unrecognized register ADD operand extraction")
    table = re.search(r"static const Ia64A3AluPattern ia64_a3_alu_patterns\[\] = \{(.*?)\};",
                      decoder, re.S)
    integer.require(table is not None
                    and re.search(r"\{ 0x0, 0, IA64_OP_ADD, false \}", table[1]),
                    "changed native register ADD decode row")
    # The ADD data emitter does not change RI or the PSR used to decide
    # IIPA collection and completion traps. Bind the active classifiers.
    classifiers = {
        "ia64_insn_may_modify_native_trap_psr": "1eb9b45aa73f69e9bcb8c68603b5267efcd9956edcce6f86b96661608e4d6f84",
        "ia64_insn_may_set_psr_ic": "266de440d898b56d28d9f3153e05d37ac28b46ee7816a697bed3beafdc47cba1",
        "ia64_insn_may_modify_psr_ic": "cc19e0571fa0247f693c5123aa07d425cda599abc0aa18cdeafb69317caa5565",
    }
    for name, digest in classifiers.items():
        fixed(translator, name, digest)
    recognize(translator, "ia64_insn_may_modify_psr_ri", "return insn->opcode == IA64_OP_MOV_GRPSR;")
    recognize(translator, "ia64_gen_raise_exception", """
        tcg_gen_movi_i64(cpu_ip, fault_ip);
        gen_helper_raise_exception(
            tcg_env, tcg_constant_i32(exception), tcg_constant_i64(fault_ip),
            tcg_constant_i64(fault_imm), tcg_constant_i32(fault_slot));
    """)
    suppression = re.search(r"#define IA64_PSR_FAULT_SUPPRESS_MASK\s*\\\s*\(([^)]+)\)", header)
    integer.require(suppression is not None, "unrecognized suppression mask")
    names = suppression[1].split("|")
    shifts = [numeric_macro(header, name.strip(), r"\(1ULL << (\d+)\)") for name in names]
    mask = 0
    for shift in shifts:
        mask |= 1 << shift
    return f"""
namespace IA64Add
open IA64Slot

def registerField (raw : Word) (low : Nat) : IA64Tcg.Reg :=
  (raw.extractLsb' low 7).toFin

def emittedDecoded (raw ip : Word) (ri : Fin 3) : IA64Tcg.Slot :=
  ⟨registerField raw {branch[1]}, registerField raw {branch[3]}, registerField raw {branch[2]},
    ((cDecodedQualifier raw).extractLsb' 0 6).toFin, 0, ip, ri, raw, false, false, false, false⟩

def captureSuppression (s : IA64Slot.State) (track : Bool) : IA64Slot.State :=
  if track then (Effect.write .savedSuppression
    (.band (.input .psr) (.literal {mask}))).run s |>.1 else s

def emitFaultIP (s : IA64Slot.State) (address : Word) : IA64Slot.State :=
  ((Effect.write .ip (.literal address)).run s).1

end IA64Add
"""


def generate(root, slot_control, decoder=None, opcodes=("ADD",)):
    source = {name: scalar.source(root, name) for name in scalar.GUARDS}
    header = scalar.source(root, "cpu.h")
    translator = source[scalar.TRANSLATE]
    generator = source[scalar.GEN_INTEGER]
    affected = scalar.known_routes(source, header)
    if decoder is None:
        decoder = scalar.source(root, "decode/decode.c")
    opcode = scalar.source(root, "decode/opcode.inc")
    values, fast = integer.generate_value_proofs(generator, translator, decoder)
    nats = scalar.nat_routes(generator, scalar.OPCODES)
    fast_nats = integer.generate_fast_nat_proofs(translator, opcode)
    chosen = set(opcodes)
    integer.require("ADD" in chosen and chosen <= scalar.OPCODES.keys(),
                    "missing ADD or invalid requested scalar opcodes")
    ordinary = [definition for name, definition in
                zip(scalar.OPCODES, scalar.ordinary_theorems(values, nats, affected), strict=True)
                if name in chosen]
    values = [v for v in values if not (name := re.match(
        r"theorem (?:ordinaryValue|predicatedValue)_(\w+)\s", v)) or name[1] in chosen]
    fast_nats = [v for v in fast_nats if not (name := re.match(
        r"theorem predicatedNaT_(\w+)\s", v)) or name[1] in chosen]
    integer.require("ADD" in fast, "lost branchless register ADD projection")
    state = (scalar.HERE / "Spec.lean").read_text(encoding="utf-8").replace("import Integer.Spec", "")
    return "\n\n".join([
        (integer.HERE / "Spec.lean").read_text(encoding="utf-8"),
        integer.wrap_generated(values + fast_nats), state,
        (scalar.HERE / "Execution.lean.inc").read_text(encoding="utf-8"),
        "namespace IA64Tcg\nopen IA64Integer\nlocal notation \"Word\" => BitVec 64\n"
        "set_option linter.unusedVariables false\nset_option maxHeartbeats 2000000",
        scalar.lift_preparation(translator), *ordinary,
        *scalar.fast_theorems(values, fast_nats, fast & chosen), "end IA64Tcg",
        slot_control.replace("import Std.Tactic.BVDecide\n", ""),
        lift_operands(decoder, translator, header),
        (HERE / "Add.lean.inc").read_text(encoding="utf-8"),
    ])
