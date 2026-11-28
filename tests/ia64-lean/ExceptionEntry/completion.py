# SPDX-License-Identifier: GPL-2.0-or-later

import re

from ExceptionEntry import check


def generate(root):
    def source(name):
        return check.uncomment((root / f"target/ia64/{name}").read_text(encoding="utf-8"))

    arch = source("arch/exception.c")
    header = source("cpu.h")
    check.expect(check.compact(source("helper.h")),
                 "DEF_HELPER_FLAGS_7(check_native_traps,TCG_CALL_NO_WG,void,env,i64,i64,i32,i32,i64,i64)",
                 "native completion helper ABI", full=False)
    check.expect(check.body(source("helper/helper-exception.c"), "helper_check_native_traps"),
                 "ia64_check_native_traps(env,target_ip,source_ip,slots,conditions,base_isr,trap_psr);",
                 "native completion call forwarding")
    helper = check.body(arch, "ia64_check_native_traps")
    check.expect(helper,
                 "uint32_ttarget_slot=slots&3;uint32_tsource_slot=(slots>>2)&3;"
                 "uint64_tcode=base_isr;IA64Exceptiontrap;"
                 "boolunimplemented=trap_psr&IA64_PSR_IT?!ia64_va_is_implemented(env,target_ip):"
                 "!ia64_pa_is_implemented(env,target_ip);"
                 "if(unimplemented){code|=IA64_ISR_CODE_UI;}"
                 "if((conditions&IA64_NATIVE_TRAP_FP)!=0){code|=IA64_ISR_CODE_FP;}"
                 "if((conditions&IA64_NATIVE_TRAP_LOWER)!=0&&(trap_psr&IA64_PSR_LP)!=0){code|=IA64_ISR_CODE_LP;}"
                 "if((conditions&IA64_NATIVE_TRAP_TAKEN)!=0&&(trap_psr&IA64_PSR_TB)!=0){code|=IA64_ISR_CODE_TB;}"
                 "if((trap_psr&IA64_PSR_SS)!=0){code|=IA64_ISR_CODE_SS;}"
                 "if(code==0){return;}"
                 "if((trap_psr&IA64_PSR_IC)!=0){env->last_successful_bundle=ia64_ip_bundle_addr(source_ip);}"
                 "if(unimplemented){trap=IA64_EXCP_UNIMPL_INST_ADDR;}"
                 "elseif((conditions&IA64_NATIVE_TRAP_FP)!=0){trap=IA64_EXCP_FP_TRAP;}"
                 "elseif((code&IA64_ISR_CODE_LP)!=0){trap=IA64_EXCP_LOWER_PRIVILEGE;}"
                 "elseif((code&IA64_ISR_CODE_TB)!=0){trap=IA64_EXCP_TAKEN_BRANCH;}"
                 "else{trap=IA64_EXCP_SINGLE_STEP;}"
                 "env->psr=(env->psr&~IA64_PSR_RI_MASK)|((uint64_t)target_slot<<IA64_PSR_RI_SHIFT);"
                 "env->ip=target_ip;env->cr_isr=code;env->exception_state.psr_suppression_before_insn=0;"
                 "env->exception_state.native_completion_trap=true;"
                 "ia64_raise_exception(env,trap,target_ip,ia64_ip_bundle_addr(source_ip),source_slot);",
                 "native completion priority and pending packet")
    delivery = check.body(arch, "ia64_deliver_exception")
    check.expect(delivery,
                 "caseIA64_EXCP_SINGLE_STEP:caseIA64_EXCP_DEBUG:caseIA64_EXCP_LOWER_PRIVILEGE:"
                 "isr_status=cpu->env.cr_isr;break;", "single-step ISR base", full=False)
    check.expect(delivery,
                 "elseif(cpu->env.exception_state.native_completion_trap){"
                 "cpu->env.cr_ipsr|=cpu->env.psr&IA64_PSR_RI_MASK;"
                 "cpu->env.cr_iip=ia64_ip_bundle_addr(cpu->env.ip);"
                 "cpu->env.cr_iipa=cpu->env.exception_state.fault_imm;}",
                 "native completion saved controls", full=False)
    dispatch = check.body(arch, "ia64_cpu_do_interrupt")
    check.get_match(r"caseIA64_EXCP_SINGLE_STEP:(?:caseIA64_EXCP_\w+:)*"
                    + re.escape("fault_addr=cpu->env.exception_state.fault_ip;break;"),
                    dispatch, "single-step dispatch addresses")
    sentinel = check.get_match(r"if\(!\(cpu->env\.psr&IA64_PSR_IC\)&&"
                               r"!ia64_exception_is_translation_fault\(excp\)&&cpu->env\.cr_iva==0&&"
                               r"\((excp==IA64_EXCP_\w+(?:\|\|excp==IA64_EXCP_\w+)*)\)\)",
                               dispatch, "bare-loader exceptions")[1]
    if "IA64_EXCP_SINGLE_STEP" in sentinel:
        raise ValueError("bare-loader path intercepts single-step")
    for name, guard in (
        ("ia64_try_defer_sal_speculative_alt_dtlb", "if(excp!=IA64_EXCP_ALT_DTLB||"),
        ("ia64_try_handle_sal_alt_tlb", "if((excp!=IA64_EXCP_ALT_ITLB&&!is_data)||"),
    ):
        check.expect(check.body(arch, name), guard, "single-step skips alternate TLB emulation", full=False)
    check.expect(check.body(arch, "ia64_try_handle_sal_alt_tlb"),
                 "boolis_data=excp==IA64_EXCP_ALT_DTLB;", "alternate DTLB selection", full=False)
    check.expect(delivery, "cpu->env.cr_ifs=0;", "collected IFS validity", full=False)
    check.expect(delivery,
                 "cpu->env.ip=(cpu->env.cr_iva&~0x7fffULL)|vector;cpu->env.instruction_group_start=true;",
                 "native vector IP", full=False)
    vector = check.get_match(r"\[IA64_EXCP_SINGLE_STEP\]\s*=\s*(0x[\da-fA-F]+)\s*,",
                             arch, "single-step vector")[1]
    terms = check.Terms(header)
    masks = {name: terms.constant("IA64_" + name) for name in (
        "PSR_IT", "PSR_LP", "PSR_TB", "PSR_SS", "PSR_IC", "PSR_RI_MASK", "PSR_RI_SHIFT",
        "NATIVE_TRAP_FP", "NATIVE_TRAP_LOWER", "NATIVE_TRAP_TAKEN", "IP_BUNDLE_MASK",
        "ISR_CODE_UI", "ISR_CODE_FP", "ISR_CODE_LP", "ISR_CODE_TB", "ISR_CODE_SS",
    )}
    codes = {name: int(check.get_match(rf"\bIA64_EXCP_{name}\s*=\s*(\d+)\s*,", header,
                                      f"native completion {name}")[1]) for name in (
        "UNIMPL_INST_ADDR", "FP_TRAP", "LOWER_PRIVILEGE", "TAKEN_BRANCH", "SINGLE_STEP",
    )}
    return f"""
namespace IA64ExceptionEntry

def cSingleStepNumber : BitVec 32 := {codes["SINGLE_STEP"]}

def cCompletion (before : Before) (req : IA64Slot.NativeRequest) (va pa : Bool) :
    Option (Before × Pending) :=
  let target := req.packedSlots &&& 3
  let source := (req.packedSlots >>> 2) &&& 3
  let psr := req.priorPsr
  let conditions := req.conditions.setWidth 32 |>.zeroExtend 64
  let ui := !(if psr &&& {masks["PSR_IT"]} ≠ 0 then va else pa)
  let fp := conditions &&& {masks["NATIVE_TRAP_FP"]} ≠ 0
  let code := req.syndrome ||| (if ui then {masks["ISR_CODE_UI"]} else 0) |||
    (if fp then {masks["ISR_CODE_FP"]} else 0) |||
    (if conditions &&& {masks["NATIVE_TRAP_LOWER"]} ≠ 0 ∧ psr &&& {masks["PSR_LP"]} ≠ 0
      then {masks["ISR_CODE_LP"]} else 0) |||
    (if conditions &&& {masks["NATIVE_TRAP_TAKEN"]} ≠ 0 ∧ psr &&& {masks["PSR_TB"]} ≠ 0
      then {masks["ISR_CODE_TB"]} else 0) |||
    (if psr &&& {masks["PSR_SS"]} ≠ 0 then {masks["ISR_CODE_SS"]} else 0)
  if code = 0 then none
  else
    let exception := if ui then {codes["UNIMPL_INST_ADDR"]} else if fp then {codes["FP_TRAP"]}
      else if code &&& {masks["ISR_CODE_LP"]} ≠ 0 then {codes["LOWER_PRIVILEGE"]}
      else if code &&& {masks["ISR_CODE_TB"]} ≠ 0 then {codes["TAKEN_BRANCH"]} else cSingleStepNumber
    let bundle := req.source.bundle &&& {masks["IP_BUNDLE_MASK"]}
    let live := {{ before with
      psr := (before.psr &&& ~~~{masks["PSR_RI_MASK"]}) ||| (target <<< {masks["PSR_RI_SHIFT"]})
      isr := code
      lastSuccessfulBundle := if psr &&& {masks["PSR_IC"]} ≠ 0 then bundle else before.lastSuccessfulBundle
      nativeCompletionTrap := true }}
    some (live, cRaiseWithCode exception ⟨req.target.bundle, source.setWidth 32, bundle⟩)

def cStepVector (before : Before) (p : Pending) : Observation :=
  let collect := cCollect before.psr
  let saved := if collect then ⟨cIip p.ip, before.psr, p.faultImm, false⟩ else before.saved
  observe before ⟨(before.iva &&& ~~~0x7fff) ||| {int(vector, 0)},
    cHandlerPsr before.psr before.dcr, before.isr ||| cIsr (p.faultSlot.setWidth 8) collect before.icInFlight,
    saved, false, true⟩

end IA64ExceptionEntry
"""
