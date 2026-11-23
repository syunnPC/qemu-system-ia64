#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check IA-64 TCG ordering masks, host lowering and bounded visibility litmuses."""

import argparse
from hashlib import sha256
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(HERE.parent))

from Integer.check import cases, compact, function, require, stripped, switches


# The narrow compare witness classifies optional host synchronization along
# these checked paths. Other C bodies are outside the classification. A change
# inside one of these bodies requires reviewing the reachability/order model.
WITNESS_BODIES = {
    "target/ia64/translate/translate.c": {
        "ia64_gen_atomic_cmpxchg_i64": "228490d6ad16f0f9e700267eef1097a233806d062a5c2d355c6f8cd740977fdc",
    },
    "target/ia64/arch/memory.c": {
        "ia64_memory_cmpxchg": "0cb0622334e81d593f2c5d2c86ab3751da0a7513fe4874487ff283e32ebc2469",
        "ia64_cmpxchg_compare_representable": "7d161227345a40b4aa666e870f1b5258aceef46d687989c7d1579d7f551d2d9e",
        "ia64_lduw_data_ra": "4198f405984752162ec2f17e1c0687ae02d347b189ae7d2e5b1af60c10ed7181",
        "ia64_ldl_data_ra": "e9ea0cf95c520f9c42895ef7f5d8edc1809aae0b3ffc13fb0a89fe64db3953e5",
    },
    "target/ia64/exec-access.c": {
        "ia64_exec_load_data": "1883ccc0e98cb2926120ed43395ea06f83a84a07b668053b51d534ce672cd095",
    },
    "tcg/tcg-op-ldst.c": {
        "tcg_gen_qemu_ld_i64_chk": "0cea9d41dda9f9f4e9731ea0af42c8e1cb2c9bde3258f9412f4c4fb521d3efb0",
        "tcg_gen_qemu_ld_i64_int": "3b59e165c4a834a9b403d032ad5eb0044032fd0382eb2fdf17dc9e5c8cd0a08d",
    },
    "accel/tcg/cputlb.c": {
        "do_ld1_mmu": "59b297eed665c0dd4bb7320a5d33f00238eed02ea85410cad5b500173243cb2d",
        "do_ld2_mmu": "35e57ae9003702ada17d129c56cecba925948e648d24c87816e6eb68ab7872cc",
        "do_ld4_mmu": "3908ffcb8e22c23640aee1aee682eff2194af2e9cfda1538f524ee3c864147f2",
    },
}


def read(relative):
    return stripped((ROOT / relative).read_text())


def require_witness_body(source, name, digest):
    actual = sha256(compact(source).encode()).hexdigest()
    require(actual == digest, f"Changed witness path {name}")


def check_witness_bodies(overrides=None):
    overrides = overrides or {}
    for relative, bodies in WITNESS_BODIES.items():
        source = overrides.get(relative)
        if source is None:
            source = read(relative)
        for name, digest in bodies.items():
            require_witness_body(function(source, name), name, digest)


def define(source, name):
    text = re.sub(r"\\\n", " ", source)
    match = re.search(rf"^#define\s+{name}\s+([^\n]+)", text, re.M)
    require(match is not None, f"Missing define {name}")
    return match[1]


def mask_value(expr, values):
    """Restricted evaluator for the constant mask expressions in TCG."""
    expr = expr.strip()
    if expr.startswith("(") and expr.endswith(")"):
        return mask_value(expr[1:-1], values)
    if "|" in expr:
        result = 0
        for term in expr.split("|"):
            result |= mask_value(term, values)
        return result
    if "&" in expr:
        left, right = expr.split("&", maxsplit=1)
        return mask_value(left, values) & mask_value(right, values)
    if expr.startswith("~"):
        return ~mask_value(expr[1:], values)
    if expr in values:
        return values[expr]
    require(re.fullmatch(r"(?:0[xX][\da-fA-F]+|\d+)", expr),
            f"Unsupported mask expression {expr}")
    return int(expr, 0)


def one_barrier(source, name, property_name, values):
    body = compact(function(source, name))
    match = re.fullmatch(rf"if\(insn->{property_name}\)\{{tcg_gen_mb\((.*)\);\}}",
                          body)
    require(match is not None, f"Unexpected {name} emission")
    return mask_value(match[1], values)


def amd64_lowering(source, values):
    body = compact(function(source, "tcg_out_mb"))
    match = re.fullmatch(r"if\(a0&([^()]*)\)\{tcg_out8\(s,0xf0\);"
                         r"tcg_out_modrm_offset\(s,OPC_ARITH_EvIb,ARITH_OR,"
                         r"TCG_REG_ESP,0\);tcg_out8\(s,0\);\}", body)
    require(match is not None, "Unsupported AMD64 fence emitter")
    return mask_value(match[1], values)


def aarch64_lowering(source, values):
    values = values | {key: int(value, 16)
                       for key, value in re.findall(r"(DMB_(?:ISH|LD|ST))\s*=\s*(0x[\da-f]+)",
                                                    source)}
    body = compact(function(source, "tcg_out_mb"))
    table = re.fullmatch(r"staticconstuint32_tsync\[\]=\{(.*)\};"
                         r"tcg_out32\(s,sync\[a0&TCG_MO_ALL\]\);", body)
    require(table is not None, "Unsupported AArch64 DMB emitter")
    result = [None] * (values["TCG_MO_ALL"] + 1)
    for row in table[1].split(","):
        if not row:
            continue
        entry = re.fullmatch(r"\[([^\]]*)\]=(.+)", row)
        require(entry is not None, f"Unsupported AArch64 DMB table row {row}")
        key, value = entry.groups()
        code = mask_value(value, values)
        if "..." in key:
            begin, end = key.split("...")
            for index in range(mask_value(begin, values), mask_value(end, values) + 1):
                result[index] = code
        else:
            result[mask_value(key, values)] = code
    require(all(code is not None for code in result), "Incomplete DMB table")
    return result


def call_positions(body, names, depth=None):
    positions = []
    for name in names:
        matches = list(re.finditer(rf"\b{name}\s*\(", body))
        require(len(matches) == 1, f"Expected one {name} in checked source path")
        position = matches[0].start()
        if depth is not None:
            before = body[:position]
            require(before.count("{") - before.count("}") == depth,
                    f"Unexpected control-flow nesting for {name}")
        positions.append(position)
    require(positions == sorted(positions), f"Unexpected emission sequence {names}")


def conventional_masks(source, values):
    helper = compact(function(source, "tcg_gen_req_mo"))
    require(helper == "type&=tcg_ctx->guest_mo;type&=~TCG_TARGET_DEFAULT_MO;"
            "if(type){tcg_gen_mb(type|TCG_BAR_SC);}", "Unsupported TCG mask filtering")
    masks = {}
    for kind in ("ld", "st"):
        requests = set()
        for size in (32, 64, 128):
            body = function(source, f"tcg_gen_qemu_{kind}_i{size}_int")
            match = re.search(r"tcg_gen_req_mo\(([^()]+)\);", body)
            require(match is not None, f"Missing default {kind} request")
            requests.add(mask_value(match[1], values))
        require(len(requests) == 1, f"Default {kind} mask depends on access size")
        masks[kind] = requests.pop()
    return masks


def check_optimizer(source):
    require(compact(function(source, "fold_mb")) ==
            "if(ctx->prev_mb){ctx->prev_mb->args[0]|=op->args[0];"
            "tcg_op_remove(ctx->tcg,op);}else{ctx->prev_mb=op;}returntrue;",
            "Unsupported TCG barrier merge")
    for name in ("finish_bb", "fold_qemu_ld_1reg", "fold_qemu_ld_2reg", "fold_qemu_st",
                 "finish_ebb", "fold_call"):
        body = compact(function(source, name))
        require(("finish_bb(ctx);" if name == "finish_ebb" else "ctx->prev_mb=NULL;")
                in body, f"TCG barrier merge no longer stops at {name}")


def check_translator_paths(translate, memory):
    load = compact(function(memory, "ia64_gen_integer_load"))
    require(load.count("ia64_gen_memory_acquire(insn);") == 2 and
            "if(done){gen_set_label(done);}ia64_gen_memory_acquire(insn);" in load and
            "ia64_gen_gr_nat_clear(insn,op->destination);}"
            "ia64_gen_memory_acquire(insn);}ia64_gen_memory_plan_base_update" in load,
            "Unsupported ordinary/check-load acquire placement")
    dispatch = cases(switches(function(memory, "ia64_gen_memory"))[0][2])
    for size in (1, 2, 4, 8):
        require(compact(dispatch[f"LD{size}"]) ==
                "ia64_gen_integer_load(ctx,insn,IA64_INTEGER_LOAD_NORMAL);break;",
                f"Unexpected {size}-byte load dispatch")
        require(compact(dispatch[f"LD{size}C_CLR"]) ==
                "ia64_gen_integer_load(ctx,insn,IA64_INTEGER_LOAD_CHECK);break;",
                f"Unexpected {size}-byte check-load dispatch")
    call_positions(dispatch["LD16"], ["ia64_gen_qemu_ld_i128",
                                     "ia64_gen_memory_acquire"], depth=1)
    for labels, operation in (("ST1REL ST2REL ST4REL ST8REL", "ia64_gen_qemu_st_i64"),
                              ("ST16", "ia64_gen_qemu_st_i128")):
        for opcode in labels.split():
            call_positions(dispatch[opcode], ["ia64_gen_memory_release", operation],
                           depth=1)
    for labels, operation in (
        ("XCHG1 XCHG2 XCHG4 XCHG8", "ia64_gen_atomic_xchg_i64"),
        ("CMPXCHG1 CMPXCHG2 CMPXCHG4 CMPXCHG8", "ia64_gen_atomic_cmpxchg_i64"),
        ("CMP8XCHG16", "gen_helper_cmp8xchg16"),
        ("FETCHADD4 FETCHADD8", "ia64_gen_atomic_fetch_add_i64"),
    ):
        for opcode in labels.split():
            call_positions(dispatch[opcode], ["ia64_gen_memory_release", operation,
                                             "ia64_gen_memory_acquire"], depth=1)
            if opcode.startswith("CMPXCHG"):
                require_witness_body(
                    dispatch[opcode], opcode,
                    "766dbc1f899018bb03b790a5613bd1ec9ecfd26b8ebad9359d47b7217f992323")
    for op in ("xchg", "fetch_add"):
        call_positions(function(translate, f"ia64_gen_atomic_{op}_i64"),
                       [f"tcg_gen_atomic_{op}_i64"], depth=0)
    compare = compact(function(translate, "ia64_gen_atomic_cmpxchg_i64"))
    for path in ("if(ctx->restart.exit_after_bundle){"
                 "gen_helper_cmpxchg(result,tcg_env,addr,cmp,value,tcg_constant_i32(size));"
                 "return;}",
                 "if(size<8){load_only=gen_new_label();"
                 "tcg_gen_brcondi_i64(TCG_COND_GTU,cmp,(1ULL<<(size*8))-1,load_only);}",
                 "if(load_only){tcg_gen_br(done);gen_set_label(load_only);"
                 "tcg_gen_qemu_ld_i64(result,addr,mmu_idx,memop);}"
                 "gen_set_label(done);"):
        require(path in compare, "Unexpected narrow inline/helper compare path")
    require(compare.index("tcg_gen_brcondi_i64(TCG_COND_GTU,cmp,") <
            compare.index("ia64_gen_alat_write_begin(ctx);") <
            compare.index("tcg_gen_atomic_cmpxchg_i64(") <
            compare.index("gen_set_label(load_only);"),
            "Narrow load-only path no longer bypasses ALAT write scope")
    alat = compact(function(translate, "ia64_gen_alat_write_begin"))
    require(alat == "if(ctx->memory.full_alat){gen_helper_alat_write_begin(tcg_env);}",
            "Unrecognized ALAT locking guard")


def check_helper_fallback(values):
    abi = compact(function(read("target/ia64/helper/helper-memory.c"), "helper_cmpxchg"))
    require(abi == "returnia64_memory_cmpxchg(env,addr,cmp,val,size,GETPC());",
            "Unexpected compare helper ABI")
    compare = compact(function(read("target/ia64/arch/memory.c"), "ia64_memory_cmpxchg"))
    require("boolcmp_representable=ia64_cmpxchg_compare_representable(cmp,size);"
            in compare, "Unexpected excess-CCV comparison guard")
    for fallback in ("ia64_exec_load_data(env,addr,1,false,ra)",
                     "ia64_lduw_data_ra(env,addr,ra)", "ia64_ldl_data_ra(env,addr,ra)"):
        require(f"else{{old={fallback};}}" in compare,
                f"Expected excess-CCV helper load via {fallback}")
    helper = compact(read("accel/tcg/backend-ldst.h"))
    require("#definetcg_req_mo(guest_mo,type)\\((type)&guest_mo&~TCG_TARGET_DEFAULT_MO)"
            in helper and "if(tcg_req_mo(cpu->cc->tcg_ops->guest_default_memory_order,"
            "type)){\\" in helper and "smp_mb();" in helper,
            "Unexpected runtime helper ordering mask")
    tla = read("accel/tcg/cputlb.c")
    masks = set()
    for size in (1, 2, 4):
        load = function(tla, f"do_ld{size}_mmu")
        call = re.search(r"cpu_req_mo\(cpu,\s*([^()]+)\);", load)
        require(call is not None, f"Missing runtime helper {size}-byte load order")
        masks.add(mask_value(call[1], values))
    require(len(masks) == 1, "Runtime narrow load masks depend on size")
    return masks.pop()


def verify_parallel_scope():
    body = compact(function(read("tcg/tcg-op.c"), "tcg_gen_mb"))
    require("#elseboolparallel=true;#endifif(parallel){"
            "tcg_gen_op1(INDEX_op_mb,0,mb_type);}" in body,
            "System TCG no longer emits explicit barriers unconditionally")
    translate_all = read("accel/tcg/translate-all.c")
    require("tcg_ctx->guest_mo=cpu->cc->tcg_ops->guest_default_memory_order;"
            in compact(translate_all), "Unexpected source of guest memory ordering")
    qr = compact(read("include/qemu/atomic.h").replace("\\\n", " "))
    require("__atomic_exchange_n(ptr,(i),__ATOMIC_SEQ_CST)" in qr and
            "__atomic_compare_exchange_n(ptr,&_old,new,false,"
            "__ATOMIC_SEQ_CST,__ATOMIC_SEQ_CST)" in qr and
            "__atomic_fetch_add(ptr,n,__ATOMIC_SEQ_CST)" in qr,
            "Unexpected QEMU compiler atomic primitive contract")
    helpers = compact(read("accel/tcg/atomic_template.h"))
    require("qatomic_cmpxchg__nocheck(haddr,cmpv,newv)" in helpers and
            "qatomic_xchg__nocheck(haddr,val)" in helpers and
            "GEN_ATOMIC_HELPER(fetch_add)" in helpers,
            "Unexpected parallel atomic helper path")


def host_mappings(system):
    check_witness_bodies()
    translate = read("target/ia64/translate/translate.c")
    memory = read("target/ia64/translate/gen-memory.c")
    check_translator_paths(translate, memory)
    values = {name: int(number, 16) for name, number in re.findall(
        r"(TCG_(?:MO|BAR)_\w+)\s*=\s*(0x[\da-fA-F]+)", read("include/tcg/tcg-mo.h"))}
    values |= {name: mask_value(define(translate, name), values)
               for name in ("IA64_TCG_MO_ACQUIRE", "IA64_TCG_MO_RELEASE")}
    value_defs = {
        "ldLd": values["TCG_MO_LD_LD"], "stLd": values["TCG_MO_ST_LD"],
        "ldSt": values["TCG_MO_LD_ST"], "stSt": values["TCG_MO_ST_ST"],
        "allPairs": values["TCG_MO_ALL"], "sc": values["TCG_BAR_SC"],
        "guestDefault": mask_value(re.search(r"\.guest_default_memory_order\s*=\s*([^,]+)",
                                              read("target/ia64/cpu.c"))[1], values),
        "acquire": one_barrier(translate, "ia64_gen_memory_acquire",
                               "mem_acquire", values),
        "release": one_barrier(translate, "ia64_gen_memory_release",
                               "mem_release", values),
        "amd64Default": mask_value(define(read("tcg/x86_64/tcg-target-mo.h"),
                                          "TCG_TARGET_DEFAULT_MO"), values),
        "aarch64Default": mask_value(define(read("tcg/aarch64/tcg-target-mo.h"),
                                            "TCG_TARGET_DEFAULT_MO"), values),
        "amd64FenceTrigger": amd64_lowering(read("tcg/x86_64/tcg-target.c.inc"), values),
        "runtimeLoad": check_helper_fallback(values),
    }
    conventional = conventional_masks(read("tcg/tcg-op-ldst.c"), values)
    value_defs.update({"defaultLoad": conventional["ld"], "defaultStore": conventional["st"]})
    fences = cases(switches(function(system, "ia64_gen_system"))[0][2])
    for opcode, name in (("MF", "fence"), ("MF_A", "acceptanceFenceRamMask")):
        match = re.fullmatch(r"tcg_gen_mb\(([^()]+)\);break;", compact(fences[opcode]))
        require(match is not None, f"Unsupported {opcode} emission")
        value_defs[name] = mask_value(match[1], values)
    arm_table = aarch64_lowering(read("tcg/aarch64/tcg-target.c.inc"), values)
    check_optimizer(read("tcg/optimize.c"))
    verify_parallel_scope()
    return value_defs | {"armTable": arm_table}


def generate(mappings):
    return "\n".join([*(f"def {name} : Mask := {val}" for name, val in mappings.items()
                        if name != "armTable"),
                       "def armTable : Array UInt32 := #["
                       + ", ".join(map(str, mappings["armTable"])) + "]"])


THEOREMS = """
inductive Host where | amd64 | aarch64
  deriving DecidableEq

def pair : Access → Access → Mask
  | .load, .load => ldLd
  | .store, .load => stLd
  | .load, .store => ldSt
  | .store, .store => stSt

def hostDefault : Host → Mask
  | .amd64 => amd64Default
  | .aarch64 => aarch64Default

def applyHost (host : Host) (before after : Access) (mask : Mask) : Bool :=
  match host with
  | .amd64 => amd64Natural before after || (mask &&& amd64FenceTrigger) != 0
  | .aarch64 => mask != 0 &&
      aarch64InstructionOrders (armTable[(mask &&& allPairs).toNat]!) before after

def filter (host : Host) (request : Mask) : Mask :=
  let needed := request &&& guestDefault &&& ~~~(hostDefault host)
  if needed = 0 then 0 else needed ||| sc

def defaultBefore (host : Host) (after : Access) : Mask :=
  filter host (if after == .load then defaultLoad else defaultStore)

def implemented (host : Host) (before after : Access)
    (acq rel mf : Bool) : Bool :=
  applyHost host before after <|
    (defaultBefore host after) |||
    (if acq && before == .load then acquire else 0) |||
    (if rel && after == .store then release else 0) |||
    (if mf then fence else 0)

def sourceMaskCovers (request effective : Mask) : Prop :=
  (request &&& allPairs) &&& (effective &&& allPairs) = (request &&& allPairs)

theorem maskNumbers : ldLd = 1 ∧ stLd = 2 ∧ ldSt = 4 ∧ stSt = 8 ∧ allPairs = 15 := by decide

theorem amd64DefaultSound (before after : Access) :
    ((pair before after &&& amd64Default) != 0) = amd64Natural before after := by
  cases before <;> cases after <;> decide

theorem aarch64DefaultSound : aarch64Default = 0 := by decide

theorem hostBarrierSound
    (host : Host) (before after : Access) (mask : BitVec 4)
    (requested : (pair before after &&& mask.zeroExtend 8) != 0) :
    applyHost host before after (mask.zeroExtend 8) = true := by
  revert requested
  cases host <;> cases before <;> cases after <;> decide +revert

theorem guestBarrierContract :
    acquire = (ldLd ||| ldSt ||| 16) ∧
    release = (ldSt ||| stSt ||| 32) ∧ fence = allPairs := by decide

-- fold_mb combines barriers without crossing a checked guest access or call.
theorem mergingPreservesPairs (left right : Mask) :
    sourceMaskCovers left (left ||| right) ∧ sourceMaskCovers right (left ||| right) := by
  simp only [sourceMaskCovers, allPairs]
  bv_decide

-- Covers every pair of ordinary cacheable loads/stores and placement of a
-- qualified acquire, release or mf relative to that pair.
theorem ordinaryRefinement (host : Host) (before after : Access) (acq rel mf : Bool) :
    SDM.requires before after acq rel mf = true →
    implemented host before after acq rel mf = true := by
  cases host <;> cases before <;> cases after <;>
    cases acq <;> cases rel <;> cases mf <;> decide

theorem loweredMessagePassing (host : Host)
    (writeData writeFlag readFlag readData : Nat)
    (writeOrder : implemented host .store .store false true false = true →
      writeData < writeFlag)
    (readOrder : implemented host .load .load true false false = true →
      readFlag < readData)
    (readsFlag : writeFlag < readFlag)
    (readsInitialData : readData < writeData) : False := by
  have wr : implemented host .store .store false true false = true := by
    cases host <;> decide
  have rd : implemented host .load .load true false false = true := by
    cases host <;> decide
  exact messagePassing _ _ _ _ (writeOrder wr) readsFlag (readOrder rd) readsInitialData

theorem loweredFencedStoreBuffering (host : Host)
    (writeX readY writeY readX : Nat)
    (thread0 : implemented host .store .load false false true = true → writeX < readY)
    (thread1 : implemented host .store .load false false true = true → writeY < readX)
    (readYInitial : readY < writeY) (readXInitial : readX < writeX) : False := by
  have edge : implemented host .store .load false false true = true := by
    cases host <;> decide
  exact fencedStoreBuffering _ _ _ _ (thread0 edge) (thread1 edge) readYInitial readXInitial

theorem ordinaryStoreBufferingPermitted (host : Host) :
    SDM.plainStoreLoadAllowed = true ∧
    implemented host .store .load false false false = false := by
  cases host <;> decide

-- The C helper uses smp_mb iff its filtered mask is nonzero. A release
-- precedes the guest operation; the filter here concerns its plain-load arm.
def cmpxchgReleaseInlineEdge (host : Host) : Bool :=
  applyHost host .store .load (release ||| defaultBefore host .load)

def cmpxchgReleaseHelperEdge (host : Host) : Bool :=
  applyHost host .store .load release || filter host runtimeLoad != 0

theorem aarch64NarrowReleaseOrders :
    cmpxchgReleaseInlineEdge .aarch64 = true ∧
    cmpxchgReleaseHelperEdge .aarch64 = true := by decide
"""

GAP = """
theorem narrowReleaseGap :
    SDM.semaphoreReleaseRequiresStoreLoad = true ∧
    cmpxchgReleaseInlineEdge .amd64 = false ∧
    cmpxchgReleaseHelperEdge .amd64 = false := by decide

theorem narrowReleaseWitness :
    ∃ priorStore compareLoad : Nat,
      compareLoad < priorStore ∧
      (cmpxchgReleaseInlineEdge .amd64 = true → priorStore < compareLoad) ∧
      (cmpxchgReleaseHelperEdge .amd64 = true → priorStore < compareLoad) := by
  exact ⟨1, 0, by decide⟩
"""

STRICT = """
theorem amd64NarrowSemaphoreReleaseRefinement :
    SDM.semaphoreReleaseRequiresStoreLoad = true →
    cmpxchgReleaseInlineEdge .amd64 = true ∧ cmpxchgReleaseHelperEdge .amd64 = true := by
  decide
"""


def run_lean(source):
    spec = (HERE / "Spec.lean").read_text(encoding="utf-8")
    with tempfile.TemporaryDirectory(prefix="ia64-lean-ordering-") as temp:
        module = Path(temp) / "Proofs.lean"
        module.write_text(spec + "\nnamespace IA64Ordering\n" + source +
                          "\nend IA64Ordering\n",
            encoding="utf-8")
        return subprocess.run(["lake", "env", "lean", "-DwarningAsError=true", str(module)],
                              cwd=HERE.parent, capture_output=True, text=True, check=False)


def witness_mutations():
    for relative, original, replacement, name in (
        ("target/ia64/translate/translate.c",
         "gen_set_label(load_only);", "gen_set_label(load_only);tcg_gen_mb(TCG_MO_ALL);",
         "ia64_gen_atomic_cmpxchg_i64"),
        ("target/ia64/arch/memory.c",
         "old = ia64_lduw_data_ra(env, addr, ra);",
         "smp_mb();old = ia64_lduw_data_ra(env, addr, ra);", "ia64_memory_cmpxchg"),
    ):
        source = read(relative)
        require(source.count(original) == 1, f"Could not locate {name} mutation target")
        mutated = source.replace(original, replacement)
        try:
            check_witness_bodies({relative: mutated})
        except ValueError as error:
            require(str(error) == f"Changed witness path {name}", str(error))
        else:
            raise ValueError(f"Witness still used changed path {name}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-test", action="store_true",
                        help="check detection of a missing mf store-to-load barrier")
    parser.add_argument("--strict", action="store_true",
                        help="also require the currently failing narrow AMD64 cmpxchg.rel edge")
    parser.add_argument("--host-mappings", action="store_true",
                        help="emit extracted host masks and AArch64 encodings as JSON")
    args = parser.parse_args()
    system = read("target/ia64/translate/gen-system.c")
    mappings = host_mappings(system)
    if args.host_mappings:
        print(json.dumps(mappings, sort_keys=True))
        return
    baseline = run_lean(generate(mappings) + THEOREMS + (STRICT if args.strict else GAP))
    require(baseline.returncode == 0, baseline.stdout + baseline.stderr)
    print("Ordering: AMD64/AArch64 cacheable TCG masks and litmus theorems passed")
    if not args.strict:
        print("COUNTEREXAMPLE ordering:amd64-narrow-cmpxchg-rel")
        print("Ordering: AMD64 store→load edge missing for excess-CCV cmpxchg.rel"
              " (inline and helper)")
    if args.mutation_test:
        mutant, count = re.subn(r"tcg_gen_mb\(TCG_MO_ALL\);",
                                "tcg_gen_mb(TCG_MO_LD_LD | TCG_MO_LD_ST);", system)
        require(count == 1, "Could not locate mf to mutate")
        mutated = run_lean(generate(host_mappings(mutant)) + THEOREMS)
        require(mutated.returncode != 0 and "decide" in mutated.stdout,
                "Lean accepted weakened mf:\n" + mutated.stdout + mutated.stderr)
        print("Ordering: Lean rejected removal of mf store→load ordering")
        witness_mutations()
        print("Ordering: added barriers invalidate both narrow compare witness paths")


if __name__ == "__main__":
    try:
        main()
    except ValueError as error:
        raise SystemExit(str(error)) from None
