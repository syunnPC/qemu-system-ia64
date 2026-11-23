# SPDX-License-Identifier: GPL-2.0-or-later
"""Reify the captured TCG NOP/ADD/NOP page-boundary program."""

import json
from pathlib import Path
import re
import shlex
import subprocess
import tempfile

from Emitted.check import fixture as add_fixture, imm, project
from HostLitmus.replay import trace_program
from Integer.check import require
from ia64.cases_interrupt import test_native_single_step_traps_predicated_off_instruction
from ia64.encoding import IA64_EXCP_ILLEGAL, add, br_cond, nop_i, nop_m, srlz_d, ssm


BUNDLE = 0xff0
NEXT = BUNDLE + 16
MARKER = re.compile(r"^\s*----\s+([0-9a-fA-F]+)[^\n]*$", re.M)
TCG_PAIR = re.compile(r"^OP:\n(.*?)^OP after optimization and liveness analysis:\n"
                      r"(.*?)(?=^OP:\n|^----------------$|\Z)", re.M | re.S)


def bundle(ic, qualifier, pred, stop):
    program, expected = add_fixture(qualifier, True, pred)
    program[2] = (*program[2][:4], br_cond(0x30, 0x40 if ic else BUNDLE))
    program[3] = (BUNDLE, int(stop), nop_m() | 2, program[3][3], nop_i() | 2)
    program[4] = (NEXT, 0x11, nop_m(), nop_i(), br_cond(NEXT, NEXT))
    if ic:
        program.extend([
            (0x40, 0x00, ssm(1 << 13), nop_i(), nop_i()),
            (0x50, 0x11, srlz_d(), nop_i(), br_cond(0x50, BUNDLE)),
        ])
    expected["ip"] = NEXT
    return program, expected


def offsets(build, path):
    database = json.loads((build / "compile_commands.json").read_text(encoding="utf-8"))
    entries = [row for row in database if row["file"].endswith("/target/ia64/translate/translate.c")]
    require(len(entries) == 1, "expected one native IA-64 translator compile command")
    entry = entries[0]
    args = iter(entry.get("arguments") or shlex.split(entry["command"]))
    flags = []
    for arg in args:
        if arg in ("-MQ", "-MT", "-MF", "-o"):
            next(args)
        elif arg not in (entry["file"], "-MD", "-MMD", "-c"):
            flags.append(arg)
    with tempfile.TemporaryDirectory(prefix="ia64-slot-offsets-") as temp:
        executable = Path(temp) / "offsets"
        subprocess.run([*flags, str(path), "-o", str(executable)], cwd=entry["directory"],
                       check=True, capture_output=True, text=True)
        result = subprocess.run([str(executable)], check=True, capture_output=True, text=True)
    history, group, fault, nop, add_opcode = map(int, result.stdout.split())
    return (history, group, fault), (nop, add_opcode)


def observed_decode(trace, address, template, expected):
    # This active dump is made by the finished TB's disassembly hook. The
    # source route uses the same low/high words, template and decoder.
    record = r"QEMU-IA64-DECODE: slot=(\d+) unit=([MIBFLX]) opcode=(\d+) valid=1 qp=(\d+) raw=0x([a-f0-9]+)\n"
    header = rf"^QEMU-IA64-DECODE: bundle=0x{address:016x} template=0x{template:02x} name=MII defined=1\n"
    matches = re.findall(header + r"((?:QEMU-IA64-DECODE: slot=[^\n]+\n){3})", trace, re.M)
    require(len(matches) == 1, "expected three decoded records for the selected MII bundle")
    slots = re.findall(record, matches[0])
    actual = [(int(index), unit, int(tag), int(qp), int(raw, 16))
              for index, unit, tag, qp, raw in slots]
    require(actual == expected, "actual decoded opcode, slot, qualifier or raw encoding changed")
    return [row[4] for row in actual]


def captured(trace, address=BUNDLE, whole_tb=True):
    programs = []
    for before, after in TCG_PAIR.findall(trace):
        found = [list(MARKER.finditer(body)) for body in (before, after)]
        if not any(int(mark[1], 16) == address for marks in found for mark in marks):
            continue
        require(all(marks and int(marks[0][1], 16) == address
                    and (not whole_tb or len(marks) == 1) for marks in found),
                "selected slot probe does not start its TB or is not a one-bundle TB")
        programs.append([
            body[marks[0].end():marks[1].start() if len(marks) > 1 else len(body)]
            for body, marks in zip((before, after), found, strict=True)
        ])
    require(len(programs) == 1, "expected one emitted and optimized slot probe TB")
    for stage, body in zip(("emitted", "optimized"), programs[0], strict=True):
        yield stage, [tuple(line.split()[:2]) for line in body.splitlines() if line.strip()]


def reify(lines, locations, ic):
    history, group, fault = locations
    retire = [i for i, (_, arg) in enumerate(lines) if arg.startswith("psr,psr,")]
    require(len(retire) >= 3, "missing one of the three slot-boundary PSR operations")
    enter, leave, wrap = retire[:3]
    require(enter < leave and leave + 1 == wrap, "unrecognized third-slot NOP body")
    prefix = lines[:enter]
    prefix = empty_predicate_skip(prefix, 2)
    if ic:
        require(len(prefix) == 1, "expected one IIPA write after first NOP")
        op, arg = prefix[0]
        val, env, offset = arg.split(",")
        require(op == "st_i64" and env == "env" and imm(offset) == history,
                "IIPA value must be stored at the compiled last-successful bundle offset")
        hist = f"Effect.write .history (.literal {imm(val)})"
    else:
        require(not prefix, "unexpected effect on the first NOP with PSR.ic clear")
        hist = "Effect.nop"

    # Every operation from the selected bundle marker to the dispatcher
    # edge is assigned to a supported stage or rejected.
    advancing = [lines[i] for i in (enter, leave, wrap)]
    flips = []
    for op, arg in advancing:
        dst, src, mask = arg.split(",")
        require(op == "xor_i64" and dst == src == "psr", "expected tracked RI writes")
        flips.append(f"Effect.write .psr (.bxor (.input .psr) (.literal {imm(mask)}))")
    epilogue = lines[wrap + 1:]
    next_ip, group_op, fault_op, clear, *tail = epilogue
    require(next_ip[0] == "mov_i64" and next_ip[1].startswith("ip,$"),
            "missing IP write after the third slot")
    gval, genv, goff = group_op[1].split(",")
    require(group_op[0] == "st8_i32" and genv == "env" and imm(goff) == group,
            "missing instruction-group write")
    require(fault_op[0] == "st_i32" and fault_op[1].split(",")[:2] == ["$0x0", "env"]
            and imm(fault_op[1].split(",")[2]) == fault, "unexpected tail fault-slot store")
    require(clear[0] == "and_i64" and clear[1].startswith("psr,psr,$"), "missing tail RI clear")
    clear_effect = f"Effect.write .psr (.band (.input .psr) (.literal {imm(clear[1].split(',')[2])}))"
    extra_ip = "Effect.nop"
    if tail and tail[0][0] == "mov_i64" and tail[0][1].startswith("ip,$"):
        extra_ip = f"Effect.write .ip (.literal {imm(tail[0][1].split(',')[1])})"
        tail = tail[1:]
    require(len(tail) == 4 and tail[0][0] == "call" and tail[1][0] == "goto_ptr"
            and tail[2][0] == "set_label" and tail[3][0] == "exit_tb",
            "unexpected dispatcher tail")
    call = tail[0][1].split(",")
    require(len(call) == 5 and call[:3] == ["lookup_tb_ptr", "$0x6", "$1"] and call[4] == "env"
            and call[3] == tail[1][1], "unexpected lookup helper or pointer")
    successor = f"Effect.write .ip (.literal {imm(next_ip[1].split(',')[1])})"
    group_effect = f"Effect.write .group (.literal {imm(gval)})"
    return (
        hist, flips, successor, group_effect, clear_effect, extra_ip, lines[enter + 1:leave],
    )


def empty_predicate_skip(lines, qp):
    if not lines or lines[0][0] != "brcond_i64":
        return lines
    predicate = re.fullmatch(rf"p{qp},\$0x0,eq,(\$L\d+)", lines[0][1])
    require(predicate is not None and len(lines) >= 2 and lines[1] == ("set_label", predicate[1]),
            "NOP predicate branch must rejoin at its next operation")
    return lines[2:]


def fault_probe(qemu, nop_tag, add_tag):
    ip = 0x80
    for qp in (0, 1):
        raw = (8 << 37) | (10 << 20) | (9 << 13) | qp
        program = [
            (0x10, 0x11, nop_m(), nop_i(), br_cond(0x10, ip)),
            (ip, 0x00, add(0, 9, 10, qp), nop_i(), nop_i()),
        ]
        expected = {"exception": IA64_EXCP_ILLEGAL, "fault_ip": ip, "fault_imm": raw}
        if qp:
            program.append((ip + 16, 0x11, nop_m(), nop_i(), br_cond(ip + 16, ip + 16)))
            expected = {"ip": ip + 16, "r9": 0}
        trace = trace_program(qemu, program, expected, f"slot-fault-p{qp}",
                              log_flags="in_asm,op,op_opt")
        observed = observed_decode(trace, ip, 0, [
            (0, "M", add_tag, qp, raw),
            (1, "I", nop_tag, 0, 1 << 27), (2, "I", nop_tag, 0, 1 << 27),
        ])[0]
        for stage, lines in captured(trace, ip, whole_tb=False):
            take = "true"
            label = None
            if qp:
                op, arg = lines.pop(0)
                gate = re.fullmatch(rf"p{qp},\$0x0,(eq|ne),(\$L\d+)", arg)
                require(op == "brcond_i64" and gate is not None,
                        "missing dynamic qualifier around ADD r0's fault")
                label = gate[2]
                take = "predicate != 0" if gate[1] == "eq" else "predicate == 0"
            op, arg = lines[0]
            require(op == "mov_i64" and arg.startswith("ip,$"), "missing IP sync before ADD fault")
            fault_ip = imm(arg.split(",")[1])
            op, arg = lines[1]
            args = arg.split(",")
            require(op == "call" and args[:4] == ["raise_exception", "$0x8", "$0", "env"]
                    and len(args) == 8, "missing noreturn ADD fault helper")
            cause, address, fault_raw, slot = map(imm, args[4:])
            resume = "s"
            if label:
                require(lines[2] == ("set_label", label), "dynamic nullification rejoins another slot")
                op, arg = lines[3]
                require(op == "xor_i64" and arg.startswith("psr,psr,$"),
                        "nullified fault path lacks slot advancement")
                mask = imm(arg.split(",")[2])
                resume = f"{{ s with psr := s.psr ^^^ {mask}#64 }}"
                spec_resume = "SDM.restart s s.ip .first false"
            else:
                spec_resume = "s"
            name = f"{stage}_addR0_p{qp}"
            yield f"""
def {name} (s : State) (predicate : Word) : State × Option FaultRequest :=
  if {take} then
    ({{ s with ip := {fault_ip} }}, some ⟨{cause}, ⟨{address}, {slot}⟩, {fault_raw}⟩)
  else ({resume}, none)

theorem {name}_matches (s : State) (predicate : Word)
    (entryRI : SDM.ri s.psr = 0#64) :
    {name} s predicate =
      match SDM.addR0Fault {observed} {ip} .first predicate with
      | some fault => ({{ s with ip := {ip} }}, some fault)
      | none => ({spec_resume}, none) := by
  have advanced := toggledRI s.psr 0#64 1#64 entryRI (by decide)
  simp [{name}, SDM.addR0Fault, SDM.qualified, SDM.qualifier, SDM.aluDestination,
        SDM.restart, SDM.successor, Slot.number]
  all_goals split <;> simp_all [SDM.withRI] <;> bv_decide
"""


def single_step_probe(qemu, history, nop_tag):
    case = test_native_single_step_traps_predicated_off_instruction
    ip = 0x80
    trace = trace_program(qemu, case.bundles, case.expected, "slot-nop-single-step",
                          log_flags="in_asm,op,op_opt")
    raw, _, _ = observed_decode(trace, ip, 0, [
        (0, "M", nop_tag, 1, (1 << 27) | 1),
        (1, "I", nop_tag, 0, 1 << 27), (2, "I", nop_tag, 0, 1 << 27),
    ])
    for stage, lines in captured(trace, ip, whole_tb=False):
        prefix = empty_predicate_skip(lines, 1)
        op, arg = prefix[0]
        value, env, location = arg.split(",")
        require(op == "st_i64" and env == "env" and imm(location) == history,
                "single-step NOP must record the completed bundle before its request")
        op, arg = prefix[1]
        args = arg.split(",")
        require(op == "call" and args[:4] == ["check_native_traps", "$0x2", "$0", "env"]
                and len(args) == 10 and args[-1] == "psr",
                "expected first-slot native completion request using current PSR")
        target, source, slots, conditions, syndrome = map(imm, args[4:-1])
        name = f"{stage}_singleStepNop"
        yield f"""
def {name} (s : State) : Effect :=
  .sequence (.write .history (.literal {imm(value)}))
    (.native ⟨⟨{target}, {slots & 3}⟩, ⟨{source}, {(slots >> 2) & 3}⟩,
      {slots}, {conditions}, {syndrome}, s.psr⟩)

theorem {name}_matches (s : State) (collecting : SDM.collecting s.psr = true) :
    ({name} s).run s =
      ({{ s with lastSuccess := SDM.success s {ip} }},
       [.native (SDM.trapPayload s {ip} .first false)]) := by
  simp [{name}, Effect.run, Expr.eval, read, assign, SDM.success, collecting,
        SDM.trapPayload, SDM.successor, SDM.completionSlot, Slot.number]

example : SDM.nopMI {raw} = true ∧ SDM.qualifier {raw} = 1 := by decide
"""


def proof(name, projected, raw, qualifier, ic, stop):
    hist, flips, successor, group, clear, extra_ip, alu = projected
    fx = f"{name}Effects"
    bit = str(ic).lower()
    stop = str(stop).lower()
    data = project(f"{name}Data", alu)
    nop1, add_raw, nop2 = raw
    data_spec = f"""(if SDM.qualified {add_raw} pred then IA64Integer.SDM.ADD left right else old,
       if SDM.qualified {add_raw} pred then IA64Integer.SDM.replaceNaT tags 8
          (IA64Integer.SDM.staticNaT tags 9 || IA64Integer.SDM.staticNaT tags 10) else tags)"""
    code = f"""
{data}

theorem {name}_decoded :
    SDM.nopMI {nop1} = true ∧ SDM.nopMI {nop2} = true ∧
    SDM.aluDestination {add_raw} = 8 ∧ SDM.aluSource1 {add_raw} = 9 ∧
    SDM.aluSource2 {add_raw} = 10 ∧ SDM.qualifier {add_raw} = {qualifier} := by decide

def {fx}ToAdd : Effect := .sequence ({hist}) ({flips[0]})

def {fx}AfterAdd : Effect := {flips[1]}

def {fx}ToLastNop : Effect := .sequence {fx}ToAdd {fx}AfterAdd

def {fx}ToSuccessor : Effect :=
  .sequence {fx}ToLastNop (.sequence ({flips[2]}) ({successor}))

def {fx} : Effect :=
  .sequence {fx}ToSuccessor (.sequence ({group}) (.sequence ({clear}) ({extra_ip})))

theorem {name}_boundaries (s : State) (ri : SDM.ri s.psr = 0#64) :
    SDM.ri ({fx}ToAdd.run s).1.psr = 1#64 ∧
    SDM.ri ({fx}ToLastNop.run s).1.psr = 2#64 ∧
    SDM.ri ({fx}ToSuccessor.run s).1.psr = 0#64 ∧
    ({fx}ToSuccessor.run s).1.ip = {NEXT} := by
  simp [{fx}ToAdd, {fx}AfterAdd, {fx}ToLastNop, {fx}ToSuccessor,
        Effect.run, Expr.eval, read, assign, SDM.ri] at *
  all_goals bv_decide

theorem {name}_common (s : State) (ri : SDM.ri s.psr = 0#64)
    (ic : SDM.collecting s.psr = {bit}) :
    {fx}.run s =
      ({{ s with ip := {NEXT}, psr := SDM.withRI s.psr 0,
                  lastSuccess := SDM.success s {BUNDLE}, groupStart := {stop} }}, []) := by
  simp [Effect.run, {fx}, {fx}ToAdd, {fx}AfterAdd, {fx}ToLastNop, {fx}ToSuccessor,
        Expr.eval, read, assign, SDM.success, ic, SDM.withRI]
  all_goals bv_decide

theorem {name}_data (old left right pred tags : Word) :
    {name}Data old left right pred tags = {data_spec} := by
  apply Prod.ext
  · simp [{name}Data, SDM.qualified, SDM.qualifier, IA64Integer.SDM.ADD] <;> bv_decide
  · simp [{name}Data, SDM.qualified, SDM.qualifier, IA64Integer.SDM.replaceNaT,
          IA64Integer.SDM.staticNaT, IA64Integer.TCG.deposit] <;> bv_decide

theorem {name}_nop (s : State) (ip : s.ip = {BUNDLE}) (ri : SDM.ri s.psr = 0#64)
    (ic : SDM.collecting s.psr = {bit}) :
    {fx}ToAdd.run s =
      (SDM.restart {{ s with lastSuccess := SDM.success s {BUNDLE} }} {BUNDLE} .first false, []) := by
  have flip := toggledRI s.psr 0#64 1#64 ri (by decide)
  simp [Effect.run, {fx}ToAdd, Expr.eval, read, assign, SDM.restart, SDM.successor,
        SDM.success, ic, ip]
  all_goals exact flip

theorem {name}_add (s : State) (old left right pred tags : Word)
    (ip : s.ip = {BUNDLE}) (ri : SDM.ri s.psr = 1#64)
    (shared : SDM.collecting s.psr = true → s.lastSuccess = {BUNDLE}) :
    ({fx}AfterAdd.run s, {name}Data old left right pred tags) =
      ((SDM.restart {{ s with lastSuccess := SDM.success s {BUNDLE} }} {BUNDLE} .second false, []),
        {data_spec}) := by
  rw [{name}_data]
  have flip := toggledRI s.psr 1#64 2#64 ri (by decide)
  cases collecting : SDM.collecting s.psr
  · simp [Effect.run, {fx}AfterAdd, Expr.eval, read, assign, SDM.restart, SDM.successor,
          SDM.success, collecting, ip]
    all_goals exact flip
  · have history := shared collecting
    simp [Effect.run, {fx}AfterAdd, Expr.eval, read, assign, SDM.restart, SDM.successor,
          SDM.success, collecting, ip, history]
    all_goals exact flip

theorem {name}_step (s : State) (old left right pred tags : Word)
    (ri : SDM.ri s.psr = 0#64) (ic : SDM.collecting s.psr = {bit}) :
    ({fx}.run s, {name}Data old left right pred tags) =
    (({{ s with ip := {NEXT}, psr := SDM.withRI s.psr 0,
                 lastSuccess := SDM.success s {BUNDLE}, groupStart := {stop} }}, []),
      {data_spec}) := by
  rw [{name}_common s ri ic, {name}_data]
"""
    return code


def generate(qemu, build, path):
    locations, (nop_tag, add_tag) = offsets(build, path)
    result = ["namespace IA64Slot", "set_option linter.unusedVariables false"]
    for qualifier, ic, stop, pred in (
        (0, False, True, True), (2, False, False, False),
        (2, True, True, False), (2, True, True, True),
    ):
        program, expected = bundle(ic, qualifier, pred, stop)
        name = f"p{qualifier}_ic{int(ic)}_stop{int(stop)}_pred{int(pred)}"
        trace = trace_program(qemu, program, expected, f"slot-{name}", log_flags="in_asm,op,op_opt")
        # MII in SDM vol. 3 Table 2-1 and I18/M48/A1 format fields.
        nop_raw = (1 << 27) | 2
        add_raw = (8 << 37) | (10 << 20) | (9 << 13) | (8 << 6) | qualifier
        raw = observed_decode(trace, BUNDLE, int(stop), [
            (0, "M", nop_tag, 2, nop_raw), (1, "I", add_tag, qualifier, add_raw),
            (2, "I", nop_tag, 2, nop_raw),
        ])
        result.extend(proof(f"{stage}_{name}", reify(lines, locations, ic), raw, qualifier, ic, stop)
                      for stage, lines in captured(trace))
    result.extend(fault_probe(qemu, nop_tag, add_tag))
    result.extend(single_step_probe(qemu, locations[0], nop_tag))
    return "\n\n".join([*result, "end IA64Slot"])
