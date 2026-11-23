#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check IA-64 global-purge callback effects and finite MTTCG scheduling traces."""

import argparse
from hashlib import sha256
import json
from pathlib import Path
import re
import subprocess
import sys
import tempfile

HERE = Path(__file__).resolve().parent
ROOT = HERE.parents[2]
sys.path.insert(0, str(HERE.parent))

from Integer.check import cases, compact, function, require, stripped, switches


# These complete bodies delimit the source projection. In particular a change
# to an exclusive wait, recipient drain, exit-request path or local/remote
# marking must be reviewed before reusing the scheduling abstraction.
GUARDS = {   'cpu-common.c': {   'queue_work_on_cpu': 'd200e21e8248418ce1096d0fae6d1f9a7f46a0cdbd80dbf4463320d027c54c9a',
                        'async_run_on_cpu': '90cd9282646805c081f08107586d5c9e0c589ed98188638ec80da5145c359594',
                        'async_safe_run_on_cpu': 'cbdeb61fc26ef261b810f2e402eff55f37a33d655b8add2d50299feff2c08fdd',
                        'process_queued_cpu_work': '97b701e5d3beccde311d86faa799269a3edeaa82810b6b90411d85fd3f0526cb',
                        'start_exclusive': 'bd71e666f32f7d58a492aaa901ab7eff655151236a8cca09c76c5912f82d15ca',
                        'cpu_exec_start': '169df7084ac42c94a38a99645826a0965a822c72b986102956b811f7c19967b8',
                        'cpu_exec_end': 'baed2e9bf831f68819ec6764f4c78c5a5c41e9e43cca3190f5573066f10f5a07',
                        'end_exclusive': '1695ff2f3485829bf83cdb1c95b68a725fa3b1f9aa87ba4860f36ace3056a9df'},
    'system/cpus.c': {   'qemu_process_cpu_events': '9bb25f6d701cb0de1310bacc54f1c3930cf0c88ab3e6a97e02e1ac7c8e0c18de',
                         'qemu_process_cpu_events_common': '2fd42a9aaf2b1ec6ff6a768c96357c4295354ec6d86c34130a8256f5df524198',
                         'cpu_thread_is_idle': 'de560d95ce234c0e4aaa1971e48755d0f8c8c89c899aa4ff4975fe38f0b959b1',
                         'qemu_cpu_kick': '367c17d9062ae83760b2401c0124d9f556049c9e5c93e40b05e6eba2b007a0e1',
                         'cpu_work_list_empty': 'e27372b05b507fda89723ffd19b9f9099e9baf2aa45dd1d6b5f8e0bfb52b401c'},
    'hw/core/cpu-common.c': {   'cpu_exit': 'cd320b2c9cd53ac770add76e9572eb50506ced15ba86efc790b49a1708615b33'},
    'accel/tcg/cpu-exec.c': {   'tcg_kick_vcpu_thread': 'e1d9a31c44d3c13571986f8e786718591649e264ed4f97922a24137a49c9ac60',
                                'cpu_handle_interrupt': '6cda10c21046389ff51251e160fa17af0cee2a7b116583cdd7297bf8bf7e4b65'},
    'accel/tcg/tcg-accel-ops.c': {   'tcg_cpu_exec': '34450cce5e32f813cc6b43026440d28fb38c4fc0bd14ba272c6ac9035727e7ad'},
    'accel/tcg/tcg-accel-ops-mttcg.c': {   'mttcg_cpu_thread_fn': '3c9ed78bd4a7b9b771defc124793318b7be04fd550be817aa9d04249809b25eb'},
    'target/ia64/arch/mmu.c': {   'ia64_mmu_ptc_purge': 'de9220c879c1d0f24e7e88e3f61bf29b0ea16e6e4dfd11e4c2109d4f3fa73854',
                                  'ia64_ptc_mark_global': '79d2d8c9c6ff1d0eb0bad56545ec5c3e247605d2214ca42a6f735b2412b7d349',
                                  'ia64_ptc_global_remote_work': '61fe3122ffaf75b19e935f3b31ac33b220e15a45f63a81817f6f4418cd1c195c',
                                  'ia64_ptc_global_source_barrier': '0e35f282dbce6e67512b36f202a94e2dcb97117e3b5b33cfdfec539dd5afea7a',
                                  'ia64_tlb_serialize': '9ed767650940e63bf3cabab490a272dfcd38c12348830d05d0b53c894e7134a7',
                                  'ia64_mark_pending_purge_entries': '20b833b8984afeddeca75d4d2890a827a79abcd43b2797026b0946560d19ca7d',
                                  'ia64_complete_pending_purges': 'bb239caadfb187154ff04fe1160ee1bfff590da2f833d1ecc547caae6b1353e1'},
    'target/ia64/helper/helper-mmu.c': {   'helper_ptc_purge': 'feb43848a82127489fb092fc29f3718052a61a7819de1bb7a31cf0ec98523716'},
    'target/ia64/translate/gen-system.c': {   'ia64_request_exit_after_bundle': 'f8f7abcd6e6e88952c0d9a556cff11816e51d8884e9ad9e8630b5cb2279474eb'}}


def source(relative):
    return stripped((ROOT / relative).read_text())


def check_paths(overrides=None):
    overrides = overrides or {}
    for path, restrictions in GUARDS.items():
        text = overrides.get(path)
        if text is None:
            text = source(path)
        for name, fingerprint in restrictions.items():
            actual = sha256(compact(function(text, name)).encode()).hexdigest()
            require(actual == fingerprint, f"Changed purge scheduling source path {name}")


def extract(system):
    check_paths()
    mmu = source("target/ia64/arch/mmu.c")
    opcodes = cases(switches(function(system, "ia64_gen_system"))[0][2])
    result = {}
    for key in ("PTC_G", "PTC_GA"):
        call = re.fullmatch(
            r"ia64_gen_check_nat_register\(insn,op->register_index\);"
            r"ia64_gen_check_nat_register\(insn,op->source\);"
            r"ia64_gen_sync_ip_for_helper\(insn\);ia64_gen_memory_release\(insn\);"
            r"gen_helper_ptc_purge\(tcg_env,ia64_gr_src\(op->register_index\),"
            r"ia64_gr_src\(op->source\),tcg_constant_i32\((\d+)\),"
            r"tcg_constant_i64\(insn->raw\),tcg_constant_i32\(insn->slot\)\);"
            r"ia64_request_exit_after_bundle\(ctx\);break;", compact(opcodes[key]))
        require(call is not None, f"Changed {key} release/helper ordering")
        result[key] = int(call[1])
    local = compact(function(mmu, "ia64_mmu_ptc_purge"))
    remote = compact(function(mmu, "ia64_ptc_global_remote_work"))
    mark = re.search(r"ia64_ptc_mark_global\(env,&template,(true|false)\);", local)
    execute = re.search(r"ia64_ptc_mark_global\(cpu_env\(cs\),work,(true|false)\);", remote)
    mode = re.search(r"\.global_alat=mode==(\d+),", local)
    allowed = re.search(r"if\(mode==(\d+)\|\|mode==(\d+)\)", local)
    require(all((mark, execute, mode, allowed)), "Unsupported purge effect arguments")
    cpu_foreach = ("CPU_FOREACH(cs){if(cs!=src){IA64PtcGlobalWork*work="
                   "g_new(IA64PtcGlobalWork,1);*work=template;async_run_on_cpu"
                   "(cs,ia64_ptc_global_remote_work,RUN_ON_CPU_HOST_PTR(work));wait=true;}}"
                   "if(wait){async_safe_run_on_cpu(src,ia64_ptc_global_source_barrier,"
                   "RUN_ON_CPU_NULL);}")
    require(cpu_foreach in local, "Unexpected recipient set or source barrier order")
    decoder = compact(source("target/ia64/decode/decode.c"))
    require("insn.mem_release=opcode==IA64_OP_PTC_L||opcode==IA64_OP_PTC_G||"
            "opcode==IA64_OP_PTC_GA||opcode==IA64_OP_PTC_E;" in decoder,
            "Missing decoded purge release completer")
    translator = compact(source("target/ia64/translate/translate.c"))
    boundary = translator.index("if(ctx->restart.exit_after_bundle){db->is_jmp=IA64_DISAS_EXIT;")
    require(translator.rindex("ia64_gen_advance_restart_point(", 0, boundary) <
            translator.rindex("ctx->restart.start_slot=0;", 0, boundary) < boundary,
            "Changed bundle-boundary exit placement")
    return (f"def sourceComplete : Bool := {mark[1]}\n"
            f"def remoteComplete : Bool := {execute[1]}\n"
            f"def ptcG : Nat := {result['PTC_G']}\n"
            f"def ptcGa : Nat := {result['PTC_GA']}\n"
            f"def clearsAlat (mode : Nat) : Bool := mode == {mode[1]}\n"
            f"def isGlobal (mode : Nat) : Bool := mode == {allowed[1]} || mode == {allowed[2]}\n")


THEOREMS = """
theorem modes :
    isGlobal ptcG = true ∧ isGlobal ptcGa = true ∧
    clearsAlat ptcG = false ∧ clearsAlat ptcGa = true := by decide

theorem sourceMarksBeforeQueue (mode : Nat) (running : Fin n → Bool) :
    let machine := issued n sourceComplete (clearsAlat mode) running
    machine.sourceView.data = .pending ∧ machine.sourceView.inst = .pending ∧
    ∀ cpu, (machine.remote cpu).pendingWork = true := by
  simp [issued, mark, sourceComplete]

theorem sourceStreamsSerializeSeparately (mode : Nat) (running : Fin n → Bool) :
    let cache := (issued n sourceComplete (clearsAlat mode) running).sourceView
    (localSerialize cache true false).data = .removed ∧
    (localSerialize cache true false).inst = .pending ∧
    (localSerialize cache false true).inst = .removed := by
  simp [issued, mark, sourceComplete, localSerialize]

theorem workCompletesBothStreams (mode : Nat) (cpu : Remote) :
    remotelyVisible (remoteWork cpu remoteComplete (clearsAlat mode)).view (clearsAlat mode) := by
  simp only [remoteWork, remoteComplete, mark, remotelyVisible]
  constructor
  · rfl
  · exact ⟨rfl, fun clears => by simp [clears]⟩

theorem ptcGPreservesRemoteAlat (cpu : Remote) :
    (remoteWork cpu remoteComplete (clearsAlat ptcG)).view.alatPresent =
      cpu.view.alatPresent := by
  have noClear := modes.2.2.1
  simp [remoteWork, mark, noClear]

theorem ptcGaClearsRemoteAlat (cpu : Remote) :
    (remoteWork cpu remoteComplete (clearsAlat ptcGa)).view.alatPresent = false := by
  have cleared := modes.2.2.2
  simp [remoteWork, mark, cleared]

theorem cpuEntryAfterOwnDrain (mode : Nat) (running : Fin n → Bool) (state : Machine n)
    (trace : Trace remoteComplete (clearsAlat mode)
      (issued n sourceComplete (clearsAlat mode) running) state)
    (cpu : Fin n) (resumes : (state.remote cpu).resumed = true) :
    remotelyVisible (state.remote cpu).view (clearsAlat mode) :=
  resumedPeerHasAppliedWork _ _ running state trace cpu resumes

theorem noDrainAtSourceBarrier (mode : Nat) (cpu : Fin (n + 1)) :
    ∃ state : Machine (n + 1),
      Trace remoteComplete (clearsAlat mode)
        (issued (n + 1) sourceComplete (clearsAlat mode) (fun _ => false)) state ∧
      state.sourceBarrierReturned = true ∧
      (state.remote cpu).pendingWork = true ∧
      (state.remote cpu).view.data = .present :=
  sourceBarrierDoesNotDrainPeers sourceComplete (clearsAlat mode) cpu
"""


def run_lean(declarations):
    spec = (HERE / "Spec.lean").read_text()
    with tempfile.TemporaryDirectory(prefix="ia64-lean-purges-") as temp:
        module = Path(temp) / "Proofs.lean"
        module.write_text(spec + "\nnamespace IA64Purges\n" + declarations
                          + THEOREMS + "\nend IA64Purges\n")
        return subprocess.run(["lake", "env", "lean", "-DwarningAsError=true", str(module)],
                              cwd=HERE.parent, capture_output=True, text=True, check=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--coverage", action="store_true",
                        help="report partial purge opcode coverage as JSON")
    parser.add_argument("--mutation-test", action="store_true",
                        help="check detection of an incorrect purge mode and queue ordering")
    args = parser.parse_args()
    system = source("target/ia64/translate/gen-system.c")
    project = extract(system)
    if args.coverage:
        print(json.dumps({f"tcg:{op}": "Local/remote TC and ALAT effects with a conditional MTTCG scheduling projection"
                         for op in ("PTC_G", "PTC_GA")}, sort_keys=True))
        return
    result = run_lean(project)
    require(result.returncode == 0, result.stdout + result.stderr)
    print("Purges: local/remote effects and conditional finite MTTCG trace invariant passed")
    print("Purges: source exclusive callback can return with remote work still queued")
    if args.mutation_test:
        mutant, count = re.subn(r"tcg_constant_i32\(3\)(?=,\s*tcg_constant_i64\(insn->raw\))",
                                "tcg_constant_i32(1)", system)
        require(count == 1, "Could not locate ptc.ga mode mutation")
        mutated = run_lean(extract(mutant))
        require(mutated.returncode != 0 and "decide" in mutated.stdout,
                "Lean did not reject ptc.ga lowered as ptc.g:\n"
                + mutated.stdout + mutated.stderr)
        queue = source("cpu-common.c").replace("QSIMPLEQ_INSERT_TAIL(&cpu->work_list, wi, node);",
                                                "QSIMPLEQ_INSERT_HEAD(&cpu->work_list, wi, node);")
        try:
            check_paths({"cpu-common.c": queue})
        except ValueError as error:
            require(str(error) == "Changed purge scheduling source path queue_work_on_cpu",
                    str(error))
        else:
            raise ValueError("Purge trace reused a changed queue order")
        print("Purges: mutation checks rejected the wrong purge mode and changed FIFO order")


if __name__ == "__main__":
    try:
        main()
    except ValueError as error:
        raise SystemExit(str(error)) from None
