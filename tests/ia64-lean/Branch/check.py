#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check native branch target arithmetic and counted-loop state projections."""

import argparse
from hashlib import sha256
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))

from Integer.check import HOST_CALLS, TARGET, cases, compact, emit, expr
from Integer.check import function, require, stripped, switches
from Branch import b1


DIRECT_TARGETS = {
    "BR_COND": r"\.direct_target\s*=\s*([^,]+),",
    "BRL_COND": r"\.direct_target\s*=\s*([^,]+),",
    "BR_CALL": r"uint64_t target\s*=\s*([^;]+);",
    "BRL_CALL": r"uint64_t target\s*=\s*([^;]+);",
    "BR_CLOOP": r"uint64_t target\s*=\s*([^;]+);",
    **{f"BR_{kind}": r"uint64_t static_target\s*=\s*([^;]+);"
       for kind in ("CTOP", "CEXIT", "WTOP", "WEXIT")},
}


def source(path):
    return stripped((TARGET / path).read_text())


def guard(body, digest, description):
    require(sha256(compact(body).encode()).hexdigest() == digest,
            f"Changed {description} control flow")


def lift_decoder(decoder):
    HOST_CALLS.update({"ia64_bits": "sourceBits", "ia64_sign_extend": "sourceSignExtend"})
    signatures = {
        "ia64_bits": ("sourceBits", "value low width"),
        "ia64_sign_extend": ("sourceSignExtend", "value width"),
        "ia64_branch_disp": ("sourceShortDisp", "raw"),
        "ia64_mlx_brl_disp": ("sourceLongDisp", "l_slot x_slot"),
    }
    definitions = []
    for name, (lean_name, parameters) in signatures.items():
        body = re.sub(r"\bunsigned\b", "uint64_t", function(decoder, name))
        values = {parameter: parameter for parameter in parameters.split()}
        emit(body, values)
        definitions.append(f"def {lean_name} ({parameters} : Word) : Word := {values['return']}")
    return "\n".join(definitions)


def target_exp(body, pattern):
    match = re.search(pattern, body)
    require(match is not None, "Unrecognized branch target operand")
    return expr(match[1], {"insn->address": "ip", "op->displacement": "displacement"})


def lift_targets(dispatch, cpu):
    for opcode, pattern in DIRECT_TARGETS.items():
        value = target_exp(dispatch[opcode], pattern)
        yield (
            f"theorem directTarget_{opcode} (ip displacement : Word)\n"
            "    (ipAligned : ip &&& 15#64 = 0)\n"
            "    (dispAligned : displacement &&& 15#64 = 0) :\n"
            f"    {value} = SDM.directTarget ip displacement := by\n"
            "  simp only [SDM.directTarget]\n  bv_decide"
        )
    define = re.search(r"#define IA64_IP_BUNDLE_MASK\s+([^\n]+)", cpu)
    size = re.search(r"#define IA64_BUNDLE_SIZE\s+(\d+)ULL", cpu)
    require(define is not None and size is not None, "Missing native bundle constants")
    mask = expr(define[1], {"IA64_BUNDLE_SIZE": f"{size[1]}#64"})
    body = dispatch["BR_INDIRECT"]
    start = re.search(r"tcg_gen_(\w+)_i64\(target,\s*cpu_br\[op->target\],"
                      r"\s*IA64_IP_BUNDLE_MASK\);", body)
    require(start is not None and ".tcg_target = target" in body,
            "Unexpected indirect branch mask/target")
    values = {"register": "br", "mask": mask}
    emit(f"tcg_gen_{start[1]}_i64(target, register, mask);", values)
    yield (
        "theorem indirectTarget (br : Word) :\n"
        f"    {values['target']} = SDM.indirectTarget br := by\n"
        "  simp only [SDM.indirectTarget]\n  bv_decide"
    )


def lift_cloop(dispatch, translate, rse):
    cloop = dispatch["BR_CLOOP"]
    step = re.search(r"tcg_gen_brcondi_i64\((TCG_COND_(?:EQ|NE)), lc, (\d+), l_nobr\);"
                     r"(?s:.*?)tcg_gen_(\w+)_i64\(lc, lc, (\d+)\);",
                     cloop)
    require(step is not None, "Unexpected cloop update")
    frozen_control = cloop[:step.start()] + "comparison; decrement;" + cloop[step.end():]
    # Full check of the code around the projected comparison and subtraction.
    require(compact(frozen_control) == compact("""
        {
            uint64_t target = insn->address + op->displacement;
            TCGv_i64 lc = tcg_temp_new_i64();
            TCGLabel *l_nobr = gen_new_label();
            tcg_gen_ld_i64(lc, tcg_env, offsetof(CPUIA64State, ar[IA64_AR_LC]));
            comparison; decrement;
            tcg_gen_st_i64(lc, tcg_env, offsetof(CPUIA64State, ar[IA64_AR_LC]));
            if (ia64_gen_self_counted_loop(ctx, target, insn->address, record_iipa,
                                          track_psr_suppression)) {
                gen_set_label(l_nobr);
                return IA64_GEN_CONTINUE;
            }
            ia64_gen_goto_completed(ctx, target, insn->address, record_iipa,
                                    track_psr_suppression);
            gen_set_label(l_nobr);
            return IA64_GEN_CONTINUE;
        }"""),
            "Unsupported ordinary cloop source around update")
    bridge = cloop[step.start():step.end()].split("l_nobr);", 1)[1]
    bridge = re.sub(r"tcg_gen_\w+_i64\(lc, lc, \d+\);\s*$", "", bridge)
    require(compact(bridge) == compact("""
        if (ia64_gen_fill_st1_cloop(ctx, insn, target, l_nobr, record_iipa,
                                   track_psr_suppression)) {
            gen_set_label(l_nobr);
            return IA64_GEN_CONTINUE;
        }"""), "Unexpected cloop helper placement")

    values = {"lc": "lc"}
    emit(f"tcg_gen_{step[3]}_i64(lc, lc, {step[4]});", values)
    comparison = "==" if step[1] == "TCG_COND_EQ" else "!="
    output = [
        f"def sourceCloop (lc : Word) : Bool × Word :=\n"
        f"  if lc {comparison} {step[2]}#64 then (false, lc) else (true, {values['lc']})",
        "theorem ordinaryCloop (lc : Word) : sourceCloop lc = SDM.nextCount lc := by\n"
        "  simp [sourceCloop, SDM.nextCount]",
    ]
    guard(function(translate, "ia64_gen_self_counted_loop"),
          "8788a7a9f6dd6c91f27b50c3cd75430ed40d33f0c83c97fa726aa8854f83e372",
          "self-counted loop")
    guard(function(translate, "ia64_gen_fill_st1_cloop"),
          "8fca247aa814c6bbd5e0f8199b836bcfe19dbbe342b856bebc0fa4cbfbc44682",
          "counted fill emitter")
    guard(function(rse, "ia64_rse_cloop_fill_st1"),
          "890225dea519b3e1e0bb89a4851b2251e8282ad15bbdfbbdd16881a5f614c25f",
          "counted fill helper")
    output.append(CLOOP_HELPERS)
    return "\n".join(output)


def lift_pipeline(rse, helper):
    for kind in ("ctop", "cexit", "wtop", "wexit"):
        adapter = function(helper, f"helper_br_{kind}")
        args = "env,qp" if kind.startswith("w") else "env"
        require(compact(adapter) == f"returnia64_rse_br_{kind}({args});",
                f"Unexpected {kind} helper adapter")
    output = []
    for kind in ("ctop", "cexit", "while_loop"):
        name = ("ia64_update_while_loop" if kind == "while_loop"
                else f"ia64_rse_br_{kind}")
        body = function(rse, name)
        body = re.sub(r"env->pr\[IA64_PR_LAST\]\s*=\s*([01]);",
                      lambda m: f"prlast = {'true' if m[1] == '1' else 'false'};", body)
        body = body.replace("ia64_rotate_loop_regs(env);", "rotate = true;")
        body = body.replace("env->ar_lc", "lc").replace("env->ar_ec", "ec")
        body = body.replace("ec--;", "ec = ec - 1;")
        objective = (f"SDM.countedPipeline {str(kind == 'cexit').lower()} lc ec"
                     if kind != "while_loop" else "SDM.whilePipeline false qp ec")
        arguments = "(lc ec : Word)"
        tuple_type = "Bool × Word × Word × Bool × Bool"
        fields = ["return", "lc", "ec", "prlast", "rotate"]
        if kind == "while_loop":
            body = re.sub(r"env->pr\[qp\s*&\s*63\]", "qp", body)
            body = body.replace("kernel_active", "immediate")
            arguments = "(qp : Bool) (ec : Word)"
            tuple_type = "Bool × Word × Bool × Bool"
            fields.remove("lc")
        values = {"lc": "lc", "ec": "ec", "prlast": "oldPrlast", "rotate": "false",
                  "qp": "qp"}
        emit(body, values)
        actual = ", ".join(values[field] for field in fields)
        source_name = f"sourcePipeline_{kind}"
        output.append(f"def {source_name} {arguments} (oldPrlast : Bool) : {tuple_type} := ({actual})")
        operands = "qp ec" if kind == "while_loop" else "lc ec"
        output.append(
            f"theorem pipeline_{kind} {arguments} (oldPrlast : Bool) :\n"
            f"    {source_name} {operands} oldPrlast = {objective} := by\n"
            "  simp [SDM.countedPipeline, SDM.whilePipeline, "
            f"{source_name}, Prod.mk.injEq] <;> bv_decide"
        )
    for opcode in ("wtop", "wexit"):
        wrapper = compact(function(rse, f"ia64_rse_br_{opcode}"))
        invert = "!" if opcode == "wexit" else ""
        require(wrapper == f"return{invert}ia64_update_while_loop(env,qp);",
                f"Unexpected {opcode} RSE wrapper")
    return "\n".join(output)


CLOOP_HELPERS = """
-- The source helper uses MIN(lc, max_stores) then starts future stores.
-- The projection applies when stores succeed, run with the same byte/base
-- operands and the scalar/host-fill loops finish their entire batch.
def sourceFillLimit (lc maxStores : Nat) : Nat := min lc maxStores
def sourceFillTail (lc done : Nat) : Bool × Nat :=
  if done < lc then (true, lc - done - 1) else (false, lc - done)

theorem projectedFillCloop (lc done : Nat) (fits : done ≤ lc) :
    sourceFillTail lc done = SDM.cloop (SDM.afterTakenStores done lc) := by
  rw [SDM.repeatedCount done lc fits]
  by_cases taken : done < lc
  · have nonzero : lc - done ≠ 0 := by omega
    simp [sourceFillTail, SDM.cloop, taken, nonzero]
  · have zero : lc - done = 0 := by omega
    simp [sourceFillTail, SDM.cloop, taken, zero]

theorem projectedFullBatch (lc maxStores : Nat) :
    sourceFillTail lc (sourceFillLimit lc maxStores) =
      SDM.cloop (SDM.afterTakenStores (sourceFillLimit lc maxStores) lc) :=
  projectedFillCloop lc _ (Nat.min_le_left _ _)

-- For a validated self edge, both exit paths name the same bundle. A zero
-- budget sends control to the normal target path, and a nonzero budget emits
-- the back edge (with RI cleared to zero). No other restart state is modeled.
def sourceInternalEdge (target budget : Word) : Word × Word × Bool :=
  if budget = 0 then (target, budget, false) else (target, budget - 1, true)

theorem selfEdgeKeepsArchitecturalTarget (target budget : Word) :
    (sourceInternalEdge target budget).1 = target := by
  simp [sourceInternalEdge]
  split <;> rfl

theorem optimizedCloop (ip fallthrough lc budget : Word) :
    (if (sourceCloop lc).1 then
      ((sourceInternalEdge ip budget).1, (sourceCloop lc).2) else
      (fallthrough, (sourceCloop lc).2)) =
    (if (SDM.nextCount lc).1 then (ip, (SDM.nextCount lc).2) else
      (fallthrough, (SDM.nextCount lc).2)) := by
  rw [ordinaryCloop, selfEdgeKeepsArchitecturalTarget]
"""

DISPLACEMENTS = """
theorem shortDisplacement (raw : Word) : sourceShortDisp raw = SDM.shortDisp raw := by
  simp only [sourceShortDisp, sourceBits, sourceSignExtend, SDM.shortDisp]
  bv_decide

theorem longDisplacement (l x : Word) : sourceLongDisp l x = SDM.longDisp l x := by
  simp only [sourceLongDisp, sourceBits, sourceSignExtend, SDM.longDisp]
  bv_decide

theorem shortDisplacementAligned (raw : Word) : sourceShortDisp raw &&& 15#64 = 0 := by
  rw [shortDisplacement]
  simp only [SDM.shortDisp]
  bv_decide

theorem longDisplacementAligned (l x : Word) : sourceLongDisp l x &&& 15#64 = 0 := by
  rw [longDisplacement]
  simp only [SDM.longDisp]
  bv_decide
"""


def generate(decoder):
    branch = function(source("translate/gen-branch.c"), "ia64_gen_branch")
    switch = switches(branch)[0]
    require(compact(branch[:switch[0]]) ==
            "constIA64BranchOperands*op=&insn->operands.branch;"
            "constuint64_tnext_ip=insn->address+16;switch(insn->opcode)",
            "Unexpected native branch emitter prefix")
    dispatch = cases(switch[2])
    return "\n\n".join([lift_decoder(decoder), DISPLACEMENTS,
                       *lift_targets(dispatch, source("cpu.h")),
                       lift_cloop(dispatch, source("translate/translate.c"),
                                  source("arch/rse.c")),
                       lift_pipeline(source("arch/rse.c"), source("helper/helper-rse.c"))])


def program(generated, joined=""):
    specification = (HERE / "Spec.lean").read_text()
    return (specification + "\nnamespace IA64Branch\n"
            'local notation "Word" => BitVec 64\n'
            "set_option linter.unusedVariables false\n" + generated
            + "\nend IA64Branch\n" + joined + "\n")


def run_lean(contents):
    with tempfile.TemporaryDirectory(prefix="ia64-lean-branch-") as temp:
        module = Path(temp) / "Proofs.lean"
        module.write_text(contents, encoding="utf-8")
        return subprocess.run(["lake", "env", "lean", "-DwarningAsError=true", str(module)],
                              cwd=HERE.parent, capture_output=True, text=True, check=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--emit-lean", type=Path)
    parser.add_argument("--mutation-test", action="store_true",
                        help="check that a changed branch-displacement scale fails")
    parser.add_argument("--coverage", action="store_true",
                        help="report partial branch opcode coverage as JSON")
    args = parser.parse_args()
    if args.coverage:
        scope = {f"tcg:{op}": "Aligned direct branch target arithmetic projection"
                 for op in DIRECT_TARGETS}
        scope["tcg:BR_INDIRECT"] = "Indirect branch target alignment projection"
        scope["tcg:BR_COND"] += ", decoded B1 slot through native continuation or first helper"
        scope["tcg:BR_CLOOP"] += ", ordinary and optimized LC/taken projections"
        for kind in ("CTOP", "CEXIT", "WTOP", "WEXIT"):
            scope[f"tcg:BR_{kind}"] += ", loop helper counter and decision projection"
        print(json.dumps(scope, sort_keys=True))
        return
    decoder = source("decode/decode.c")
    root = HERE.parents[2]
    generated = generate(decoder)
    contents = program(generated, b1.lift(root, target_exp))
    if args.emit_lean:
        args.emit_lean.write_text(contents, encoding="utf-8")
        return
    baseline = run_lean(contents)
    require(baseline.returncode == 0, baseline.stdout + baseline.stderr)
    print("Branch: decoded B1 slot control, target alignment, counted loops and pipeline projections passed")
    if args.mutation_test:
        mutant = decoder.replace("ia64_sign_extend(field, 21) * 16",
                                 "ia64_sign_extend(field, 21) * 8")
        require(mutant != decoder, "Could not locate short-branch mutation target")
        mutated = run_lean(program(generate(mutant)))
        require(mutated.returncode != 0 and "counterexample" in mutated.stdout,
                "Lean did not reject mutated branch displacement:\n"
                + mutated.stdout + mutated.stderr)
        print("Branch: Lean rejected changing the short displacement scale")
        predicate = run_lean(program(generated, b1.lift(root, target_exp, predicate_mutation=True)))
        require(predicate.returncode != 0 and "unsolved goals" in predicate.stdout,
                "Lean did not reject the inverted predicate test:\n"
                + predicate.stdout + predicate.stderr)
        print("Branch: Lean rejected inverting the dynamic B1 predicate test")


if __name__ == "__main__":
    try:
        main()
    except ValueError as error:
        raise SystemExit(str(error)) from None
