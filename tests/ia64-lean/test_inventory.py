# SPDX-License-Identifier: GPL-2.0-or-later
"""Regression tests for the inputs and completeness gate."""

import subprocess
import sys
import tempfile
import tomllib
import unittest
from pathlib import Path
from unittest.mock import patch

from inventory import c_functions, opcode_rows
from verify import (
    DIRECTORY,
    check_ledger,
    counterexample_checks,
    describe_focused_coverage,
    proof_claims,
    read_registry,
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
