# SPDX-License-Identifier: GPL-2.0-or-later

from hashlib import sha256
from pathlib import Path
import re

from Integer.check import cases, compact, function, matching, require, stripped, switches
from SlotControl import bundle, check
from SlotControl.records import CALLERS
from SlotControl.reify import fixed, numeric_macro, recognize

TRANSLATOR = "target/ia64/translate/translate.c"
TCG_TRANSLATOR = "accel/tcg/translator.c"
HERE = Path(__file__).resolve().parent
SOURCE_PINS = {
    "ia64_gen_complete_branch": "96889394dcb49845e0c8fe046daea578c44009a284ddd55772eab483792aa108",
    "ia64_gen_completed_direct_branch": "cadaaf30f5c334433babb798d85fd297b8695b4e7eb5183bf9d767be714b2b63",
    "ia64_gen_goto_completed": "078cb13732da7fec3dff025d5e2fbbcfd21c0048b5a0c9acfd93c90c05c52c50",
    "ia64_gen_lookup_or_exit": "5fd471065742e54f8141895323e6b68c5f44b91f5db501f90af7d00450b973a4",
    "ia64_insn_generator": "647af414fdea11fd4290cf0aafe3264da1bc524c1be77de5f382127727e11aa6",
    "ia64_gen_dispatch": "3bc4394b720eecbf04ab698867bd569d144c6b7f4d41adce9a1d07e495e2755b",
    "ia64_gen_prepare_insn": "0e4c08174cef80aa042f7113ee5322250e92b3da80876b4cc48dcae3062ae703",
    "ia64_gen_predicated_integer": "53f09bc44584f64be7304ead42ece8982c0fe9a4f5413c659fcbd2fcd9d432d7",
    "ia64_predicate_known": "9df0ecddb5214a7a463839972a3cb5ffe987ad8cfca97b13dd5f7d9cc9e10a03",
    "ia64_predicate_nullifies_insn": "1daeba7f84778ca59f7d61d05a96ee6d4c5df4ff8df33746e2c46ac49e8b6faa",
    "ia64_current_insn_cannot_execute": "61006930746a864aeaa5283e4dc9d318b6685350e980d33d375387fafe06a6b8",
    "ia64_gen_predicate_end": "8a56c902008952a0a4f85b63798184dc69f2456295c8843a29cc842b7bdcb73f",
    "ia64_insn_is_empty_hint": "0281e83828b3593d20de4957132bb2941f25479da3a7f121af9bfeee212e6838",
    "ia64_analyze_self_counted_loop": "19f85170ddbb2cb196909d4c049af609e8bc34ae4cd8c76991f03f6bdef4535e",
    "ia64_prepare_self_counted_loop": "7edbe09aa728a0627867c677ec2aecb4ce66041d48d023d920ea129a82f4bf76",
    "ia64_is_fill_st1_postinc": "e30485a68f768bb05066970095625466d219d8d828b0761f15c06d9d1bf6d43a",
    "ia64_gen_save_fault_slot_for_exit": "3c848fdca3220a266132726b31f86115516d60486b024f7cdaea175c9a341a73",
    "ia64_gen_set_fault_slot": "0e7e53185772c27a8081b79722eb19a589a275fc076b9c9b7087c7018dca0cc0",
    "ia64_all_gr_nats_known_clear": "d9b6d27a2a357c31d9130ad69fa0f2d49234cd8e4cb49ccfa0de7262b4f0f9c5",
    "ia64_insn_may_set_fault_suppression": "4c427f0aa5b536ea7022ae0d8c6e77174ca86c46e3a4a954078462ae8ecf05d7",
    "ia64_insn_may_set_psr_ic": "266de440d898b56d28d9f3153e05d37ac28b46ee7816a697bed3beafdc47cba1",
    "ia64_insn_may_modify_psr_ic": "cc19e0571fa0247f693c5123aa07d425cda599abc0aa18cdeafb69317caa5565",
    "ia64_insn_may_modify_native_trap_psr": "1eb9b45aa73f69e9bcb8c68603b5267efcd9956edcce6f86b96661608e4d6f84",
    "ia64_insn_may_modify_psr_ri": "82d0fd9eeb8916728540acda46aae41af5e22dab656d9d973af56d66afd4cc1a",
    "ia64_insn_must_start_group": "e6a9778cf2790e83a486dbc1b9f58121e1bd2fc7b4be80cd4a785b74b5ce1722",
    "ia64_insn_must_end_group": "2c42f939cf9a45b413f750acbf9317b2db59c706e7acb837bba590f65463c52b",
    "ia64_insn_requires_slot2": "09698a1915bb7dd5e63fa04569edf89310894e6c8a599ed65c95e9f6dad04976",
    "ia64_insn_has_invalid_fp_target": "45c0306fd45d9f1874843c03e6a961af33c53e352d6c2f8597629a03bdd51be8",
    "ia64_insn_has_illegal_register": "4efa09e06e7b784d8284540fc5dd9e18bfb7d05e31ae289f3d8cd7e220223449",
    "ia64_insn_has_reserved_ar": "77ae67d6e442b84ec6c568dd09fb882ac68faaf282276f4a167b66b3bbdeb5b6",
    "ia64_insn_has_reserved_mask_field": "f5ac762856557dedc8a216db747d9e76ba97e2fe68e499374af09731c1cb297e",
    "ia64_insn_psr_ic_must_be_clear": "79b33c238d4dc58bd8111efe4323a95a1aaf7383249625833c1dc9017a01309d",
    "ia64_insn_is_privileged": "38dc979633c187b6b6410a89988bb76e18ee51bdafecfb983a6c27ee901a39cd",
    "ia64_insn_writes_gr_r1": "bd78788433b765d5d182dd911fff0cc99054f8a553dff2f8ee7eb8ef0622003a",
    "ia64_insn_required_integer_feature": "fb503583563814860d372f87a8a937ee3b6266c2b5f51884b642ca30f5fc7965",
    "ia64_insn_needs_16byte_atomics": "423c5dd7cd14046830cd6858252cb66d54b97f64f4da4d1e7762a99af9277303",
    "ia64_gen_check_fp_pair_bank": "a232a426f3fac170472735b1a4b57e81015ce778234b4ea6ff124955d27ec9dd",
    "ia64_load_base_update_has_same_target": "5350f53841cc9db7275023cd9a6e8369b79a6acddef1b4991cd3027cbddc2402",
    "ia64_compare_has_equal_targets": "2b32a72af4bc2466047ac4ef6969a0fff9c47275e7b2ce5772a6d6e82d230cfb",
    "ia64_insn_fp_read_sets": "32fdfb44f4ba8b3e9ddac789eab820ea2c294399bf33aedb94381f3e82e4ef75",
    "ia64_insn_fp_write_sets": "8d22cfe1fd2a2b2d2d0221de8d36a7c408d8fb0c92b1b1692e7b19333fa17506",
    "ia64_gen_sync_rotating_fr": "fe994be2fb1fe76089c47f96b90d10bc38739067c55531866cb3b4057782a246",
    "ia64_gen_check_disabled_fp": "5a6d974dab225fecdae9cd53595ac6ff3a57c3291c7969924f88a705b5a3a28b",
}


def read(root, path):
    return stripped((root / path).read_text(encoding="utf-8"))


def definition(source, name):
    found = re.search(rf"^(?:static\s+)?\w+\s+\*?\s*{name}\s*\([^;{{}}]+\)\s*\{{",
                      source, re.MULTILINE)
    require(found is not None, f"missing function definition {name}")
    start = found.end() - 1
    return source[start + 1:matching(source, start, "{", "}")]


def lift_suppression(root):
    translator = read(root, TRANSLATOR)
    fixed(translator, "ia64_tr_translate_insn", CALLERS["ia64_tr_translate_insn"])
    fixed(translator, "ia64_gen_insn", CALLERS["ia64_gen_insn"])
    header = read(root, "target/ia64/cpu.h")
    mask = re.search(r"(?m)^#define IA64_PSR_FAULT_SUPPRESS_MASK \\\s*\(([^\n]+)\)$", header)
    require(mask is not None and re.fullmatch(r"IA64_PSR_\w+(?: \| IA64_PSR_\w+)*", mask[1]),
            "unexpected instruction fault-suppression mask")
    bits = [numeric_macro(header, name, r"\(1ULL << (\d+)\)") for name in mask[1].split(" | ")]
    adapter = read(root, "target/ia64/helper/helper-system.c")
    recognize(adapter, "helper_clear_psr_fault_suppression",
              "ia64_system_clear_psr_fault_suppression(env);")
    system = read(root, "target/ia64/arch/system.c")
    recognize(system, "ia64_system_clear_psr_fault_suppression", """
        uint64_t old_mask = env->exception_state.psr_suppression_before_insn &
                            IA64_PSR_FAULT_SUPPRESS_MASK;
        uint64_t clear_mask = env->psr & old_mask;
        if (clear_mask) {
            ia64_set_psr(env, env->psr & ~clear_mask);
        }
        if (old_mask & (IA64_PSR_DA | IA64_PSR_IA)) {
            ia64_flush_suppressed_tlb(env);
        }
        env->exception_state.psr_suppression_before_insn = 0;
    """)
    recognize(system, "ia64_set_psr", """
        uint64_t changed = env->psr ^ value;
        if (changed & IA64_PSR_IC) {
            env->exception_state.psr_ic_inflight = true;
        }
        if (!(value & IA64_PSR_I) || (value & IA64_PSR_IS)) {
            env->exception_state.psr_i_deferred = false;
        } else if ((changed & IA64_PSR_I) && env->exception_state.psr_ic_inflight) {
            env->exception_state.psr_i_deferred = true;
        }
        if (changed & IA64_PSR_BN) {
            ia64_swap_banked_gr(env);
        }
        env->psr = value;
        if (changed & (IA64_PSR_I | IA64_PSR_IC | IA64_PSR_MC | IA64_PSR_IS)) {
            ia64_sapic_update_interrupt(env);
        }
    """)
    cpu = read(root, "target/ia64/cpu.c")
    recognize(cpu, "ia64_flush_suppressed_tlb", """
        CPUState *cs = env_cpu(env);
        uint8_t i;
        if (env->exception_state.suppressed_tlb_overflow) {
            tlb_flush(cs);
        } else {
            for (i = 0; i < env->exception_state.suppressed_tlb_count; i++) {
                tlb_flush_page_by_mmuidx(
                    cs, env->exception_state.suppressed_tlb_pages[i],
                    env->exception_state.suppressed_tlb_idxmaps[i]);
            }
        }
        env->exception_state.suppressed_tlb_count = 0;
        env->exception_state.suppressed_tlb_overflow = false;
    """)
    terms = " ||| ".join(f"(1#64 <<< {bit}#64)" for bit in bits)
    return f"def IA64Branch.B1.cSuppressionMask : IA64Slot.Word := {terms}"


def lift(root, target_exp, predicate_mutation=False):
    translator = read(root, TRANSLATOR)
    header = read(root, "target/ia64/cpu.h")
    for name, digest in CALLERS.items():
        fixed(translator, name, digest)
    for name, digest in SOURCE_PINS.items():
        body = definition(translator, name)
        require(sha256(compact(body).encode()).hexdigest() == digest,
                f"changed BR_COND control flow in {name}")
    table = compact(read(root, "target/ia64/decode/opcode.inc"))
    require("IA64_OPCODE(BR_COND,BRANCH,false,UNKNOWN)" in table,
            "changed BR_COND generator or register effects")
    operands = compact(read(root, "target/ia64/decode/operand.h"))
    require("IA64_OPERAND_LAYOUT_ASSERT(IA64BranchOperands,displacement);" in operands
            and "_Static_assert(offsetof(IA64DecodedOperands,imm)==16," in operands,
            "changed BR_COND displacement operand ABI")
    body = function(read(root, "target/ia64/translate/gen-branch.c"), "ia64_gen_branch")
    (begin, _, dispatch), = switches(body)
    require(compact(body[:begin]) == "constIA64BranchOperands*op=&insn->operands.branch;"
            "constuint64_tnext_ip=insn->address+16;switch(insn->opcode)",
            "changed branch dispatch prologue")
    case = cases(dispatch)["BR_COND"]
    target_field = r"\.direct_target\s*=\s*([^,]+),"
    require(compact(re.sub(target_field, ".direct_target = @target@,", case))
            == "returnia64_gen_complete_branch(ctx,skip,&(IA64BranchCompletion){"
            ".target_kind=IA64_BRANCH_TARGET_DIRECT,.direct_target=@target@,"
            ".completed_ip=insn->address,.record_iipa=record_iipa,"
            ".track_psr_suppression=track_psr_suppression,});",
            "changed BR_COND direct completion emitter")
    target = target_exp(case, target_field)
    if predicate_mutation:
        old = "TCG_COND_EQ, qp_value, 0, skip"
        require(old in translator, "missing predicate mutation site")
        translator = translator.replace(old, old.replace("TCG_COND_EQ", "TCG_COND_NE"), 1)
    skip = recognize(translator, "ia64_gen_predicate_skip", """
        TCGLabel *skip;
        if (insn->qp == 0 || insn->opcode == IA64_OP_BR_WEXIT ||
            insn->opcode == IA64_OP_BR_WTOP) {
            return NULL;
        }
        if (ctx->reg.current_qp_known && ctx->reg.current_qp_value) {
            return NULL;
        }
        skip = gen_new_label();
        if (ctx->reg.current_qp_known) {
            tcg_gen_br(skip);
        } else {
            tcg_gen_brcondi_i64(TCG_COND_@compare@, qp_value, 0, skip);
        }
        return skip;
    """, compare=r"(?:EQ|NE)")
    comparison = "!=" if skip["compare"] == "EQ" else "=="
    trap_flag = numeric_macro(header, "IA64_NATIVE_TRAP_TAKEN", r"\(1U << (\d+)\)")
    tcg_translator = read(root, TCG_TRANSLATOR)
    recognize(tcg_translator, "translator_use_goto_tb", """
        if (tb_cflags(db->tb) & CF_NO_GOTO_TB) {
            return false;
        }
        return translator_is_same_page(db, dest);
    """)
    recognize(tcg_translator, "translator_is_same_page", """
        return ((addr ^ db->pc_first) & TARGET_PAGE_MASK) == 0;
    """)
    page_bits = numeric_macro(read(root, "target/ia64/cpu-param.h"), "TARGET_PAGE_BITS", r"(\d+)")
    control = check.generate(root)
    return "\n\n".join((
        control.replace("import Std.Tactic.BVDecide\n", ""),
        bundle.lift_decoder(root, ("BR_COND",)),
        (HERE / "B1.lean.inc").read_text(encoding="utf-8"),
        f"""namespace IA64Branch.B1
open IA64Slot

def cTake (c : Context) (prs : BitVec 6 → Word) (qp : BitVec 6) : Bool :=
  if c.predZero.getLsbD qp.toNat then false
  else if c.predOne.getLsbD qp.toNat then true
  else if qp = 0 then true
  else prs qp {comparison} 0

def cTarget (ip displacement : Word) : Word := {target}

def cNativeTaken (c : Context) (before : State) (target source : Word) (slot : Slot) : Effect :=
  if c.nativeDynamic || c.singleStep || c.takenBranch || !implemented c target then
    .native ⟨⟨target, 0⟩, ⟨source, slot.number⟩,
      (slot.number &&& 3) <<< 2#64, 1#64 <<< {trap_flag}#64, 0, before.psr⟩
  else .nop

def cTransfer (c : Context) (target : Word) : Transfer :=
  let eligible := c.chainSlot < 2 && c.natSafe && !c.deferIRQ && !c.chainDisabled &&
    ((target ^^^ c.firstIp) &&& ~~~((1#64 <<< {page_bits}#64) - 1) == 0)
  let keyMatches := c.natsKnownClear || (c.anyNat != c.natClearKey)
  if eligible && keyMatches then .chain c.chainSlot
  else if c.deferIRQ then .mainLoop else .lookup

end IA64Branch.B1""",
        (HERE / "B1Proof.lean.inc").read_text(encoding="utf-8"),
        lift_suppression(root),
        (HERE / "Suppression.lean.inc").read_text(encoding="utf-8"),
    ))
