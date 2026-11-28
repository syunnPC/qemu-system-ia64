#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Verify TCG projected copy-ring updates and their effect on scalar folds."""

import argparse
from pathlib import Path
import re
import sys

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))

from Integer.check import compact, require
from Optimizer import check, extras
from SlotControl.reify import function, recognize


def write_links(text, name, parameters, objects, pointers, prefix, masks):
    stores = re.fullmatch(
        r"((?:\w+->(?:next|prev)_copy=(?:\w+->(?:next|prev)_copy|\w+);){4})",
        compact(text))
    require(stores is not None, f"changed copy-link stores in {name}")
    links = {"next": "m.next", "prev": "m.prev"}
    lines = []
    assignments = re.findall(r"(\w+)->(next|prev)_copy=([^;]+);", stores[0])
    for sequence, (dest, field, value) in enumerate(assignments):
        if "->" in value:
            ref, which = value.split("->")
            value = f"({links[which.removesuffix('_copy')]} {objects[ref]})"
        else:
            value = pointers[value]
        lines.append(f"  let link{sequence} := patch {links[field]} {objects[dest]} {value}")
        links[field] = f"link{sequence}"
    return "\n".join([
        f"def {name} [DecidableEq Temp] (m : Metadata Temp) ({parameters} : Temp) : Metadata Temp :=",
        *prefix, *lines, f"  ⟨{links['next']}, {links['prev']}, {masks}⟩",
    ])


def generated(source):
    reset = compact(function(source, "reset_ts"))
    prefix = ("TempOptInfo*ti=ts_info(ts);TCGTemp*pts=ti->prev_copy;"
              "TCGTemp*nts=ti->next_copy;TempOptInfo*pi=ts_info(pts);TempOptInfo*ni=ts_info(nts);")
    suffix = "ti->z_mask=-1;ti->o_mask=0;ti->s_mask=0;if("
    require(reset.startswith(prefix) and reset.count(suffix) == 1, "changed TCG reset preparation")
    unlinks = reset[len(prefix):reset.index(suffix)]
    remove = write_links(
        unlinks, "emittedKill", "dst", {"ti": "dst", "pi": "p", "ni": "n"}, {"ts": "dst"},
        ("  let p := m.prev dst", "  let n := m.next dst"), "setMasks m.masks dst Masks.unknown",
    )
    make_move = compact(function(source, "tcg_opt_gen_mov"))
    pattern = re.search(
        r"di->z_mask=si->z_mask;di->o_mask=si->o_mask;di->s_mask=si->s_mask;"
        r"if\(src_ts->type==dst_ts->type\){TempOptInfo\*ni=ts_info\(si->next_copy\);"
        r"(.*?)if\(!QSIMPLEQ_EMPTY",
        make_move)
    require(pattern is not None, "changed same-width copy insertion")
    link = write_links(
        pattern[1], "emittedLink", "dst src", {"di": "dst", "si": "src", "ni": "n"},
        {"dst_ts": "dst", "src_ts": "src"}, ("  let n := m.next src",),
        "setMasks m.masks dst (m.masks src)",
    )
    recognize(source, "ts_is_copy", "return ts_info(ts)->next_copy != ts;")
    recognize(source, "ts_are_copies", """
        TCGTemp *i;
        if (ts1 == ts2) {
            return true;
        }
        if (!ts_is_copy(ts1) || !ts_is_copy(ts2)) {
            return false;
        }
        for (i = ts_info(ts1)->next_copy; i != ts1; i = ts_info(i)->next_copy) {
            if (i == ts2) {
                return true;
            }
        }
        return false;
    """)
    recognize(source, "cmp_better_copy", "return a->kind < b->kind ? b : a;")
    recognize(source, "find_better_copy", """
        TCGTemp *i, *ret;
        if (temp_readonly(ts)) {
            return ts;
        }
        ret = ts;
        for (i = ts_info(ts)->next_copy; i != ts; i = ts_info(i)->next_copy) {
            ret = cmp_better_copy(ret, i);
        }
        return ret;
    """)
    recognize(source, "copy_propagate", """
        for (int i = nb_oargs; i < nb_oargs + nb_iargs; i++) {
            TCGTemp *ts = arg_temp(op->args[i]);
            if (ts_is_copy(ts)) {
                op->args[i] = temp_arg(find_better_copy(ts));
            }
        }
    """)
    return "\n\n".join(["namespace IA64Optimizer", remove, link, "end IA64Optimizer"])


def generate(root, source=None):
    code = check.load(root, check.SOURCE) if source is None else source
    return "\n\n".join([
        check.generate(root, code),
        (HERE / "Copy.lean.inc").read_text(encoding="utf-8"),
        generated(code),
        (HERE / "CopyExecution.lean.inc").read_text(encoding="utf-8"),
        (HERE / "CopyCycle.lean.inc").read_text(encoding="utf-8"),
        (HERE / "Extras.lean.inc").read_text(encoding="utf-8"),
        extras.generate(root, code),
        (HERE / "ExtraTheorems.lean.inc").read_text(encoding="utf-8"),
        (HERE / "CopyBlock.lean.inc").read_text(encoding="utf-8"),
        (HERE / "Total.lean.inc").read_text(encoding="utf-8"),
        (HERE / "CopyExample.lean.inc").read_text(encoding="utf-8"),
    ])


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=HERE.parents[2])
    parser.add_argument("--emit-lean", type=Path)
    parser.add_argument("--mutation-test", action="store_true")
    args = parser.parse_args()
    program = generate(args.source_root)
    if args.emit_lean:
        args.emit_lean.write_text(program, encoding="utf-8")
        return
    result = check.verify(program)
    require(result.returncode == 0, result.stdout + result.stderr)
    print("TCG optimizer: checked copy-ring scan, update and finite scalar block refinement")
    if args.mutation_test:
        source = check.load(args.source_root, check.SOURCE)
        mutant, count = re.subn(r"pi->next_copy = ti->next_copy;", "pi->next_copy = ts;", source, count=1)
        require(count == 1, "missing stale copy-link mutation")
        result = check.verify(generate(args.source_root, mutant))
        require(result.returncode == 1 and "emittedKill m dst = unlink m dst" in result.stdout,
                "Lean did not reject failing to unlink the destination\n"
                + result.stdout + result.stderr)
        print("TCG optimizer: Lean rejected keeping a stale predecessor link")


if __name__ == "__main__":
    try:
        main()
    except (OSError, ValueError) as error:
        raise SystemExit(str(error)) from None
