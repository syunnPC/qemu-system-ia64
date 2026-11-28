#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check source-projected TCG integer loads and stores on an aligned WB cell."""

import argparse
import hashlib
import json
import re
import subprocess
import sys
import tempfile
from pathlib import Path

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parent
sys.path.insert(0, str(PROJECT))
sys.path.insert(0, str(PROJECT / "Memory"))

from Memory.check import (
    CPU_HEADER,
    FUSED_BODIES,
    MEMOP,
    MEMORY,
    TRANSLATOR,
    check,
    descriptor_constants,
    translation,
    widths,
)
from Memory.source import body, compact, require
from Ordering.check import (
    cases,
    define,
    function,
    mask_value,
    one_barrier,
    stripped,
    switches,
)
from TcgMemory.atomic import ATOMIC_FAMILIES, CPU
from TcgMemory.atomic import generate as generate_atomic
from TcgMemory.compare import generate as generate_compare
from TcgMemory.compare_wide import generate as generate_compare_wide
from TcgMemory.encoded import generate as generate_encoded
from TcgMemory.encoded import without_plain_acquire, wrong_load_increment_sign
from TcgMemory.host_order import generate as generate_host_order
from TcgMemory.lookup import CPUTLB
from TcgMemory.lookup import generate as generate_lookup
from TcgMemory.primitive import LDST
from TcgMemory.primitive import generate as generate_primitive

GEN_MEMORY = Path("target/ia64/translate/gen-memory.c")
GEN_SYSTEM = Path("target/ia64/translate/gen-system.c")
MASKS = Path("include/tcg/tcg-mo.h")
OPCODES = Path("target/ia64/decode/opcode.inc")
DECODER = Path("target/ia64/decode/decode.c")
SIZES = (1, 2, 4, 8)
LOADS = tuple(f"LD{n}" for n in SIZES)
STORES = tuple(f"ST{n}" for n in SIZES)
RELEASE = tuple(f"ST{n}REL" for n in SIZES)
OPERATORS = {"add": "+", "sub": "-", "or": "||", "and": "&&"}

# Keep each control-flow projection tied to its entire normalized C body.
# The explicit variable TCG operations below are interpreted instead of pinned.
BODY_HASHES = {
    **{name: ("translator", digest)
       for name, digest in FUSED_BODIES["translate/translate.c"].items()},
    "ia64_gen_predicated_integer": ("translator",
        "53f09bc44584f64be7304ead42ece8982c0fe9a4f5413c659fcbd2fcd9d432d7"),
    "ia64_insn_nat_known": ("translator",
        "6092743aaa45bdb579a7379c06aa66dd7bbfaae5fa68844d63b1ff9025cfc530"),
    "ia64_nat_known": ("translator",
        "8f49c86bb608b848edb1db827fced9ce54130df7e9f9bb9b63d482ff6d49f908"),
    "ia64_gen_check_alignment_model": ("translator",
        "94e068ff98052d65db1e3f2dc30d4eb9979c17f33b6a7af7bf5ea9f150edb280"),
    "ia64_gen_prepare_insn": ("translator",
        "0e4c08174cef80aa042f7113ee5322250e92b3da80876b4cc48dcae3062ae703"),
    "ia64_gen_insn": ("translator",
        "340706a890cd42d117ccc06055de41fd16c333d5294b2912fc5e189ad229ac6d"),
    "ia64_gen_check_gr_in_frame": ("translator",
        "4b9ca805eec9cdc795b6bad464a631a2df1fe0d1dbb570268fb29b134db04485"),
    "ia64_gen_predicate_skip": ("translator",
        "f17f0df28fe8edd2c381bb5ad09919c16dadc0b2600bd6c43e111456597aa890"),
    "ia64_gen_predicate_end": ("translator",
        "8a56c902008952a0a4f85b63798184dc69f2456295c8843a29cc842b7bdcb73f"),
    "ia64_predicate_known": ("translator",
        "9df0ecddb5214a7a463839972a3cb5ffe987ad8cfca97b13dd5f7d9cc9e10a03"),
    "ia64_current_insn_cannot_execute": ("translator",
        "09b44e5eadf833e37ffc76200524db71dbb76526cda3de862e7b3a8a4c6966e7"),
    "ia64_load_base_update_has_same_target": ("translator",
        "5350f53841cc9db7275023cd9a6e8369b79a6acddef1b4991cd3027cbddc2402"),
    "ia64_insn_has_base_update": ("translator",
        "9bfb32479dfdc338dbc4dcdaad293841dbcbd6e2e648e2f4c7757bf5b0ce30fa"),
    "ia64_insn_writes_gr_r1": ("translator",
        "bd78788433b765d5d182dd911fff0cc99054f8a553dff2f8ee7eb8ef0622003a"),
    "ia64_insn_generator": ("translator",
        "647af414fdea11fd4290cf0aafe3264da1bc524c1be77de5f382127727e11aa6"),
    "ia64_gen_dispatch": ("translator",
        "3bc4394b720eecbf04ab698867bd569d144c6b7f4d41adce9a1d07e495e2755b"),
    "ia64_gen_gr_nat_clear": ("translator",
        "88d9a97e1c3bebefc21a1b185102f57e5f7376b32ee9959f7cb41ec249c7158f"),
    "ia64_gen_gr_nat_assign": ("translator",
        "110470c6a770ea67a4a3251735df184bd43eb4a9b29cb95c2815aa50e863bc4d"),
    "ia64_gen_gr_nat_assign_masked": ("translator",
        "0fd43dcbef5631a3f1b50a9af5f398477537ed3fe1e888cb1b4088f55a8b8025"),
    "ia64_gen_gr_nat_read": ("translator",
        "a4147dc7899d6eb6b21907930c617c4aa6cfe15418d6a2f0d45eaf918b97023c"),
    "ia64_gen_note_stacked_gr_write": ("translator",
        "0f6d9ad83a04614eff929c5223b1d63831fc23ac1bebecc3a9e2f1ea010906cb"),
    "ia64_gen_qemu_ld_i64": ("translator",
        "bd29177bd7f655bc3c2675897601b888669dcff69b03c513996d6e688cc2c1a7"),
    "ia64_gen_qemu_st_i64": ("translator",
        "f56cf58368d263a9a8a2dfbb091b44587eb332b501447195889c0b8e1dba0c9a"),
    "ia64_gen_check_nat_consumption": ("translator",
        "38757c4729da981b7c099a0fce084307b271fe00ef1ee14c53bb604dbdb49346"),
    "ia64_gen_check_alignment": ("translator",
        "5c3ebc02302d3cbde9788f4549b57563cf958d2fe92f92cbd421e2491b3dfa0d"),
    "ia64_gen_integer_load": ("generator",
        "9ba4e2405148f55972fc6d8ba52b8fe8ea1330c497b83fa676f87bc5eae27d5f"),
    "ia64_memory_plan": ("generator",
        "38173a3fca6e065f65e52d53647c83df8c58f1ab7e4aada4ddb5cd050f467816"),
    "ia64_gen_load_reg_base_update_inputs": ("generator",
        "885eedf7a3609c11fe7d3c00c876de2eb2b46e141f940d39d2ddc5213ace61cb"),
    "ia64_gen_check_nat_access": ("generator",
        "a71b30daeec2dc198883817407a62f81be36a01eee1e3f7367907c7cbd9739d4"),
}
UPDATE_HASHES = {
    "load-register": "35067b539ef308890a5c0eae8817d7ce3861dd12ef7e5ca946f5f1bcd4a059da",
    "load-immediate": "46dc8dc54b5b7905d7bca9dd76fcae5238aef1eafdc880f441830569d02ca238",
    "store": "7a710583ff163f9aa5f4e35a52d2f10bc35d011217b3a7a056998fc81ced8fe6",
}


def checked_hash(source, expected, name):
    digest = hashlib.sha256(source.encode()).hexdigest()
    require(digest == expected, f"{name} changed; review the TCG memory projection")


def lower_operator(source, arguments, *, immediate=False):
    suffix = "i" if immediate else ""
    pattern = rf"tcg_gen_(add|sub){suffix}_i64\({re.escape(arguments)}\);"
    matches = re.findall(pattern, source)
    require(len(matches) == 1, f"unrecognized arithmetic at {arguments}")
    return OPERATORS[matches[0]], re.sub(pattern, "address_update;", source)


def project_updates(generator, store):
    registers = body(generator, "ia64_gen_load_reg_base_update")
    reg_add, registers = lower_operator(registers, "new_base,addr,increment")
    expression = r"tcg_gen_(or|and)_i64\(new_nat,base_nat,increment_nat\);"
    operations = re.findall(expression, registers)
    require(len(operations) == 1, "unrecognized register postincrement NaT assignment")
    registers = re.sub(expression, "nat_update;", registers)
    immediate = body(generator, "ia64_gen_memory_plan_base_update")
    immediate_add, immediate = lower_operator(
        immediate, "cpu_gr[op->base],plan->address,op->immediate", immediate=True)
    store_add, store = lower_operator(
        store, "cpu_gr[op->base],plan.address,op->immediate", immediate=True)
    for name, source in (("load-register", registers), ("load-immediate", immediate),
                         ("store", store)):
        checked_hash(source, UPDATE_HASHES[name], name)
    return {
        "loadRegisterAdd": reg_add, "loadImmediateAdd": immediate_add,
        "storeImmediateAdd": store_add, "loadRegisterNaT": OPERATORS[operations[0]],
    }


def check_dispatch(root, translator, generator):
    table = (root / OPCODES).read_text()
    for name in (*LOADS, *STORES, *RELEASE):
        writes_gr, nat = ("true", "CLEAR") if name in LOADS else ("false", "UNKNOWN")
        entry = f"IA64_OPCODE({name},MEMORY,{writes_gr},{nat})"
        require(compact(table).count(entry) == 1, f"changed instruction properties for {name}")
    decoder = stripped((root / DECODER).read_text())
    load_predicate = function(decoder, "ia64_opcode_is_load")
    start, end, load_cases = switches(load_predicate, "opcode")[0]
    require(compact(load_predicate[:start]) == "switch(opcode)" and
            not compact(load_predicate[end + 1:]), "unsupported load classifier")
    load_table = cases(load_cases)
    for name in LOADS:
        require(compact(load_table[name]) == "returntrue;", f"{name} no longer marked as load")
    for name in (*STORES, *RELEASE):
        require(compact(load_table.get(name, load_table["default"])) == "returnfalse;",
                f"{name} now marked as load")
    fast = function(stripped(translator), "ia64_gen_predicated_integer")
    eligibility = cases(switches(fast)[0][2])
    require(not ({*LOADS, *STORES, *RELEASE} & eligibility.keys()) and
            compact(eligibility["default"]) == "returnfalse;",
            "scalar loads or stores can enter the predicated integer optimization")

    fn = function(stripped(generator), "ia64_gen_memory")
    start, end, dispatch_body = switches(fn)[0]
    require(compact(fn[:start]) ==
            "constIA64MemoryOperands*op=&insn->operands.memory;switch(insn->opcode)" and
            compact(fn[end + 1:]) == "returnIA64_GEN_CONTINUE;",
            "unsupported memory generator entry/exit")
    dispatch = cases(dispatch_body)
    for name in LOADS:
        require(compact(dispatch[name]) ==
                "ia64_gen_integer_load(ctx,insn,IA64_INTEGER_LOAD_NORMAL);break;",
                f"{name} no longer takes the ordinary load path")
    store = compact(dispatch["ST1"])
    for name in (*STORES, *RELEASE):
        require(compact(dispatch[name]) == store, f"{name} no longer takes the scalar store path")
    return store


def project_descriptors(root):
    constants = descriptor_constants((root / MEMOP).read_text())
    swap = constants["MO_BSWAP"]
    require(swap > 0 and swap & (swap - 1) == 0, "MemOp byte swap is not a bit")
    table, _ = widths((root / MEMORY).read_text())
    for name in (*LOADS, *STORES, *RELEASE):
        require(not (constants[table[name]] & constants["MO_SIGN"]),
                f"unexpected signed MemOp for {name}")
    out = [f"def swapBit : Nat := {swap.bit_length() - 1}"]
    out += ["def loadDescriptor : Width → BitVec 32"]
    for n, name in zip(SIZES, LOADS):
        out += [f"  | .w{n} => {constants[table[name]]}"]
    out += ["def storeDescriptor : Width → Bool → BitVec 32"]
    for n, st, rel in zip(SIZES, STORES, RELEASE):
        for name, release in ((st, "false"), (rel, "true")):
            out += [f"  | .w{n}, {release} => {constants[table[name]]}"]
    return out


def project_barriers(root, translator, system):
    values = {name: int(number, 16) for name, number in re.findall(
        r"(TCG_(?:MO|BAR)_\w+)\s*=\s*(0x[\da-fA-F]+)", (root / MASKS).read_text())}
    for name in ("IA64_TCG_MO_ACQUIRE", "IA64_TCG_MO_RELEASE"):
        values[name] = mask_value(define(translator, name), values)
    pairs = [("loadLoad", "TCG_MO_LD_LD"), ("storeLoad", "TCG_MO_ST_LD"),
             ("loadStore", "TCG_MO_LD_ST"), ("storeStore", "TCG_MO_ST_ST")]
    out = [f"def {name}Mask : BitVec 8 := {values[token]}" for name, token in pairs]
    for suffix in ("acquire", "release"):
        mask = one_barrier(translator, f"ia64_gen_memory_{suffix}", f"mem_{suffix}", values)
        out += [f"def {suffix}Mask : BitVec 8 := {mask}"]
    system_cases = cases(switches(function(stripped(system), "ia64_gen_system"))[0][2])
    mf = re.fullmatch(r"tcg_gen_mb\(([^()]+)\);break;", compact(system_cases["MF"]))
    require(mf is not None, "unsupported mf lowering")
    out += [f"def programFenceMask : BitVec 8 := {mask_value(mf[1], values)}"]
    return out


def generate(root, generator=None, ldst=None, cputlb=None, compare_guard_mutation=False,
             compare_half_mutation=False, system=None, cpu_source=None, translator_source=None):
    translator = (root / TRANSLATOR).read_text() if translator_source is None else translator_source
    if generator is None:
        generator = (root / GEN_MEMORY).read_text()
    for name, (source_file, digest) in BODY_HASHES.items():
        checked_hash(body(translator if source_file == "translator" else generator, name),
                     digest, name)

    operations = project_updates(generator, check_dispatch(root, translator, generator))
    source_gr = re.fullmatch(r"returnreg==0\?tcg_constant_i64\((\d+)\):cpu_gr\[reg\];",
                             body(translator, "ia64_gr_src"))
    require(source_gr is not None, "unrecognized GR source selector")
    cpu = (root / CPU_HEADER).read_text()
    stacked = re.search(r"(?m)^#define IA64_STACKED_GR_BASE\s+(\d+)$", cpu)
    require(stacked is not None, "missing first stacked general register")

    result = ["import TcgMemory.Spec", translation(root), "namespace TcgMemory"]
    result += project_descriptors(root)
    if system is None:
        system = (root / GEN_SYSTEM).read_text()
    result += project_barriers(root, translator, system)
    result += [f"def stackedBase : Nat := {stacked[1]}",
               "def sourceGR (s : Registers) (r : Reg) : Word :=",
               f"  if r = 0 then {source_gr[1]} else s.gr r"]
    for name, operator in operations.items():
        typename = "Bool" if name == "loadRegisterNaT" else "Word"
        result += [f"def {name} (a b : {typename}) : {typename} := a {operator} b"]
    result += ["end TcgMemory", generate_primitive(root, ldst)]
    result += [(HERE / "Primitive.lean.inc").read_text(), generate_lookup(root, cputlb),
               (HERE / "Implementation.lean.inc").read_text(),
               (HERE / "Compose.lean.inc").read_text(),
               (HERE / "Mixed.lean.inc").read_text(),
               (HERE / "AtomicSpec.lean.inc").read_text(), generate_atomic(root, ldst, cpu_source=cpu_source),
               (HERE / "Atomic.lean.inc").read_text(),
               (HERE / "AtomicTrace.lean.inc").read_text(),
               (HERE / "AtomicOrdering.lean.inc").read_text(),
               (HERE / "LinearAtomic.lean.inc").read_text(),
               (HERE / "LinearExamples.lean.inc").read_text(),
               (HERE / "ByteAtomic.lean.inc").read_text(),
               (HERE / "ByteAtomicExamples.lean.inc").read_text(),
               generate_compare(root, ldst, mutation=compare_guard_mutation),
               (HERE / "Compare.lean.inc").read_text(),
               generate_compare_wide(root, mutation=compare_half_mutation),
               (HERE / "CompareWide.lean.inc").read_text(),
               (HERE / "CompareWideReplay.lean.inc").read_text(),
               (HERE / "CompareWideConcurrent.lean.inc").read_text(),
               (HERE / "Program.lean.inc").read_text(),
               (HERE / "ProgramExamples.lean.inc").read_text(),
               (HERE / "DeferredAcquire.lean.inc").read_text(),
               (HERE / "DeferredAcquireExamples.lean.inc").read_text()]
    return "\n".join(result) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=PROJECT.parents[1])
    output = parser.add_mutually_exclusive_group()
    output.add_argument("--coverage", action="store_true")
    output.add_argument("--emit-lean", type=Path)
    parser.add_argument("--mutation-test", action="store_true")
    args = parser.parse_args()
    try:
        source = generate(args.source_root)
        if args.coverage:
            coverage = {
                f"tcg:{name}": "Projected scalar aligned WB memory, register and abort effects"
                for name in (*LOADS, *STORES, *RELEASE)
            }
            coverage.update({
                f"tcg:{family}{size}": "Projected aligned WB old value, update, abort and RMW phase masks"
                for family, sizes in ATOMIC_FAMILIES.items() for size in sizes
            })
            coverage.update({
                f"tcg:CMPXCHG{size}": "Projected WB full CCV, return, abort and conditional memory update"
                for size in SIZES
            })
            coverage["tcg:CMP8XCHG16"] = "Projected WB half comparison and complete 16-byte replacement"
            print(json.dumps(coverage))
            return
        encoded = generate_encoded(args.source_root, LOADS, STORES, RELEASE)
        memory_models = "\n".join(("", generate_host_order(args.source_root),
                                   (HERE / "Bypass.lean.inc").read_text()))
        subprocess.run(["lake", "build", "+TcgMemory.Spec"], cwd=PROJECT, check=True,
                       stdout=subprocess.DEVNULL)
        if args.emit_lean:
            args.emit_lean.write_text(source + encoded + memory_models, encoding="utf-8")
            return
        check(source + encoded + memory_models)
        print("PASS aligned WB load/store and semaphore effects with scoped multi-vCPU ordering",
              flush=True)
        if args.mutation_test:
            system = (args.source_root / GEN_MEMORY).read_text()
            addition = "tcg_gen_add_i64(new_base, addr, increment);"
            require(system.count(addition) == 1, "register postincrement addition not unique")
            mutant = system.replace(addition, addition.replace("gen_add", "gen_sub"))
            check(generate(args.source_root, mutant),
                  expected_diagnostic="unsolved goals")
            print("PASS Lean rejects subtracting the load's register increment")
            ldst = (args.source_root / LDST).read_text()
            bswap = "tcg_gen_bswap16_i64(val, val, flags);"
            require(ldst.count(bswap) == 1, "load byte-swap mutation anchor changed")
            changed = ldst.replace(bswap, bswap.replace("bswap16", "bswap32"))
            check(generate(args.source_root, ldst=changed),
                  expected_diagnostic="is false")
            print("PASS Lean rejects an incorrect width for the emitted load byte swap")
            helper = (args.source_root / CPUTLB).read_text()
            original = (
                "    crosspage = mmu_lookup(cpu, addr, oi, ra, MMU_DATA_STORE, &l);\n"
                "    tcg_debug_assert(!crosspage);\n\n"
                "    do_st_1(cpu, &l.page[0], val, l.mmu_idx, ra);"
            )
            require(helper.count(original) == 1, "slow store mutation anchor changed")
            lookup, check_crosspage, _, store = original.splitlines()
            changed = helper.replace(original, f"{store}\n{lookup}\n{check_crosspage}")
            check(generate(args.source_root, cputlb=changed),
                  expected_diagnostic="unsolved goals")
            print("PASS Lean rejects a slow store that touches RAM before lookup")
            atomic_source = (args.source_root / LDST).read_text()
            expected = "GEN_ATOMIC_HELPER(fetch_add, add, 0)"
            require(atomic_source.count(expected) == 1, "fetch-add mutation anchor changed")
            changed = atomic_source.replace(expected, expected.replace(", 0", ", 1"))
            check(generate(args.source_root, ldst=changed),
                  expected_diagnostic="unsolved goals")
            print("PASS Lean rejects returning the new value for serial fetch-add")
            check(generate(args.source_root, compare_guard_mutation=True),
                  expected_diagnostic="found a counterexample")
            print("PASS Lean rejects treating the maximum representable CCV as oversized")
            check(generate(args.source_root, compare_half_mutation=True),
                  expected_diagnostic="sourceHalf")
            print("PASS Lean rejects comparing the wrong half of a big-endian CMP8XCHG16")
            sample = "let loaded := sampleCAS old.inputs block.bytes"
            require(source.count(sample) == 1, "wide CAS sample mutation anchor changed")
            check(source.replace(sample, "let loaded := sampleCAS old.inputs old.inputs.cell.bytes"),
                  expected_diagnostic="sampleCAS current.inputs block.bytes")
            print("PASS Lean rejects a wide CAS that samples the initial instead of current block")
            system = (args.source_root / GEN_SYSTEM).read_text()
            full = "tcg_gen_mb(TCG_MO_ALL);"
            require(system.count(full) == 1, "mf mutation anchor changed")
            changed = system.replace(full, "tcg_gen_mb(TCG_MO_LD_LD);")
            check(generate(args.source_root, system=changed), expected_diagnostic="is false")
            print("PASS Lean rejects allowing mf to pass a pending store")
            cpu_source = (args.source_root / CPU).read_text()
            default_order = ".guest_default_memory_order = TCG_MO_ALL & ~TCG_MO_ST_LD"
            require(cpu_source.count(default_order) == 1, "CPU default-order mutation anchor changed")
            weakened = cpu_source.replace(default_order, f"{default_order} & ~TCG_MO_ST_ST")
            check(generate(args.source_root, cpu_source=weakened), expected_diagnostic="found a counterexample")
            print("PASS Lean rejects losing the emitted ordinary store/store edge")
            translator = (args.source_root / TRANSLATOR).read_text()
            acquire = "(TCG_BAR_LDAQ | TCG_MO_LD_LD | TCG_MO_LD_ST)"
            require(translator.count(acquire) == 1, "explicit acquire mutation anchor changed")
            without_explicit_ll = translator.replace(acquire, "(TCG_BAR_LDAQ | TCG_MO_LD_ST)")
            without_default_ll = cpu_source.replace(default_order, f"{default_order} & ~TCG_MO_LD_LD")
            check(generate(args.source_root, cpu_source=without_default_ll,
                           translator_source=without_explicit_ll),
                  expected_diagnostic="DeferredAcquire.litmus")
            print("PASS Lean rejects losing both sources of load/load order after acquire")
            with tempfile.TemporaryDirectory(prefix="ia64-memory-decoder-mutation-") as temp:
                mutant_root = without_plain_acquire(args.source_root, Path(temp))
                mutant = generate_encoded(mutant_root, LOADS, STORES, RELEASE)
            check(source + mutant + memory_models, expected_diagnostic="found a counterexample")
            print("PASS Lean rejects dropping the acquire flag for encoded ordinary loads")
            with tempfile.TemporaryDirectory(prefix="ia64-memory-decoder-mutation-") as temp:
                mutant_root = wrong_load_increment_sign(args.source_root, Path(temp))
                mutant = generate_encoded(mutant_root, LOADS, STORES, RELEASE)
            check(source + mutant + memory_models, expected_diagnostic="found a counterexample")
            print("PASS Lean rejects sign-extending the encoded load increment from the wrong bit")
            younger = "ordinaryByte m.initial m.committed t.pending"
            require(source.count(younger) == 1, "ordinary forwarding mutation anchor changed")
            changed = source.replace(younger, younger.replace("t.pending", "t.pending.reverse"))
            check(changed + encoded + memory_models, expected_diagnostic="pending.reverse")
            print("PASS Lean rejects sampling the oldest pending writer")
    except (ValueError, KeyError, OSError, subprocess.CalledProcessError) as error:
        parser.exit(1, f"TCG memory check failed: {error}\n")


if __name__ == "__main__":
    main()
