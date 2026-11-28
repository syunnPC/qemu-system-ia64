# SPDX-License-Identifier: GPL-2.0-or-later
"""Instantiate the resident frame theorems at live TCG helper calls."""

import re

from Emitted.check import imm
from HostLitmus.replay import trace_program
from ia64.encoding import (
    br_cond,
    cover_b,
    ld8,
    ld8_postinc,
    mov_ar,
    mov_i_imm_ar,
    mov_m_ar_gr,
    mov_m_cr_gr,
    movl_mlx,
    nop_i,
    nop_m,
    raw_bundle,
)

ALLOC_IP = 0x20
COVER_IP = 0x30


def capture(qemu):
    # ALLOC has a valid p0 group entry,
    # and COVER is the group's stop. Reset starts with CPL=VM=PSR.ic=0.
    alloc = (1 << 37) | (6 << 33) | (1 << 27) | (9 << 20) | (16 << 13) | (5 << 6)
    bundles = [
        (0x10, 0x01, nop_m(), mov_i_imm_ar(64, 0x23), nop_i()),
        (ALLOC_IP, 0x01, alloc, nop_i(), nop_i()),
        (COVER_IP, 0x11, nop_m(), nop_i(), 0x10000000),
        (0x40, 0x09, mov_m_ar_gr(6, 17), mov_m_cr_gr(7, 23), nop_i()),
        (0x50, 0x11, nop_m(), nop_i(), br_cond(0x50, 0x50)),
    ]
    expected = {"ip": 0x50, "r5": 0x23, "r5_nat": 0, "r6": 0x80, "r7": 0x8000000000004490}
    return trace_program(qemu, bundles, expected, "rse-frame", log_flags="op,op_opt")


def slices(trace, addresses=(ALLOC_IP, COVER_IP)):
    pairs = re.findall(
        r"^OP:\n(.*?)^OP after optimization and liveness analysis:\n(.*?)(?=^OP:\n|\Z)",
        trace, re.M | re.S,
    )
    markers = re.compile(r"^\s*----\s+([\da-fA-F]+)[^\n]*\n", re.M)
    for index, stage in enumerate(("emitted", "optimized")):
        slices_by_address = {}
        for pair in pairs:
            marks = list(markers.finditer(pair[index]))
            for i, mark in enumerate(marks):
                addr = int(mark[1], 16)
                if addr not in addresses:
                    continue
                if addr in slices_by_address:
                    raise ValueError(f"{stage}: multiple frame slices at {addr:#x}")
                end = marks[i + 1].start() if i + 1 < len(marks) else len(pair[index])
                slices_by_address[addr] = [
                    " ".join(line.split()[:2])
                    for line in pair[index][mark.end():end].splitlines() if line.strip()
                ]
        if slices_by_address.keys() != set(addresses):
            raise ValueError(f"{stage}: expected RSE slices at {addresses}")
        yield stage, *(slices_by_address[address] for address in addresses)


def alloc_operands(stage, alloc, expected_ip):
    alloc_call = re.fullmatch(
        r"call alloc_rse,\$0x0,\$0,env,(\$0x[\da-f]+),(\$0x[\da-f]+),"
        r"(\$0x[\da-f]+),(\$0x[\da-f]+)", alloc[0],
    )
    if not alloc_call:
        raise ValueError(f"{stage}: expected direct ALLOC helper at instruction entry")
    r, packed, ip, slot = map(imm, alloc_call.groups())
    if r >= 128 or packed >= 1 << 32 or (ip, slot) != (expected_ip, 0):
        raise ValueError(f"{stage}: invalid ALLOC helper operands")
    return r, packed


def project(stage, alloc, cover):
    r, packed = alloc_operands(stage, alloc, ALLOC_IP)
    try:
        dispatch = cover.index("call cover_rse,$0x0,$0,env")
    except ValueError as exc:
        raise ValueError(f"{stage}: expected COVER helper") from exc
    prefix = cover[:dispatch]
    updates = ["xor_i64 psr,psr,$0x20000000000", "xor_i64 psr,psr,$0x60000000000"]
    if prefix[:2] != updates:
        raise ValueError(f"{stage}: unexpected COVER slot-entry operations")
    if stage == "emitted":
        setup, branch, write_ip, raise_fault, label = prefix[2:]
        if write_ip != f"mov_i64 ip,${COVER_IP:#x}":
            raise ValueError(f"{stage}: unexpected COVER fault IP setup")
    else:
        setup, branch, raise_fault, label = prefix[2:]
    mask = re.fullmatch(r"and_i64 (\w+),psr,(\$0x[\da-f]+)", setup)
    test = re.fullmatch(r"brcond_i64 (\w+),(\$0x[\da-f]+),ne,(\$L\d+)", branch)
    if (not mask or not test or mask[1] != test[1] or label != f"set_label {test[3]}" or
            raise_fault != f"call raise_exception,$0x8,$0,env,$0x21,${COVER_IP:#x},$0x10000000,$0x2"):
        raise ValueError(f"{stage}: unexpected COVER virtualization path")

    return f"""
namespace RseImplementation
open RseSpec

theorem {stage}_alloc_live (s : FrameState) (old : Word) (nat : Bool)
    (history : s.complete)
    (rotation : (1 : BitVec 4) = s.cfm.sor ∨ s.cfm.gr = 0 ∧ s.cfm.fr = 0 ∧ s.cfm.pr = 0)
    (resident : (16 : Int) - s.cfm.sof.toNat ≤ s.invalid + s.clean) :
    allocResidentC s {packed} {r} old nat =
      (allocResident s 16 9 1, allocDestination s) := by
  exact allocResident_matches s 16 9 1 5 old nat 0 0 true false
    (by simp [allocSite]) history (by simp [Frame.valid]) (by decide) (by decide) rotation resident

theorem {stage}_cover_guard (psr : Word) :
    (((psr ^^^ 0x20000000000 ^^^ 0x60000000000) &&& {imm(mask[2])}) ≠ {imm(test[2])}) =
      (psr.extractLsb' 32 2 ≠ 0 ∨ psr.getLsbD 46 = false) := by
  bv_decide

theorem {stage}_cover_live (s : FrameState) (history : s.complete)
    (allowed : ((s.psr ^^^ 0x20000000000 ^^^ 0x60000000000) &&& {imm(mask[2])}) ≠ {imm(test[2])}) :
    coverFrameC s = coverFrame s := by
  rw [{stage}_cover_guard] at allowed
  exact coverFrame_matches s true true ⟨rfl, Or.inr allowed⟩ history

end RseImplementation
"""


def capture_spill(qemu):
    address = 0x1001f0
    spill_ip = 0x70
    alloc_full = (1 << 37) | (6 << 33) | (96 << 20) | (96 << 13) | (5 << 6)
    alloc_two = (1 << 37) | (6 << 33) | (2 << 20) | (2 << 13) | (5 << 6)
    bundles = [
        (0x10, *movl_mlx(3, address)),
        (0x20, 0x01, mov_ar(3, 18), nop_i(), nop_i()),
        (0x30, 0x01, alloc_full, nop_i(), nop_i()),
        (0x40, *movl_mlx(32, 0x1111)),
        (0x50, *movl_mlx(33, 0x2222)),
        (0x60, 0x18, nop_m(), nop_m(), cover_b()),
        (spill_ip, 0x01, alloc_two, nop_i(), nop_i()),
        (0x80, 0x01, ld8_postinc(8, 3, 8), nop_i(), nop_i()),
        (0x90, 0x01, ld8_postinc(9, 3, 8), nop_i(), nop_i()),
        (0xa0, 0x01, ld8(10, 3), nop_i(), nop_i()),
        (0xb0, 0x01, mov_m_ar_gr(11, 18), nop_i(), nop_i()),
        (0xc0, 0x11, nop_m(), nop_i(), br_cond(0xc0, 0xc0)),
        raw_bundle(address, 0, 4),
    ]
    expected = {"ip": 0xc0, "r8": 0x1111, "r9": 4, "r10": 0x2222, "r11": address + 24}
    trace = trace_program(qemu, bundles, expected, "mandatory-alloc-spill", log_flags="op,op_opt")
    for stage, alloc in slices(trace, (spill_ip,)):
        r, packed = alloc_operands(stage, alloc, spill_ip)
        yield f"""
namespace RseImplementation
open RseSpec

theorem {stage}_mandatory_alloc_spill :
    let result := spillRamTraceC liveSpillInput liveSpillStart
      (mandatorySpillNeedC liveSpillFrame {packed}) 3
    ({r} : BitVec 7) = 5 ∧
    result.writes = [⟨{address}, 0x1111, false⟩, ⟨{address + 8}, 4, false⟩, ⟨{address + 16}, 0x2222, false⟩] ∧
    result.stop = .retired ∧ result.state.counters.bspstore = {address + 24} := by decide

end RseImplementation
"""


def generate(qemu):
    frames = (project(stage, alloc, cover) for stage, alloc, cover in slices(capture(qemu)))
    return "\n".join([*frames, *capture_spill(qemu)])
