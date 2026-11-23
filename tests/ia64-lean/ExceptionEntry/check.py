#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Project native Illegal Operation delivery and check defined SDM state."""

import argparse
from pathlib import Path
import re
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE.parent))

from Decode.check import Expression, ROOT, get_match, run_lean, uncomment


def compact(text):
    return re.sub(r"\s+", "", text)


def body(source, name):
    match = get_match(
        rf"^[\w \t*]+\b{re.escape(name)}\s*\([^{{}};]*\)\s*\{{(.*?)^\}}",
        source, name,
    )
    return compact(match[1])


def expect(text, pattern, context, *, full=True):
    return get_match(re.escape(pattern), text, context, full=full)


def macro(source, name):
    source = re.sub(r"\\\n", "", source)
    return get_match(rf"^#define[ \t]+{name}[ \t]+([^\n]+)", source, name)[1].strip()


class Terms:
    def __init__(self, header):
        self.header = header
        self.cache = {}

    def constant(self, name):
        if name not in self.cache:
            raw = re.sub(r"\b(0x[\da-fA-F]+|\d+)(?:UL|U)\b", r"\1ULL", macro(self.header, name))
            self.cache[name] = self.word(raw)
        return self.cache[name]

    def word(self, expression, replacements=None):
        replacements = replacements or {}
        for c_value, lean_name in replacements.items():
            expression = expression.replace(c_value, lean_name)
        names = {name: name for name in replacements.values()}
        for value in set(re.findall(r"\bIA64_[A-Z0-9_]+\b", expression)):
            names[value] = self.constant(value)
        return Expression(expression, names, {}).lean()


def helper_packet(files, terms):
    translator = files["target/ia64/translate/translate.c"]
    expect(compact(files["target/ia64/helper.h"]),
           "DEF_HELPER_5(raise_exception,noreturn,env,i32,i64,i64,i32)", "TCG helper ABI", full=False)
    expect(body(translator, "ia64_gen_raise_exception"),
           "tcg_gen_movi_i64(cpu_ip,fault_ip);gen_helper_raise_exception(tcg_env,"
           "tcg_constant_i32(exception),tcg_constant_i64(fault_ip),tcg_constant_i64(fault_imm),"
           "tcg_constant_i32(fault_slot));", "emitted Illegal Operation request")
    expect(body(translator, "ia64_translate_init"),
           'cpu_ip=tcg_global_mem_new_i64(tcg_env,offsetof(CPUIA64State,ip),"ip");',
           "TCG pending IP global", full=False)
    expect(body(files["target/ia64/helper/helper-exception.c"], "helper_raise_exception"),
           "ia64_raise_exception(env,exception,fault_ip,fault_imm,fault_slot);", "helper argument forwarding")
    expect(body(files["accel/tcg/cpu-exec-common.c"], "cpu_loop_exit"),
           "cpu->neg.can_do_io=true;qemu_plugin_disable_mem_helpers(cpu);siglongjmp(cpu->jmp_env,1);",
           "helper loop exit")
    exception = files["target/ia64/arch/exception.c"]
    prefix = (
        "CPUState*cs=env_cpu(env);trace_ia64_exception_raise(cs->cpu_index,exception,fault_ip,"
        "fault_imm,fault_slot,env->cr_isr);if(exception==IA64_EXCP_RESERVED_REG_FIELD){"
    )
    fields = ("env->ip", "env->exception_state.fault_ip", "env->exception_state.fault_imm",
              "env->exception_state.fault_slot", "env->exception_state.fault_exception",
              "env->exception_state.exception", "cs->exception_index")
    captured = get_match(
        re.escape(prefix) + r"qemu_log_mask\([^{};]+\);\}" +
        "".join(re.escape(field) + r"=([^;]+);" for field in fields) +
        re.escape("cpu_loop_exit(cs);"),
        body(exception, "ia64_raise_exception"), "helper pending exception assignments", full=True,
    ).groups()
    exception_number, = get_match(r"\bIA64_EXCP_ILLEGAL\s*=\s*(\d+)\s*,", terms.header,
                                  "Illegal Operation exception number").groups()
    for name, width in (("fault_ip", 64), ("fault_imm", 64), ("fault_slot", 32),
                        ("fault_exception", 32), ("exception", 32)):
        get_match(rf"uint{width}_t\s+{name};", files["target/ia64/internals.h"], "pending field ABI")
    hints = {"fault_ip": "ip", "fault_imm": "raw", "fault_slot": "slotWord",
             "exception": "code"}
    values = [terms.word(expression, hints) for expression in captured]
    for index in range(3, len(values)):
        values[index] = f"({values[index]}).setWidth 32"
    fields_lean = ", ".join(values)
    return f"""
def cIllegalNumber : Word := {exception_number}

def cRaiseWithCode (exception : BitVec 32) (fault : Fault) : Pending :=
  let ip := fault.bundle
  let raw := fault.raw
  let slotWord := fault.slot.zeroExtend 64
  let code := exception.zeroExtend 64
  ⟨{fields_lean}⟩

def cRaise (fault : Fault) : Pending := cRaiseWithCode (cIllegalNumber.setWidth 32) fault
"""


def classify_exception(source, name, tail="returnfalse;"):
    cases, = get_match(r"switch\(excp\)\{(.*)default:" + re.escape(tail) + r"\}", body(source, name),
                       name, full=True).groups()
    if "caseIA64_EXCP_ILLEGAL:" in cases:
        raise ValueError(f"Illegal Operation takes a different branch in {name}")


def dispatch(files, terms):
    exception = files["target/ia64/arch/exception.c"]
    cpu = files["target/ia64/cpu.c"]
    expect(compact(cpu), ".do_interrupt=ia64_cpu_do_interrupt,"
           ".do_interrupt_unlocked=ia64_cpu_do_interrupt_unlocked,", "TCG exception dispatch", full=False)
    expect(body(cpu, "ia64_cpu_class_init"), "cc->tcg_ops=&ia64_tcg_ops;",
           "active IA-64 TCG operation table", full=False)
    expect(compact(files["accel/tcg/cpu-exec.c"]),
           "if(replay_mode!=REPLAY_MODE_NONE||!tcg_ops->do_interrupt_unlocked||"
           "!tcg_ops->do_interrupt_unlocked(cpu)){bql_lock();tcg_ops->do_interrupt(cpu);"
           "bql_unlock();}cpu->exception_index=-1;",
           "ordinary external exception scheduler callback", full=False)
    unlocked = body(exception, "ia64_cpu_do_interrupt_unlocked")
    cases, = get_match(r"switch\(cs->exception_index\)\{((?:caseIA64_EXCP_\w+:)+)"
                       r"ia64_cpu_do_interrupt\(cs\);returntrue;default:returnfalse;\}$",
                       unlocked, "native BQL-free exception set").groups()
    if "caseIA64_EXCP_ILLEGAL:" in cases:
        raise ValueError("unexpected unlocked Illegal Operation path")
    for name in ("ia64_exception_is_translation_fault", "ia64_exception_uses_psr_ri_slot",
                 "ia64_exception_writes_ifa"):
        classify_exception(exception, name)
    expect(body(exception, "ia64_try_defer_sal_speculative_alt_dtlb"),
           "if(excp!=IA64_EXCP_ALT_DTLB||", "Illegal Operation rejects speculative SAL assist", full=False)
    expect(body(exception, "ia64_try_handle_sal_alt_tlb"),
           "if((excp!=IA64_EXCP_ALT_ITLB&&!is_data)||", "Illegal Operation rejects SAL miss assist", full=False)

    handler = body(exception, "ia64_cpu_do_interrupt")
    expect(handler,
           "IA64CPU*cpu=ia64_cpu_from_cpu_state(cs);intexcp=cs->exception_index;"
           "uint64_tfault_addr;uint8_tslot;boolia32_entry_trap;"
           "if(excp==IA64_EXCP_NONE){return;}cpu->env.exception_state.fault_exception=excp;"
           "ia32_entry_trap=(cpu->env.psr&IA64_PSR_IS)&&cpu->env.exception_state.ia32_transition_trap;",
           "pending index controls native delivery", full=False)
    sentinel, = get_match(r"if\(!\(cpu->env\.psr&([^()]+)\)&&"
                          r"!ia64_exception_is_translation_fault\(excp\)&&cpu->env\.cr_iva==0&&"
                          r"\(excp==IA64_EXCP_BREAK\|\|excp==IA64_EXCP_ILLEGAL(?:\|\|excp==\w+)+\)\)"
                          r"\{if\(excp==IA64_EXCP_BREAK\)\{cs->halted=1;cs->exception_index=IA64_EXCP_NONE;\}"
                          r"else\{cpu->env\.ip=cpu->env\.exception_state\.fault_ip;\}return;\}",
                          handler, "bare-loader Illegal Operation path").groups()
    expect(handler, "caseIA64_EXCP_ILLEGAL:", "Illegal Operation fault address group", full=False)
    get_match(r"caseIA64_EXCP_ILLEGAL:(?:caseIA64_EXCP_\w+:)*"
              + re.escape("fault_addr=cpu->env.exception_state.fault_ip;break;"), handler,
              "fault address passed into delivery")
    expect(handler, "slot=cpu->env.exception_state.fault_slot;if(ia32_entry_trap){}"
           "elseif(cpu->env.psr&IA64_PSR_IS){slot=0;}"
           "elseif(!cpu->env.exception_state.native_completion_trap&&"
           "ia64_exception_uses_psr_ri_slot(excp,cpu->env.cr_isr)){"
           "slot=(cpu->env.psr&IA64_PSR_RI_MASK)>>IA64_PSR_RI_SHIFT;}",
           "native Illegal Operation physical fault slot", full=False)
    expect(handler,
           "if(ia64_try_defer_sal_speculative_alt_dtlb(cs,excp,fault_addr,slot)){"
           "cs->exception_index=IA64_EXCP_NONE;return;}"
           "if(ia64_try_handle_sal_alt_tlb(cs,excp,fault_addr)){"
           "cs->exception_index=IA64_EXCP_NONE;return;}"
           "if(excp==IA64_EXCP_UNALIGNED&&ia64_try_emulate_firmware_unaligned(cs,fault_addr,slot)){"
           "cs->exception_index=IA64_EXCP_NONE;return;}"
           "if(excp==IA64_EXCP_EXTINT){if(ia64_sapic_has_pending(&cpu->env)){"
           "ia64_deliver_exception(cs,excp,fault_addr,slot);cpu->env.interrupt.pending_extint=0;}}"
           "else{ia64_deliver_exception(cs,excp,fault_addr,slot);}cs->exception_index=IA64_EXCP_NONE;",
           "ordinary interrupt continuation", full=False)
    return f"""
def cSentinel (psr iva : Word) : Prop :=
  (psr &&& {terms.word(sentinel)}) = 0 ∧ iva = 0

instance (psr iva : Word) : Decidable (cSentinel psr iva) := by unfold cSentinel; infer_instance
"""


def delivery(files, terms):
    source = files["target/ia64/arch/exception.c"]
    code = body(source, "ia64_deliver_exception")
    expect(code,
           "ia32=cpu->env.psr&IA64_PSR_IS;"
           "ia32_transition_trap=cpu->env.exception_state.ia32_transition_trap;"
           "ia32_entry_trap=ia32&&ia32_transition_trap;",
           "native state and transition-trap selection", full=False)
    vector, = get_match(r"\[IA64_EXCP_ILLEGAL\]\s*=\s*(0x[\da-fA-F]+)\s*,",
                        source, "Illegal Operation vector entry").groups()
    base_isr, = get_match(r"uint64_tisr_status=([^;]+);", code, "initial illegal ISR value").groups()
    before = (
        "vector=ia64_ivt_vectors[excp];switch(excp){"
    )
    span, = get_match(re.escape(before) + r"(.*?)" + re.escape(
        "qemu_log_mask(CPU_LOG_INT,"), code, "selected ISR initialization").groups()
    if "caseIA64_EXCP_ILLEGAL:" in span or not span.endswith("default:break;}"):
        raise ValueError("Illegal Operation has an unprojected ISR initializer")
    names = {"cpu->env.psr": "psr", "cpu->env.ip": "ip", "cpu->env.cr_iva": "iva",
             "cpu->env.last_successful_bundle": "history"}
    collect, ipsr, slot_ri, iip, iipa, ifs = get_match(
        r"psr_ic_inflight=cpu->env\.exception_state\.psr_ic_inflight;collect=([^;]+);"
        r"trace_ia64_exception_deliver\([^;]+\);ia64_flush_suppressed_tlb\(&cpu->env\);"
        r"cpu->env\.exception_state\.psr_suppression_before_insn=0;"
        r"ia64_tlb_serialize\(&cpu->env,1,1\);if\(collect\)\{cpu->env\.cr_ipsr=([^;]+);"
        r"if\(ia32_entry_trap\)\{[^{};]+;[^{};]+;\}"
        r"elseif\(ia32\|\|ia32_transition_trap\)\{[^{};]+;[^{};]+;\}"
        r"elseif\(cpu->env\.exception_state\.native_completion_trap\)\{[^{};]+;[^{};]+;[^{};]+;\}"
        r"else\{cpu->env\.cr_ipsr\|=([^;]+);cpu->env\.cr_iip=ia64_ip_bundle_addr\(([^)]+)\);"
        r"cpu->env\.cr_iipa=excp==IA64_EXCP_FP_TRAP\?cpu->env\.exception_state\.fault_imm:([^;]+);\}"
        r"if\(ia64_exception_writes_ifa\(excp\)\)\{cpu->env\.cr_ifa=fault_addr;\}"
        r"cpu->env\.cr_ifs=([^;]+);if\(excp==IA64_EXCP_BREAK\)\{"
        r"cpu->env\.cr_iim=cpu->env\.exception_state\.fault_imm;\}\}",
        code, "native Illegal Operation collected controls",
    ).groups()
    ei, ni, live_ip = get_match(
        r"if\(excp!=IA64_EXCP_DATA_NESTED_TLB\)\{cpu->env\.cr_isr=isr_status;"
        r"if\(\(!ia32\|\|ia32_entry_trap\)&&slot>0\)\{cpu->env\.cr_isr\|=([^;]+);\}"
        r"if\(!collect\|\|psr_ic_inflight\)\{cpu->env\.cr_isr\|=([^;]+);\}\}"
        r"ia64_rse_delivery_check\(&cpu->env,excp\);"
        r"ia64_firmware_debug_capture\(&cpu->env,vector,collect\);cpu->env\.rse\.rse_cfle=false;"
        r"ia64_set_psr\(&cpu->env,ia64_interruption_psr\(&cpu->env\)\);"
        r"cpu->env\.exception_state\.psr_ic_inflight=false;"
        r"cpu->env\.ip=([^;]+);cpu->env\.instruction_group_start=true;"
        r"if\(excp==IA64_EXCP_EXTINT\)\{cs->halted=0;\}"
        r"cpu->env\.exception_state\.exception=0;cpu->env\.exception_state\.ia32_trap=false;"
        r"cpu->env\.exception_state\.ia32_transition_trap=false;"
        r"cpu->env\.exception_state\.native_completion_trap=false;"
        r"cpu->env\.exception_state\.psr_before_insn=0;cpu->env\.interrupt\.pending_extint=0;$",
        code, "native Illegal Operation final controls",
    ).groups()
    expect(body(terms.header, "ia64_ip_bundle_addr"), "returnip&IA64_IP_BUNDLE_MASK;", "native bundle alignment")
    # ia64_set_psr commits the supplied value after its bank swap, then
    # notifies SAPIC. Its non-PSR bookkeeping is outside the projection.
    expect(body(files["target/ia64/arch/system.c"], "ia64_set_psr"),
           "if(changed&IA64_PSR_BN){ia64_swap_banked_gr(env);}env->psr=value;"
           "if(changed&(IA64_PSR_I|IA64_PSR_IC|IA64_PSR_MC|IA64_PSR_IS)){"
           "ia64_sapic_update_interrupt(env);}", "entry PSR state commit", full=False)
    psr_base, be_input, be_output, pp_input, pp_output = get_match(
        r"uint64_tpsr=([^;]+);if\(env->cr_dcr&([^()]+)\)\{psr\|=([^;]+);\}"
        r"if\(env->cr_dcr&([^()]+)\)\{psr\|=([^;]+);\}returnpsr;",
        body(source, "ia64_interruption_psr"), "handler interruption PSR", full=True,
    ).groups()
    psr_base = terms.word(psr_base.replace("env->psr", "oldPsr").replace(
        "IA64_PSR_INTERRUPTION_PRESERVED_MASK", macro(source, "IA64_PSR_INTERRUPTION_PRESERVED_MASK")),
        {"oldPsr": "psr"})
    terms_slot = terms.word(slot_ri, {"slot": "slotWord"})
    terms_ei = terms.word(ei, {"slot": "slotWord"})
    terms_iip = terms.word(f"({iip})&IA64_IP_BUNDLE_MASK", names)
    return f"""
def cCollect (psr : Word) : Bool := decide ({terms.word(collect, names)} ≠ 0)

def cNativeOrdinary (before : Before) : Prop :=
  before.psr &&& {terms.constant("IA64_PSR_IS")} = 0 ∧
    before.ia32Transition = false ∧ before.nativeCompletionTrap = false

instance (before : Before) : Decidable (cNativeOrdinary before) := by
  unfold cNativeOrdinary
  infer_instance

def cHandlerPsr (psr dcr : Word) : Word :=
  {psr_base} |||
    (if dcr &&& {terms.word(be_input)} ≠ 0 then {terms.word(be_output)} else 0) |||
    (if dcr &&& {terms.word(pp_input)} ≠ 0 then {terms.word(pp_output)} else 0)

def cHandlerIp (iva : Word) : Word :=
  let vector : Word := {int(vector, 0)}
  {terms.word(live_ip, names | {"vector": "vector"})}

def cIip (ip : Word) : Word := {terms_iip}
def cIpa (history : Word) : Word := {terms.word(iipa, names)}

def cIpsr (psr : Word) (slot : BitVec 8) : Word :=
  let slotWord := slot.zeroExtend 64
  ({terms.word(ipsr, names)}) ||| ({terms_slot})

def cIsr (slot : BitVec 8) (collect inFlight : Bool) : Word :=
  let slotWord := slot.zeroExtend 64
  {terms.word(base_isr)} ||| (if slot = 0 then 0 else {terms_ei}) |||
  (if !collect || inFlight then {terms.word(ni)} else 0)

def cDelivery (before : Before) (pending : Pending) : Entry :=
  let collect := cCollect before.psr
  let slot := pending.faultSlot.setWidth 8
  if cSentinel before.psr before.iva then
    ⟨pending.faultIp, before.psr, before.isr, before.saved, before.cfle, false⟩
  else
    let saved := if collect then
        ⟨cIip pending.ip, cIpsr before.psr slot, cIpa before.lastSuccessfulBundle,
          ({terms.word(ifs)} : Word).getLsbD 63⟩
      else before.saved
    ⟨cHandlerIp before.iva, cHandlerPsr before.psr before.dcr,
      cIsr slot collect before.icInFlight, saved, false, true⟩

def cDispatch (before : Before) (pending : Pending) : Option Entry :=
  if pending.exceptionIndex = cIllegalNumber.setWidth 32 ∧ cNativeOrdinary before then
    some (cDelivery before pending)
  else none
"""


FILES = (
    "target/ia64/cpu.h", "target/ia64/internals.h", "target/ia64/helper.h",
    "target/ia64/translate/translate.c", "target/ia64/helper/helper-exception.c",
    "target/ia64/arch/system.c", "target/ia64/arch/exception.c", "target/ia64/cpu.c",
    "accel/tcg/cpu-exec-common.c", "accel/tcg/cpu-exec.c",
)


def generate(root, include_slot_spec=True):
    files = {path: uncomment((root / path).read_text(encoding="utf-8")) for path in FILES}
    terms = Terms(files["target/ia64/cpu.h"])
    slot_spec = ""
    if include_slot_spec:
        slot_spec = (HERE.parent / "SlotControl/Spec.lean").read_text(encoding="utf-8")
        slot_spec = re.sub(r"^import [^\n]+\n", "", slot_spec, flags=re.M)
    return (
        (HERE / "Spec.lean").read_text(encoding="utf-8") + "\n" + slot_spec
        + "\nnamespace IA64ExceptionEntry\nset_option maxHeartbeats 0\n"
        + helper_packet(files, terms) + dispatch(files, terms) + delivery(files, terms)
        + (HERE / "Proof.lean.inc").read_text(encoding="utf-8") + "\nend IA64ExceptionEntry\n"
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=ROOT)
    parser.add_argument("--emit-lean", type=Path)
    options = parser.parse_args()
    try:
        src = generate(options.source_root.resolve())
        if options.emit_lean:
            options.emit_lean.write_text(src, encoding="utf-8")
            return
        with tempfile.TemporaryDirectory(prefix="ia64-exception-entry-") as tmp:
            work = Path(tmp)
            proof = work / "Check.lean"
            proof.write_text(src, encoding="utf-8")
            run_lean(work, proof, "-DwarningAsError=true")
    except (ValueError, OSError, subprocess.CalledProcessError) as exc:
        parser.exit(1, f"Exception entry check failed: {exc}\n")
    print("IA-64 ordinary Illegal Operation helper request and defined vector-entry state verified")


if __name__ == "__main__":
    main()
