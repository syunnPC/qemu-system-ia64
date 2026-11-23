#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Lift the checked IA-64 integer emitter cases to Lean word expressions."""

import argparse
from hashlib import sha256
import json
from pathlib import Path
import re
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
TARGET = HERE.parents[2] / "target" / "ia64"

# Architectural operands that are read by the SDM operations in Spec.lean.
OPERANDS = {
    "ADD": "x y", "ADD_ONE": "x y", "ADDS": "y imm", "ADDL": "y imm",
    "ADDP4": "x y", "ADDP4_IMM": "y imm",
    "SUB": "x y", "SUB_ONE": "x y", "SUB_IMM": "y imm",
    "AND": "x y", "AND_IMM": "y imm", "ANDCM": "x y", "ANDCM_IMM": "y imm",
    "OR": "x y", "OR_IMM": "y imm", "XOR": "x y", "XOR_IMM": "y imm",
    "SHL": "x y", "SHRU": "x y", "SHR": "x y",
    "SHL_IMM": "y imm", "SHRU_IMM": "y imm", "SHR_IMM": "y imm",
    "SHLADD": "x y imm", "SHLADDP4": "x y imm", "SHRP_IMM": "x y imm",
    "SXT1": "y", "SXT2": "y", "SXT4": "y",
    "ZXT1": "y", "ZXT2": "y", "ZXT4": "y", "MOVL": "imm",
    "MPY4": "x y", "MPYSHL4": "x y", "POPCNT": "y", "CLZ": "y",
    "DEP": "x y pos len", "DEP_IMM": "y pos len fill", "DEPZ": "x pos len",
    "DEPZ_IMM": "pos len fill", "EXTR": "y pos len", "EXTRU": "y pos len",
}
BITFIELD = {"DEP", "DEP_IMM", "DEPZ", "DEPZ_IMM", "EXTR", "EXTRU"}

OPERATIONS = {
    "add": "+", "sub": "-", "mul": "*", "and": "&&&", "andc": "&&&",
    "or": "|||", "xor": "^^^", "eqv": "^^^", "shl": "<<<", "shr": ">>>",
}
HOST_OPERATIONS = {
    "+": (8, "+"), "-": (8, "-"), "*": (9, "*"),
    "%": (9, "%"), "/": (9, "/"),
    "<<": (7, "<<<"), ">>": (7, ">>>"),
    "&": (4, "&&&"), "|": (2, "|||"), "^": (3, "^^^"),
    "<": (6, "<"), ">": (6, ">"), ">=": (6, "≥"), "<=": (6, "≤"),
    "==": (5, "=="), "!=": (5, "!="), "&&": (1, "&&"), "||": (0, "||"),
}
HOST_CALLS = {
    "tcg_constant_i64": "",
    "UINT64_C": "",
    "ia64_low_mask": "sourceLowMask",
    "ia64_bitfield_effective_len": "sourceEffectiveLen",
    "ia64_deposit_mask": "sourceDepositMask",
}
CONSTANTS = {
    "op->immediate": "imm", "cpugr": "old",
    "UINT64_MAX": "18446744073709551615#64",
    "IA64_NAT_UNKNOWN": "18446744073709551615#64",
    "IA64_NAT_CLEAR": "0#64", "IA64_NAT_SET": "1#64",
    "true": "true", "false": "false",
}
TOKEN = re.compile(r"op->\w+|insn->\w+|ctx->\w+|0[xX][\da-fA-F]+[uUlL]*|"
                   r"\d+[uUlL]*|[a-zA-Z_]\w*|<<|>>|==|!=|>=|<=|&&|\|\||"
                   r"\S")


def require(condition, message):
    if not condition:
        raise ValueError(message)


def stripped(source):
    return re.sub(r"//[^\n]*|/\*.*?\*/", "", source, flags=re.S)


def compact(source):
    return "".join(source.split())


def matching(source, start, left, right):
    depth = 0
    for at in range(start, len(source)):
        if source[at] == left:
            depth += 1
        if source[at] == right:
            depth -= 1
            if not depth:
                return at
    raise ValueError(f"Unclosed {left} at {start}")


def function(source, name):
    match = re.search(rf"^\w[^;\n]*\b{name}\s*\([^;]*?\)\s*\{{",
                      source, re.M)
    require(match is not None, f"Missing function: {name}")
    start = match.end() - 1
    return source[start + 1:matching(source, start, "{", "}")]


def switches(source, subject="insn->opcode"):
    result = []
    for match in re.finditer(r"switch\s*\(\s*" + re.escape(subject)
                             + r"\s*\)\s*\{", source):
        start = match.end() - 1
        end = matching(source, start, "{", "}")
        result.append((start, end, source[start + 1:end]))
    return result


def cases(source):
    marks = list(re.finditer(r"^    (?:case IA64_OP_(\w+)|default):",
                              source, re.M))
    require(marks and not source[:marks[0].start()].strip(),
            "Unexpected source at start of switch")
    result = {}
    names = []
    for index, mark in enumerate(marks):
        names.append(mark.group(1) or "default")
        end = marks[index + 1].start() if index + 1 < len(marks) else len(source)
        body = source[mark.end():end].strip()
        if body:
            for name in names:
                require(name not in result, f"Duplicate switch label: {name}")
                result[name] = body
            names = []
    require(not names, "Switch ends with labels without a body")
    return result


class Expression:
    def __init__(self, source, values):
        self.tokens = TOKEN.findall(source)
        self.values = values
        self.index = 0

    def peek(self):
        return self.tokens[self.index] if self.index < len(self.tokens) else ""

    def take(self, expected=None):
        token = self.peek()
        require(token and (expected is None or token == expected),
                f"Expected {expected} in {' '.join(self.tokens)}, got {token}")
        self.index += 1
        return token

    def parse(self, minimum=-1):
        token = self.take()
        if token == "(":
            if self.peek() in {"uint64_t", "int64_t", "int8_t"}:
                cast = self.take()
                self.take(")")
                term = self.parse(10)
                if cast == "int8_t":
                    term = f"(TCG.signExtend8 {term})"
            else:
                term = self.parse()
                self.take(")")
        elif token in {"~", "!"}:
            operator = "~~~" if token == "~" else "!"
            term = f"({operator}{self.parse(10)})"
        elif token[0].isdigit():
            term = f"{int(token.rstrip('uUlL'), 0)}#64"
        elif token == "ia64_gr_src":
            self.take("(")
            field = self.take()
            self.take(")")
            term = self.values.get(field)
            require(term is not None, f"Unexpected GR operand: {field}")
        elif token == "ia64_gen_gr_nat_read":
            self.take("(")
            field = self.take()
            self.take(")")
            require(field in {"op->source1", "op->source2"},
                    f"Unexpected NaT operand: {field}")
            term = f"(sourceReadNaT natWord {self.values[field + '_index']})"
        elif token in HOST_CALLS:
            self.take("(")
            operands = [self.parse()]
            while self.peek() == ",":
                self.take(",")
                operands.append(self.parse())
            self.take(")")
            call = HOST_CALLS[token]
            term = f"({call} {' '.join(operands)})" if call else operands[0]
        elif token in {"cpu_gr", "cpu_pr", "cpu_nat"}:
            self.take("[")
            index = self.take()
            self.take("]")
            term = self.values.get(f"{token}[{index}]")
            require(term is not None, f"Unexpected indexed operand: {token}[{index}]")
        else:
            term = self.values.get(token, CONSTANTS.get(token))
            require(term is not None, f"Unexpected operand: {token}")
        while self.peek() in HOST_OPERATIONS:
            precedence, operator = HOST_OPERATIONS[self.peek()]
            if precedence < minimum:
                break
            self.take()
            right = self.parse(precedence + 1)
            term = f"({term} {operator} {right})"
        if minimum == -1 and self.peek() == "?":
            self.take("?")
            when_true = self.parse()
            self.take(":")
            when_false = self.parse()
            term = f"(if {term} then {when_true} else {when_false})"
        return term

    def finish(self):
        result = self.parse()
        require(not self.peek(), f"Unparsed expression: {' '.join(self.tokens)}")
        return result


def expr(source, values):
    return Expression(source, values).finish()


def arguments(source):
    result = []
    start = depth = 0
    for index, char in enumerate(source):
        depth += (char == "(") - (char == ")")
        if char == "," and not depth:
            result.append(source[start:index].strip())
            start = index + 1
    return result + [source[start:].strip()]


def apply_tcg(name, args, values):
    operands = [expr(arg, values) for arg in args[1:]]
    if name in {"mov", "movi"}:
        [term] = operands
        return term
    if name == "not":
        [arg] = operands
        return f"(~~~{arg})"
    if name == "ctpop":
        [arg] = operands
        return f"(TCG.populationCount {arg})"
    if name == "clzi":
        arg, zero_result = operands
        return f"(TCG.leadingZeros {arg} {zero_result})"
    if name in ("sar", "sari"):
        left, right = operands
        return f"({left}.sshiftRight' {right})"
    if name in {"ext8s", "ext16s", "ext32s", "ext8u", "ext16u", "ext32u"}:
        [arg] = operands
        extension = "sign" if name[-1] == "s" else "zero"
        return f"(TCG.{extension}Extend{name[3:-1]} {arg})"
    if name == "deposit":
        base, value, offset, width = operands
        return f"(TCG.deposit {base} {value} {offset} {width})"
    operation = OPERATIONS.get(name, OPERATIONS.get(name.removesuffix("i")))
    if operation is not None:
        left, right = operands
        if name == "andc":
            right = f"(~~~{right})"
        term = f"({left} {operation} {right})"
        return f"(~~~{term})" if name == "eqv" else term
    raise ValueError(f"Unsupported TCG operation: {name}")


def tcg_call(name, args, values):
    if name == "movcond":
        condition, dst, left, right, yes, no = args
        conditions = {"TCG_COND_GTU": "GTU",
                      "TCG_COND_EQ": "EQ", "TCG_COND_NE": "NE",
                      "TCG_COND_TSTNE": "TSTNE"}
        require(condition in conditions, f"Unsupported TCG condition: {condition}")
        left, right, yes, no = (expr(arg, values) for arg in (left, right, yes, no))
        values[compact(dst)] = (f"(TCG.select (TCG.condition{conditions[condition]}"
                                f" {left} {right}) {yes} {no})")
    else:
        values[compact(args[0])] = apply_tcg(name, args, values)


def branch_end(source):
    start = source.index("(")
    end = matching(source, start, "(", ")")
    start = end + 1
    while source[start].isspace():
        start += 1
    require(source[start] == "{", "C branch body must be braced")
    end = matching(source, start, "{", "}") + 1
    suffix = re.match(r"\s*else\s*(?=if\s*\(|\{)", source[end:])
    if suffix:
        start = end + suffix.end()
        if source[start] == "{":
            return matching(source, start, "{", "}") + 1
        return start + branch_end(source[start:])
    return end


def emit(source, values, addp4=None):
    """Apply straight-line C/TCG and braced C branches to symbolic temporaries."""
    while source.strip():
        source = source.lstrip()
        if source.startswith("{"):
            end = matching(source, 0, "{", "}")
            emit(source[1:end], values, addp4)
            source = source[end + 1:]
            continue
        branch = re.match(r"if\s*\(", source)
        if branch:
            condition_start = branch.end() - 1
            condition_end = matching(source, condition_start, "(", ")")
            condition_text = source[condition_start + 1:condition_end].strip()
            cond = expr(condition_text, values)
            if re.fullmatch(r"\w+", condition_text) and condition_text != "immediate":
                cond = f"({cond} != 0#64)"
            rest = source[condition_end + 1:].lstrip()
            require(rest.startswith("{"), "C branch body must be braced")
            end = matching(rest, 0, "{", "}")
            yes = values.copy()
            emit(rest[1:end], yes, addp4)
            rest = rest[end + 1:].lstrip()
            no = values.copy()
            if rest.startswith("else"):
                rest = rest[4:].lstrip()
                if rest.startswith("if"):
                    end = branch_end(rest)
                    emit(rest[:end], no, addp4)
                    rest = rest[end:]
                else:
                    require(rest.startswith("{"), "C else body must be braced")
                    end = matching(rest, 0, "{", "}")
                    emit(rest[1:end], no, addp4)
                    rest = rest[end + 1:]
            for name in yes.keys() & no.keys():
                if yes[name] != no[name]:
                    values[name] = f"(if {cond} then {yes[name]} else {no[name]})"
                else:
                    values[name] = yes[name]
            source = rest
            continue
        statement, sep, source = source.partition(";")
        require(sep, f"Expected statement delimiter: {statement}")
        statement = statement.strip()
        if statement == "break":
            require(not source.strip(), f"Source after terminal break: {source}")
            return
        returned = re.fullmatch(r"return\s+(.+)", statement, re.S)
        if returned:
            require(not source.strip(), "Trailing source after return")
            values["return"] = expr(returned.group(1), values)
            return
        declaration = re.fullmatch(r"(?:const\s+)?(?:TCGv_i64|u?int64_t|bool|"
                                   r"IA64KnownNat)\s+(\w+)(?:\s*=\s*(.+))?",
                                   statement, re.S)
        if declaration:
            name, initializer = declaration.groups()
            if initializer and not re.fullmatch(r"tcg_temp_(?:ebb_)?new_i64\(\)",
                                                compact(initializer)):
                values[name] = expr(initializer, values)
            continue
        assign = re.fullmatch(r"(\w+)\s*(&?=)\s*(.*)", statement, re.S)
        if assign:
            name, operator, initializer = assign.groups()
            if compact(initializer) != "tcg_temp_new_i64()":
                term = expr(initializer, values)
                values[name] = term if operator == "=" else f"({values[name]} &&& {term})"
            continue
        call = re.fullmatch(r"(tcg_gen_(\w+)_i64|ia64_gen_addp4_result)"
                            r"\s*\((.*)\)", statement, re.S)
        temp_free = re.fullmatch(r"tcg_temp_free_i64\s*\(\s*(\w+)\s*\)", statement)
        if temp_free:
            require(values.pop(temp_free.group(1), None) is not None,
                    f"Freeing an uninitialized temporary: {statement}")
            continue
        require(call is not None, f"Unsupported C statement: {statement}")
        callee, generator, text = call.groups()
        args = arguments(text)
        if callee == "ia64_gen_addp4_result":
            require(addp4 is not None, "Unexpected addp4 helper")
            output, left, right = args
            values[compact(output)] = (f"(sourceAddp4 {expr(left, values)} "
                                       f"{expr(right, values)})")
        else:
            tcg_call(generator, args, values)


def value_inputs():
    return {
        "op->source1": "x", "op->source2": "y",
        "cpu_gr[op->destination]": "old", "op->destination": "dst",
        "cpu_pr[insn->qp]": "predicate", "cpu_nat[0]": "natWord",
        "op->source1_index": "r2", "op->source2_index": "r3",
    }


def fast_sections(translator):
    body = function(translator, "ia64_gen_predicated_integer")
    sections = switches(body)
    require(len(sections) == 2, "Expected two fast integer opcode switches")
    first, second = sections
    require(compact(body[:first[0]]).startswith(
        "constIA64IntegerOperands*op=&insn->operands.integer;"
        "IA64KnownNatold_nat,new_nat;TCGv_i64result;boolimmediate;"
        "if(!insn->valid||insn->placement_illegal||insn->reserved_field||"
        "!insn->qp||ia64_predicate_known(ctx,insn->qp)!=IA64_PREDICATE_UNKNOWN||"
        "!op->destination||op->destination>=IA64_STACKED_GR_BASE||"
        "op->source1>=IA64_STACKED_GR_BASE||op->source2>=IA64_STACKED_GR_BASE)"
        "{returnfalse;}switch(insn->opcode)"), "Unexpected fast-path guard")
    require(compact(body[first[1] + 1:second[0]]) ==
            "result=tcg_temp_new_i64();switch(insn->opcode)",
            "Unexpected source between fast-path switches")
    first_cases = cases(first[2])
    second_cases = cases(second[2])
    require(compact(first_cases["default"]) == "returnfalse;" and
            compact(second_cases["default"]) == "g_assert_not_reached();",
            "Unexpected fast-path switch default")
    supported = set(second_cases) - {"default"}
    require(set(first_cases) - {"default"} == supported and supported <= OPERANDS.keys(),
            "Unknown or inconsistent predicated integer opcode coverage")
    return first_cases, second_cases, supported, body[second[1] + 1:]


def generate_value_proofs(integer, translator, decoder):
    helper = function(integer, "ia64_gen_addp4_result")
    help_values = {"src1": "x", "src3": "y", "dst": "old"}
    emit(helper, help_values)
    output = [f"def sourceAddp4 (x y : Word) : Word := {help_values['dst']}"]
    output.append(
        "theorem sourceAddp4_agrees (x y : Word) : sourceAddp4 x y = SDM.ADDP4 x y := by\n"
        "  simp only [sourceAddp4, SDM.ADDP4, TCG.zeroExtend32]\n"
        "  bv_decide"
    )
    for source_name, lean_name, parameters in (
        ("ia64_low_mask", "sourceLowMask", "len"),
        ("ia64_bitfield_effective_len", "sourceEffectiveLen", "pos len"),
        ("ia64_deposit_mask", "sourceDepositMask", "pos len"),
    ):
        values = {name: name for name in parameters.split()}
        emit(function(decoder, source_name), values)
        output.append(f"def {lean_name} ({parameters} : Word) : Word := {values['return']}")
    ordinary = cases(switches(function(integer, "ia64_gen_native_integer_write"))[0][2])
    dispatch = cases(switches(function(integer, "ia64_gen_integer"))[0][2])
    fast_kinds, fast_ops, supported, tail = fast_sections(translator)

    # Bind the normal integer dispatch and zero-destination guards.
    ordinary_prefix = function(integer, "ia64_gen_native_integer_write").split("switch")[0]
    require(compact(ordinary_prefix) ==
            "constIA64IntegerOperands*op=&insn->operands.integer;"
            "TCGv_i64tmp;TCGv_i64tmp2;if(op->destination==0){return;}",
            "Unexpected ordinary integer emitter prefix")
    for opcode, inputs in OPERANDS.items():
        selected = ordinary.get(opcode)
        if selected is None or "g_assert_not_reached" in selected:
            wrapper = dispatch[opcode]
            match = re.fullmatch(r"if\s*\(op->destination != 0\)\s*\{(.*)\}"
                                 r"\s*break;", wrapper, re.S)
            require(match is not None, f"Missing standalone emitter: {opcode}")
            selected, count = re.subn(r"ia64_gen_gr_nat_\w+\([^;]*;","",
                                      match.group(1))
            require(count == 1, f"Missing standalone NaT emitter: {opcode}")
        else:
            require(compact(dispatch[opcode]) ==
                    "ia64_gen_native_integer_write(insn);"
                    "ia64_gen_native_integer_nat(insn);break;",
                    f"Unexpected normal integer dispatch: {opcode}")
        values = value_inputs()
        emit(selected, values, addp4=True)
        actual = values["cpu_gr[op->destination]"]
        expected = f"SDM.{opcode} {inputs}"
        premise = ""
        if opcode in {"SHL_IMM", "SHRU_IMM", "SHR_IMM", "SHRP_IMM"}:
            premise = "(legalCount : imm < 64#64) "
        if opcode in BITFIELD:
            packed = "(pos ||| (len <<< 6#64)"
            length_max = 16 if opcode == "DEP" else 64
            premise = ("(pos len fill : Word) (legalPos : pos < 64#64) "
                       f"(legalLen : 1#64 ≤ len ∧ len ≤ {length_max}#64) ")
            if opcode in {"DEP_IMM", "DEPZ_IMM"}:
                packed += " ||| (fill <<< 13#64)"
                fill_max = 2 if opcode == "DEP_IMM" else 256
                premise += f"(legalFill : fill < {fill_max}#64) "
            actual = f"let imm := {packed}); {actual}"
            expected = f"({expected})"
        output.append(f"theorem ordinaryValue_{opcode} (x y old imm : Word) "
                      f"{premise}: {actual} = {expected} := by\n"
                      f"{value_tactic(opcode)}")

    head = re.match(r"\s*(tcg_gen_movcond_i64\([^;]+;)", tail)
    require(head is not None, "Missing predicated integer write")
    for opcode in sorted(supported):
        values = value_inputs()
        emit(fast_ops[opcode], values)
        emit(head.group(1), values)
        actual = values["cpu_gr[op->destination]"]
        output.append(f"theorem predicatedValue_{opcode} (x y old imm predicate : Word) :\n"
                      f"    {actual} =\n"
                      f"      SDM.qualifiedWrite (predicate != 0) old (SDM.{opcode} "
                      f"{OPERANDS[opcode]}) := by\n{value_tactic(opcode)}")
    return output, supported


def value_tactic(opcode):
    if opcode == "ADDP4":
        return "  exact sourceAddp4_agrees x y"
    dependent = {
        "ADDL": "SDM.ADDS",
        "ADDP4_IMM": "sourceAddp4_agrees",
        "SHLADDP4": "sourceAddp4_agrees",
        "AND_IMM": "SDM.AND",
        "ANDCM_IMM": "SDM.ANDCM",
        "OR_IMM": "SDM.OR",
        "XOR_IMM": "SDM.XOR",
        "SUB_IMM": "SDM.SUB",
        "SHL_IMM": "SDM.SHL",
        "SHR_IMM": "SDM.SHR",
        "SHRU_IMM": "SDM.SHRU",
    }
    rewrites = [f"SDM.{opcode}", "SDM.qualifiedWrite", "TCG.select",
                "TCG.conditionGTU", "TCG.conditionEQ", "TCG.conditionNE",
                "BitVec.add_comm", "BitVec.and_comm", "BitVec.or_comm",
                "BitVec.xor_comm"]
    if opcode[:3] in {"SXT", "ZXT"}:
        mode = "sign" if opcode.startswith("SXT") else "zero"
        rewrites.append(f"TCG.{mode}Extend{8 * int(opcode[-1])}")
    if opcode in {"MPY4", "MPYSHL4"}:
        rewrites.extend(["TCG.zeroExtend32"])
    if opcode == "CLZ":
        return ("  simp only [TCG.leadingZeros, SDM.CLZ]\n"
                "  split <;> rename_i h\n"
                "  · subst y; decide\n  · rfl")
    if opcode == "POPCNT":
        return "  rfl"
    if opcode in BITFIELD:
        rewrites.extend(["sourceDepositMask", "sourceEffectiveLen", "sourceLowMask",
                         "SDM.fieldShift", "SDM.lowField", "SDM.DEP", "SDM.DEPZ",
                         "TCG.signExtend8"])
    if opcode in dependent:
        rewrites.append(dependent[opcode])
    return f"  simp only [{', '.join(rewrites)}] <;> bv_decide"


def conditional_return(source, values):
    """Read a sequence of C guarded returns followed by a fallback return."""
    source = source.strip()
    branch = re.match(r"if\s*\(", source)
    if branch:
        start = branch.end() - 1
        end = matching(source, start, "(", ")")
        cond = expr(source[start + 1:end], values)
        rest = source[end + 1:].lstrip()
        require(rest.startswith("{"), "Expected braced conditional return")
        end = matching(rest, 0, "{", "}")
        first = conditional_return(rest[1:end], values)
        other = conditional_return(rest[end + 1:], values)
        return f"(if {cond} then {first} else {other})"
    returned = re.fullmatch(r"return\s+(.+);", source, re.S)
    require(returned is not None, f"Expected conditional return, got {source}")
    return expr(returned.group(1), values)


def source_nat_read(translator):
    body = function(translator, "ia64_gen_gr_nat_read")
    check = re.fullmatch(r"\s*TCGv_i64\s+bit\s*;\s*"
                         r"if\s*\(([^}]+?)\)\s*\{\s*return\s+([^;]+);\s*\}"
                         r"(.*)return\s+bit\s*;\s*", body, re.S)
    require(check is not None, "Unsupported GR NaT reader")
    cond, zero, tail = check.groups()
    tail, count = re.subn(r"cpu_nat\[reg\s*/\s*64\]", "packed", tail)
    require(count == 1, "Unsupported packed GR NaT word address")
    values = {"reg": "reg", "packed": "natWord"}
    emit(tail, values)
    return (f"def sourceReadNaT (natWord reg : Word) : Word :=\n"
            f"  if {expr(cond, values)} then {expr(zero, values)} else {values['bit']}")


def generate_fast_nat_proofs(translator, opcode_table):
    fast_kinds, _, supported, tail = fast_sections(translator)
    tail = tail.partition(";")[2]
    start = re.match(r"\s*old_nat\s*=\s*ia64_nat_known\(ctx, op->destination\);\s*"
                     r"new_nat\s*=\s*ia64_nat_result_known\(ctx, insn, old_nat\);",
                     tail)
    require(start is not None, "Unsupported fast-path NaT fact inputs")
    end = re.search(r"ctx->reg.current_qp_known\s*=\s*false;\s*"
                    r"ctx->reg.current_qp_value\s*=\s*false;\s*return true;\s*$",
                    tail[start.end():])
    require(end is not None, "Unsupported fast-path NaT epilogue")
    body = tail[start.end():start.end() + end.start()]
    facts = function(translator, "ia64_nat_result_known")
    prefix, _, _ = facts.partition("switch")
    require(compact(prefix) ==
            "IA64KnownNatr2=ia64_nat_known(ctx,insn->operands.common.source1);"
            "IA64KnownNatr3=ia64_nat_known(ctx,insn->operands.common.source2);",
            "Unexpected NaT source facts")
    table = dict(re.findall(r"IA64_OPCODE\((\w+),\s*INTEGER,\s*true,\s*(\w+)\)",
                            opcode_table))
    policy = function(translator, "ia64_nat_result_policy")
    require(compact(policy[policy.rindex("default:"):]) ==
            "default:return(unsigned)insn->opcode<IA64_OP_COUNT?"
            "ia64_opcode_properties[insn->opcode].nat_policy:IA64_NAT_RESULT_UNKNOWN;}",
            "Unsupported NaT policy lookup")
    join = conditional_return(function(translator, "ia64_nat_or"),
                              {"a": "a", "b": "b"})
    definitions = [source_nat_read(translator),
                   f"def sourceNatOr (a b : Word) : Word := {join}"]
    unique = {}
    for opcode in sorted(supported):
        kinds = {"immediate=true;break;": "true", "immediate=false;break;": "false"}
        nat_policy = table.get(opcode)
        fact = re.search(rf"case IA64_NAT_RESULT_{nat_policy}:\s*return ([^;]+);", facts)
        require(fact is not None, f"Missing fast-path NaT policy for {opcode}")
        fact_text = fact.group(1)
        if fact_text == "ia64_nat_or(r2, r3)":
            new = "sourceNatOr r2Fact r3Fact"
        else:
            new = expr(fact_text, {"r2": "r2Fact", "r3": "r3Fact", "old_r1": "oldFact"})
        kind = compact(fast_kinds[opcode])
        require(kind in kinds, f"Unsupported fast-path {opcode} NaT kind")
        values = value_inputs()
        values.update({"immediate": kinds[kind], "old_nat": "oldFact",
                       "new_nat": "newFact"})
        emit(body, values)
        actual = values["cpu_nat[0]"]
        reads_r2 = str("x" in OPERANDS[opcode].split()).lower()
        key = (actual, new, reads_r2)
        theorem = unique.get(key)
        if theorem is None:
            theorem = f"predicatedNaT_{opcode}"
            unique[key] = theorem
            tactic = ("  simp only [sourceNatOr, sourceReadNaT, SDM.staticNaT, "
                      "SDM.validNatFact, SDM.replaceNaT, SDM.natBit, SDM.sourceNaT, "
                      "SDM.qualifiedWrite, TCG.select, TCG.conditionNE, TCG.deposit] at *\n"
                      "  bv_decide")
        else:
            tactic = f"  exact {theorem} natWord dst r2 r3 predicate oldFact r2Fact r3Fact a b c f g h i"
        definitions.append(
            f"theorem predicatedNaT_{opcode}\n"
            "    (natWord dst r2 r3 predicate oldFact r2Fact r3Fact : Word)\n"
            "    (a : 0#64 < dst) (b : dst < 32#64) (c : r2 < 32#64) (f : r3 < 32#64)\n"
            "    (g : SDM.validNatFact oldFact (SDM.staticNaT natWord dst))\n"
            "    (h : SDM.validNatFact r2Fact (SDM.staticNaT natWord r2))\n"
            "    (i : SDM.validNatFact r3Fact (SDM.staticNaT natWord r3)) :\n"
            f"    let newFact := {new};\n"
            f"    {actual} =\n"
            "      SDM.qualifiedWrite (predicate != 0) natWord\n"
            f"        (SDM.replaceNaT natWord dst (SDM.sourceNaT {reads_r2} true\n"
            "          (SDM.staticNaT natWord r2) (SDM.staticNaT natWord r3))) := by\n"
            f"{tactic}"
        )
    return definitions


COMPARE_OPCODE = re.compile(
    r"CMP(4)?_(EQ|NE|LT|LE|GT|GE|LTU|LEU|GTU|GEU)(_OR_ANDCM|_AND|_OR)?(_IMM)?$")
COMPARE_MODES = {
    None: "normal", "_AND": "parallelAnd", "_OR": "parallelOr",
    "_OR_ANDCM": "parallelOrAndcm",
}
COMPARE_CONDITIONS = {
    "EQ": "{a} == {b}", "NE": "{a} != {b}", "LT": "{a}.slt {b}",
    "LE": "{a}.sle {b}", "GT": "{b}.slt {a}", "GE": "{b}.sle {a}",
    "LTU": "{a} < {b}", "LEU": "{a} ≤ {b}", "GTU": "{b} < {a}",
    "GEU": "{b} ≤ {a}",
}


def comparison_dispatch(integer):
    dispatch = cases(switches(function(integer, "ia64_gen_integer"))[0][2])
    opcodes = sorted(opcode for opcode in dispatch if COMPARE_OPCODE.fullmatch(opcode))
    body = dispatch["CMP_EQ"]
    require(all(dispatch[opcode] == body for opcode in opcodes),
            "Unexpected compare generator grouping")
    section = switches(body)[0]
    selected = cases(re.sub(r"^    ", "", section[2], flags=re.M))
    require(set(selected) == set(opcodes) | {"default"},
            "Compare switch groups differ")
    return opcodes, body, section, selected


def compare_signature(body):
    match = re.fullmatch(
        r"cond=TCG_COND_(\w+);(.*)break;", compact(body))
    require(match is not None and match[1] in COMPARE_CONDITIONS,
            f"Unexpected compare relation: {body}")
    flags = re.findall(r"(is_\w+)=true;", match[2])
    require(match[2] == "".join(f"{name}=true;" for name in flags)
            and set(flags) <= {"is_cmp4", "is_cmp_imm", "is_signed_cmp4",
                               "is_and", "is_or", "is_or_andcm"},
            f"Unexpected compare flags: {body}")
    return match[1], set(flags)


def generate_compare_proofs(integer):
    opcodes, body, section, selected = comparison_dispatch(integer)
    tail = compact(body[section[1] + 1:])
    begin = tail.index("src2=is_cmp_imm?")
    data_end = tail.index("tcg_gen_setcond_i64(")
    update_begin = tail.index(";", data_end) + 1
    update_end = tail.index("if(cmp_done!=NULL)")
    for block, digest, description in (
        (body[:section[0]], "bcc378f5ee752d6b6ab7e920ccf08e860cf8e7ef6405ac4e0af0f8e901ae8d23",
         "compare prefix"),
        (tail[:begin], "05ee76cef31728bd410130a71173f6db81e11b82bcff333f6e10f132c07c51ec",
         "compare NaT routing"),
        (tail[update_end:], "9e046285906fe99415845c36aa4b5040f91a972dfc9685bb94710a0775175a23",
         "compare completion"),
    ):
        require(sha256(compact(block).encode()).hexdigest() == digest,
                f"Unexpected {description}")
    require(tail[data_end:update_begin] == "tcg_gen_setcond_i64(cond,tmp,src2,src3);",
            "Unexpected TCG compare call")
    # Keep the original spacing for declarations consumed by the lifter.
    raw_tail = body[section[1] + 1:]
    data = raw_tail[raw_tail.index("        src2 ="):raw_tail.index("        tcg_gen_setcond_i64")]
    update = raw_tail[raw_tail.index("        if (is_and)"):raw_tail.index("        if (cmp_done")]
    candidates = {}
    proofs = []
    for opcode in opcodes:
        cmp4, relation, form, immediate = COMPARE_OPCODE.fullmatch(opcode).groups()
        actual_cond, flags = compare_signature(selected[opcode])
        values = value_inputs()
        for flag in ("is_cmp4", "is_signed_cmp4", "is_and", "is_or", "is_or_andcm"):
            values[flag] = "1#64" if flag in flags else "0#64"
        values["is_cmp_imm"] = str("is_cmp_imm" in flags).lower()
        values["immOperand"] = "imm"
        emit(data, values)
        cond = COMPARE_CONDITIONS[actual_cond].format(a=values["src2"], b=values["src3"])
        if actual_cond.endswith("U"):
            cond = f"(decide ({cond}))"
        values.update({"tmp": "SDM.natBit condition",
                       "cpu_pr[op->predicate1]": "(SDM.natBit p1)",
                       "cpu_pr[op->predicate2]": "(SDM.natBit p2)",
                       "op->predicate1": "i1", "op->predicate2": "i2"})
        emit(update, values)
        actual_pair = (values["cpu_pr[op->predicate1]"], values["cpu_pr[op->predicate2]"])
        old = "(SDM.natBit p1, SDM.natBit p2)"
        clears = "(if i1 = 0 then SDM.natBit p1 else 0, if i2 = 0 then SDM.natBit p2 else 0)"
        nan_update = old if flags & {"is_or", "is_or_andcm"} else clears
        nat = "n3" if "is_cmp_imm" in flags else "(n2 || n3)"
        target_nat = "n3" if immediate else "(n2 || n3)"
        left = "imm" if immediate else "x"
        right = "y"
        if cmp4:
            left = f"({left}.extractLsb' 0 32)"
            right = f"({right}.extractLsb' 0 32)"
        target_cond = f"SDM.cmp{relation.capitalize()} {left} {right}"
        conclusion = (
            f"    let condition : Bool := {cond};\n"
            f"    (if {nat} then {nan_update} else ({actual_pair[0]}, {actual_pair[1]})) =\n"
            f"      SDM.compareWrites .{COMPARE_MODES[form]} ({target_cond}) {target_nat}"
            " p1 p2 i1 i2"
        )
        if conclusion in candidates:
            tactic = f"  exact {candidates[conclusion]} x y imm i1 i2 n2 n3 p1 p2"
        else:
            candidates[conclusion] = f"comparedPredicate_{opcode}"
            tactic = (
                "  apply Prod.ext <;> simp [TCG.fstIf, TCG.sndIf, SDM.compareWrites, "
                "SDM.comparePredicateUpdate, SDM.natBit, "
                f"SDM.cmp{relation.capitalize()}, TCG.signExtend32, TCG.zeroExtend32,"
                " Prod.mk.injEq] <;> bv_decide"
            )
        proofs.append(
            f"theorem comparedPredicate_{opcode} (x y imm i1 i2 : Word)"
            f" (n2 n3 p1 p2 : Bool) :\n{conclusion} := by\n{tactic}")
    return proofs


def generated_source(integer, translator, decoder, opcode_table):
    output, _ = generate_value_proofs(integer, translator, decoder)
    output.extend(generate_fast_nat_proofs(translator, opcode_table))
    output.extend(generate_compare_proofs(integer))
    return wrap_generated(output)


def wrap_generated(definitions):
    return "\n\n".join(["namespace IA64Integer", "set_option maxRecDepth 4096",
                         "set_option maxHeartbeats 2000000",
                         "set_option linter.unusedVariables false",
                         'local notation "Word" => BitVec 64',
                         *definitions, "end IA64Integer"])


def run_lean(source):
    specification = (HERE / "Spec.lean").read_text(encoding="utf-8")
    with tempfile.TemporaryDirectory(prefix="ia64-lean-integer-") as temp:
        module = Path(temp) / "Proofs.lean"
        module.write_text(specification + "\n" + source, encoding="utf-8")
        return subprocess.run(["lake", "env", "lean", "-DwarningAsError=true",
                               str(module)], cwd=HERE.parent,
                              capture_output=True, text=True, check=False)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mutation-test", action="store_true",
                        help="check that replacing predicated ADD with SUB fails")
    parser.add_argument("--coverage", action="store_true",
                        help="report partial opcode coverage as JSON")
    args = parser.parse_args()
    integer = stripped((TARGET / "translate/gen-integer.c").read_text())
    translator = stripped((TARGET / "translate/translate.c").read_text())
    decoder = stripped((TARGET / "decode/decode.c").read_text())
    opcode_table = stripped((TARGET / "decode/opcode.inc").read_text())
    if args.coverage:
        _, _, fast, _ = fast_sections(translator)
        scope = {
            f"tcg:{op}": ("Ordinary TCG value and branchless predicate/packed NaT projections"
                          if op in fast else "Ordinary post-decode TCG value projection")
            for op in OPERANDS
        }
        scope.update({f"tcg:{op}": "Qualified CMP/CMP4 comparison and predicate-write projection"
                      for op in comparison_dispatch(integer)[0]})
        print(json.dumps(scope, sort_keys=True))
        return
    baseline = run_lean(generated_source(integer, translator, decoder, opcode_table))
    require(baseline.returncode == 0, baseline.stdout + baseline.stderr)
    _, _, fast, _ = fast_sections(translator)
    print(f"Integer: {len(OPERANDS)} ordinary TCG value and {len(fast)} branchless"
          " predicate/packed NaT projections passed")
    print(f"Integer: {len(comparison_dispatch(integer)[0])} qualified CMP/CMP4"
          " comparison/predicate projections passed")
    if args.mutation_test:
        mutant, count = re.subn(r"tcg_gen_add_i64(?=\(result, ia64_gr_src\(op->source1\))",
                                "tcg_gen_sub_i64", translator)
        require(count == 1, "Could not locate predicated ADD mutation target")
        generated, _ = generate_value_proofs(integer, mutant, decoder)
        check = [proof for proof in generated if proof.startswith("theorem predicatedValue_ADD ")]
        require(len(check) == 1, "Could not isolate predicated ADD theorem")
        mutated = run_lean(wrap_generated(check))
        require(mutated.returncode != 0 and "counterexample" in mutated.stdout,
                "Lean did not reject mutated predicated ADD:\n"
                + mutated.stdout + mutated.stderr)
        print("Integer: Lean rejected the predicated ADD-to-SUB mutation")
        compare_mutant, count = re.subn(r"(case IA64_OP_CMP4_LT:\s+cond = )TCG_COND_LT;",
                                        r"\1TCG_COND_LTU;", integer)
        require(count == 1, "Could not locate signed CMP4 mutation target")
        generated = generate_compare_proofs(compare_mutant)
        check = [proof for proof in generated if proof.startswith("theorem comparedPredicate_CMP4_LT ")]
        require(len(check) == 1, "Could not isolate CMP4.lt theorem")
        mutated = run_lean(wrap_generated(check))
        require(mutated.returncode != 0 and "counterexample" in mutated.stdout,
                "Lean did not reject a signed CMP4.lt lowered as unsigned:\n"
                + mutated.stdout + mutated.stderr)
        print("Integer: Lean rejected a signed CMP4.lt lowered as unsigned")


if __name__ == "__main__":
    try:
        main()
    except ValueError as error:
        raise SystemExit(str(error)) from None
