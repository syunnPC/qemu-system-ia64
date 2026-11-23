# SPDX-License-Identifier: GPL-2.0-or-later
"""SDM opcode selectors for the complete decoder contracts."""

from dataclasses import dataclass
from itertools import product

ALU = {
    "ADD": ".a1 0 0", "ADD_ONE": ".a1 0 1",
    "SUB_ONE": ".a1 1 0", "SUB": ".a1 1 1",
    "ADDP4": ".a1 2 0",
    "AND": ".a1 3 0", "ANDCM": ".a1 3 1", "OR": ".a1 3 2", "XOR": ".a1 3 3",
    "SHLADD": ".a2 4", "SHLADDP4": ".a2 6",
    "SUB_IMM": ".a3 9 1",
    "AND_IMM": ".a3 11 0", "ANDCM_IMM": ".a3 11 1",
    "OR_IMM": ".a3 11 2", "XOR_IMM": ".a3 11 3",
    "ADDS": ".a4 2", "ADDP4_IMM": ".a4 3", "ADDL": ".a5",
}


def comparisons():
    # Tables 4-10 and 4-11. The (C,D,E) columns determine the relation
    # for normal/unc forms and the parallel update for the other rows.
    results = {}
    columns = [(12, "LT", "AND"), (13, "LTU", "OR"), (14, "EQ", "OR_ANDCM")]
    for (major, relation, update), (width, prefix), format in product(
        columns, ((0, "CMP"), (1, "CMP4")), (6, 8)
    ):
        immediate = format == 8
        x2 = width + 2 * immediate
        suffix = "_IMM" if immediate else ""
        results[f"{prefix}_{relation}{suffix}"] = f".cmp .a{format} {major} {x2} 0 none"
        for c, op in enumerate(("EQ", "NE")):
            results[f"{prefix}_{op}_{update}{suffix}"] = (
                f".cmp .a{format} {major} {x2} 1 (some {c})"
            )
    for (major, _, update), (x2, prefix), (ta, c) in product(
        columns, ((0, "CMP"), (1, "CMP4")), product((0, 1), repeat=2)
    ):
        op = ("GT", "LE", "GE", "LT")[2 * ta + c]
        results[f"{prefix}_{op}_{update}"] = f".cmp .a7 {major} {x2} {ta} (some {c})"
    return results


COMPARE = comparisons()

# Alternate assembler spellings do not have an independent raw encoding.
# These internal tags occur in the opcode enum, but need not be emitted.
COMPARE_ALIASES = (
    "CMP_NE",
    *(f"{prefix}_{rel}" for prefix in ("CMP", "CMP4")
      for rel in ("LE", "GT", "GE", "LEU", "GTU", "GEU")),
)


def multimedia():
    # Tables 4-12–4-15 and §4.2.3.1–2. The two size selector bits
    # select elements of 1, 2, or 4 bytes. Size 4 has no saturation.
    forms = {}
    for (element_bytes, size), (op, x4) in product(
        ((1, 0), (2, 1), (4, 2)), (("PADD", 0), ("PSUB", 1))
    ):
        forms[f"{op}{element_bytes}"] = f".a9 {size} {x4} 0 {0 if size == 2 else 3}"
    for element_bytes, size in ((1, 0), (2, 1)):
        forms[f"PAVG{element_bytes}"] = f".a9 {size} 2 2 3"
        forms[f"PAVGSUB{element_bytes}"] = f".a9 {size} 3 2 2"
    for (element_bytes, size), (op, x2) in product(
        ((1, 0), (2, 1), (4, 2)), (("EQ", 0), ("GT", 1))
    ):
        forms[f"PCMP{element_bytes}_{op}"] = f".a9 {size} 9 {x2} {x2}"
    forms["PSHLADD2"] = ".a10 4"
    forms["PSHRADD2"] = ".a10 6"
    return forms


MULTIMEDIA = multimedia()


def integer_major_seven():
    # Tables 4-17–4-20 and §4.3.1. Selector order is
    # (za:zb, x2a, x2b, x2c). I1 uses ct2d in the x2c field.
    forms = {
        "PMPYSH2": ".iMajorSeven .i1 1 0 3 none",
        "PMPYSH2_U": ".iMajorSeven .i1 1 0 1 none",
        "MUX1": ".iMajorSeven .i3 0 3 2 (some 2)",
        "MUX2": ".iMajorSeven .i4 1 3 2 (some 2)",
        "POPCNT": ".iMajorSeven .i9 1 1 1 (some 2)",
        "CLZ": ".iMajorSeven .i9 1 1 1 (some 3)",
    }
    registers = {
        "PMPY2_L": (1, 2, 3, 3), "PMPY2_R": (1, 2, 1, 3),
        "MPY4": (2, 0, 1, 3), "MPYSHL4": (2, 0, 3, 3),
        "PACK2_USS": (1, 2, 0, 0), "PACK2_SSS": (1, 2, 2, 0),
        "PACK4_SSS": (2, 2, 2, 0),
        "PMIN1_U": (0, 2, 1, 0), "PMAX1_U": (0, 2, 1, 1),
        "PMIN2": (1, 2, 3, 0), "PMAX2": (1, 2, 3, 1),
        "PSAD1": (0, 2, 3, 2),
    }
    for size, width in enumerate((1, 2, 4)):
        for (op, c, first), (b, suffix) in product(
            (("MIX", 2, "R"), ("UNPACK", 1, "H")), ((0, ""), (2, "L"))
        ):
            registers[f"{op}{width}_{suffix or first}"] = (size, 2, b, c)
    for name, (size, a, b, c) in registers.items():
        forms[name] = f".iMajorSeven .i2 {size} {a} {b} (some {c})"
    for size, width in ((1, "2"), (2, "4"), (3, "")):
        stem = "PSH" if width else "SH"
        for left, unsigned in ((True, False), (False, False), (False, True)):
            suffix = ("_U" if width else "U") if unsigned else ""
            name = f"{stem}{'L' if left else 'R'}{width}{suffix}"
            forms[name] = f".iShift {size} {str(left).lower()} {str(unsigned).lower()}"
    return forms


INTEGER_MAJOR_SEVEN = integer_major_seven()
INTEGER_FEATURES = {"CLZ": "CZ", "MPY4": "X2", "MPYSHL4": "X2"}

# Tables 4-21–4-23 and §§4.3.2–4.3.3, 4.3.9. The immediate scalar
# shift spellings are aliases of these bitfield forms.
INTEGER_BITFIELDS = {
    "SHRP_IMM": ".iBitfield .i10",
    "EXTR": ".iBitfield (.i11 true)",
    "EXTRU": ".iBitfield (.i11 false)",
    "DEPZ": ".iBitfield .i12",
    "DEPZ_IMM": ".iBitfield .i13",
    "DEP": ".iBitfield .i15",
}

INTEGER_TESTS = {
    f"{opcode}_{sense}": f".iPredicate .{format} {nonnull}"
    for (opcode, format), (sense, nonnull) in product(
        (("TBIT", "i16"), ("TNAT", "i17"), ("TF", "i30")),
        (("Z", "false"), ("NZ", "true")),
    )
}

INTEGER_BITFIELD_ALIASES = ("SHL_IMM", "SHR_IMM", "SHRU_IMM")
INTEGER_TEST_UPDATE_ALIASES = ("TBIT_Z_OR_ANDCM", "TBIT_NZ_OR_ANDCM", "TNAT_NZ_AND")

# Tables 4-24–4-27 and 4-74. Shared tags incorporate the M20–M21
# speculation checks and the M29–M31 application-register transfers.
INTEGER_MISC = {
    "HINT_I": ".iMisc .hint",
    "CHK_S": ".iMisc .check",
    "MOV_GRBR": ".iMisc .toBranch",
    "MOV_BRGR": ".iMisc .fromBranch",
    "MOV_GRPR": ".iMisc .toPredicates",
    "MOV_PR_ROT_IMM": ".iMisc .rotatingPredicates",
    "MOV_PRGR": ".iMisc .fromPredicates",
    "MOV_CURRENT_IP": ".iMisc .fromIp",
    "MOV_GRAR": ".iMisc .toAr",
    "MOV_IMMAR": ".iMisc .immediateAr",
    "MOV_ARGR": ".iMisc .fromAr",
    **{opcode: f".iMisc (.extend {x6})" for opcode, x6 in (
        ("ZXT1", 0x10), ("ZXT2", 0x11), ("ZXT4", 0x12),
        ("SXT1", 0x14), ("SXT2", 0x15), ("SXT4", 0x16),
    )},
}

# All F9 rows leave bits 36:34 ignored and use x=0.
FP_LOGIC = {
    "FCLASS": ".fClass",
    "FPABS": ".fpMergeAlias .absolute",
    "FPNEG": ".fpMergeAlias .negate",
    "FPNEGABS": ".fpMergeAlias .negateAbsolute",
    **{opcode: f".f9 0 {x6}" for opcode, x6 in (
        ("FMERGE_S", 0x10), ("FMERGE", 0x11), ("FMERGE_SE", 0x12),
        ("FPACK", 0x28),
        ("FAND", 0x2c), ("FANDCM", 0x2d), ("FOR", 0x2e), ("FXOR", 0x2f),
        ("FSWAP", 0x34), ("FSWAP_NL", 0x35), ("FSWAP_NR", 0x36),
        ("FMIX_LR", 0x39), ("FMIX_R", 0x3a), ("FMIX_L", 0x3b),
        ("FSXT_R", 0x3c), ("FSXT_L", 0x3d),
    )},
    **{opcode: f".f9 1 {x6}" for opcode, x6 in (
        ("FPMERGE_S", 0x10), ("FPMERGE", 0x11), ("FPMERGE_SE", 0x12),
    )},
}


def integer_memory():
    # Tables 4-30–4-33, §4.4.1 and §4.4.3. The width column occupies
    # the low two bits of x6; the completer selects the other four.
    forms = {}
    for byte_count, width in ((1, 0), (2, 1), (4, 2), (8, 3)):
        forms[f"LD{byte_count}"] = f".load {width} (some {16 + width}) (some {20 + width})"
        for completer, row in (("S", 1), ("A", 2), ("SA", 3), ("C_NC", 9)):
            forms[f"LD{byte_count}{completer}"] = f".load {4 * row + width} none none"
        forms[f"LD{byte_count}C_CLR"] = f".load {32 + width} none (some {40 + width})"
        forms[f"ST{byte_count}"] = f".store {48 + width} false"
        forms[f"ST{byte_count}REL"] = f".store {52 + width} true"
        forms[f"CMPXCHG{byte_count}"] = f".semaphore {width} (some {4 + width}) false"
        forms[f"XCHG{byte_count}"] = f".semaphore {8 + width} none false"
        if width >= 2:
            forms[f"FETCHADD{byte_count}"] = f".semaphore {16 + width} (some {20 + width}) true"
    forms["LD8FILL"] = ".load 27 none none"
    forms["ST8SPILL"] = ".store 59 false"
    return forms


INTEGER_MEMORY = integer_memory()

# Table 4-33 and §§4.4.1.1, 4.4.1.4, 4.4.3.1. The architecture
# reports these optional forms through CPUID[4].ao (Vol. 1 Table 3-8).
ATOMICS16 = {
    "LD16": ".wideLoad",
    "ST16": ".wideStore",
    "CMP8XCHG16": ".semaphore 32 (some 36) false",
}

# Tables 4-34–4-38. Operand format is selected by M and X, independently
# of the six FP load completers. Spill/fill have individual x6 values.
FP_LOADS = {
    **{name: f".fpLoad {width} false" for width, name in enumerate(("LDFE", "LDF8", "LDFS", "LDFD"))},
    "LDF_FILL": ".fpLoad 3 true",
    **{name: f".fpPair {width}" for width, name in enumerate(("LDFP8", "LDFPS", "LDFPD"), 1)},
}

@dataclass(frozen=True)
class Refutation:
    form: str
    raw: int
    table: str
    slot: int = 0
    rejected_as: str | None = None


def fp_refutations():
    forms = {}
    for name, x6 in (("STFE", 0x30), ("STF8", 0x31), ("STFS", 0x32),
                     ("STFD", 0x33), ("STF_SPILL", 0x3b)):
        # Table 4-35 reserves the entire m=1 store row if PR[qp] is true.
        raw = (6 << 37) | (1 << 36) | (x6 << 30) | (3 << 20) | (2 << 13)
        forms[name] = Refutation(f".fpStore {x6}", raw, "4-35")
    for (prefix, major, table), (kind, x6) in product(
        (("GETF", 4, "4-28"), ("SETF", 6, "4-38")),
        (("SIG", 28), ("EXP", 29), ("S", 30), ("D", 31)),
    ):
        # M18/M19 require m=0. Their m=1, x=1 positions are purple,
        # even in a legal M slot. Use register operands 1 and 2.
        raw = (major << 37) | (1 << 36) | (x6 << 30) | (1 << 27) | (2 << 13) | (1 << 6)
        forms[f"{prefix}_{kind}"] = Refutation(f".fpTransfer {major} {x6}", raw, table)
    return forms


REFUTATIONS = {
    **fp_refutations(),
    # I14 bit 13 is white, not an explicit zero. Deposit one bit at
    # position 0 into r2 and store the result in r1. PR[qp] is p0.
    "DEP_IMM": Refutation(".iBitfield .i14",
                          (5 << 37) | (3 << 34) | (1 << 33) |
                          (2 << 20) | (63 << 14) | (1 << 13) | (1 << 6),
                          "4-22", slot=1, rejected_as="CONSTANT_ZERO_VIOLATION"),
    # I29 bit 36 is white. Use a valid destination, source, and p0.
    **{opcode: Refutation(f".iMisc (.extend {x6})",
                          (1 << 36) | (x6 << 27) | (2 << 20) | (1 << 6),
                          "4-25", slot=1, rejected_as="RESERVED_IF_QP")
       for opcode, x6 in (("CZX1_L", 0x18), ("CZX2_L", 0x19),
                          ("CZX1_R", 0x1c), ("CZX2_R", 0x1d))},
}
