# SPDX-License-Identifier: GPL-2.0-or-later
"""Bind the scalar dataflow projection to the TCG cache entry/update paths."""

from hashlib import sha256
import re

from Integer import check as integer


# These bodies implement effects represented by the logical maps in
# Facts.lean.inc. Keep the mapping conditional on the recognized bodies.
FLOW_BODIES = {
    "ia64_update_nat_known": "918ae2cec105993f15c837a12cd3d5f6cfc8227e0a0f318df8d4c53c5d021266",
    "ia64_nat_after_predication": "40613a90c0670186ed426fcff258e8c2b3e376cc3be755d800eaf2248fb97cbc",
    "ia64_nat_merge": "df575dfe37f607a0cf9817174f3ba3ca157d8302ed48f317bfe3a4c9fee9df3e",
    "ia64_nat_or": "cf01e6eb8fbdf22d822bdad1a8f5bd6e1031b83a947efdab69984af8c4326fcd",
    "ia64_nat_set_known": "927ef7099a15a298c3d7f5743088c8dff3957b6e3d29cafd81ae9c60a548c274",
    "ia64_nat_result_known": "82b07c45f4790bf83273cecdc3276701d96de0141bbe5eae10dd1dfd9794e833",
    "ia64_nat_result_policy": "40fabc2156fb928f0a256d10c3f22c5502d24df73e063af8fde476743715e7bf",
    "ia64_invalidate_nat_known_for_insn": "7fc3231de0de44f1232bc7f68b818ceebfc9a50badb1d560714a8ac5e52d244c",
    "ia64_insn_may_rewrite_stacked_gr_view": "233717b072b28fe7085d7750b3532af70d727b6e80fc95c797bf7104f0f3e808",
    "ia64_insn_may_swap_banked_gr": "c1b7e976f3cc98ca9c14468f5800f221fc0f74af032431dc56c709a08036e92c",
    "ia64_insn_writes_gr_r1": "bd78788433b765d5d182dd911fff0cc99054f8a553dff2f8ee7eb8ef0622003a",
    "ia64_update_predicate_known": "1d1d365ad04c9960a27a294f5925dbf429b5ea6ff9d9e15d509b3d0070fc9493",
    "ia64_insn_writes_predicate_pair": "a559fb2b8d76e13292b9d2673ee72e0d135f64b3f489409c57bf0a650e812c33",
    "ia64_insn_predicate_write_mask": "c7330bb952bd740a0e95c17453cbc418b4253d707922f7a80ae4c9d6d4c79940",
    "ia64_invalidate_predicate_mask": "5d0365b6a4bc43245e0cf65e1ba42ce9b1a910238f1df26dc55b702cf93aae08",
    "ia64_update_rse_dirty_known": "194633551f5a08ac3bf1f6b2dd7b4f683be0fac934254dcd983524385d471bef",
    "ia64_insn_resets_rse_dirty": "a71800808e434a50a4f8a4d79f70c0fcecfc9530f08d812134d3b880ac5f0d0d",
    "ia64_insn_may_preserve_gr_destination": "6383e47eaa41671bce0db6226d3f467e817c1838c82a707c4fb578fb07cf7563",
    "ia64_rse_dirty_set_known": "b4a40a059536a807ab2f782aef8a6f970359be25232dae259a57d7e5ad78c11e",
    "ia64_current_insn_definitely_executes": "1d0c49b122179189f145002ccd7ae60748ab7b72baa8e0bc1a3bc8874175c6e6",
    "ia64_update_cached_register_state": "b2e1c0f00c5db6ae32e840649e327a5990787555ebd31f3cbdd625fb8ba2bc9a",
    "ia64_insn_may_modify_cfm_sof": "777af8721650cfa80e18fb51966b5b81cb553185cbbcabbff67e8e699bf5d532",
    "ia64_tr_init_disas_context": "11c36accd09726ea291b21b14bb28b5aaa026439508a4f5aeec8e472065eb5f3",
    "ia64_tr_translate_insn": "20bcdf87f0506b2138d5b738068091c1edb0625ebab566ba3c0c32dd5380cc38",
    "ia64_translate_init": "ad578135674f02e03a00335bb342e510ea5db66d17feefe9fd5484a4f0b50e63",
    "ia64_gen_goto_tb_group": "c5f02d9adf59a9366f9c908fc61e58339673fb74aa6c53d9ec92e1fbe328042c",
}


def check_flows(translator, cpu, headers, opcode_source, forms):
    for name, digest in FLOW_BODIES.items():
        actual = sha256(integer.compact(integer.function(translator, name)).encode()).hexdigest()
        integer.require(actual == digest, f"Changed scalar fact-cache update {name}")

    opcode_table = dict(re.findall(r"IA64_OPCODE\((\w+),\s*INTEGER,\s*true,\s*(\w+)\)",
                                  opcode_source))
    policies = {
        frozenset(): "CLEAR",
        frozenset({"x"}): "SOURCE_R2",
        frozenset({"y"}): "SOURCE_R3",
        frozenset({"x", "y"}): "SOURCES_R2_R3",
    }
    for opcode, operands in forms.items():
        uses = set(operands.split()) & {"x", "y"}
        integer.require(opcode_table.get(opcode) == policies[frozenset(uses)],
                        f"Scalar flow recipe changed for {opcode}")
    for selective in ("ia64_insn_may_rewrite_stacked_gr_view", "ia64_insn_may_swap_banked_gr",
                      "ia64_insn_predicate_write_mask", "ia64_insn_resets_rse_dirty",
                      "ia64_insn_may_preserve_gr_destination", "ia64_insn_may_modify_cfm_sof"):
        body = integer.function(translator, selective)
        cases = set(re.findall(r"case IA64_OP_(\w+):", body))
        integer.require(not cases.intersection(forms),
                        f"Scalar flow must account for {selective} on {cases.intersection(forms)}")
    bindings = integer.compact(integer.function(translator, "ia64_translate_code"))
    integer.require(".init_disas_context=ia64_tr_init_disas_context" in bindings
                    and ".translate_insn=ia64_tr_translate_insn" in bindings,
                    "Replaced the native scalar translation callbacks")
    integer.require(re.search(r"\.get_tb_cpu_state\s*=\s*ia64_get_tb_cpu_state,", cpu),
                    "Replaced the IA-64 TB-state selector callback")
    definitions = re.findall(r"^#define\s+(IA64_TB_FLAG_\w+)\s+([^\n]+)", headers, re.M)
    flag_digest = sha256(integer.compact(str(definitions)).encode()).hexdigest()
    integer.require(flag_digest == "d9a239a46d74eaad7b9296ab2c6c7a1dee60f0317f4b9e8372c767d3fad531c5",
                    "Changed IA-64 TB key bit assignments")
    cpu_state = integer.function(cpu, "ia64_get_tb_cpu_state")
    chosen = re.search(
        r"if\s*\(likely\(\(cpu->env\.nat\[0\]\s*([|&^])\s*cpu->env\.nat\[1\]\)"
        r"\s*(==|!=)\s*0\)\)\s*\{\s*flags\s*\|=\s*IA64_TB_FLAG_NAT_CLEAR;\s*\}",
        cpu_state,
    )
    integer.require(chosen is not None, "Unrecognized native TB NaT-key selection")
    # Canonicalize the two operators translated into Lean. The rest of the
    # function pins the native branch, other flag assignments and return.
    test = cpu_state[chosen.start():chosen.end()]
    canonical = cpu_state[:chosen.start()] + test.replace(chosen[1], "|", 1).replace(chosen[2], "==", 1)
    canonical += cpu_state[chosen.end():]
    digest = sha256(integer.compact(canonical).encode()).hexdigest()
    integer.require(digest == "333500e90ba991898882b76a557eb39fbb3bfb486aba1e54d17b4756132edaa6",
                    "Changed native TB-state selector outside the lifted NaT test")
    operator = {"|": "|||", "&": "&&&", "^": "^^^"}[chosen[1]]
    return f"""namespace IA64Tcg
def emittedLookupNatClear (s : State α) : Bool :=
  (s.natWord 0 {operator} s.natWord 1) {chosen[2]} 0#64

theorem emittedLookupNatClear_eq (s : State α) :
    emittedLookupNatClear s = lookupNatClear s := by
  simp only [emittedLookupNatClear, lookupNatClear]
  try rfl
  all_goals bv_decide

theorem emittedLookupTraceFacts {{start finish : State α}} {{final : Flow}}
    (trace : ScalarTrace start (newFlow (emittedLookupNatClear start)) finish final) :
    Flow.Sound finish final := by
  rw [emittedLookupNatClear_eq] at trace
  exact lookupTraceFacts trace

-- The guarded goto-tb arm rejoins dispatch when the actual OR is nonzero
-- for a clear-key TB or zero for a non-clear-key TB.
def emittedGuardUsesLookup (key : Bool) (s : State α) : Bool :=
  if key then (s.natWord 0 ||| s.natWord 1) != 0#64
  else (s.natWord 0 ||| s.natWord 1) == 0#64

theorem testedDirectEdgeFacts (s : State α) (key : Bool)
    (edge : emittedGuardUsesLookup key s = false) : Flow.Sound s (newFlow key) := by
  apply guardedEntry_sound
  cases key <;> simpa [emittedGuardUsesLookup, guardedDirectEdge, lookupNatClear] using edge

theorem forgottenBoundaryFacts (s : State α) : Flow.Sound s (newFlow false) :=
  freshEntry_sound s false (by simp)

end IA64Tcg
"""
