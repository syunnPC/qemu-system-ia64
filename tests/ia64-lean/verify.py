# SPDX-License-Identifier: GPL-2.0-or-later
"""Run the focused IA-64 checks or require full architectural evidence."""

import argparse
import json
import re
import subprocess
import sys
import tempfile
import tomllib
from collections import Counter
from pathlib import Path

from inventory import (
    DIRECTORY,
    REPOSITORY,
    fingerprints,
    in_tcg_inventory,
    in_tcg_scope,
    load_inventory,
    tcg_scope,
)
from sdm import from_pdfs, read_catalog

CHECKS = {
    "alat": "Alat/check.py",
    "branch": "Branch/check.py",
    "decode": "Decode/check.py",
    "exception-entry": "ExceptionEntry/check.py",
    "exception-entry-alu": "ExceptionEntry/join.py",
    "floating": "Floating/check.py",
    "full-decode": "FullDecode/check.py",
    "integer": "Integer/check.py",
    "llvm": "Llvm/check.py",
    "memory": "Memory/check.py",
    "mmu": "Mmu/check.py",
    "optimizer": "Optimizer/check.py",
    "optimizer-copy": "Optimizer/copy.py",
    "optimizer-payload": "Optimizer/payload.py",
    "optimizer-slot": "Optimizer/slot.py",
    "ordering": "Ordering/check.py",
    "purges": "Purges/check.py",
    "refinement": "Refinement/check.py",
    "rse": "Rse/check.py",
    "simd": "Simd/check.py",
    "slot-control": "SlotControl/check.py",
    "slot-control-alu": "SlotControl/alu.py",
    "slot-control-bundle": "SlotControl/bundle.py",
    "slot-control-movl": "SlotControl/movl.py",
    "system": "System/check.py",
    "tcg": "Tcg/check.py",
    "tcg-memory": "TcgMemory/check.py",
    "traceorder": "TraceOrder/check.py",
}
SUPPLEMENTARY_CHECKS = {"llvm", "ordering"}
GENERATED_MODULES = {
    "Branch.Implementation": "branch",
    "ExceptionEntry.Implementation": "exception-entry",
    "ExceptionEntry.AluImplementation": "exception-entry-alu",
    "Floating.Implementation": "floating",
    "FullDecode.Implementation": "full-decode",
    "Memory.Implementation": "memory",
    "Optimizer.Implementation": "optimizer",
    "Optimizer.CopyImplementation": "optimizer-copy",
    "Optimizer.PayloadImplementation": "optimizer-payload",
    "Optimizer.SlotImplementation": "optimizer-slot",
    "Rse.Implementation": "rse",
    "SlotControl.Implementation": "slot-control",
    "SlotControl.AluImplementation": "slot-control-alu",
    "SlotControl.BundleImplementation": "slot-control-bundle",
    "SlotControl.MovlImplementation": "slot-control-movl",
    "TcgMemory.Implementation": "tcg-memory",
    "TraceOrder.Implementation": "traceorder",
}
FOCUSED_REPORTS = {
    "branch": "tcg:",
    "floating": "tcg:",
    "integer": "tcg:",
    "llvm": "llvm:",
    "purges": "tcg:",
    "simd": "tcg:",
    "slot-control": "tcg:",
    "slot-control-alu": "tcg:",
    "tcg": "tcg:",
    "tcg-memory": "tcg:",
}
PROOF_AREAS = (
    DIRECTORY, DIRECTORY / "Branch", DIRECTORY / "ExceptionEntry", DIRECTORY / "Floating",
    DIRECTORY / "FullDecode", DIRECTORY / "Optimizer", DIRECTORY / "Rse", DIRECTORY / "SlotControl",
    DIRECTORY / "TcgMemory", DIRECTORY / "TraceOrder",
)
FOCUSED_SCOPES = {
    "feature:bundle-legality": ("decode", "bundle/template and immediate projections"),
    "feature:exceptions": ("exception-entry", "native Illegal Operation helper and entry projection"),
    "feature:issue-restart": ("slot-control", "native slot restart and completion requests"),
    "feature:fp": ("floating", "register formats and cached significand projections"),
    "feature:memory": ("memory", "integer descriptors, endian selection and compare bounds"),
    "feature:rse": ("rse", "backing-store arithmetic, RNAT and rotation helpers"),
    "feature:translation": ("mmu", "page rights, Merced hit qualification and VHPT arithmetic"),
    "ordering:alat-race": ("alat", "conditional paired-generation and store-token results"),
    "ordering:ia64-on-aarch64": ("ordering", "cacheable load/store barrier projections"),
    "ordering:ia64-on-amd64": ("ordering", "cacheable load/store barrier projections"),
    "ordering:purge-shootdown": ("purges", "local and peer effects with conditional scheduling"),
    "refinement:architecture": ("refinement", "finite refinement of the endian selector"),
    "refinement:tcg": ("refinement", "finite refinement of the endian selector"),
    "ordering:tcg-observation-graph": ("traceorder", "conditional coherent WB graph closure"),
    "route:tcg-counted-loop": ("branch", "loop exit/continue, LC and predicate effects"),
    "route:tcg-psr": ("slot-control", "IP/RI, IIPA and completion request projection"),
    "route:tcg-integer": ("tcg", "scalar value/NaT slot and sound incoming fact projections"),
    "route:tcg-fp-format": ("floating", "binary and spill representation projections"),
    "route:tcg-memory": ("tcg-memory", "scalar aligned WB values, ordering and abort projection"),
    "route:tcg-simd": ("simd", "packed arithmetic and comparison value projections"),
}
RECORDED_WITNESSES = {
    "ordering:amd64-narrow-cmpxchg-rel": "ordering",
    "ordering:fused-release-loop": "memory",
    "ordering:tcg-narrow-cmpxchg-rel": "traceorder",
}
TOOLCHAIN = "4.19.0"
LEAN_NAME = re.compile(r"[A-Za-z_][A-Za-z_0-9]*(?:\.[A-Za-z_][A-Za-z_0-9]*)*")
LEAN_AXIOMS = {"Classical.choice", "Lean.ofReduceBool", "Quot.sound", "propext"}


def snapshot_changes(previous, current):
    changes = []
    for path in sorted(previous.keys() | current.keys()):
        if path not in previous:
            changes.append(f"added {path}")
        elif path not in current:
            changes.append(f"removed {path}")
        elif previous[path] != current[path]:
            changes.append(f"modified {path}")
    return changes


def read_snapshot(path):
    hashes = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line or line.startswith("#"):
            continue
        digest, source = line.split("  ", 1)
        if source in hashes or not re.fullmatch(r"[a-f0-9]{64}", digest):
            raise ValueError(f"invalid snapshot entry for {source}")
        hashes[source] = digest
    if not hashes:
        raise ValueError("empty implementation snapshot")
    return hashes


def read_registry(filename, table, areas=None, *, optional=False):
    records = {}
    for directory in PROOF_AREAS if areas is None else areas:
        path = directory / filename
        manifest = tomllib.loads(path.read_text(encoding="utf-8"))
        source = manifest.get("source", {})
        for name, digest in source.items():
            if not isinstance(digest, str) or not re.fullmatch(r"[a-f0-9]{64}", digest):
                raise ValueError(f"invalid shared source fingerprint in {path}: {name}")
        entries = manifest.get(table, {}) if optional else manifest[table]
        for key, entry in entries.items():
            if key in records:
                raise ValueError(f"duplicate {table} {key} in {path.relative_to(directory.parent)}")
            if table in {"proof", "counterexample"} and isinstance(entry.get("sources"), list):
                bindings = {}
                for name in entry["sources"]:
                    if not isinstance(name, str) or name not in source:
                        raise ValueError(f"{key} references undeclared source {name}")
                    if name in bindings:
                        raise ValueError(f"{key} repeats source {name}")
                    bindings[name] = source[name]
                entry = {**entry, "sources": bindings}
            records[key] = entry
    return records


def check_ledger(obligations, proofs):
    if not obligations:
        raise ValueError("empty obligation inventory")
    unknown = sorted(proofs.keys() - obligations.keys())
    if unknown:
        raise ValueError(f"unknown obligation {unknown[0]} in proofs.toml")
    return sorted(obligations.keys() - proofs.keys())


def check_command(command, capture=False):
    try:
        return subprocess.run(
            command,
            cwd=DIRECTORY,
            check=True,
            text=True,
            stdout=subprocess.PIPE if capture else None,
            stderr=subprocess.STDOUT if capture else None,
        ).stdout
    except subprocess.CalledProcessError as error:
        if error.stdout:
            print(error.stdout, end="", file=sys.stderr)
        raise ValueError(f"{command[0]} exited with status {error.returncode}") from error


def check_toolchain():
    version = check_command(["lake", "env", "lean", "--version"], capture=True)
    if not re.search(rf"\bLean \(version {re.escape(TOOLCHAIN)}(?:,|\))", version):
        raise ValueError(f"expected Lean {TOOLCHAIN}, got {version.strip()}")


def run_checks(names, counterexamples, records=()):
    check_toolchain()
    remaining = list(records)
    for name in dict.fromkeys(names):
        print(f"Running {name} focused check", flush=True)
        expected = [key for key, check in counterexamples.items() if check == name]
        command = [sys.executable, CHECKS[name]]
        claims = ([entry for entry in remaining if entry[0]["module"] == "FullDecode.Implementation"]
                  if name == "full-decode" else [])
        if claims:
            bound_names, source = proof_claims(claims)
            with tempfile.TemporaryDirectory(prefix="ia64-decode-bindings-") as temporary:
                path = Path(temporary) / "Bindings.lean"
                path.write_text(source, encoding="utf-8")
                output = check_command([*command, "--binding-claims", str(path)], capture=True)
            check_axioms(output, bound_names)
        else:
            output = check_command(command, capture=bool(expected))
        if claims or expected:
            summary = "".join(line for line in output.splitlines(keepends=True)
                              if not line.startswith("'ia64_coverage_"))
            print(summary, end="", flush=True)
        if expected:
            for key in expected:
                if not re.search(rf"(?m)^COUNTEREXAMPLE {re.escape(key)}(?:\s|$)", output):
                    raise ValueError(f"{name} did not reproduce {key}")
            print(f"COMPLETED {name} focused check with counterexample", flush=True)
        else:
            print(f"PASS {name} focused check", flush=True)
        if claims:
            remaining = [entry for entry in remaining if entry[0]["module"] != "FullDecode.Implementation"]
            print(f"PASS {len(claims)} independently typed proof bindings (full-decode)", flush=True)
    return remaining


def validate_contract(key, contract):
    fields = {"check", "module", "statement"}
    if set(contract) != fields:
        raise ValueError(f"{key} contract must specify {', '.join(sorted(fields))}")
    for field, value in contract.items():
        if not isinstance(value, str) or not value.strip():
            raise ValueError(f"{key} has an empty or invalid {field}")
    check = contract["check"]
    module = contract["module"]
    if check not in CHECKS or not LEAN_NAME.fullmatch(module):
        raise ValueError(f"{key} has an invalid Lean module or implementation check")
    if module in GENERATED_MODULES:
        if check != GENERATED_MODULES[module]:
            raise ValueError(f"{key} uses the wrong generator for {module}")
    else:
        path = DIRECTORY.joinpath(*module.split(".")).with_suffix(".lean")
        if not path.is_file():
            raise ValueError(f"{key} has no local Lean module {module}")


def validate_proof(key, obligation, proof, contract, hashes):
    if contract is None:
        raise ValueError(f"{key} has no independent contract")
    if set(proof) != {"theorem", "sources"}:
        raise ValueError(f"{key} proof must specify theorem and sources")
    theorem = proof["theorem"]
    if not isinstance(theorem, str) or not LEAN_NAME.fullmatch(theorem):
        raise ValueError(f"{key} has an invalid Lean theorem")
    sources = proof["sources"]
    if not isinstance(sources, dict) or not sources:
        raise ValueError(f"{key} has no implementation bindings")
    if not set(obligation.sources) <= sources.keys():
        raise ValueError(f"{key} omits required implementation anchors")
    for name, digest in sources.items():
        if name not in hashes or digest != hashes[name]:
            raise ValueError(f"{key} has a missing or stale source binding for {name}")


def counterexample_checks(obligations, proofs, counterproofs, contracts, scope):
    counterexamples = {key: lane for key, lane in RECORDED_WITNESSES.items()
                       if not scope or in_tcg_scope(key, scope, "obligations", "ids")}
    for key in counterproofs:
        contract = contracts.get(key)
        if contract is None or not contract["statement"].strip().startswith("¬"):
            raise ValueError(f"{key} requires an independent negated contract")
        # A previously recorded witness may have a separate replay driver.
        counterexamples.setdefault(key, contract["check"])
    for key, lane in counterexamples.items():
        if key not in obligations or lane not in CHECKS:
            raise ValueError(f"counterexample references unknown obligation or check: {key}")
        if key in proofs:
            raise ValueError(f"cannot submit a proof for known counterexample {key}")
    return counterexamples


def check_axioms(output, names):
    for name in names:
        axiom_line = re.search(
            rf"(?m)^'{re.escape(name)}' "
            r"(does not depend on any axioms|depends on axioms: \[([^\]]*)\])$",
            output,
        )
        if not axiom_line:
            raise ValueError(f"missing Lean axiom output for {name}")
        if axiom_line[2]:
            unexpected = {item.strip() for item in axiom_line[2].split(",")} - LEAN_AXIOMS
            if unexpected:
                raise ValueError(f"{name} uses unexpected axioms {', '.join(sorted(unexpected))}")


def proof_claims(records):
    lean_code = ["set_option warningAsError true"]
    names = []
    for index, (contract, proof) in enumerate(records):
        name = f"ia64_coverage_{index}"
        names.append(name)
        lean_code.append(f"theorem {name} : {contract['statement']} := @{proof['theorem']}")
        lean_code.append(f"#print axioms {name}")
    return names, "\n".join(lean_code)


def verify_proofs(records):
    modules = {}
    for contract, proof in records:
        modules.setdefault(contract["module"], []).append((contract, proof))
    build = sorted(modules.keys() - GENERATED_MODULES.keys())
    if build:
        check_command(["lake", "build", *(f"+{module}" for module in build)])
    with tempfile.TemporaryDirectory(prefix="ia64-coverage-") as temporary:
        path = Path(temporary) / "Verify.lean"
        for module, claims in modules.items():
            check = GENERATED_MODULES.get(module)
            if check:
                check_command([sys.executable, CHECKS[check], "--emit-lean", str(path)])
                source = path.read_text(encoding="utf-8")
            else:
                source = f"import {module}\n"
            names, lean_code = proof_claims(claims)
            path.write_text(f"{source}\n{lean_code}\n", encoding="utf-8")
            lean = ["lake", "env", "lean"]
            if check == "full-decode":
                lean += ["-s", "32768"]
            output = check_command([*lean, str(path)], capture=True)
            check_axioms(output, names)
    if records:
        print(f"PASS {len(records)} independently typed proof bindings", flush=True)


def describe_focused_coverage(obligations, scope, show):
    descriptions = {}
    for key, (lane, detail) in FOCUSED_SCOPES.items():
        if scope and not in_tcg_inventory(key, scope):
            continue
        if key not in obligations or lane not in CHECKS:
            raise ValueError(f"unknown focused scope or check for {key}")
        descriptions[key] = [f"{lane}: {detail}"]
    for name, category in FOCUSED_REPORTS.items():
        if scope and name in SUPPLEMENTARY_CHECKS:
            continue
        if not any(prefix.startswith(category) or category.startswith(prefix) for prefix in show):
            continue
        output = check_command([sys.executable, CHECKS[name], "--coverage"], capture=True)
        coverage = json.loads(output)
        if not isinstance(coverage, dict):
            raise TypeError(f"{name} returned invalid focused coverage")
        for key, description in coverage.items():
            if scope and not in_tcg_inventory(key, scope):
                continue
            if (not key.startswith(category) or key not in obligations or
                    not isinstance(description, str) or not description):
                raise ValueError(f"{name} returned invalid focused coverage for {key}")
            if any(key.startswith(prefix) for prefix in show):
                descriptions.setdefault(key, []).append(f"{name}: {description}")
    return descriptions


def summarize(obligations, proofs, counterproofs, counterexamples, show, scope):
    required = {key for key in obligations if not scope or
                in_tcg_scope(key, scope, "obligations", "ids")}
    counts = Counter(key.split(":")[0] for key in required)
    registered = Counter(key.split(":")[0] for key in proofs.keys() & required)
    title = "IA-64 to TCG" if scope else "Historical full IA-64 scope"
    print(f"{title}. {len(required)} obligations, {sum(registered.values())} submitted proof bindings, "
          f"{len(counterproofs)} submitted refutations, {len(counterexamples)} recorded witnesses")
    if scope:
        supporting = obligations.keys() - required
        print(f"  {len(supporting)} supporting function/helper entries, "
              f"{len(proofs.keys() & supporting)} submitted supporting bindings")
    for kind, count in sorted(counts.items()):
        print(f"  {kind:15} {registered[kind]:4}/{count:<4} bindings")
    descriptions = describe_focused_coverage(obligations, scope, show) if show else {}
    for prefix in show:
        selected = [key for key in sorted(obligations) if key.startswith(prefix)]
        if not selected:
            raise ValueError(f"no inventory entries matching {prefix!r}")
        for key in selected:
            status = "refutation" if key in counterproofs else "witness" if key in counterexamples else (
                "submitted" if key in proofs else "open"
            )
            if key not in required:
                status = "support/submitted" if key in proofs else "support"
            focused = ", ".join(descriptions.get(key, []))
            detail = obligations[key].detail
            if focused and status == "open":
                status = "open/partial"
                detail += f" ({focused})"
            print(f"{status:14} {key}\t{detail}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--inventory", action="store_true", help="list coverage categories")
    mode.add_argument("--check", action="append", choices=CHECKS, help="run one focused check")
    mode.add_argument("--require-tcg", action="store_true", help="require complete IA-64 to TCG refinement")
    mode.add_argument("--require-full", action="store_true", help="require all obligations and verify proofs")
    mode.add_argument("--verify-bindings", action="store_true", help="check submitted bindings")
    parser.add_argument("--show", action="append", default=[], metavar="PREFIX",
                        help="show obligation IDs and scopes matching PREFIX")
    parser.add_argument("--scope", choices=("tcg", "all"),
                        help="choose the default TCG or historical full inventory")
    parser.add_argument("--check-sdm", type=Path, nargs="+", metavar="DIR",
                        help="check the inventory and selected rules against pinned local PDFs")
    args = parser.parse_args()
    if args.show and not args.inventory:
        parser.error("--show requires --inventory")
    if args.require_tcg and args.scope == "all":
        parser.error("--require-tcg uses the TCG scope")
    if args.require_full and args.scope == "tcg":
        parser.error("--require-full uses the historical full scope")
    supplementary = SUPPLEMENTARY_CHECKS.intersection(args.check or [])
    if supplementary and args.scope == "tcg":
        parser.error("supplementary backend checks use --scope all")
    scope = None if args.require_full or args.scope == "all" or supplementary else tcg_scope()
    strict = args.require_tcg or args.require_full

    if args.check_sdm:
        from sdm import NATIVE_UPDATE

        checked, has_update = from_pdfs(args.check_sdm)
        catalog = read_catalog(DIRECTORY / "sdm-index.tsv", include_update=has_update, effective=False)
        if changes := snapshot_changes(catalog, checked):
            raise ValueError("SDM inventory differs:\n  " + "\n  ".join(changes))
        mode = f"four volumes and {NATIVE_UPDATE}" if has_update else "four base volumes only"
        print(f"PASS SDM inventory regeneration ({mode}, {len(checked)} entries)", flush=True)
        check_toolchain()
        manual_inputs = [argument for directory in args.check_sdm
                         for argument in ("--sdm-dir", str(directory.resolve()))]
        check_command([sys.executable, "Manual/check.py", *manual_inputs])
        if not (args.inventory or strict or args.check or args.verify_bindings):
            return 0

    obligations, paths = load_inventory(scope=scope)
    previous = read_snapshot(DIRECTORY / "sources.sha256")
    if scope:
        anchors = {path for entry in obligations.values() for path in entry.sources}
        paths = [path for path in paths
                 if path in anchors or in_tcg_scope(path, scope, "sources", "paths")]
        previous = {path: digest for path, digest in previous.items()
                    if path in anchors or in_tcg_scope(path, scope, "sources", "paths")}
    hashes = fingerprints(REPOSITORY, paths)
    changes = snapshot_changes(previous, hashes)
    if changes:
        print("Implementation inventory needs review", file=sys.stderr)
        for change in changes:
            print(f"  {change}", file=sys.stderr)

    proofs = read_registry("proofs.toml", "proof")
    counterproofs = read_registry("proofs.toml", "counterexample", optional=True)
    contracts = read_registry("contracts.toml", "contract")
    if scope:
        proofs = {key: proof for key, proof in proofs.items()
                  if in_tcg_inventory(key, scope)}
        counterproofs = {key: proof for key, proof in counterproofs.items()
                         if in_tcg_scope(key, scope, "obligations", "ids")}
        contracts = {key: contract for key, contract in contracts.items()
                     if in_tcg_inventory(key, scope)}
    for key, contract in contracts.items():
        if key not in obligations:
            raise ValueError(f"unknown obligation {key} in contracts.toml")
        validate_contract(key, contract)
    missing = check_ledger(obligations, proofs)
    if scope:
        missing = [key for key in missing if in_tcg_scope(key, scope, "obligations", "ids")]
    counterexamples = counterexample_checks(obligations, proofs, counterproofs, contracts, scope)
    summarize(obligations, proofs, counterproofs, counterexamples, args.show, scope)
    if args.inventory:
        return bool(changes)
    selected = args.check or [name for name in CHECKS
                              if not scope or name not in SUPPLEMENTARY_CHECKS]
    records = []
    for key, proof in proofs.items():
        if args.require_tcg and not in_tcg_scope(key, scope, "obligations", "ids"):
            continue
        contract = contracts.get(key)
        validate_proof(key, obligations[key], proof, contract, hashes)
        if contract["check"] in selected:
            records.append((contract, proof))
    refutations = []
    for key, proof in counterproofs.items():
        contract = contracts.get(key)
        validate_proof(key, obligations[key], proof, contract, hashes)
        if contract["check"] in selected:
            refutations.append((contract, proof))

    if args.verify_bindings:
        if not (records or refutations):
            raise ValueError("no proof bindings have been submitted")
        selected = sorted({contract["check"] for contract, _ in records + refutations}
                          | {counterexamples[key] for key in counterproofs})
    elif args.require_full and (changes or missing):
        selected = sorted({contract["check"] for contract, _ in records + refutations}
                          | set(counterexamples.values()))
    verify_proofs(run_checks(selected, counterexamples, records + refutations))
    if refutations:
        print(f"Checked {len(refutations)} negated scoped properties.", flush=True)
    if strict:
        subject = "IA-64 to TCG verification" if scope else "full IA-64 verification"
        if changes or missing:
            print(f"INCOMPLETE {subject}. {len(missing) - len(refutations)} unresolved obligations.",
                  file=sys.stderr)
            for key in sorted(counterexamples):
                verdict = "VIOLATED" if key in counterproofs else "WITNESS"
                print(f"{verdict} {key}", file=sys.stderr)
            print("Run --inventory --show PREFIX to list them.", file=sys.stderr)
            return 1
        print(f"PASS {subject}")
    return 0


if __name__ == "__main__":
    try:
        sys.exit(main())
    except (KeyError, OSError, TypeError, ValueError) as error:
        print(f"IA-64 verification failed: {error}", file=sys.stderr)
        sys.exit(1)
