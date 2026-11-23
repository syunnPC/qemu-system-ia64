# SPDX-License-Identifier: GPL-2.0-or-later
"""SDM selector cases for the compiled instruction decoder."""

from dataclasses import dataclass
from itertools import product
import os
from pathlib import Path
import shlex
import subprocess

OBSERVATIONS = (
    "qp", "r1", "r2", "r3", "p1", "p2", "b1", "b2", "sf", "imm",
    "compare_unc", "reg_base_update", "imm_base_update", "mem_acquire",
    "mem_release", "check_fp", "skip", "fp_precision", "pred_update",
    "clear_p2_before_predicate",
)
REGS = {"r1": (6, 7), "r2": (13, 7), "r3": (20, 7)}
PREDS = {"p1": (6, 6), "p2": (27, 6)}
IMM21 = ((6, 20), (36, 1))
BRANCH_DISP = ((13, 20), (36, 1))
IMM8 = ((13, 7), (36, 1))


def mask(low, width):
    return ((1 << width) - 1) << low


@dataclass
class Format:
    unit: str
    opcode: str
    encoding: str
    fixed: int
    variable: int
    paired: bool
    expected: dict

    def inputs(self):
        variable = self.variable
        # A clean encoding, the two extreme patterns, every independently
        # varied field bit and its complement, and two mixed vectors.
        patterns = {0, variable, variable & 0x555555555555555555555,
                    variable & 0xAAAAAAAAAAAAAAAAAAAAA}
        for bit in range(variable.bit_length()):
            if variable & (1 << bit):
                patterns.update((1 << bit, variable ^ (1 << bit)))
        for value, template in product(sorted(patterns), (4, 5) if self.paired else (0,)):
            packed = self.fixed | value
            yield self.unit, template, packed & ((1 << 41) - 1), packed >> 41


def form(unit, opcode, major, selectors=(), *, fields=None, free=(),
         values=None, qp=True, encoding="defined", paired=False):
    fields = fields or {}
    values = values or {}
    if qp:
        fields = {"qp": (0, 6)} | fields
    expected = {name: f"field raw {low} {width}" for name, (low, width) in fields.items()}
    expected |= values
    if not qp:
        expected.setdefault("qp", "0")
    if paired:
        expected["skip"] = "1"
    fixed, occupied, variable = 0, 0, 0
    for low, width, value in ((37, 4, major),) + tuple(selectors):
        bits = mask(low, width)
        if occupied & bits or not 0 <= value < (1 << width):
            raise ValueError(f"invalid selector for {unit}/{opcode}")
        fixed |= value << low
        occupied |= bits
    for low, width in (*fields.values(), *free):
        variable |= mask(low, width)
    if variable & occupied:
        raise ValueError(f"selector overlaps an operand in {unit}/{opcode}")
    if expected.keys() - set(OBSERVATIONS):
        raise ValueError(f"unknown observation in {unit}/{opcode}")
    return Format(unit, opcode, encoding, fixed, variable, paired, expected)


def alu_forms():
    # Vol. 3 Tables 4-8 to 4-11, formats A1-A8.
    register = {
        (0, 0): "ADD", (0, 1): "ADD_ONE", (1, 0): "SUB_ONE", (1, 1): "SUB",
        (2, 0): "ADDP4", (3, 0): "AND", (3, 1): "ANDCM", (3, 2): "OR",
        (3, 3): "XOR",
    }
    immediate = {
        (9, 1): "SUB_IMM", (11, 0): "AND_IMM", (11, 1): "ANDCM_IMM",
        (11, 2): "OR_IMM", (11, 3): "XOR_IMM",
    }
    r1r3 = {"r1": REGS["r1"], "r3": REGS["r3"]}
    for unit in "IM":
        for x4, x2b in product(range(16), range(4)):
            selectors = ((33, 3, 0), (29, 4, x4), (27, 2, x2b))
            if (x4, x2b) in register:
                yield form(unit, register[x4, x2b], 8, selectors, fields=REGS,
                           free=((36, 1),))
            elif x4 in (4, 6):
                yield form(unit, "SHLADD" if x4 == 4 else "SHLADDP4", 8,
                           selectors, fields=REGS, free=((36, 1),),
                           values={"imm": str(x2b + 1)})
            elif (x4, x2b) in immediate:
                yield form(unit, immediate[x4, x2b], 8, selectors, fields=r1r3,
                           free=IMM8, values={"imm": "imm8 raw"})
            else:
                yield form(unit, "ILLEGAL", 8, selectors, free=((36, 1),),
                           encoding="reserved_if_qp")
        for x2a, opcode in [(2, "ADDS"), (3, "ADDP4_IMM")]:
            yield form(unit, opcode, 8, ((33, 1, 0), (34, 2, x2a)),
                       fields=r1r3, free=(*IMM8, (27, 6)), values={"imm": "imm14 raw"})
        yield form(unit, "ADDL", 9, fields={"r1": (6, 7), "r3": (20, 2)},
                   free=(*IMM8, (22, 5), (27, 9)), values={"imm": "imm22 raw"})

        # A6/A8 normal and unconditional compares (ta=0).
        for major, compare in [(12, "LT"), (13, "LTU"), (14, "EQ")]:
            for x2, unc in product(range(4), range(2)):
                imm_compare = x2 >= 2
                opcode = f"CMP{'4' if x2 % 2 else ''}_{compare}{'_IMM' if imm_compare else ''}"
                selectors = ((34, 2, x2), (33, 1, 0), (12, 1, unc))
                fields = PREDS | {"r3": REGS["r3"]}
                values = {"compare_unc": str(unc)}
                if imm_compare:
                    values["imm"] = "imm8 raw"
                else:
                    selectors += ((36, 1, 0),)
                    fields["r2"] = REGS["r2"]
                yield form(unit, opcode, major, selectors, fields=fields,
                           free=IMM8 if imm_compare else (), values=values)


def integer_forms():
    # Vol. 3 formats I10-I15 and I19-I29. The decoder packs lengths,
    # positions and immediate fragments into its canonical immediate field.
    def position(low, width, complemented=False):
        value = f"field raw {low} {width}"
        return f"(63 - {value})" if complemented else value

    def packed(pos, length_width, immediate=None):
        value = f"{pos} ||| ((field raw 27 {length_width} + 1) <<< 6)"
        return value if immediate is None else f"({value}) ||| (({immediate}) <<< 13)"

    yield form("I", "SHRP_IMM", 5, ((33, 3, 6),), fields=REGS,
               free=((27, 6), (36, 1)), values={"imm": "field raw 27 6"})
    r1, r2, r3 = (REGS[name] for name in REGS)
    for signed in range(2):
        yield form("I", "EXTR" if signed else "EXTRU", 5,
                   ((33, 3, 2), (13, 1, signed)), fields={"r1": r1, "r3": r3},
                   free=((14, 6), (27, 6), (36, 1)),
                   values={"imm": packed(position(14, 6), 6)})
    yield form("I", "DEPZ", 5, ((33, 3, 3), (26, 1, 0)),
               fields={"r1": r1, "r2": r2}, free=((20, 6), (27, 6), (36, 1)),
               values={"imm": packed(position(20, 6, True), 6)})
    yield form("I", "DEPZ_IMM", 5, ((33, 3, 3), (26, 1, 1)), fields={"r1": r1},
               free=(*IMM8, (20, 6), (27, 6)),
               values={"imm": packed(position(20, 6, True), 6, "imm8 raw &&& 255")})
    yield form("I", "DEP_IMM", 5, ((33, 3, 7), (13, 1, 0)),
               fields={"r1": r1, "r3": r3}, free=((14, 6), (27, 6), (36, 1)),
               values={"imm": packed(position(14, 6, True), 6, "field raw 36 1")})
    yield form("I", "DEP", 4, fields=REGS, free=((27, 10),),
               values={"imm": packed(position(31, 6, True), 4)})
    yield form("I", "ILLEGAL", 5, ((33, 3, 7), (13, 1, 1)),
               encoding="constant_zero_violation")
    yield form("I", "CHK_S", 0, ((33, 3, 1),), fields={"r2": r2},
               free=((6, 7), (20, 13), (36, 1)), values={"imm": "checkDisplacement raw"})
    for x6, opcode in [(16, "ZXT1"), (17, "ZXT2"), (18, "ZXT4"),
                       (20, "SXT1"), (21, "SXT2"), (22, "SXT4")]:
        yield form("I", opcode, 0, ((33, 3, 0), (27, 6, x6)),
                   fields={"r1": r1, "r3": r3})
    yield form("I", "MOV_GRPR", 0, ((33, 3, 3),), fields={"r1": r2},
               free=((6, 7), (24, 8), (36, 1)), values={"imm": "mask17 raw"})
    yield form("I", "MOV_PR_ROT_IMM", 0, ((33, 3, 2),), free=((6, 27), (36, 1)),
               values={"imm": "imm44 raw"})


def memory_forms():
    # SDM rev. 2.3 Vol. 3 Tables 4-30 to 4-33 and 4-74.
    load_suffix = {0: "", 1: "S", 2: "A", 3: "SA", 4: "", 5: "",
                   8: "C_CLR", 9: "C_NC", 10: "C_CLR"}
    r1, r2, r3 = (REGS[name] for name in REGS)
    for row, column in product(range(16), range(4)):
        size = 1 << column
        if row in load_suffix:
            opcode = f"LD{size}{load_suffix[row]}"
        elif (row, column) == (6, 3):
            opcode = "LD8FILL"
        elif row in (12, 13):
            opcode = f"ST{size}{'REL' if row == 13 else ''}"
        elif (row, column) == (14, 3):
            opcode = "ST8SPILL"
        else:
            opcode = "ILLEGAL"

        load = opcode.startswith("LD")
        for major, m in [(4, 0), (4, 1), (5, 0)]:
            selectors = ((30, 6, 4 * row + column),)
            if major == 4:
                selectors += ((36, 1, m), (27, 1, 0))
            if opcode == "ILLEGAL" or (m and not load):
                yield form("M", "ILLEGAL", major, selectors, free=((28, 2),),
                           encoding="reserved_if_qp")
                continue
            fields = ({"r1": r1} if load else {"r2": r2}) | {"r3": r3}
            values = {"mem_acquire": str(int(row in (5, 10))),
                      "mem_release": str(int(row == 13)),
                      "reg_base_update": str(m), "imm_base_update": str(int(major == 5))}
            free = ((28, 2),)
            if m:
                fields["r2"] = r2
            if major == 5:
                free += ((13 if load else 6, 7), (27, 1), (36, 1))
                values["imm"] = f"{'load' if load else 'store'}Inc raw"
            yield form("M", opcode, major, selectors, fields=fields, free=free, values=values)

    for column in range(4):
        size = 1 << column
        for row in (0, 1):
            yield form("M", f"CMPXCHG{size}", 4,
                       ((36, 1, 0), (30, 6, 4 * row + column), (27, 1, 1)),
                       fields=REGS, free=((28, 2),),
                       values={"mem_acquire": str(1 - row), "mem_release": str(row)})
        yield form("M", f"XCHG{size}", 4, ((36, 1, 0), (30, 6, 8 + column), (27, 1, 1)),
                   fields=REGS, free=((28, 2),))
    for row, column in product((4, 5), (2, 3)):
        yield form("M", f"FETCHADD{1 << column}", 4,
                   ((36, 1, 0), (30, 6, 4 * row + column), (27, 1, 1)),
                   fields={"r1": r1, "r3": r3}, free=((13, 3), (28, 2)),
                   values={"imm": "fetchaddInc raw", "mem_acquire": str(int(row == 4)),
                           "mem_release": str(int(row == 5))})


def branch_forms():
    # SDM Vol. 3 formats B1-B9 and Tables 4-46 to 4-53.
    for btype, opcode in [(0, "BR_COND"), (2, "BR_WEXIT"), (3, "BR_WTOP"),
                          (5, "BR_CLOOP"), (6, "BR_CEXIT"), (7, "BR_CTOP")]:
        yield form("B", opcode, 4, ((6, 3, btype),), qp=btype < 5,
                   free=(*BRANCH_DISP, (12, 1), (33, 3)),
                   values={"imm": "branchDisplacement raw"})
    yield form("B", "BR_CALL", 5, fields={"b1": (6, 3)},
               free=(*BRANCH_DISP, (12, 1), (33, 3)), values={"imm": "branchDisplacement raw"})
    yield form("B", "BR_CALL_INDIRECT", 1, ((32, 1, 1),),
               fields={"b1": (6, 3), "b2": (13, 3)}, free=((12, 1), (33, 3)))
    for x6, btype, opcode in [(32, 0, "BR_INDIRECT"), (32, 1, "BR_IA"), (33, 4, "BR_RET")]:
        yield form("B", opcode, 0, ((27, 6, x6), (6, 3, btype)), qp=btype != 1,
                   fields={"b2": (13, 3)}, free=((12, 1), (33, 3)))
    for major in (3, 6):
        yield form("B", "NOP", major, qp=False, free=((0, 37),), encoding="ignored")
    for x6, opcode in [(2, "COVER"), (4, "CLRRRB"), (5, "CLRRRB_PR"), (8, "RFI"),
                       (12, "BSW0"), (13, "BSW1"), (16, "EPC")]:
        yield form("B", opcode, 0, ((27, 6, x6),), qp=False)


def floating_forms():
    # SDM Vol. 3 Tables 4-58 to 4-64, formats F1-F14.
    fp = {"sf": (34, 2)}
    for major, names in [(8, ("FMA", "FMA")), (9, ("FMA", "FPMA")),
                         (10, ("FMS", "FMS")), (11, ("FMS", "FPMS")),
                         (12, ("FNMA", "FNMA")), (13, ("FNMA", "FPNMA"))]:
        for x, opcode in enumerate(names):
            # f2 and f4 nontrivial distinguish fused forms from aliases
            # using the architectural constants f0 and f1.
            precision = 2 if major & 1 else x
            yield form("F", opcode, major, ((36, 1, x), (19, 1, 1), (33, 1, 1)),
                       fields=fp | {"r1": (6, 7), "r3": (20, 7)},
                       free=((13, 6), (27, 6)),
                       values={"r2": "field raw 13 7", "p1": "field raw 27 7",
                               "fp_precision": str(precision)})
    yield form("F", "FCMP", 4, fields=fp | PREDS | {"r2": (13, 7), "r3": (20, 7)},
               free=((12, 1), (33, 1), (36, 1)),
               values={"compare_unc": "field raw 12 1",
                       "imm": "(field raw 33 1 <<< 1) ||| field raw 36 1"})
    yield form("F", "FCLASS", 5, fields=PREDS | {"r2": (13, 7)},
               free=((12, 1), (20, 7), (33, 2)),
               values={"compare_unc": "field raw 12 1",
                       "imm": "(field raw 20 7 <<< 2) ||| field raw 33 2"})
    for major, q, opcode in [(0, 0, "FRCPA"), (0, 1, "FRSQRTA"),
                              (1, 0, "FPRCPA"), (1, 1, "FPRSQRTA")]:
        fields = fp | {"r1": (6, 7), "r3": (20, 7), "p2": (27, 6)}
        if q == 0:
            fields["r2"] = (13, 7)
        yield form("F", opcode, major, ((33, 1, 1), (36, 1, q)), fields=fields,
                   values={"clear_p2_before_predicate": "1"})
    for x6, opcode in [(16, "FMERGE_S"), (17, "FMERGE"), (18, "FMERGE_SE"),
                       (20, "FMIN"), (21, "FMAX"), (22, "FAMIN"), (23, "FAMAX"),
                       (40, "FPACK"), (44, "FAND"), (45, "FANDCM"), (46, "FOR"),
                       (47, "FXOR")]:
        yield form("F", opcode, 0, ((33, 1, 0), (27, 6, x6)), fields=REGS)
    yield form("F", "FCHKF", 0, ((33, 1, 0), (27, 6, 8)), fields=fp,
               free=IMM21, values={"imm": "fpCheckDisplacement raw"})


def misc_forms():
    # SDM Vol. 3 formats I19, M37, B9, and F15, and rev. 2.3 X1-X5.
    for unit, xfields in [("I", ((33, 3, 0), (27, 6, 0))),
                          ("M", ((33, 3, 0), (27, 6, 0))),
                          ("B", ((27, 6, 0),)),
                          ("F", ((33, 1, 0), (27, 6, 0)))]:
        # The break instruction description specifies zero IIM for B9.
        yield form(unit, "BREAK", 0, xfields, free=IMM21,
                   values={"imm": "0" if unit == "B" else "imm21 raw"})
        nop_fields = xfields if unit == "B" else xfields[:-1] + ((27, 6, 1),)
        yield form(unit, "NOP", 2 if unit == "B" else 0, nop_fields, free=IMM21,
                   values={"imm": "imm21 raw"})
    for bits, opcode, immediate in [(0, "BREAK", "imm21 raw"),
                                    (2, "NOP", "imm62 left raw"),
                                    (3, "HINT_X", "imm62 left raw")]:
        yield form("L", opcode, 0, ((26, 7, bits), (33, 3, 0)),
                   free=(*IMM21, (41, 41)), values={"imm": immediate}, paired=True)
    yield form("L", "MOVL", 6, ((20, 1, 0),), fields={"r1": (6, 7)},
               free=((13, 7), (21, 16), (41, 41)),
               values={"imm": "imm64 left raw"}, paired=True)
    yield form("L", "BRL_COND", 12, ((6, 3, 0),),
               free=(*BRANCH_DISP, (12, 1), (33, 3), (41, 41)),
               values={"imm": "longDisplacement left raw"}, paired=True)
    yield form("L", "BRL_CALL", 13, fields={"b1": (6, 3)},
               free=(*BRANCH_DISP, (12, 1), (33, 3), (41, 41)),
               values={"imm": "longDisplacement left raw"}, paired=True)


def check_slots(root, work, run_lean):
    executable = work / "observe"
    here = Path(__file__).resolve().parent
    cc = shlex.split(os.environ.get("CC", "cc"))
    subprocess.run(
        [*cc, "-std=c11", "-O2", "-I", str(root), str(here / "observe.c"),
         str(root / "target/ia64/decode/decode.c"), "-o", str(executable)],
        check=True,
    )
    tests = []
    for family in (alu_forms, integer_forms, memory_forms, branch_forms,
                   floating_forms, misc_forms):
        for fmt in family():
            tests.extend((fmt, inputs) for inputs in fmt.inputs())
    packet = "".join(f"{unit} {template:x} {raw:x} {left:x}\n"
                     for _, (unit, template, raw, left) in tests)
    outputs = subprocess.run([str(executable)], input=packet, text=True, check=True,
                             capture_output=True).stdout.splitlines()
    if len(outputs) != len(tests):
        raise ValueError("compiled decoder returned an incomplete observation stream")

    # Lean computes each expected operand and compares it with the value
    # from the compiled decoder. Keep input vectors out of the Lean source.
    lines = ["import Decode.Spec", "open IA64.Decode",
             "set_option linter.unusedVariables false"]
    expectations = {}
    vectors = []
    for (fmt, (unit, template, raw, left)), output in zip(tests, outputs, strict=True):
        opcode, encoding, *observed = output.split()
        where = f"{unit} raw={raw:#013x} L={left:#013x} template={template:02x}"
        if (opcode, encoding) != (fmt.opcode, fmt.encoding):
            raise ValueError(f"{where}: expected {fmt.opcode}/{fmt.encoding}, "
                             f"got {opcode}/{encoding}")
        actual = dict(zip(OBSERVATIONS, map(int, observed), strict=True))
        fields = tuple(fmt.expected.values())
        if fields not in expectations:
            idx = len(expectations)
            expectations[fields] = idx
            lines.append(f"def expected{idx} (raw left : Word) : Array Word := "
                         f"#[{', '.join(fields)}]")
        observed = " ".join(str(actual[field]) for field in fmt.expected)
        vectors.append(f"{expectations[fields]} {raw} {left} {observed}")
    functions = ", ".join(f"expected{idx}" for idx in expectations.values())
    lines.append(f"def checks : Array (Word → Word → Array Word) := #[{functions}]")
    lines.append("def main : List String → IO UInt32 := checkRows checks")
    proof = work / "DecoderCases.lean"
    proof.write_text("\n".join(lines) + "\n")
    data = work / "cases.txt"
    data.write_text("\n".join(vectors) + "\n")
    run_lean(work, proof, "--run", script_args=(str(data),))
    count = len({fmt.opcode for fmt, _ in tests})
    print(f"IA-64 compiled decoder verified on {len(tests)} encodings ({count} opcode tags)")
