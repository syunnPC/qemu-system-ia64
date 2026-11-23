# SPDX-License-Identifier: GPL-2.0-or-later
"""Extract factual coverage identifiers from pinned Intel SDM volumes."""

import hashlib
import re
import subprocess
from pathlib import Path

MANUALS = {
    1: ("245317-006", "8a6b8dafa6c2d1b52cdee89c7f89dd8f2d42b67f9c31bbbaac897f56f6e5492d"),
    2: ("245318-006", "f08296c0c7ded2e4c9fd47555e43cc7499b07f40fcecf007cc1a3aef5949b029"),
    3: ("323207-001", "67cdd91b3d3781fc432fc1c2c1b31053d1f611ddc5064209431fc32029ffe6f2"),
    4: ("323208-001", "b1c6a84ceb532670de204a8f3e325f31f86ccf351935fd50b5a4d46be959ec45"),
}
NATIVE_PAGE = re.compile(
    r"Volume 3: Instruction Reference +3:(\d+)|3:(\d+) +Volume 3: Instruction Reference"
)
NATIVE_FORM = re.compile(
    r"(?m)^ *\(?qp\)? +([a-z][a-z0-9.]*).*? +([AIBMFX]\d+|pseudo-op)\b"
    r"|^ *([a-z][a-z0-9.]*) +.*? +([AIBMFX]\d+|pseudo-op)\b",
    re.MULTILINE,
)
IA32_HEADING = re.compile(
    r"^([A-Z][A-Z0-9_.]*(?:cc|n)?"
    r"(?:[/, ]+[A-Z][A-Z0-9_.]*(?:cc|n)?)*)\s*(?:-?[—–]|: )\s*.+$"
)
PAL_ENTRY = re.compile(
    r"(?m)^(?:\f?Table (11-\d+)\.|(PAL_[A-Z0-9_]+)[a-z]? +(\d+) +(?:Req|Opt)\.)"
)
SECTION = re.compile(
    r"(?m)^ *([2-9]|1[0-3])((?:\.\d+)*) +[^\n]+? *"
    r"(?:(?:\. *){2,}|\n {10,})([12]:\d+) *$"
)


def extract_native(manual):
    for page in manual.split("\f"):
        found = NATIVE_PAGE.search(page)
        body = re.search(r"(?ms)^( *)Format:(.*?)^ *(?:Description|Operation):", page)
        if not (found and body):
            continue
        number = next(int(group) for group in found.groups() if group)
        name = page.strip().splitlines()[0].strip()
        family = "-".join(re.findall(r"[a-z0-9.]+", name))
        # Chapter 2 sometimes continues a format block on the next page.
        # The chapter 4 section rows also require comprehensive format coverage.
        yield f"sdm-native:{family}", f"v3 p.3:{number}"
        # Preserve the column of the first mnemonic for wrapped rows.
        block = " " * (len(body[1]) + len("Format:")) + body[2]
        for index, (mnemonic, form) in enumerate(native_format_rows(block, number), start=1):
            yield f"sdm-form:{family}:{mnemonic}:{form}:{index}", f"v3 p.3:{number}"


def native_format_rows(block, page):
    pending = ""
    indent = 0
    for line in block.splitlines():
        text = line.strip()
        if not text:
            continue
        column = len(line) - len(line.lstrip())
        if pending:
            if column <= indent or NATIVE_FORM.match(text):
                raise ValueError(f"incomplete native Format row on page 3:{page}: {pending}")
            text = f"{pending} {text}"
        else:
            indent = column
        match = NATIVE_FORM.match(text)
        if match:
            yield match[1] or match[3], match[2] or match[4]
            pending = ""
        else:
            pending = text
    if pending:
        raise ValueError(f"incomplete native Format row on page 3:{page}: {pending}")


def extract_ia32(manual):
    references = {}
    for page in manual.split("\f"):
        if not page.strip():
            continue
        page_number = re.search(r"(?m)^ *4:(\d+) +Volume 4:|Volume 4:.*? +4:(\d+) *$", page)
        heading = IA32_HEADING.fullmatch(page.strip().splitlines()[0])
        if not (heading and page_number):
            continue
        number = next(group for group in page_number.groups() if group)
        for mnemonic in re.split(r"[/, ]+", heading[1].strip()):
            references.setdefault(mnemonic, []).append(f"4:{number}")
    for mnemonic, pages in references.items():
        yield f"sdm-ia32:{mnemonic}", f"v4 {', '.join(pages)}"


def check_instruction_index(rows, manual):
    index = re.split(r"\f\s*Index\s*\n", manual, maxsplit=1)[1]
    pages = {int(number) for number in re.findall(r"\b3:(\d+)\b", index)
             if 14 <= int(number) <= 280}
    found = {int(reference.split(":")[-1]) for key, reference in rows.items()
             if key.startswith("sdm-native:")}
    # The alphabetical index puts these ten reference pages under mov.
    grouped_mov_pages = {174, 175, 177, 178, 179, 180, 183, 184, 185, 186}
    if found != pages | grouped_mov_pages:
        raise ValueError("native instruction pages disagree with the SDM index")
    # The combined alphabetical index is printed in Volume 3.
    ia32_pages = {int(number) for key, reference in rows.items()
                  if key.startswith("sdm-ia32:")
                  for number in re.findall(r"\b4:(\d+)\b", reference)}
    mnemonics = re.findall(
        r"\b[A-Z][A-Za-z0-9.]+ Instruction ((?:[1-4]:\d+(?:, )?)+)", index
    )
    indexed_pages = {int(number) for references in mnemonics
                     for number in re.findall(r"\b4:(\d+)\b", references)}
    missing = indexed_pages - ia32_pages
    if missing:
        raise ValueError(f"missing IA-32 instruction index pages: {sorted(missing)}")


def extract_sections(volume, manual):
    contents = manual.split("\fContents", 1)[1].split("\nFigures", 1)[0]
    guide, programmer = contents.replace("\f", "\n").split("Part II:", 1)
    for entry in SECTION.finditer(guide):
        yield f"sdm-section:v{volume}:{entry[1]}{entry[2]}", f"v{volume} p.{entry[3]}"
    if volume == 2:
        for entry in SECTION.finditer(programmer):
            yield (f"sdm-section:v2-p2:{entry[1]}{entry[2]}",
                   f"v2 Part II p.{entry[3]}")


def extract_catalog(manuals):
    rows = dict(extract_native(manuals[3]))
    rows.update(extract_ia32(manuals[4]))
    check_instruction_index(rows, manuals[3])
    for volume in (1, 2):
        rows.update(extract_sections(volume, manuals[volume]))
    # Include the encoding-table sections in addition to Chapter 2 formats.
    for match in re.finditer(
        r"(?m)^ *(4\.[1-8](?:\.\d+)?) +.+? *(?:\. *){3,}(3:\d+) *$",
        manuals[3].split("Figures", 1)[0],
    ):
        rows[f"sdm-section:v3:{match[1]}"] = f"v3 p.{match[2]}"
    pal_table = manuals[2].split("\n11.10.1 PAL Procedure Summary\n", 1)[1]
    pal_table = pal_table.split("\n11.10.2", 1)[0]
    for match in PAL_ENTRY.finditer(pal_table):
        if match[1]:
            table = match[1]
        else:
            rows[f"sdm-pal:{match[2]}"] = f"v2 Table {table}, index {match[3]}"
    for kind in ("sdm-native", "sdm-form", "sdm-ia32", "sdm-section", "sdm-pal"):
        if not any(key.startswith(f"{kind}:") for key in rows):
            raise ValueError(f"empty SDM inventory category {kind}")
    return rows


def from_pdfs(directories):
    """Verify local PDFs by content so their filenames can vary."""
    expected = {digest: volume for volume, (_, digest) in MANUALS.items()}
    paths = {}
    for directory in directories:
        if not directory.is_dir():
            raise ValueError(f"manual directory does not exist: {directory}")
        for path in sorted(directory.glob("*.pdf")):
            digest = hashlib.sha256(path.read_bytes()).hexdigest()
            if digest in expected:
                paths[expected[digest]] = path
    missing = sorted(set(MANUALS) - set(paths))
    if missing:
        documents = ", ".join(MANUALS[volume][0] for volume in missing)
        raise ValueError(f"pinned SDM PDFs missing: {documents}")
    return extract_catalog(
        {
            volume: subprocess.check_output(
                ["pdftotext", "-layout", str(path), "-"], encoding="utf-8"
            )
            for volume, path in paths.items()
        }
    )


def read_catalog(path: Path):
    rows = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        if not line or line.startswith("#"):
            continue
        key, reference = line.split("\t")
        if key in rows:
            raise ValueError(f"duplicate SDM entry {key}")
        rows[key] = reference
    if not rows:
        raise ValueError("empty SDM inventory")
    return rows
