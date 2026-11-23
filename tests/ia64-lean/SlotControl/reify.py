# SPDX-License-Identifier: GPL-2.0-or-later
"""Lift the supported slot-control emitter bodies into a small TCG IR."""

from hashlib import sha256
import re

from Decode.check import c_function
from Integer.check import compact, require


OPS = {"and": "band", "or": "bor", "xor": "bxor"}
NUM = r"[0-9]+"
BIN = "|".join(OPS)


def function(source, name):
    return c_function(source, name)[1]


def recognize(source, name, pattern, **variables):
    """Require the complete normalized form, capturing supported operands."""
    pieces = re.split(r"@([a-z_0-9]+)@", compact(pattern))
    regex = re.escape(pieces[0])
    for key, suffix in zip(pieces[1::2], pieces[2::2], strict=True):
        regex += f"(?P<{key}>{variables[key]})" + re.escape(suffix)
    found = re.fullmatch(regex, compact(function(source, name)))
    require(found is not None, f"unsupported slot-control function {name}")
    return found.groupdict()


def numeric_macro(header, name, expression):
    match = re.search(rf"(?m)^#define[ \t]+{name}[ \t]+{expression}[ \t]*$", header)
    require(match is not None, f"unsupported slot-control constant {name}")
    return int(match[1])


def fixed(source, name, digest):
    body = compact(function(source, name))
    require(sha256(body.encode()).hexdigest() == digest,
            f"slot-control caller changed in {name}; review the lifted slice")
    return body


def lift_restart(source, header):
    ri_mask = numeric_macro(header, "IA64_PSR_RI_MASK", r"\(3ULL << (\d+)\)")
    ri_shift = numeric_macro(header, "IA64_PSR_RI_SHIFT", r"(\d+)")
    clear = recognize(source, "ia64_gen_clear_ri", """
        tcg_gen_@op@i_i64(cpu_psr, cpu_psr, ~IA64_PSR_RI_MASK);
    """, op=BIN)
    tracked = recognize(source, "ia64_gen_set_ri_tracked", """
        if (ctx->restart.current_ri_known && ctx->restart.current_ri == slot) {
            return;
        }
        if (ctx->restart.current_ri_known) {
            tcg_gen_@toggle@i_i64(cpu_psr, cpu_psr,
                             (uint64_t)(ctx->restart.current_ri ^ slot)
                                 << IA64_PSR_RI_SHIFT);
        } else {
            ia64_gen_clear_ri();
            if (slot != 0) {
                tcg_gen_@merge@i_i64(cpu_psr, cpu_psr,
                                    (uint64_t)slot << IA64_PSR_RI_SHIFT);
            }
        }
        ctx->restart.current_ri = slot;
        ctx->restart.current_ri_known = true;
    """, toggle=BIN, merge=BIN)
    advance = recognize(source, "ia64_gen_advance_restart_point", """
        if (slot == @end@ || (slot == @last@ && mlx_long)) {
            ia64_gen_set_ri_tracked(ctx, @reset@);
            tcg_gen_movi_i64(cpu_ip, bundle_ip + @delta@);
        } else {
            ia64_gen_set_ri_tracked(ctx, slot + @step@);
        }
    """, end=NUM, last=NUM, reset=NUM, delta=NUM, step=NUM)
    return f"""
def emitRI (known : Option Word) (slot : Word) : Effect :=
  match known with
  | some old =>
    if old = slot then .nop
    else .write .psr (.{OPS[tracked['toggle']]} (.input .psr)
                       (.literal ((old ^^^ slot) <<< {ri_shift}#64)))
  | none =>
    let clear := Effect.write .psr (.{OPS[clear['op']]} (.input .psr)
                                   (.literal (~~~(3#64 <<< {ri_mask}#64))))
    if slot = 0#64 then clear
    else .sequence clear (.write .psr (.{OPS[tracked['merge']]} (.input .psr)
                                       (.literal (slot <<< {ri_shift}#64))))

def emitAdvance (known : Option Word) (bundle : Word) (slot : Slot) (long : Bool) : Effect :=
  if slot.number = {advance['end']} ∨ (slot.number = {advance['last']} ∧ long) then
    .sequence (emitRI known {advance['reset']}) (.write .ip (.literal (bundle + {advance['delta']})))
  else emitRI known (slot.number + {advance['step']})
"""


def lift_success(source, header):
    size = numeric_macro(header, "IA64_BUNDLE_SIZE", r"(\d+)ULL")
    ic_shift = numeric_macro(header, "IA64_PSR_IC", r"\(1ULL << (\d+)\)")
    recognize(header, "ia64_ip_bundle_addr", "return ip & IA64_IP_BUNDLE_MASK;")
    require("#define IA64_IP_BUNDLE_MASK (~(IA64_BUNDLE_SIZE - 1))" in header,
            "unsupported bundle address mask")
    success = recognize(source, "ia64_gen_note_successful_bundle", """
        if (record_iipa && (!ctx->restart.psr_ic_known || ctx->restart.psr_ic)) {
            TCGLabel *l_done = NULL;
            if (!ctx->restart.psr_ic_known) {
                TCGv_i64 collecting = tcg_temp_new_i64();
                l_done = gen_new_label();
                tcg_gen_@op@i_i64(collecting, cpu_psr, IA64_PSR_IC);
                tcg_gen_brcondi_i64(TCG_COND_@record_skip@, collecting, 0, l_done);
            }
            tcg_gen_st_i64(tcg_constant_i64(ia64_ip_bundle_addr(bundle_ip)),
                           tcg_env, offsetof(CPUIA64State, last_successful_bundle));
            if (l_done) {
                gen_set_label(l_done);
            }
        }
        if (track_psr_suppression) {
            TCGv_i64 suppression = tcg_temp_new_i64();
            TCGLabel *l_no_suppression = gen_new_label();
            tcg_gen_ld_i64(suppression, tcg_env,
                           offsetof(CPUIA64State,
                                    exception_state.psr_suppression_before_insn));
            tcg_gen_brcondi_i64(TCG_COND_@clear_skip@, suppression, 0, l_no_suppression);
            gen_helper_clear_psr_fault_suppression(tcg_env);
            gen_set_label(l_no_suppression);
        }
    """, op=BIN, record_skip=r"EQ", clear_skip=r"EQ")
    return f"""
def emitSuccess (known : Option Bool) (record track : Bool) (bundle : Word) : Effect :=
  let store := Effect.write .history (.literal (bundle &&& ~~~({size}#64 - 1)))
  let history := if record then
      match known with
      | some true => store
      | some false => .nop
      | none => .ifNonzero (.{OPS[success['op']]} (.input .psr)
                            (.literal (1#64 <<< {ic_shift}#64))) store
    else .nop
  .sequence history (if track then .ifNonzero (.input .savedSuppression) .clearSuppression else .nop)
"""


def lift_traps(source, header):
    recognize(source, "ia64_static_native_trap_needed", """
        uint32_t flags = ctx->base.tb->flags;
        if (base_isr != 0 || ctx->restart.native_trap_psr_dynamic ||
            (conditions & (IA64_NATIVE_TRAP_FP | IA64_NATIVE_TRAP_LOWER)) != 0 ||
            (flags & IA64_TB_FLAG_PSR_SS) != 0 ||
            ((conditions & IA64_NATIVE_TRAP_TAKEN) != 0 &&
             (flags & IA64_TB_FLAG_PSR_TB) != 0)) {
            return true;
        }
        if (flags & IA64_TB_FLAG_IT) {
            return !ia64_va_is_implemented(ctx->env, target_ip);
        }
        return !ia64_pa_is_implemented(ctx->env, target_ip);
    """)
    recognize(source, "ia64_gen_check_static_native_traps", """
        if (!ia64_static_native_trap_needed(ctx, target_ip, conditions, base_isr)) {
            return;
        }
        ia64_gen_check_native_traps(
            tcg_constant_i64(target_ip), target_slot, source_ip, source_slot,
            tcg_constant_i32(conditions), base_isr, ia64_gen_native_trap_psr(ctx));
    """)
    recognize(source, "ia64_gen_check_native_traps", """
        if (conditions == NULL) {
            conditions = tcg_constant_i32(0);
        }
        gen_helper_check_native_traps(
            tcg_env, target_ip, tcg_constant_i64(source_ip),
            tcg_constant_i32(IA64_NATIVE_TRAP_SLOTS(target_slot, source_slot)),
            conditions, tcg_constant_i64(base_isr), trap_psr);
    """)
    recognize(source, "ia64_gen_native_trap_psr", """
        TCGv_i64 trap_psr;
        if (!ctx->restart.native_trap_saved_psr) {
            return cpu_psr;
        }
        trap_psr = tcg_temp_new_i64();
        tcg_gen_ld_i64(trap_psr, tcg_env,
                     offsetof(CPUIA64State, exception_state.psr_before_insn));
        return trap_psr;
    """)
    recognize(source, "ia64_gen_snapshot_psr_before_insn", """
        tcg_gen_st_i64(cpu_psr, tcg_env,
                   offsetof(CPUIA64State, exception_state.psr_before_insn));
    """)
    require(re.search(
        r"#define IA64_NATIVE_TRAP_SLOTS\(target, source\)\s+\\\s+"
        r"\(\(\(target\) & 3U\) \| \(\(\(source\) & 3U\) << 2\)\)", header),
        "unsupported completion trap slot ABI")
    boundary = recognize(source, "ia64_gen_check_sequential_native_traps", """
        uint64_t target_ip = insn->address;
        uint8_t target_slot = insn->slot + @step@;
        if (insn->opcode == IA64_OP_RFI) {
            return;
        }
        if (insn->slot == @end@ || (insn->slot == @last@ && mlx_long)) {
            target_ip += @delta@;
            target_slot = @reset@;
        }
        ia64_gen_check_static_native_traps(
            insn->ctx, target_ip, target_slot, insn->address,
            mlx_long && insn->slot == @pair@ ? @src@ : insn->slot, @cond@, @isr@);
    """, step=NUM, end=NUM, last=NUM, delta=NUM, reset=NUM, pair=NUM, src=NUM,
        cond=r"0", isr=r"0")
    return f"""
def emitSnapshot : Effect := .write .savedPsr (.input .psr)

def emitNative (s : State) (saved dynamic ss virtual vaImplemented paImplemented : Bool)
    (bundle : Word) (slot : Slot) (long : Bool) : Effect :=
  let wrap := slot.number = {boundary['end']} ∨ (slot.number = {boundary['last']} ∧ long)
  let nextIp := if wrap then bundle + {boundary['delta']} else bundle
  let nextSlot := if wrap then {boundary['reset']} else slot.number + {boundary['step']}
  let completed := if long && slot.number == {boundary['pair']} then {boundary['src']} else slot.number
  let packed := (nextSlot &&& 3) ||| ((completed &&& 3) <<< 2#64)
  if dynamic || ss || !(if virtual then vaImplemented else paImplemented) then
    .native ⟨⟨nextIp, nextSlot⟩, ⟨bundle, completed⟩, packed, {boundary['cond']}, {boundary['isr']},
      if saved then s.savedPsr else s.psr⟩
  else .nop
"""


def lift_debug(source, header):
    db_shift = numeric_macro(header, "IA64_PSR_DB", r"\(1ULL << (\d+)\)")
    debug = recognize(source, "ia64_gen_check_instruction_debug", """
        if (!ctx->restart.instruction_debug_psr_dynamic) {
            if (!(ctx->base.tb->flags & IA64_TB_FLAG_PSR_DB)) {
                return;
            }
            gen_helper_instruction_debug(tcg_env, tcg_constant_i64(address),
                                         tcg_constant_i32(slot));
        } else {
            TCGv_i64 enabled = tcg_temp_new_i64();
            TCGLabel *done = gen_new_label();
            tcg_gen_@op@i_i64(enabled, cpu_psr, IA64_PSR_DB);
            tcg_gen_brcondi_i64(TCG_COND_EQ, enabled, 0, done);
            gen_helper_instruction_debug(tcg_env, tcg_constant_i64(address),
                                         tcg_constant_i32(slot));
            gen_set_label(done);
        }
    """, op=BIN)
    return f"""
def emitDebug (dynamic db : Bool) (address : Address) : Effect :=
  if dynamic then
    .ifNonzero (.{OPS[debug['op']]} (.input .psr) (.literal (1#64 <<< {db_shift}#64))) (.debug address)
  else if db then .debug address else .nop
"""


def lift_group(source):
    recognize(source, "ia64_gen_store_instruction_group_start", """
        tcg_gen_st8_i32(tcg_constant_i32(group_start), tcg_env,
                        offsetof(CPUIA64State, instruction_group_start));
    """)
    recognize(source, "ia64_gen_goto_tb", """
        ia64_gen_goto_tb_group(ctx, dest, ctx->restart.instruction_group_start);
    """)
    return """
def emitGroupAtExit (nextGroup : Bool) : Effect :=
  .write .group (.literal (if nextGroup then 1 else 0))
"""
