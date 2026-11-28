# SPDX-License-Identifier: GPL-2.0-or-later
"""Read IA-64 verification obligations independently of the proof registry."""

import hashlib
import re
import tomllib
from dataclasses import dataclass
from pathlib import Path

from sdm import read_catalog

DIRECTORY = Path(__file__).resolve().parent
REPOSITORY = DIRECTORY.parents[1]
OPCODE_FILE = "target/ia64/decode/opcode.inc"
DECODER = "target/ia64/decode/decode.c"
TRANSLATOR = "target/ia64/translate/translate.c"
LLVM_GUARD = "contrib/ia64-llvm/insn.h"
LLVM_FRONTEND = "contrib/ia64-llvm/frontend.c"
LLVM_LOWER = "contrib/ia64-llvm/lower.c"
GENERATOR_FILES = {
    group: f"target/ia64/translate/gen-{group.lower()}.c"
    for group in ("BRANCH", "FP", "INTEGER", "MEMORY", "SIMD", "SYSTEM")
}
SOURCE_SUFFIXES = {".c", ".cpp", ".h", ".inc"}
OPCODE_ROW = re.compile(
    r"IA64_OPCODE\(\s*([A-Z][A-Z0-9_]*)\s*,\s*([A-Z]+)\s*,"
    r"\s*(true|false)\s*,\s*([A-Z][A-Z0-9_]*)\s*\)"
)
C_COMMENT_OR_LITERAL = re.compile(
    r"(?P<comment>/\*[\s\S]*?\*/|//(?:\\\r?\n|[^\r\n])*)"
    r"""|(?P<literal>"(?:\\[\s\S]|[^"\\\r\n])*"|'(?:\\[\s\S]|[^'\\\r\n])*')"""
)
C_DIRECTIVE = re.compile(r"(?m)^[ \t]*#(?:[^\n]*\\\n)*[^\n]*")
C_FUNCTION = re.compile(
    r"(?m)^[ \t]*(?:(?:[A-Za-z_]\w*(?:::\w+)*(?:<[^{}\n;()]+>)?[ \t\n]+"
    r"|[*&][ \t]*)+)"
    r"((?:[A-Za-z_]\w*::)*[A-Za-z_]\w*)[ \t\n]*\([^;{}]*?\)"
    r"(?:[ \t\n]+(?:G_GNUC_\w+|QEMU_\w+|const|noexcept|override|final))*[ \t\n]*\{"
)
CPP_CLASS = re.compile(r"(?m)^[ \t]*class[ \t]+([A-Za-z_]\w*)[^;{]*\{")


@dataclass(frozen=True)
class Obligation:
    detail: str
    sources: tuple = ()


def read_source(path):
    return path.read_text(encoding="utf-8")


def strip_comments_and_literals(source, *, keep_literals=False):
    def replace(match):
        if keep_literals and match.lastgroup == "literal":
            return match[0]
        return re.sub(r"[^\r\n]", " ", match[0])

    return C_COMMENT_OR_LITERAL.sub(replace, source)


def opcode_rows(path):
    rows = {}
    for line in strip_comments_and_literals(read_source(path), keep_literals=True).splitlines():
        line = line.strip()
        if not line:
            continue
        row = OPCODE_ROW.fullmatch(line)
        if not row:
            raise ValueError(f"unrecognized opcode row in {path.name}: {line}")
        opcode, generator, writes_gr, nat = row.groups()
        if opcode in rows or generator not in (*GENERATOR_FILES, "NONE"):
            raise ValueError(f"invalid opcode row in {path.name}: {line}")
        rows[opcode] = generator, writes_gr, nat
    if not rows:
        raise ValueError("empty QEMU opcode table")
    return rows


def c_functions(source):
    source = strip_comments_and_literals(source)
    source = C_DIRECTIVE.sub("", source)
    names = set()
    classes = []
    for cls in CPP_CLASS.finditer(source):
        depth = 1
        for found in re.finditer(r"[{}]", source[cls.end():]):
            depth += 1 if found[0] == "{" else -1
            if depth == 0:
                classes.append((cls[1], cls.end(), cls.end() + found.start()))
                break
    for match in C_FUNCTION.finditer(source):
        name = match[1]
        if name in {"if", "for", "while", "switch", "__attribute__"}:
            continue
        owner = next((name for name, start, end in classes if start <= match.start() < end),
                     None)
        names.add(f"{owner}::{name}" if owner else name)
    for name, start, end in classes:
        constructor = re.search(
            rf"(?m)^[ \t]*{name}[ \t]*\([^;{{}}]*?\)[ \t\n]*(?::[^;{{}}]*)?\{{",
            source[start:end],
        )
        if constructor:
            names.add(f"{name}::{name}")
    return names


def implementation_paths(root, *, include_backends=True):
    target = root / "target/ia64"
    llvm = root / "contrib/ia64-llvm"
    paths = {
        path.relative_to(root).as_posix()
        for directory in (target, llvm) if include_backends or directory == target
        for path in directory.rglob("*")
        if path.is_file() and path.suffix in SOURCE_SUFFIXES
        and path != llvm / "test.c"
    }
    paths.update({
        "accel/tcg/atomic_common.c.inc",
        "accel/tcg/atomic_template.h",
        "accel/tcg/backend-ldst.h",
        "accel/tcg/cpu-exec-common.c",
        "accel/tcg/cpu-exec.c",
        "accel/tcg/cputlb.c",
        "accel/tcg/ldst_atomicity.c.inc",
        "accel/tcg/ldst_common.c.inc",
        "accel/tcg/tb-maint.c",
        "accel/tcg/tcg-accel-ops-mttcg.c",
        "accel/tcg/tcg-accel-ops-rr.c",
        "accel/tcg/tcg-accel-ops.c",
        "accel/tcg/tcg-all.c",
        "accel/tcg/translate-all.c",
        "accel/tcg/translator.c",
        "configs/targets/ia64-softmmu.mak",
        "contrib/ia64-llvm/Makefile",
        "cpu-common.c",
        "hw/core/cpu-common.c",
        "include/exec/memop.h",
        "include/qemu/atomic.h",
        "include/qemu/bitops.h",
        "include/qemu/host-utils.h",
        "include/qemu/int128.h",
        "include/qemu/atomic128.h",
        "include/qemu/thread.h",
        "include/system/physmem.h",
        "fpu/meson.build",
        "tcg/tcg-has.h",
        "tcg/tcg-internal.h",
        "meson_options.txt",
        "target/ia64/Kconfig",
        "target/ia64/meson.build",
        "target/ia64/trace-events",
        "system/physmem.c",
        "system/cpus.c",
        "tcg/optimize.c",
        "tcg/tcg.c",
    })
    # Cross-target semantics consumed by emitted IR and the FP helpers.
    # Native backend sources are collected separately below.
    common = [
        path for directory in ("fpu", "include/fpu", "include/tcg")
        for path in (root / directory).rglob("*")
    ]
    common.extend((root / "tcg").glob("tcg-op*"))
    common.extend(root / "tcg" / arch / "tcg-target-mo.h" for arch in ("x86_64", "aarch64"))
    paths.update(path.relative_to(root).as_posix()
                 for path in common if path.is_file() and path.suffix in SOURCE_SUFFIXES)
    if include_backends:
        for backend in ("x86_64", "aarch64"):
            paths.update(path.relative_to(root).as_posix()
                         for path in (root / "tcg" / backend).iterdir()
                         if path.is_file() and path.suffix in SOURCE_SUFFIXES)
    # The IA-32 target reuses these translation units and their local includes.
    meson = read_source(target / "meson.build")
    for name in re.findall(r"'\.\./(i386/[^']+\.[ch])'", meson):
        path = root / "target" / name
        paths.add(path.relative_to(root).as_posix())
        paths.update(
            file.relative_to(root).as_posix()
            for file in path.parent.rglob("*.inc")
        )
    return sorted(paths)


def fingerprints(root, paths):
    return {
        path: hashlib.sha256((root / path).read_bytes()).hexdigest()
        for path in paths
    }


def tcg_scope():
    return tomllib.loads(read_source(DIRECTORY / "tcg-scope.toml"))


def in_tcg_scope(key, scope, table, exact):
    selectors = scope[table]
    return key in selectors[exact] or key.startswith(tuple(selectors["prefixes"]))


def in_tcg_inventory(key, scope):
    return any(in_tcg_scope(key, scope, table, "ids")
               for table in ("obligations", "supporting"))


def review_inventory(root, paths, catalog, features, scope=None):
    obligations = {key: Obligation(reference) for key, reference in catalog.items()}
    paths = set(paths)
    seen = set()

    def add(key, detail, *sources):
        if key in obligations or key in seen:
            raise ValueError(f"duplicate obligation {key}")
        seen.add(key)
        if scope and not in_tcg_inventory(key, scope):
            return
        missing = sorted(set(sources) - paths)
        if missing:
            raise ValueError(f"{key} references unknown source: {', '.join(missing)}")
        obligations[key] = Obligation(detail, tuple(sources))

    opcodes = opcode_rows(root / OPCODE_FILE)
    llvm_ops = set()
    if scope is None:
        llvm_guard = read_source(root / LLVM_GUARD)
        llvm_ops = set(re.findall(r"\bcase\s+IA64_OP_([A-Z][A-Z0-9_]*)\s*:", llvm_guard))
        if not llvm_ops or llvm_ops - opcodes.keys():
            raise ValueError("cannot match the LLVM eligibility cases to the opcode table")
    for opcode, (generator, writes_gr, nat) in opcodes.items():
        generator_sources = (GENERATOR_FILES[generator],) if generator != "NONE" else ()
        add(f"decode:{opcode}", "All encodings, operand fields and CPU model gates",
            OPCODE_FILE, DECODER)
        add(f"tcg:{opcode}", f"{generator}, GR write={writes_gr}, NaT recipe={nat}",
            TRANSLATOR, OPCODE_FILE, *generator_sources)
        add(f"llvm-guard:{opcode}",
            "Region eligibility and rejected cases" if opcode in llvm_ops else "TCG fallback",
            LLVM_GUARD)
        if opcode in llvm_ops:
            add(f"llvm:{opcode}", "Eligible operands, lowering and resulting architectural state",
                LLVM_GUARD, LLVM_FRONTEND, LLVM_LOWER)

    for name in paths:
        if not name.startswith(("target/ia64/", "contrib/ia64-llvm/")):
            continue
        if scope and not in_tcg_scope(name, scope, "sources", "paths"):
            continue
        path = root / name
        if path.suffix not in SOURCE_SUFFIXES:
            continue
        for symbol in c_functions(read_source(path)):
            add(f"function:{name}:{symbol}", "Definition and reachable behavior", name)

    helper_header = "target/ia64/helper.h"
    helpers = re.findall(
        r"(?m)^DEF_HELPER(?:_FLAGS)?_\d+\(\s*(\w+)\s*,",
        read_source(root / helper_header),
    )
    if not helpers:
        raise ValueError("empty TCG helper registration table")
    for helper in helpers:
        if scope and helper in {"llvm_region", "llvm_region_fixed"}:
            continue
        add(f"helper:{helper}", "TCG call ABI, side effects and exception behavior",
            helper_header)

    for line in read_source(features).splitlines():
        if not line or line.startswith("#"):
            continue
        key, detail, *sources = line.split("\t")
        add(key, detail, *sources)

    if scope:
        obligations = {key: entry for key, entry in obligations.items()
                       if in_tcg_inventory(key, scope)}
        unknown = sorted(set(scope["obligations"]["ids"]) - obligations.keys())
        if unknown:
            raise ValueError(f"unknown TCG obligation {unknown[0]}")
    return obligations


def load_inventory(root=REPOSITORY, *, scope=None):
    catalog = read_catalog(DIRECTORY / "sdm-index.tsv")
    paths = implementation_paths(root, include_backends=scope is None)
    obligations = review_inventory(root, paths, catalog, DIRECTORY / "routes.tsv", scope)
    return obligations, paths
