#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Check the IA-64 bundle and decoder against SDM encodings."""

import argparse
import os
from pathlib import Path
import re
import subprocess
import tempfile

if __package__:
    from .cases import check_slots
else:
    from cases import check_slots

HERE = Path(__file__).resolve().parent
PROJECT = HERE.parent
ROOT = PROJECT.parents[1]

FUNCTIONS = (
    ("ia64_bits", None),
    ("ia64_b_op", "major"),
    ("ia64_sign_extend", None),
    ("ia64_immu21", "imm21"),
    ("ia64_imm14", "imm14"),
    ("ia64_imm8", "imm8"),
    ("ia64_pr_mask", "mask17"),
    ("ia64_pr_rot_imm", "imm44"),
    ("ia64_psr_mask", "imm24"),
    ("ia64_imm22", "imm22"),
    ("ia64_branch_disp", "branchDisplacement"),
    ("ia64_mlx_x1_imm62", "imm62"),
    ("ia64_mlx_brl_disp", "longDisplacement"),
    ("ia64_chk_disp", "checkDisplacement"),
    ("ia64_chk_a_disp", "branchDisplacement"),
)
PRECEDENCE = {
    "|": 1,
    "^": 2,
    "&": 3,
    "<<": 4,
    ">>": 4,
    "+": 5,
    "-": 5,
    "*": 6,
}
LEAN_OP = {"|": "|||", "^": "^^^", "&": "&&&", "<<": "<<<", ">>": ">>>"}
TOKEN = re.compile(
    r"0[xX][0-9a-fA-F]+(?:ULL)?|[0-9]+(?:ULL)?|[a-zA-Z_]\w*"
    r"|<<|>>|[()~+*\-&,|^]"
)
UINT64 = ("uint64_t", "int64_t", "unsigned", "uint8_t")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def get_match(pattern, source, context, *, full=False):
    found = (re.fullmatch if full else re.search)(pattern, source, re.S | re.M)
    require(found is not None, f"unrecognized C in {context}")
    return found


def uncomment(source):
    return re.sub(r"/\*.*?\*/|//[^\n]*", "", source, flags=re.S)


def c_function(source, name):
    # The final brace at column zero is the end of the selected function.
    return get_match(
        rf"\b{re.escape(name)}\s*\(([^{{}};]*)\)\s*\{{(.*?)^\}}",
        source,
        name,
        full=False,
    ).groups()


class Expression:
    """Project the selected scalar C expressions into 64-bit bitvectors."""

    def __init__(self, source, names, functions):
        self.source = source
        self.names = names
        self.functions = functions
        compact = re.sub(r"\s+", "", source)
        self.tokens = TOKEN.findall(source)
        require("".join(self.tokens) == compact, f"unsupported C expression: {source}")
        self.pos = 0

    def peek(self):
        return self.tokens[self.pos] if self.pos < len(self.tokens) else None

    def take(self, expected=None):
        token = self.peek()
        require(token is not None and (expected is None or token == expected),
                f"unexpected token in C expression: {self.source}")
        self.pos += 1
        return token

    def atom(self):
        token = self.take()
        if token == "(":
            if self.peek() in ("uint64_t", "int64_t"):
                self.take()
                self.take(")")
                return self.atom()
            value = self.binary(0)
            self.take(")")
            return value
        if token == "~":
            return f"(~~~{self.atom()})"
        if token.isdigit() or re.fullmatch(r"(?:0[xX][0-9a-fA-F]+|[0-9]+)ULL|0[xX][0-9a-fA-F]+", token):
            return f"({int(token.removesuffix('ULL'), 0)} : Word)"
        if self.peek() == "(":
            require(token in self.functions, f"unsupported C call: {token}")
            self.take("(")
            args = [self.binary(0)]
            while self.peek() == ",":
                self.take(",")
                args.append(self.binary(0))
            self.take(")")
            require(len(args) == self.functions[token], f"wrong arity for {token}")
            return f"(c_{token} {' '.join(args)})"
        require(token in self.names, f"unsupported C identifier: {token}")
        return self.names[token]

    def binary(self, minimum):
        value = self.atom()
        while PRECEDENCE.get(self.peek(), 0) > minimum:
            op = self.take()
            rhs = self.binary(PRECEDENCE[op])
            value = f"({value} {LEAN_OP.get(op, op)} {rhs})"
        return value

    def lean(self):
        value = self.binary(0)
        require(self.peek() is None, f"trailing tokens in C expression: {self.source}")
        return value


def expression(source, names, functions=None):
    return Expression(source, names, functions or {}).lean()


def scalar_declarations(statements, names, functions):
    result = []
    for statement in statements:
        update = re.fullmatch(r"\s*(\w+)\s*&=\s*(.*)", statement, re.S)
        if update:
            var, rhs = update.groups()
            require(var in names, f"assignment to unknown C name: {var}")
            result.append(f"  let {var} := {expression(f'{var} & ({rhs})', names, functions)}")
            continue
        var, rhs = get_match(
            r"(?:const\s+)?(?:uint64_t|int64_t)\s+(\w+)\s*=\s*(.*)",
            statement.strip(),
            statement,
            full=True,
        ).groups()
        result.append(f"  let {var} := {expression(rhs, names, functions)}")
        require(var not in names, f"shadowing in selected declaration: {var}")
        names[var] = var
    return result


def statements(body):
    parts = body.strip().split(";")
    require(not parts[-1].strip() and len(parts) > 1, "invalid C statement list")
    return parts[:-1]


def scalar_function(source, name, functions):
    params, body = c_function(source, name)
    names = {}
    for param in params.split(","):
        c_type, var = param.split()
        require(c_type in UINT64 and var not in names, f"unsupported parameter: {param}")
        names[var] = var
    args = tuple(names)
    parts = statements(body)
    lines = scalar_declarations(parts[:-1], names, functions)
    rhs = get_match(r"\s*return\s+(.*)", parts[-1], name, full=True)[1]
    lines.append(f"  {expression(rhs, names, functions)}")
    return args, f"def c_{name} ({' '.join(args)} : Word) : Word :=\n" + "\n".join(lines)


def prove(name, args, lhs, rhs):
    binders = f" ({' '.join(args)} : Word)" if args else ""
    return f"theorem {name}{binders} : {lhs} = {rhs} := by BITVECTOR_PROOF\n"


def project_movl(source, functions):
    params, body = c_function(source, "ia64_fill_mlx_movl")
    get_match(r"Ia64Instruction \*insn,\s*uint64_t l_slot,\s*uint64_t x_slot",
              params, "ia64_fill_mlx_movl parameters", full=True)
    args = ("l_slot", "x_slot")
    names = {name: name for name in args}
    parts = statements(body)
    declarations = scalar_declarations(parts[:-3], names, functions)
    proofs = []
    for part, lhs, spec in zip(
        parts[-3:],
        ("qp", "operands.decoder.r1", "operands.decoder.imm"),
        ("field x_slot 0 6", "field x_slot 6 7", "imm64 l_slot x_slot"),
        strict=True,
    ):
        rhs = get_match(rf"\s*insn->{re.escape(lhs)}\s*=\s*(.*)",
                        part, lhs, full=True)[1]
        extractor = "c_movl_" + lhs.split(".")[-1]
        proofs.append(f"def {extractor} (l_slot x_slot : Word) : Word :=\n"
                      + "\n".join(declarations)
                      + f"\n  {expression(rhs, names, functions)}")
        proofs.append(prove(f"{extractor}_correct", args,
                            f"{extractor} l_slot x_slot", spec))
    return proofs


def project_templates(source):
    _, body = c_function(source, "ia64_template_info")
    index = get_match(r"\s*return\s*&ia64_templates\[(.*)\];\s*",
                      body, "ia64_template_info", full=True)[1]
    rows = get_match(r"\bia64_templates\[32\]\s*=\s*\{(.*?)\n\};", source,
                     "template table")[1].strip()
    entry = re.compile(
        r'\[(0x[0-9a-f]+)\]\s*=\s*\{\s*(true|false),\s*"([A-Z]+|reserved)",'
        r"\s*\{\s*IA64_UNIT_(\w+),\s*IA64_UNIT_(\w+),\s*IA64_UNIT_(\w+)\s*\},"
        r"\s*\{\s*(true|false),\s*(true|false),\s*(true|false)\s*\}\s*\},\s*"
    )
    table = {}
    while rows:
        found = entry.match(rows)
        require(found is not None, "unrecognized entry in C template table")
        key, defined, name, u0, u1, u2, s0, s1, s2 = found.groups()
        idx = int(key, 16)
        require(idx not in table, f"duplicate template {key}")
        table[idx] = f'⟨{defined}, "{name}", (.{u0.lower()}, .{u1.lower()}, .{u2.lower()}), ({s0}, {s1}, {s2})⟩'
        rows = rows[found.end():]
    require(set(table) == set(range(32)), "C template table must cover all 32 codes")
    lean_table = ",\n  ".join(table[idx] for idx in range(32))
    return [
        f"def c_templateTable : Array Template := #[\n  {lean_table}]",
        f"def c_templateIndex (code : Word) : Word := {expression(index, {'code': 'code'})}",
        prove("templateIndex_correct", ("code",), "c_templateIndex code",
              "(templateCode code).zeroExtend 64"),
        "theorem templateTable_correct : (List.range 32).all "
        "(fun n => decide (c_templateTable[n]! = template n)) = true := by decide",
    ]


def project_bundle(source, header):
    mask = get_match(r"^#define IA64_SLOT_MASK ([^\n]+)$", header, "IA64_SLOT_MASK")[1]
    fixed = {"IA64_SLOT_MASK": expression(mask, {})}
    _, body = c_function(source, "ia64_bundle_template_code")
    rhs = get_match(r"\s*return\s+(.*);\s*", body, "bundle template", full=True)[1]
    result = [
        f"def c_bundleTemplate (low : Word) : Word := {expression(rhs, {'low_word': 'low'})}",
        prove("bundleTemplate_correct", ("low",), "c_bundleTemplate low",
              "(templateCode low).zeroExtend 64"),
    ]
    _, body = c_function(source, "ia64_bundle_slot")
    match = get_match(
        r"\s*switch\s*\(slot\)\s*\{\s*case 0:\s*return ([^;]+);"
        r"\s*case 1:\s*return ([^;]+);\s*case 2:\s*return ([^;]+);"
        r"\s*default:\s*return ([^;]+);\s*\}\s*", body, "bundle slot", full=True,
    )
    names = fixed | {"low_word": "low", "high_word": "high"}
    for idx, rhs in enumerate(match.groups()):
        result.append(f"def c_slot{idx} (low high : Word) : Word := {expression(rhs, names)}")
        spec = f"(slot{idx} low high).zeroExtend 64" if idx < 3 else "0"
        result.append(prove(f"slot{idx}_correct", ("low", "high"),
                            f"c_slot{idx} low high", spec))
    return result


def verify_source(root, work):
    decoder = uncomment((root / "target/ia64/decoder.c").read_text())
    header = uncomment((root / "target/ia64/decoder.h").read_text())
    insns = uncomment((root / "target/ia64/decode/decode.c").read_text())
    lines = ["import Decode.Spec", "open IA64.Decode", "set_option linter.unusedVariables false"]
    lines += project_templates(decoder)
    lines += project_bundle(decoder, header)
    functions = {}
    for name, spec in FUNCTIONS:
        args, definition = scalar_function(insns, name, functions)
        lines.append(definition)
        functions[name] = len(args)
        if spec:
            inputs = " ".join(args)
            lines.append(prove(f"{name}_correct", args,
                               f"c_{name} {inputs}", f"{spec} {inputs}"))
    lines += project_movl(insns, functions)
    proof = work / "Source.lean"
    definitions = [line for line in lines if not line.startswith("theorem ")]
    names = [line.split()[1] for line in definitions if line.startswith("def ")]
    unfolds = ", ".join(names + ["bundle", "templateCode", "slot0", "slot1", "slot2",
                                 "field"] + sorted({spec for _, spec in FUNCTIONS if spec})
                        + ["imm64"])
    theorems = [line.replace("BITVECTOR_PROOF", f"simp only [{unfolds}] <;> bv_decide")
                for line in lines if line.startswith("theorem ")]
    proof.write_text("\n\n".join(definitions + theorems) + "\n")
    run_lean(work, proof)


def run_lean(work, filename, *flags, script_args=()):
    env = os.environ.copy()
    env["LEAN_PATH"] = os.pathsep.join(filter(None, [str(work), env.get("LEAN_PATH")]))
    subprocess.run(
        ["lake", "env", "lean", *flags, str(filename), *script_args],
        cwd=PROJECT,
        env=env,
        check=True,
    )


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source-root", type=Path, default=ROOT,
                        help="read the decoder source and headers from this checkout")
    args = parser.parse_args()
    root = args.source_root.resolve()
    with tempfile.TemporaryDirectory(prefix="ia64-decode-lean-") as tmp:
        work = Path(tmp)
        (work / "Decode").mkdir()
        run_lean(work, HERE / "Spec.lean", "-o", str(work / "Decode/Spec.olean"))
        verify_source(root, work)
        print("IA-64 bundle, template and immediate equations verified", flush=True)
        check_slots(root, work, run_lean)


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as exc:
        raise SystemExit(f"IA-64 decode check failed: {exc}") from None
