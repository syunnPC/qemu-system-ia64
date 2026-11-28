# SPDX-License-Identifier: GPL-2.0-or-later

import re
from pathlib import Path

from Memory.source import compact, require
from SlotControl.bundle import lift_decoder

HERE = Path(__file__).resolve().parent
OPERANDS = Path("target/ia64/decode/operand.h")


def check_operand_view(root):
    source = compact((root / OPERANDS).read_text())
    suffix = "auxiliary1 auxiliary2 branch1 branch2 status_field precision"
    structures = (
        ("IA64DecodedOperands", "r1 r2 r3 p1 p2 b1 b2 sf fp_precision", "imm"),
        ("IA64MemoryOperands", f"destination source base {suffix}", "immediate"),
        ("IA64CommonOperands", f"destination source1 source2 {suffix}", "immediate"),
    )
    for name, fields, immediate in structures:
        expected = "".join(f"uint8_t{field};" for field in fields.split())
        expected += f"int64_t{immediate};"
        require(f"typedefstruct{name}{{{expected}}}{name};" in source,
                f"unsupported operand layout for {name}")
    union = re.search(r"typedefunionIA64Operands\{([^{}]+)\}IA64Operands;", source)
    require(union is not None and "IA64DecodedOperandsdecoder;" in union[1] and
            "IA64MemoryOperandsmemory;" in union[1] and "IA64CommonOperandscommon;" in union[1],
            "unsupported memory operand view")


def generate(root, loads, stores, releases):
    check_operand_view(root)
    columns = tuple(zip(loads, stores, releases, strict=True))
    names = tuple(name for column in columns for name in column)
    output = [lift_decoder(root, names), "namespace TcgMemory.Encoded",
              "open FullDecodeSpec FullDecodeImplementation", "",
              "inductive Tag where"]
    output += ["  | " + " | ".join(name.lower() for name in names)]
    output += ["", "def tags : List Tag := [" + ", ".join(f".{n.lower()}" for n in names) + "]",
               "", "def decode : Tag → Word → Word → Word → BitVec 2 → Observation"]
    output += [f"  | .{name.lower()} => bundle{name}" for name in names]
    output += ["", "def cForm : Tag → Form"]
    for row, (load, store, release) in enumerate(columns):
        output += [f"  | .{load.lower()} => .load {row} (some {16 + row}) (some {20 + row})",
                   f"  | .{store.lower()} => .store {48 + row} false",
                   f"  | .{release.lower()} => .store {52 + row} true"]
    output += ["", "theorem decodedRecord (tag : Tag) :",
               "    BundleContract reservedUnit mUnit iUnit bUnit fUnit lUnit xUnit",
               "      (cForm tag) (decode tag) := by", "  cases tag"]
    output += [f"  case {name.lower()} => exact {name}_complete" for name in names]
    output += ["", (HERE / "Encoded.lean.inc").read_text()]
    return "\n".join(output)


def decoder_mutation(root, work, anchor, replacement):
    original = root / "target/ia64"
    target = work / "target/ia64"
    decode = target / "decode"
    decode.mkdir(parents=True)
    for entry in original.iterdir():
        if entry.name != "decode":
            (target / entry.name).symlink_to(entry.resolve(), target_is_directory=entry.is_dir())
    for entry in (original / "decode").iterdir():
        if entry.name != "decode.c":
            (decode / entry.name).symlink_to(entry.resolve())
            continue
        source = entry.read_text()
        require(source.count(anchor) == 1, "decoder mutation anchor changed")
        (decode / entry.name).write_text(source.replace(anchor, replacement))
    return work


def without_plain_acquire(root, work):
    return decoder_mutation(root, work, "(x6a >= 0x14 && x6a <= 0x17) || ", "")


def wrong_load_increment_sign(root, work):
    load = ("insn.operands.decoder.imm = ia64_sign_extend(imm9, 9);\n"
            "            insn.imm_base_update = true;\n"
            "            insn.mem_acquire =")
    return decoder_mutation(root, work, load, load.replace("imm9, 9", "imm9, 8"))
