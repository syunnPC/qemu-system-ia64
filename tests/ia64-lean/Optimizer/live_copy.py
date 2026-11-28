# SPDX-License-Identifier: GPL-2.0-or-later
"""Reify a captured ADD-by-zero fold and its register or conditional consumer."""

import re

from Emitted.check import ADD_IP, fixture, project
from HostLitmus.replay import trace_program
from Integer.check import compact, require
from SlotControl.probe import MARKER, TCG_PAIR
from SlotControl.reify import function, recognize


def check_source(root, source):
    init = compact(function(source, "init_ts_info"))
    require("if(test_bit(idx,ctx->temps_used.l)){return;}set_bit(idx,ctx->temps_used.l);" in init,
            "ADD metadata must be initialized on first use")
    require(init.endswith(compact("""
        ti->next_copy = ts;
        ti->prev_copy = ts;
        QSIMPLEQ_INIT(&ti->mem_copy);
        if (ts->kind == TEMP_CONST) {
            ti->z_mask = ts->val;
            ti->o_mask = ts->val;
            ti->s_mask = INT64_MIN >> clrsb64(ts->val);
        } else {
            ti->z_mask = -1;
            ti->o_mask = 0;
            ti->s_mask = 0;
        }
    """)), "ADD operand metadata initialization changed")
    recognize(source, "init_arguments", """
        for (int i = 0; i < nb_args; i++) {
            TCGTemp *ts = arg_temp(op->args[i]);
            init_ts_info(ctx, ts);
        }
    """)
    optimize = compact(function(source, "tcg_optimize"))
    require("OptContextctx={.tcg=s};" in optimize,
            "TCG optimizer must start with fresh per-TB metadata")
    require("init_arguments(&ctx,op,def->nb_oargs+def->nb_iargs);"
            "copy_propagate(&ctx,op,def->nb_oargs,def->nb_iargs);" in optimize,
            "ADD operands must be initialized before copy propagation")
    header = compact((root / "include/tcg/tcg.h").read_text(encoding="utf-8"))
    positions = [header.index(name, header.index("typedefenumTCGTempKind{"))
                 for name in ("TEMP_EBB", "TEMP_TB", "TEMP_GLOBAL", "TEMP_FIXED", "TEMP_CONST")]
    require(positions == sorted(positions), "TCG copy preference roles changed")
    dump = (root / "tcg/tcg.c").read_text(encoding="utf-8")
    require('caseTEMP_TB:snprintf(buf,buf_size,"loc%d",idx-s->nb_globals);' in compact(dump),
            "TCG loc argument dump convention changed")

    translator = (root / "target/ia64/translate/translate.c").read_text(encoding="utf-8")
    recognize(translator, "ia64_gr_src", "return reg == 0 ? tcg_constant_i64(0) : cpu_gr[reg];")
    recognize(translator, "ia64_gen_gr_nat_read", """
        TCGv_i64 bit;
        if (reg == 0) { return tcg_constant_i64(0); }
        bit = tcg_temp_new_i64();
        tcg_gen_shri_i64(bit, cpu_nat[reg / 64], reg % 64);
        tcg_gen_andi_i64(bit, bit, 1);
        return bit;
    """)
    registers = compact(function(translator, "ia64_translate_init"))
    for field, name, array in (("gr", "gr_names[i]", "gr"), ("pr", "pr_names[i]", "pr"),
                               ("nat", '"nat"', "nat")):
        require(f'cpu_{array}[i]=tcg_global_mem_new_i64(tcg_env,offsetof(CPUIA64State,{field}[i]),{name});'
                in registers, f"TCG {field} no longer addresses the corresponding CPU state")


def slices(trace, source):
    pairs = []
    for emitted, optimized in TCG_PAIR.findall(trace):
        blocks = []
        for body in (emitted, optimized):
            marks = list(MARKER.finditer(body))
            if len(marks) != 1 or int(marks[0][1], 16) != ADD_IP:
                break
            prefix = body[:marks[0].start()]
            require(not re.search(rf"\b(?:r8|r{source}|nat|p2)\b", prefix),
                    "ADD inputs were already touched in the TB prologue")
            ops = [tuple(line.split()[:2]) for line in body[marks[0].end():].splitlines()
                   if line.strip()]
            enter = ("xor_i64", "psr,psr,$0x20000000000")
            leave = ("xor_i64", "psr,psr,$0x60000000000")
            require(ops and ops[0] == enter and ops.count(leave) == 1,
                    "unexpected ADD slot boundary")
            blocks.append((ops[1:ops.index(leave)], prefix))
        if len(blocks) == 2:
            pairs.append(blocks)
    require(len(pairs) == 1, "expected a unique one-bundle ADD block")
    (before, prefix), (after, _) = pairs[0]
    require(before, "missing emitted ADD")
    dest = before[0][1].split(",")[0]
    if dest.startswith("loc"):
        require(not re.search(rf"\b{re.escape(dest)}\b", prefix),
                "ADD scratch was already used in the TB prologue")
    return before, after


def capture(qemu):
    for source in (8, 9):
        for qp, predicate in ((0, 1), (2, 0), (2, 1)):
            program, expected = fixture(qp, True, predicate)
            addr, template, nop, _, branch = program[3]
            program[3] = (addr, template, nop, (8 << 37) | (source << 13) | (8 << 6) | qp, branch)
            if source == 8:
                expected.update(r8=0x11, r8_nat=0)
            name = f"addCopy_r{source}_p{qp}_v{predicate}"
            trace = trace_program(qemu, program, expected, name, log_flags="op,op_opt")
            yield name, source, qp, *slices(trace, source)


def reify(name, src, qp, before, after):
    op, args = before[0]
    dest, lhs, rhs = args.split(",")
    require(op == "add_i64" and lhs == f"r{src}" and rhs == "$0x0",
            f"{name}: ADD value inputs differ from the decoded GR and architectural zero")
    alias = src == 8
    source_role = ".output" if alias else ".source"
    result_role = ".scratch" if qp else ".output"
    elided = alias and not qp
    result_op = ".nop" if elided else f".replace .first {result_role} {source_role} {source_role}"
    if not qp:
        require(dest == "r8" and (not after if alias else after[0] == ("mov_i64", "r8,r9")),
                f"{name}: incorrect self-copy deletion or register MOV")
        consumer = ""
    else:
        require(re.fullmatch(r"loc\d+", dest) is not None and
                before[1] == ("movcond_i64", f"r8,p2,$0x0,{dest},r8,ne"),
                f"{name}: missing ADD scratch consumer")
        require((all(not args.startswith("r8,") for _, args in after) if alias else
                 after[0] == ("movcond_i64", "r8,p2,$0x0,r9,r8,ne")),
                f"{name}: incorrect conditional copy propagation or self-write deletion")
        picked = "[1].foldl (fun current n => pickCopy addTempKind current (follow m .scratch n)) .scratch"
        consumer = f"""
theorem {name}Consumer :
    let m := capturedCopy .scratch {source_role}
    ({picked}) = AddTemp{source_role} := by decide
"""
    flags = f"{str(bool(qp)).lower()} {str(alias).lower()}"

    return f"""
{project(f"{name}Emitted", before)}
{project(f"{name}Optimized", after)}

{consumer}

theorem {name}Translation :
    Translation addPool (initialized addPool.readonly) [addMoveInput {result_role} {source_role}]
      [{result_op}] (capturedCopy {result_role} {source_role}) := by
  exact addZeroTrace {result_role} {source_role} (by decide) (by decide)

theorem {name}Effects (old source pred tags : Word) :
    {name}Emitted old source 0 pred tags = observeAddCopy {flags} old source pred tags ∧
    {name}Optimized old source 0 pred tags = observeAddCopy {flags} old source pred tags := by
  rw [observeAddCopy_matches]
  constructor <;> apply Prod.ext <;>
    simp [{name}Emitted, {name}Optimized, SDM.ADD, IA64Integer.TCG.deposit,
      SDM.replaceNaT, SDM.staticNaT] <;> bv_decide

"""


def generate(captures):
    return "\n".join([
        "namespace IA64OptimizedSlotLive", "open IA64Integer IA64Optimizer",
        'local notation "Word" => BitVec 64', "set_option linter.unusedVariables false",
        *(reify(*item) for item in captures), "end IA64OptimizedSlotLive",
    ])
