# SPDX-License-Identifier: GPL-2.0-or-later
"""Project IA-64 CMPXCHG1/2/4/8 comparison and TCG dispatch."""

import re
from pathlib import Path

from Memory.check import MEMOP, MEMORY, descriptor_constants, widths
from Memory.source import body, compact, require
from Ordering.check import cases, function, stripped, switches
from TcgMemory.atomic import (
    ATOMIC_MACROS,
    ATOMIC_TEMPLATE,
    GEN_MEMORY,
    SIZE_BYTES,
    TRANSLATOR,
    macro_bodies,
)
from TcgMemory.primitive import LDST, check_body

ARCH_MEMORY = Path("target/ia64/arch/memory.c")
EXEC_ACCESS = Path("target/ia64/exec-access.c")
HELPER_MEMORY = Path("target/ia64/helper/helper-memory.c")

BODIES = {
    TRANSLATOR: (
        "ia64_gen_read_simple_ar", "ia64_gen_atomic_cmpxchg_i64",
    ),
    GEN_MEMORY: ("ia64_memory_plan",),
    ARCH_MEMORY: (
        "ia64_cmpxchg_compare_representable", "ia64_memory_cmpxchg",
        "ia64_lduw_data_ra", "ia64_ldl_data_ra",
    ),
    HELPER_MEMORY: ("helper_cmpxchg",),
    EXEC_ACCESS: ("ia64_exec_cmpxchg", "ia64_exec_load_data"),
    LDST: (
        "tcg_gen_nonatomic_cmpxchg_i64_int", "tcg_gen_atomic_cmpxchg_i64_int",
        "tcg_gen_nonatomic_cmpxchg_i32_int", "tcg_gen_atomic_cmpxchg_i32_int",
    ),
}
DIGESTS = {
    "qatomic_cmpxchg__nocheck": "55643f974e3dceb8990d3e914958db43f8b8469f7d7907bef84c507dbb883c0a",
    "table_cmpxchg": "3aff3683e1f6208bbd12fcf45b3c7db9b3925e041ecf3776aae7cf1d8607b31e",
    "ia64_gen_read_simple_ar": "2bf37dc38f5c1d49a47559bd46b7fdb683eef37b605aebcea3b734c1110a38a4",
    "ia64_gen_atomic_cmpxchg_i64": "e7e9c05b10f99f34b0bd07ae16b4d007722845302bcee59fdcad1bb7633dae02",
    "ia64_memory_plan": "38173a3fca6e065f65e52d53647c83df8c58f1ab7e4aada4ddb5cd050f467816",
    "ia64_cmpxchg_compare_representable": "7d161227345a40b4aa666e870f1b5258aceef46d687989c7d1579d7f551d2d9e",
    "ia64_memory_cmpxchg": "0cb0622334e81d593f2c5d2c86ab3751da0a7513fe4874487ff283e32ebc2469",
    "ia64_lduw_data_ra": "4198f405984752162ec2f17e1c0687ae02d347b189ae7d2e5b1af60c10ed7181",
    "ia64_ldl_data_ra": "e9ea0cf95c520f9c42895ef7f5d8edc1809aae0b3ffc13fb0a89fe64db3953e5",
    "helper_cmpxchg": "7c80bc6f449ad230513c9b7e79f0645d19a1d51c82d0d9600df82f4f1e92fbfc",
    "ia64_exec_cmpxchg": "55b8e97b267471a4a7101903caa5504a9d7321ad59c9d11d3430dbfe62ca32ba",
    "ia64_exec_load_data": "1883ccc0e98cb2926120ed43395ea06f83a84a07b668053b51d534ce672cd095",
    "tcg_gen_nonatomic_cmpxchg_i64_int": "30908376d09783cecf180e901dc064dfe7f8be56a4e3c2a76a9e92a9d40d5c28",
    "tcg_gen_atomic_cmpxchg_i64_int": "655544bc4100f63053ff09e5d1323f99d9cf0de5d716ee6b301310e643c3e675",
    "tcg_gen_nonatomic_cmpxchg_i32_int": "f31c263efd1fbe47a3510f515b3b24e7fcc881cc2a581bcc5e6e02e1c0e76190",
    "tcg_gen_atomic_cmpxchg_i32_int": "1bef595cef88e0c9704aaee1a5c3ac457218cc3088c10fa27c7aec77a54d5866",
    "CMPXCHG": "766dbc1f899018bb03b790a5613bd1ec9ecfd26b8ebad9359d47b7217f992323",
    "compareNative": "4c5c12199bb84871b30e3b172e02e2d26ba247f8fb71c88b849d593a660dc6ab",
    "compareReverse": "91560a77b4b03aa06b0aac6c21a0016c51d9d4b0f5339751dcabe2b3ef22707e",
}


def selected(root, ldst, mutation):
    for path, names in BODIES.items():
        source = ldst if path == LDST and ldst is not None else (root / path).read_text()
        for name in names:
            # Reify the inline unsigned comparison; check the branch target and threshold.
            value = body(source, name)
            if name == "ia64_gen_atomic_cmpxchg_i64":
                if mutation:
                    marker = "tcg_gen_brcondi_i64(TCG_COND_GTU,cmp,(1ULL<<(size*8))-1,load_only);"
                    require(value.count(marker) == 1, "CMPXCHG guard mutation anchor changed")
                    value = value.replace(marker, marker.replace("GTU", "GEU"))
                guard = re.search(r"tcg_gen_brcondi_i64\(TCG_COND_(GTU|GEU),cmp,"
                                  r"\(1ULL<<\(size\*8\)\)-1,load_only\);", value)
                require(guard is not None, "unrecognised full-CCV guard")
                yield "guard", guard[1]
                value = value.replace(guard[0], guard[0].replace(guard[1], "COMPARE"))
            yield name, value
    generator = function(stripped((root / GEN_MEMORY).read_text()), "ia64_gen_memory")
    dispatch = cases(switches(generator)[0][2])
    handlers = [compact(dispatch[f"CMPXCHG{w}"]) for w in SIZE_BYTES]
    require(all(handler == handlers[0] for handler in handlers), "CMPXCHG dispatch split")
    yield "CMPXCHG", handlers[0]
    template = (root / ATOMIC_TEMPLATE).read_text()
    helpers = re.findall(r"(?ms)^ABI_TYPE ATOMIC_NAME\(cmpxchg\)\([^;]+?\)\n\{(.*?)^\}",
                         template)
    require(len(helpers) == 2, "missing same/reverse-endian compare-exchange helpers")
    yield "compareNative", compact(helpers[0])
    yield "compareReverse", compact(helpers[1])
    qatomic = macro_bodies((root / ATOMIC_MACROS).read_text(), "qatomic_cmpxchg__nocheck")
    require(len(qatomic) == 1, "missing portable compare-exchange primitive")
    yield "qatomic_cmpxchg__nocheck", qatomic[0]
    table = re.search(r"(?ms)^static void \* const table_cmpxchg[^\n]+\{(.*?)^\};",
                      ldst if ldst is not None else (root / LDST).read_text())
    require(table is not None, "missing TCG compare-exchange helper table")
    yield "table_cmpxchg", compact(table[1])


def generate(root, ldst, mutation=False):
    source = dict(selected(root, ldst, mutation))
    guard = source.pop("guard")
    for name, value in source.items():
        check_body(value, DIGESTS[name], name)
    actual, _ = widths((root / MEMORY).read_text())
    constants = descriptor_constants((root / MEMOP).read_text())
    output = ["namespace TcgMemory", "def compareDescriptor : Width → BitVec 32"]
    for size in SIZE_BYTES:
        output += [f"  | .w{size} => {constants[actual[f'CMPXCHG{size}']]}"]
    operation = "<" if guard == "GTU" else "≤"
    output += [
        "def sourceInlineLoadOnly (w : Width) (ccv : Word) : Bool :=",
        "  match w with",
        "  | .w8 => false",
        f"  | _ => decide (((BitVec.allOnes (8 * w.bytes)).zeroExtend 64) {operation} ccv)",
        "end TcgMemory",
    ]
    return "\n".join(output)
