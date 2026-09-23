#!/usr/bin/env python3
"""ABI-critical layout checks for the project IA-64 firmware image."""

from __future__ import annotations

import os
import re
import subprocess
import sys


FW_LOAD_BASE = 0x00100000
FW_MAX_SPAN = 0x00200000
RUNTIME_ALIGNMENT = 0x2000


def command(argv: list[str]) -> str:
    result = subprocess.run(argv, check=False, stdout=subprocess.PIPE,
                            stderr=subprocess.STDOUT, text=True)
    if result.returncode != 0:
        raise RuntimeError(f"{' '.join(argv)} failed\n{result.stdout}")
    return result.stdout


def symbols(elf: str) -> dict[str, int]:
    output = command(["ia64-linux-gnu-nm", "-n", elf])
    result = {}
    for line in output.splitlines():
        match = re.match(r"^([0-9a-fA-F]+)\s+\S\s+(\S+)$", line)
        if match:
            result[match.group(2)] = int(match.group(1), 16)
    return result


def allocated_sections(elf: str):
    output = command(["ia64-linux-gnu-objdump", "-h", elf])
    lines = output.splitlines()
    result = []
    header = re.compile(
        r"^\s*\d+\s+(\S+)\s+([0-9a-fA-F]+)\s+([0-9a-fA-F]+)\s+"
        r"([0-9a-fA-F]+)\s+[0-9a-fA-F]+\s+2\*\*(\d+)")
    for index, line in enumerate(lines[:-1]):
        match = header.match(line)
        if match and "ALLOC" in lines[index + 1]:
            name, size, vma, lma, alignment = match.groups()
            result.append((name, int(vma, 16), int(size, 16),
                           1 << int(alignment), int(lma, 16)))
    return result


def run_checks(binary: str, elf: str):
    if not os.path.isfile(binary) or not os.path.isfile(elf):
        raise RuntimeError("firmware .bin and .elf must both exist")
    yield "firmware artifacts exist"

    undefined = command(["ia64-linux-gnu-nm", "-u", elf]).strip()
    if undefined:
        raise RuntimeError("firmware has unresolved symbols:\n" + undefined)
    yield "firmware has no unresolved symbols"

    sym = symbols(elf)
    required = ("_start", "_end", "_bss_end", "__firmware_payload_end",
                "__gp", "pal_proc_entry",
                "sal_proc_gp_anchor", "sal_proc_entry", "sal_proc_dispatch",
                "__runtime_code_start", "__runtime_data_start")
    missing = [name for name in required if name not in sym]
    if missing:
        raise RuntimeError("missing ABI linker symbols: " + ", ".join(missing))
    if sym["_start"] != FW_LOAD_BASE or not (
            0 < sym["_end"] - FW_LOAD_BASE < FW_MAX_SPAN):
        raise RuntimeError(
            f"firmware address range {sym['_start']:#x}-{sym['_end']:#x} "
            "exceeds the firmware fixed-span limit")
    if sym["_bss_end"] != sym["__firmware_payload_end"] or not (
            FW_LOAD_BASE < sym["__firmware_payload_end"] == sym["_end"]):
        raise RuntimeError(
            "firmware payload does not identify the fixed-span end")
    with open(binary, 'rb') as stream:
        if stream.read(4) != b'\x7fELF':
            raise RuntimeError("firmware .bin must contain the relocatable ELF")
    yield "firmware fixed span is less than 2 MiB"

    elf_header = command(["ia64-linux-gnu-readelf", "-h", elf])
    entry = re.search(r"Entry point address:\s+0x([0-9a-fA-F]+)", elf_header)
    if entry is None or int(entry.group(1), 16) != sym["_start"]:
        raise RuntimeError("ELF entry does not identify the firmware entry")
    disassembly = command(["ia64-linux-gnu-objdump", "-d", elf])
    start = re.search(
        r"<start_after_pal>:(.*?)(?=\n[0-9a-fA-F]{16} <|\Z)",
        disassembly, re.DOTALL)
    gp = re.search(r"movl r1=0x([0-9a-fA-F]+)", start.group(1) if start else "")
    if start is None or gp is None or int(gp.group(1), 16) != sym["__gp"]:
        raise RuntimeError("entry does not establish the linked IA-64 GP")
    yield "entry address and GP handoff are valid"

    if sym["pal_proc_entry"] & 0xf or \
            not (FW_LOAD_BASE <= sym["pal_proc_entry"] < sym["_end"]):
        raise RuntimeError("PAL entry is misaligned or outside firmware")
    if sym["__runtime_code_start"] % RUNTIME_ALIGNMENT or \
            sym["__runtime_data_start"] % RUNTIME_ALIGNMENT or \
            not (sym["pal_proc_entry"] < sym["__runtime_code_start"] <=
                 sym["__runtime_data_start"] < sym["__runtime_end"]):
        raise RuntimeError("runtime section boundary/alignment is invalid")
    yield "PAL and runtime boundaries are valid"

    if not (sym["__runtime_code_start"] <= sym["sal_proc_gp_anchor"] <
            sym["__runtime_data_start"] and
            sym["__runtime_code_start"] <= sym["sal_proc_entry"] <
            sym["__runtime_data_start"] and
            sym["__runtime_code_start"] <= sym["sal_proc_dispatch"] <
            sym["__runtime_data_start"] and
            sym["__runtime_data_start"] <= sym["__gp"] < sym["__runtime_end"]):
        raise RuntimeError("SAL entry, dispatcher, or GP has the wrong usage")
    sal_entry = re.search(
        r"<sal_proc_entry>:(.*?)(?=\n[0-9a-fA-F]{16} "
        r"<fpswa_emulation_entry>:|\Z)",
        disassembly, re.DOTALL)
    sal_entry_text = sal_entry.group(1) if sal_entry else ""
    sal_physical = re.search(
        r"\n[0-9a-fA-F]{16} <sal_proc_physical>:",
        sal_entry_text)
    sal_virtual_bridge = (
        sal_entry_text[:sal_physical.start()] if sal_physical else "")
    if not re.search(r"\bmov\s+r26=ip\b", sal_virtual_bridge) or \
            re.search(r"\bbr\.call\b", sal_virtual_bridge):
        raise RuntimeError(
            "SAL entry clobbers ar.pfs before saving the caller's frame")
    required_sal_entry_ops = (
        r"\bflushrs\b",
        r"\binvala\b",
        r"\bmov(?:\.[a-z]+)*\s+ar\.bspstore\s*=",
        r"\bmov(?:\.[a-z]+)*\s+ar\.rnat\s*=",
        r"\bmov(?:\.[a-z]+)*\s+ar\.rsc\s*=",
        r"\brfi\b",
        r"\bbr\.call(?:\.[a-z]+)*\s+(?:b0\s*=\s*)?"
        r"[0-9a-f]+\s+<sal_proc_dispatch>",
    )
    if any(not re.search(pattern, sal_entry_text)
           for pattern in required_sal_entry_ops):
        raise RuntimeError(
            "SAL entry does not isolate and restore the caller's RSE state")
    yield "SAL entry isolates and restores caller RSE state"

    sections = sorted(allocated_sections(elf), key=lambda item: item[1])
    if not sections:
        raise RuntimeError("firmware ELF has no allocated sections")
    previous_end = 0
    for name, address, size, alignment, load_address in sections:
        if address != load_address or address % alignment:
            raise RuntimeError(f"section {name} has invalid load/alignment")
        if size and address < previous_end:
            raise RuntimeError(f"allocated section {name} overlaps its predecessor")
        previous_end = max(previous_end, address + size)
    if previous_end > sym["__firmware_payload_end"]:
        raise RuntimeError("allocated section extends beyond firmware payload")
    yield "allocated firmware sections do not overlap"


def main() -> int:
    if len(sys.argv) != 3:
        print("Bail out! usage: test-ia64-firmware-layout.py BIN ELF")
        return 1
    print("TAP version 13")
    print("1..7")
    try:
        for index, name in enumerate(run_checks(sys.argv[1], sys.argv[2]), 1):
            print(f"ok {index} - {name}")
    except Exception as exc:
        index = locals().get("index", 0) + 1
        print(f"not ok {index} - firmware layout")
        for line in str(exc).splitlines():
            print(f"# {line}")
        for rest in range(index + 1, 8):
            print(f"not ok {rest} - skipped after layout failure")
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
