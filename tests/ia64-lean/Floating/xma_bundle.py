# SPDX-License-Identifier: GPL-2.0-or-later
"""Join the F2 bundle decoder with native TCG instruction admission."""

from hashlib import sha256
from pathlib import Path
import re
import subprocess
import sys
import tempfile

from check import get_match
from merge import expect
from xma import XMA


ENTRY_PINS = {
    "ia64_tr_translate_insn": "20bcdf87f0506b2138d5b738068091c1edb0625ebab566ba3c0c32dd5380cc38",
    "ia64_gen_insn": "340706a890cd42d117ccc06055de41fd16c333d5294b2912fc5e189ad229ac6d",
    "ia64_gen_prepare_insn": "0e4c08174cef80aa042f7113ee5322250e92b3da80876b4cc48dcae3062ae703",
    "ia64_gen_predicated_integer": "53f09bc44584f64be7304ead42ece8982c0fe9a4f5413c659fcbd2fcd9d432d7",
    "ia64_gen_dispatch": "3bc4394b720eecbf04ab698867bd569d144c6b7f4d41adce9a1d07e495e2755b",
    "ia64_insn_generator": "647af414fdea11fd4290cf0aafe3264da1bc524c1be77de5f382127727e11aa6",
    "ia64_predicate_known": "9df0ecddb5214a7a463839972a3cb5ffe987ad8cfca97b13dd5f7d9cc9e10a03",
    "ia64_predicate_nullifies_insn": "1daeba7f84778ca59f7d61d05a96ee6d4c5df4ff8df33746e2c46ac49e8b6faa",
    "ia64_current_insn_cannot_execute": "61006930746a864aeaa5283e4dc9d318b6685350e980d33d375387fafe06a6b8",
    "ia64_gen_predicate_skip": "f17f0df28fe8edd2c381bb5ad09919c16dadc0b2600bd6c43e111456597aa890",
    "ia64_gen_predicate_end": "8a56c902008952a0a4f85b63798184dc69f2456295c8843a29cc842b7bdcb73f",
    "ia64_insn_must_start_group": "e6a9778cf2790e83a486dbc1b9f58121e1bd2fc7b4be80cd4a785b74b5ce1722",
    "ia64_insn_must_end_group": "2c42f939cf9a45b413f750acbf9317b2db59c706e7acb837bba590f65463c52b",
    "ia64_insn_requires_slot2": "09698a1915bb7dd5e63fa04569edf89310894e6c8a599ed65c95e9f6dad04976",
    "ia64_insn_has_illegal_register": "4efa09e06e7b784d8284540fc5dd9e18bfb7d05e31ae289f3d8cd7e220223449",
    "ia64_insn_has_reserved_ar": "77ae67d6e442b84ec6c568dd09fb882ac68faaf282276f4a167b66b3bbdeb5b6",
    "ia64_insn_has_reserved_mask_field": "f5ac762856557dedc8a216db747d9e76ba97e2fe68e499374af09731c1cb297e",
    "ia64_insn_required_integer_feature": "fb503583563814860d372f87a8a937ee3b6266c2b5f51884b642ca30f5fc7965",
    "ia64_insn_needs_16byte_atomics": "423c5dd7cd14046830cd6858252cb66d54b97f64f4da4d1e7762a99af9277303",
    "ia64_insn_writes_gr_r1": "bd78788433b765d5d182dd911fff0cc99054f8a553dff2f8ee7eb8ef0622003a",
    "ia64_insn_psr_ic_must_be_clear": "79b33c238d4dc58bd8111efe4323a95a1aaf7383249625833c1dc9017a01309d",
    "ia64_insn_is_privileged": "38dc979633c187b6b6410a89988bb76e18ee51bdafecfb983a6c27ee901a39cd",
    "ia64_gen_check_fp_pair_bank": "a232a426f3fac170472735b1a4b57e81015ce778234b4ea6ff124955d27ec9dd",
    "ia64_load_base_update_has_same_target": "5350f53841cc9db7275023cd9a6e8369b79a6acddef1b4991cd3027cbddc2402",
    "ia64_compare_has_equal_targets": "2b32a72af4bc2466047ac4ef6969a0fff9c47275e7b2ce5772a6d6e82d230cfb",
    "ia64_insn_writes_predicate_pair": "a559fb2b8d76e13292b9d2673ee72e0d135f64b3f489409c57bf0a650e812c33",
    "ia64_insn_disabled_fp_isr_flags": "bd3ec796c2d06429d5b6e11628f86db3d1f1482f57689f9fc19692729bb8b946",
    "ia64_gen_raise_exception": "be4f82afe4605984a398263e0b2ac956e2e511b313488836e1cbbba33a32b319",
    "ia64_insn_is_empty_hint": "0281e83828b3593d20de4957132bb2941f25479da3a7f121af9bfeee212e6838",
}


def lift_decoder(root):
    tests = Path(__file__).resolve().parent.parent
    with tempfile.TemporaryDirectory(prefix="ia64-f2-decoder-") as tmp:
        output = Path(tmp) / "Decoder.lean"
        choices = [arg for opcode, _ in XMA for arg in ("--emit-opcode", opcode)]
        subprocess.run([
            sys.executable, "FullDecode/check.py", "--source-root", str(root),
            "--emit-lean", str(output), *choices,
        ], cwd=tests, check=True)
        return output.read_text(encoding="utf-8").replace("import Std.Tactic.BVDecide\n", "")


def function_body(source, name, body):
    start = get_match(
        rf"^(?:static\s+)?\w+\s+\*?\s*{name}\s*\([^;{{}}]+\)\s*\{{",
        source, name,
    ).start()
    return body(source[start:], name)


def lift_admission(translate, cpu_header, helpers, operands, opcodes, body):
    for name, expected in ENTRY_PINS.items():
        actual = sha256(function_body(translate, name, body).encode()).hexdigest()
        expect(actual, expected, f"F2 native entry {name}")

    tags = re.findall(r"IA64_OPCODE\((\w+),", opcodes)
    for opcode, _ in XMA:
        get_match(rf"^IA64_OPCODE\({opcode},\s*FP,\s*false,\s*UNKNOWN\)", opcodes,
                  f"F2 generator table entry for {opcode}")
        if not tags.index("FADD") <= tags.index(opcode) <= tags.index("XMPY_HU"):
            raise ValueError(f"{opcode} excluded from F2 fixed-target check")
    get_match(
        re.escape("IA64OpcodePropertiesia64_opcode_properties[IA64_OP_COUNT]={"
                  "#defineIA64_OPCODE(name,generator,gr,nat)\\"
                  "[IA64_OP_##name]={IA64_GENERATOR_##generator,\\"
                  "IA64_NAT_RESULT_##nat,gr},"
                  '#include"target/ia64/decode/opcode.inc"#undefIA64_OPCODE};'),
        re.sub(r"\s+", "", translate), "opcode generator and write properties",
    )

    for view, fields in (
        ("Common", ("destination", "source1", "source2", "auxiliary1")),
        ("Floating", ("destination", "source1", "source2", "auxiliary1")),
        ("Decoded", ("r1", "r2", "r3", "p1")),
    ):
        get_match(rf"typedef struct IA64{view}Operands\s*\{{"
                  + "".join(rf"\s*uint8_t {field};" for field in fields), operands,
                  f"F2 {view} operand overlay")
    fixed = get_match(
        re.escape("if((opcode>=IA64_OP_FADD&&opcode<=IA64_OP_XMPY_HU)||"
                  "(opcode>=IA64_OP_FMOV&&opcode<=IA64_OP_FCVT_FXU)||"
                  "(opcode>=IA64_OP_SETF_D&&opcode<=IA64_OP_SETF_EXP)||"
                  "(opcode>=IA64_OP_FMERGE_S&&opcode<=IA64_OP_FPRCPA)||"
                  "(opcode>=IA64_OP_FMS&&opcode<=IA64_OP_FPNMA)){"
                  "returninsn->operands.common.destination<=") + r"(\d+);\}",
        body(translate, "ia64_insn_has_invalid_fp_target"), "F2 fixed destination check",
    )[1]
    branches = "|".join(opcode for opcode, _ in XMA)
    get_match(rf"((?:caseIA64_OP_(?:{branches}):){{4}})" + re.escape(
        "returnia64_fp_reg_set(insn->operands.common.source1)|"
        "ia64_fp_reg_set(insn->operands.common.source2)|"
        "ia64_fp_reg_set(insn->operands.common.auxiliary1);"),
        body(translate, "ia64_insn_fp_read_sets"), "F2 bank checks for all sources")
    writes = get_match(r"((?:caseIA64_OP_\w+:)+)"
                       + re.escape("returnia64_fp_reg_set(insn->operands.common.destination);"),
                       body(translate, "ia64_insn_fp_write_sets"), "F2 bank check for destination")[1]
    for opcode, _ in XMA:
        if f"caseIA64_OP_{opcode}:" not in writes:
            raise ValueError(f"{opcode} does not check its FP destination bank")
    get_match(r"DEF_HELPER_5\(raise_exception,\s*noreturn,\s*env,\s*i32,\s*i64,\s*i64,\s*i32\)",
              helpers, "XMA first exception request ABI")
    illegal, disabled = (get_match(rf"\bIA64_EXCP_{kind}\s*=\s*(\d+)\s*,", cpu_header,
                                   f"F2 {kind.lower()} exception code")[1]
                         for kind in ("ILLEGAL", "DISABLED_FP"))
    return f"""
def cXmaIllegalCode : Word := {illegal}
def cXmaDisabledCode : Word := {disabled}
def cXmaFixedDestination (reg : BitVec 7) : Bool := decide (reg ≤ {fixed})
"""
