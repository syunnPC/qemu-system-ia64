# SPDX-License-Identifier: GPL-2.0-or-later
"""Project the native XCHG/FETCHADD TCG emission and portable atomic helpers."""

import re
from pathlib import Path

from Memory.check import MEMOP, MEMORY, descriptor_constants, widths
from Memory.source import body, compact, require
from Ordering.check import (
    cases,
    check_optimizer,
    conventional_masks,
    function,
    mask_value,
    stripped,
    switches,
)
from TcgMemory.primitive import LDST, check_body

GEN_MEMORY = Path("target/ia64/translate/gen-memory.c")
TRANSLATOR = Path("target/ia64/translate/translate.c")
ATOMIC_TEMPLATE = Path("accel/tcg/atomic_template.h")
ATOMIC_MACROS = Path("include/qemu/atomic.h")
CPUTLB = Path("accel/tcg/cputlb.c")
CPU_EXEC = Path("accel/tcg/cpu-exec.c")
CPU = Path("target/ia64/cpu.c")
TCG_OP = Path("tcg/tcg-op.c")
OPTIMIZER = Path("tcg/optimize.c")
MASKS = Path("include/tcg/tcg-mo.h")
MMU = Path("target/ia64/arch/mmu.c")
MMU_HELPERS = Path("target/ia64/helper/helper-mmu.c")
DECODE = Path("target/ia64/decode/decode.c")

SOURCE_BODIES = {
    TCG_OP: {"tcg_gen_mb": "51501112619db7f3bb98c75582e11b566307780d307257c8a725c5df64574f81"},
    TRANSLATOR: {
        "ia64_gen_atomic_xchg_i64": "618eb0d0a0e91f0bc3066343fc32be2869a39ecaad2957657b3b4ea27449ef03",
        "ia64_gen_atomic_fetch_add_i64": "cd5a9253b0e3eebe57107f0613a0ea72592a2bdfcff253e4866d8c9d3a4971fc",
        "ia64_gen_check_alignment_access": "48275bf04300033c6d3fed26d99c4060874285d523934753e326c041617f8599",
    },
    GEN_MEMORY: {
        "ia64_gen_check_nat_semaphore": "b417a1647dfed50b777fe9c9c1f45cc3c675c48b53a064dfadc3fc8d67f701f0",
    },
    MMU_HELPERS: {
        "helper_check_semaphore_access": "ca522946f414fa309ae116d89c21d08e7c754409d58a934f4a22c898766ed072",
    },
    MMU: {
        "ia64_mmu_check_semaphore_access": "4038bf63ada08f3dae6176972753bd6bc98370840c5dee4125945a7fb4979e5c",
    },
    CPU_EXEC: {
        "cpu_exec_step_atomic": "fa2f36f4bf9b61cd3499bda171ed762819dce2fec30d9acf4b59240b56e4bd8b",
    },
    CPUTLB: {
        "atomic_mmu_lookup": "c27e94341dfbf2257ccb5e73cd960a1365f4131d7b04278837dfbeebe7c3ab23",
    },
    LDST: {
        "do_nonatomic_op_i64": "37e812b823dccdeeb3486afae542890a4f065834406ac245102ac5a41241f4d6",
        "do_atomic_op_i32": "23111e9e64fbe3454af89cfe245ff34126df07a19691d8f6fb68e5a56a6a1b43",
        "do_atomic_op_i64": "81ea92ce6a6a4a97e809651e4aa324251c6bcd5d5755dfe8b1d23f1087139743",
        "tcg_gen_mov2_i64": "3a2293c184509481299d850c29bb691ba623a1f544515b999d9091d88a044f21",
    },
    DECODE: {
        "ia64_decode_insn": "c1e8a756a3f7272a42e461d86d0c8619275945ae3ff2906b1dbd8e5a3b8173c7",
        "ia64_fetchadd_x6a_is_acquire": "44c3f2dd6c28a9d8be7e3babf22e2daae99954254a377334fe769ab7ab168a87",
        "ia64_fetchadd_x6a_is_release": "e0adf40614fab19422903b0b90d73413b2cb1212b13ceed334e5dfbcd92df475",
    },
}
ATOMIC_BODIES = {
    "XCHG1": "b7580aaa7623e092fb423696cf888d71113ea70ce43cded20cc104ad3112c032",
    "FETCHADD4": "98593cdb660fd717f325ee8a967f6212abc446360d619a647d1de27dd89b7ccc",
    "exchangeNative": "348a022b282f947f7079c3c53db8ce366658294c61571c9547702ce0b8a024fd",
    "exchangeReverse": "614eaa5f23d3bd86db1f4262e768a287965c68869ec22b004a0a7d8672392866",
    "fetchNative": "897fe969c0adad3e272470c3755eae26e9c8b864fde806e098c0acdaa8c15f23",
    "fetchReverse": "b49fb5337a82d15de92e51c2e6a5236adc1550f35b0aafc00917af223a16a374",
    "GEN_ATOMIC_HELPER": "45a97c9cb0d1628d392d5f8479eb866d6edc38ce9585b35bd24ab0c3d5a3e7a3",
    "GEN_ATOMIC_HELPER128": "1ad79d6d4b1bf2831f888ca9c0199a5f645d55e4cd6f0005ed058f8749c41c20",
}
SIZE_BYTES = (1, 2, 4, 8)
ATOMIC_FAMILIES = {"XCHG": SIZE_BYTES, "FETCHADD": (4, 8)}


def macro_bodies(source, name):
    return [compact(match.replace("\\\n", "")) for match in re.findall(
        rf"(?m)^#define {name}\([^\n]+\)(?:[^\n]*\\\n)+[^\n]+", source
    )]


def ordered_cases(source):
    function_body = function(stripped(source), "ia64_gen_memory")
    dispatch = cases(switches(function_body)[0][2])
    for family, indices in ATOMIC_FAMILIES.items():
        key = f"{family}{indices[0]}"
        selected = compact(dispatch[key])
        for size in indices:
            require(compact(dispatch[f"{family}{size}"]) == selected,
                    f"changed {family} generator grouping")
        check_body(selected, ATOMIC_BODIES[key], key)


def serial_return(source, family):
    operation = "mov2" if family == "xchg" else "add"
    suffix = "128" if family == "xchg" else ""
    rows = re.findall(rf"(?m)^GEN_ATOMIC_HELPER{suffix}\({family}, {operation}, ([01])\)$",
                      source)
    require(len(rows) == 1, f"missing {family} serial result selection")
    return "false" if rows[0] == "1" else "true"


def atomic_templates(source):
    exchange = re.findall(r"(?ms)^ABI_TYPE ATOMIC_NAME\(xchg\)\([^;]+?\)\n\{(.*?)^\}",
                          source)
    require(len(exchange) == 2, "missing native and reverse-endian exchange helpers")
    for key, value in zip(("exchangeNative", "exchangeReverse"), exchange):
        check_body(compact(value), ATOMIC_BODIES[key], key)
    native = macro_bodies(source, "GEN_ATOMIC_HELPER")
    reverse = macro_bodies(source, "GEN_ATOMIC_HELPER_FN")
    require(len(native) == 2 and len(reverse) == 2, "unexpected atomic helper templates")
    check_body(native[0], ATOMIC_BODIES["fetchNative"], "native fetch-add helper")
    check_body(reverse[1], ATOMIC_BODIES["fetchReverse"], "reverse-endian CAS loop")
    definitions = compact(source)
    add = re.search(r"#defineADD\(X,Y\)\(X([+-])Y\)", definitions)
    require(add is not None and "GEN_ATOMIC_HELPER(fetch_add)" in definitions and
            "GEN_ATOMIC_HELPER_FN(fetch_add,ADD,DATA_TYPE,old)" in definitions,
            "unexpected fetch-add operation or return")
    for size, label in zip(SIZE_BYTES, "bwlq"):
        dtype = f"#defineSUFFIX{label}#defineDATA_TYPEuint{8 * size}_t"
        require(f"#elifDATA_SIZE=={size}{dtype}" in definitions,
                f"atomic helper data width changed for {size} bytes")
        if size != 1:
            require(f"#defineSDATA_TYPEint{8 * size}_t#defineBSWAPbswap{8 * size}" in definitions,
                    f"atomic helper byte swap changed for {size} bytes")
    require("#ifDATA_SIZE==1#defineEND#elifHOST_BIG_ENDIAN#defineEND_be"
            "#else#defineEND_le#endif" in definitions and
            "#ifHOST_BIG_ENDIAN#defineEND_le#else#defineEND_be#endif" in definitions,
            "atomic helper endian suffixes changed")
    return add[1]


def defaults(root, ldst, cpu_source=None):
    masks = {name: int(value, 16) for name, value in re.findall(
        r"(TCG_(?:MO|BAR)_\w+)\s*=\s*(0x[\da-fA-F]+)", (root / MASKS).read_text())}
    if cpu_source is None:
        cpu_source = (root / CPU).read_text()
    match = re.search(r"\.guest_default_memory_order\s*=\s*([^,]+)", cpu_source)
    require(match is not None, "missing IA-64 guest order default")
    requests = conventional_masks(ldst, masks)
    check_optimizer(stripped((root / OPTIMIZER).read_text()))
    return {
        "atomicGuestDefault": mask_value(match[1], masks),
        "atomicLoadRequest": requests["ld"],
        "atomicStoreRequest": requests["st"],
        "atomicReversePrefixMask": masks["TCG_MO_ALL"],
    }


def generate(root, ldst_source=None, *, cpu_source=None):
    for path, functions in SOURCE_BODIES.items():
        source = (root / path).read_text()
        for name, digest in functions.items():
            check_body(body(source, name), digest, name)
    ordered_cases((root / GEN_MEMORY).read_text())
    ldst = (root / LDST).read_text() if ldst_source is None else ldst_source
    for macro in ("GEN_ATOMIC_HELPER", "GEN_ATOMIC_HELPER128"):
        definitions = macro_bodies(ldst, macro)
        require(len(definitions) == 1, f"missing TCG {macro} helper dispatch")
        check_body(definitions[0], ATOMIC_BODIES[macro], macro)
    add = atomic_templates((root / ATOMIC_TEMPLATE).read_text())
    macros = compact((root / ATOMIC_MACROS).read_text().replace("\\\n", ""))
    mode = r"(__ATOMIC_[A-Z_]+)"
    orders = {}
    for key, expression in (
        ("exchange", rf"__atomic_exchange_n\(ptr,\(i\),{mode}\)"),
        ("fetchAdd", rf"#defineqatomic_fetch_add\(ptr,n\)__atomic_fetch_add\(ptr,n,{mode}\)"),
        ("compareExchange", rf"__atomic_compare_exchange_n\(ptr,&_old,new,false,{mode},{mode}\)"),
    ):
        found = re.findall(expression, macros)
        require(len(found) == 1, f"unsupported atomic memory-order contract for {key}")
        ordering = found[0] if isinstance(found[0], tuple) else (found[0],)
        orders[key] = str(all(token == "__ATOMIC_SEQ_CST" for token in ordering)).lower()

    mappings, _ = widths((root / MEMORY).read_text())
    constants = descriptor_constants((root / MEMOP).read_text())
    output = ["namespace TcgMemory"]
    for family, constructor in (("XCHG", "exchange"), ("FETCHADD", "fetchAdd")):
        output += [f"def {constructor}Descriptor : Width → BitVec 32"]
        for size in SIZE_BYTES:
            if size in (1, 2) and family == "FETCHADD":
                output += [f"  | .w{size} => 0"]
            else:
                output += [f"  | .w{size} => {constants[mappings[f'{family}{size}']]}"]
    output += [f"def reverseAdd (old increment : Word) : Word := old {add} increment",
               f"def serialExchangeReturnsOld : Bool := {serial_return(ldst, 'xchg')}",
               f"def serialAddReturnsOld : Bool := {serial_return(ldst, 'fetch_add')}"]
    output += [f"def {name}SeqCst : Bool := {sc}" for name, sc in orders.items()]
    output += [f"def {name} : BitVec 8 := {mask}" for name, mask in defaults(root, ldst, cpu_source).items()]
    output += ["end TcgMemory"]
    return "\n".join(output)
