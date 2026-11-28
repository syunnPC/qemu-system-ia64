# SPDX-License-Identifier: GPL-2.0-or-later
"""Regression tests for the inputs and completeness gate."""

import io
import subprocess
import sys
import tempfile
import tomllib
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path
from unittest.mock import patch

from inventory import Obligation, c_functions, implementation_paths, in_tcg_scope, opcode_rows, tcg_scope
from verify import (
    CHECKS,
    DIRECTORY,
    SUPPLEMENTARY_CHECKS,
    check_ledger,
    counterexample_checks,
    describe_focused_coverage,
    main,
    proof_claims,
    read_registry,
    run_checks,
    snapshot_changes,
    verify_proofs,
)


class InventoryTests(unittest.TestCase):
    def test_c_definitions(self):
        source = """
static unsigned first(int (*fn)(int))
{
    /* unsigned fake(void) { } */
    if (fn(1)) {
        return 3;
    }
}
static void declaration(void);
#define FAKE() void macro(void) { }
unsigned
second(unsigned a)
{
    return a;
}
"""
        self.assertEqual(c_functions(source), {"first", "second"})

    def test_comments_and_literals_cannot_hide_functions(self):
        source = """
/* A caller's comment has an apostrophe. */
int first(void)
{
    const char *markers = "/* not a C comment //";
    return '\\'';
}
/* The parser shouldn't consume the intervening definition. */
const char *second(void)
{
    return "*/ not a terminator";
}
"""
        self.assertEqual(c_functions(source), {"first", "second"})

    def test_cpp_named_jit_callables(self):
        source = """
class Mapper final : public Base
{
  public:
    Mapper(int size) : Base(size) { }
    void reserve(int size) override { }
};
Expected<std::unique_ptr<Mapper>>
create_mapper()
{
    return std::make_unique<Mapper>(0);
}
"""
        self.assertEqual(c_functions(source),
                         {"Mapper::Mapper", "Mapper::reserve", "create_mapper"})

    def test_opcodes_reject_unrecognized_rows(self):
        with tempfile.TemporaryDirectory() as temp:
            path = Path(temp) / "opcode.inc"
            path.write_text(
                "IA64_OPCODE(ADD, INTEGER, true, SOURCES_R2_R3)\n"
                "IA64_OPCODE(SUB, INTEGER, true)\n",
                encoding="utf-8",
            )
            with self.assertRaisesRegex(ValueError, "unrecognized"):
                opcode_rows(path)

    def test_novel_input_cannot_inherit_coverage(self):
        old = {"target/ia64/old.c": "old", "target/ia64/changed.h": "before"}
        new = {"target/ia64/new.c": "new", "target/ia64/changed.h": "after"}
        self.assertEqual(
            snapshot_changes(old, new),
            ["modified target/ia64/changed.h", "added target/ia64/new.c",
             "removed target/ia64/old.c"],
        )

    def test_shared_tcg_sources_are_discovered_without_native_backends(self):
        selected = {
            "fpu/softfloat.c", "fpu/softfloat-parts-new.c.inc",
            "include/fpu/softfloat.h", "include/tcg/tcg.h", "include/tcg/tcg-op-common.h",
            "include/tcg/tcg-opc.h", "include/tcg/tcg-op-mem.h",
            "include/tcg/tcg-op-new.h", "tcg/tcg-internal.h", "tcg/tcg-op-gvec.c",
            "tcg/x86_64/tcg-target-mo.h", "tcg/aarch64/tcg-target-mo.h",
        }
        native = {"tcg/x86_64/tcg-target.c.inc", "tcg/aarch64/tcg-target.h"}
        with tempfile.TemporaryDirectory() as temp:
            root = Path(temp)
            for name in selected | native | {"target/ia64/meson.build"}:
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.touch()
            paths = set(implementation_paths(root, include_backends=False))
        self.assertFalse(selected - paths)
        self.assertTrue(native.isdisjoint(paths))
        scope = tcg_scope()
        self.assertTrue(all(in_tcg_scope(path, scope, "sources", "paths") for path in selected))
        self.assertFalse(any(in_tcg_scope(path, scope, "sources", "paths") for path in native))

    def test_incomplete_tcg_runs_all_applicable_checks(self):
        with (patch.object(sys, "argv", ["verify.py", "--require-tcg"]),
              patch("verify.load_inventory", return_value=({"tcg:ADD": Obligation("ADD")}, [])),
              patch("verify.read_snapshot", return_value={}),
              patch("verify.read_registry", return_value={}),
              patch("verify.RECORDED_WITNESSES", {}),
              patch("verify.verify_proofs"),
              patch("verify.run_checks") as run,
              redirect_stdout(io.StringIO()), redirect_stderr(io.StringIO())):
            self.assertEqual(main(), 1)
        run.assert_called_once_with([key for key in CHECKS if key not in SUPPLEMENTARY_CHECKS], {}, [])

    def test_full_decoder_checks_focused_run_and_binding_types_together(self):
        first = ({"module": "FullDecode.Implementation", "statement": "True"},
                 {"theorem": "observed"})
        second = ({"module": "FullDecode.Implementation", "statement": "¬ False"},
                  {"theorem": "refuted"})
        static = ({"module": "Other", "statement": "True"},
                  {"theorem": "True.intro"})

        def execute(command, *, capture):
            self.assertEqual(command[:3], [sys.executable, CHECKS["full-decode"], "--binding-claims"])
            self.assertTrue(capture)
            self.assertEqual(len(command), 4)
            claims = Path(command[3]).read_text(encoding="utf-8")
            self.assertIn("theorem ia64_coverage_0 : True := @observed", claims)
            self.assertIn("theorem ia64_coverage_1 : ¬ False := @refuted", claims)
            self.assertNotIn("True.intro", claims)
            with tempfile.TemporaryDirectory() as temporary:
                module = Path(temporary) / "Decode.lean"
                module.write_text("theorem observed : True := True.intro\n"
                                  "theorem refuted : ¬ False := fun h => h\n" + claims,
                                  encoding="utf-8")
                output = subprocess.run(["lake", "env", "lean", str(module)], cwd=DIRECTORY,
                                        capture_output=True, text=True, check=True).stdout
            return output + "COUNTEREXAMPLE decode:MOVL\n"

        with (patch("verify.check_toolchain"), patch("verify.check_command", side_effect=execute) as run,
              redirect_stdout(io.StringIO())):
            remaining = run_checks(["full-decode", "full-decode"],
                                   {"decode:MOVL": "full-decode"}, [first, second, static])
        self.assertEqual(remaining, [static])
        run.assert_called_once()

    def test_full_decoder_rejects_missing_replay_or_unchecked_binding(self):
        record = ({"module": "FullDecode.Implementation", "statement": "True"},
                  {"theorem": "observed"})
        good_axioms = "'ia64_coverage_0' does not depend on any axioms\n"
        witness = "COUNTEREXAMPLE decode:MOVL\n"
        forbidden = "'ia64_coverage_0' depends on axioms: [sorryAx]\n"
        examples = [
            (good_axioms, "did not reproduce decode:MOVL"),
            (witness, "missing Lean axiom output"),
            (forbidden + witness, "unexpected axioms sorryAx"),
        ]
        for output, message in examples:
            with (self.subTest(message=message), patch("verify.check_toolchain"),
                  patch("verify.check_command", return_value=output), redirect_stdout(io.StringIO())):
                with self.assertRaisesRegex(ValueError, message):
                    run_checks(["full-decode"], {"decode:MOVL": "full-decode"}, [record])

    def test_full_gate_rejects_empty_or_unknown_scope(self):
        with self.assertRaisesRegex(ValueError, "empty obligation"):
            check_ledger({}, {})
        with self.assertRaisesRegex(ValueError, "unknown obligation"):
            check_ledger({"expected": None}, {"stale": None})
        self.assertEqual(check_ledger({"expected": None}, {}), ["expected"])

    def test_trivial_theorem_cannot_replace_the_memory_contract(self):
        contracts = tomllib.loads(
            (DIRECTORY / "contracts.toml").read_text(encoding="utf-8")
        )["contract"]
        contract = contracts["function:target/ia64/arch/memory.c:ia64_memop_size"]
        _, claims = proof_claims([(contract, {"theorem": "True.intro"})])
        with tempfile.TemporaryDirectory() as temp:
            module = Path(temp) / "MemoryProof.lean"
            subprocess.run(
                [sys.executable, "Memory/check.py", "--emit-lean", str(module)],
                cwd=DIRECTORY, check=True,
            )
            source = module.read_text(encoding="utf-8")
            module.write_text(f"{source}\n{claims}\n", encoding="utf-8")
            result = subprocess.run(
                ["lake", "env", "lean", str(module)], cwd=DIRECTORY,
                capture_output=True, text=True, check=False,
            )
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("type mismatch", result.stdout)

    def test_registry_can_import_a_static_lake_module(self):
        verify_proofs([(
            {"module": "Memory.Fused",
             "statement": "∀ v : FusedRelease.Visibility, "
                          "FusedRelease.guestOrdering v → "
                          "¬FusedRelease.messagePassingFailure v"},
            {"theorem": "FusedRelease.guest_forbids_messagePassingFailure"},
        )])

    def test_registry_resolves_shared_source_hashes_and_rejects_duplicates(self):
        with tempfile.TemporaryDirectory() as temp:
            first = Path(temp) / "a"
            second = Path(temp) / "b"
            first.mkdir()
            second.mkdir()
            proof = (
                '[source]\n"target/ia64/cpu.h" = "' + "a" * 64 + '"\n'
                '[proof."sample"]\ntheorem = "Spec.theorem"\n'
                'sources = ["target/ia64/cpu.h"]\n'
            )
            for directory in (first, second):
                (directory / "proofs.toml").write_text(proof, encoding="utf-8")
            observed = read_registry("proofs.toml", "proof", (first,))
            self.assertEqual(
                observed["sample"]["sources"], {"target/ia64/cpu.h": "a" * 64}
            )
            with self.assertRaisesRegex(ValueError, "duplicate proof sample"):
                read_registry("proofs.toml", "proof", (first, second))

    def test_focused_reports_run_only_for_matching_inventory_prefixes(self):
        obligations = {"decode:ADD": None, "tcg:ADD": None}
        with (patch("verify.FOCUSED_SCOPES", {}),
              patch("verify.FOCUSED_REPORTS", {"integer": "tcg:", "llvm": "llvm:"}),
              patch("verify.check_command", return_value='{"tcg:ADD": "scalar slot"}') as run):
            self.assertEqual(describe_focused_coverage(obligations, None, ["decode:ADD"]), {})
            run.assert_not_called()
            self.assertEqual(
                describe_focused_coverage(obligations, None, ["tcg:ADD"]),
                {"tcg:ADD": ["integer: scalar slot"]},
            )
            run.assert_called_once_with(
                [sys.executable, "Integer/check.py", "--coverage"], capture=True,
            )

    def test_refutations_register_replay_through_their_contract(self):
        key = "decode:ST1"
        witnesses = {"ordering:fused-release-loop": "memory"}
        obligations = {name: None for name in (*witnesses, key)}
        refutations = {key: None}
        contracts = {key: {"statement": "¬ expected", "check": "full-decode"}}
        with patch("verify.RECORDED_WITNESSES", witnesses):
            self.assertEqual(
                counterexample_checks(obligations, {}, refutations, contracts, None),
                {**witnesses, key: "full-decode"},
            )
            with self.assertRaisesRegex(ValueError, "independent negated contract"):
                counterexample_checks(obligations, {}, refutations, {}, None)
            with self.assertRaisesRegex(ValueError, "cannot submit a proof"):
                counterexample_checks(obligations, {key: None}, refutations, contracts, None)

    def test_registry_rejects_an_unknown_shared_source(self):
        with tempfile.TemporaryDirectory() as temp:
            area = Path(temp)
            (area / "proofs.toml").write_text(
                '[proof."sample"]\ntheorem = "Spec.theorem"\nsources = ["missing.c"]\n',
                encoding="utf-8",
            )
            with self.assertRaisesRegex(ValueError, "undeclared source missing.c"):
                read_registry("proofs.toml", "proof", (area,))


if __name__ == "__main__":
    unittest.main()
