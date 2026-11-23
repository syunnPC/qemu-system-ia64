# SPDX-License-Identifier: GPL-2.0-or-later
"""Execute the typed C subset used by the standalone decoder symbolically."""

from dataclasses import dataclass

from bits import Bits


class Unsupported(ValueError):
    pass


def need(condition, message):
    if not condition:
        raise Unsupported(message)


def children(node):
    return node.get("inner", ())


def ctype(node):
    info = node["type"]
    value = info.get("desugaredQualType", info["qualType"])
    for token in ("const ", "volatile ", "restrict "):
        need(token == "const " or token not in value, f"unsupported C qualifier in {value}")
        value = value.replace(token, "")
    return value


def array_type(kind):
    if "[" not in kind:
        return None
    base, rest = kind.split("[", 1)
    length, tail = rest.split("]", 1)
    need(length.isdigit(), f"unsupported array extent: {kind}")
    return base + tail, int(length)


INTEGER_TYPES = {
    "bool": (1, False), "_Bool": (1, False),
    "char": (8, True), "signed char": (8, True), "unsigned char": (8, False),
    "short": (16, True), "unsigned short": (16, False),
    "int": (32, True), "unsigned int": (32, False),
    "long": (64, True), "unsigned long": (64, False),
    "long long": (64, True), "unsigned long long": (64, False),
}


@dataclass(frozen=True)
class Record:
    fields: dict


@dataclass(frozen=True)
class Array:
    elements: tuple


@dataclass(frozen=True)
class Ref:
    root: str
    path: tuple = ()


@dataclass(frozen=True)
class Pointer:
    targets: tuple


NIL = Pointer(((1, None),))
UNDEFINED = object()


@dataclass
class Frame:
    guard: int
    ram: dict

    def fork(self, guard):
        return Frame(guard, self.ram.copy())


@dataclass
class Exit:
    frame: Frame
    kind: str
    value: object = None


class Machine:
    def __init__(self, document):
        self.bits = Bits()
        self.types = {}
        self.aliases = {}
        self.enums = {}
        self.records = {}
        self.functions = {}
        self.globals = {}
        self.global_values = {}
        self.calls = set()
        self.called = set()
        self.blocks = set()
        for item in children(document):
            kind = item["kind"]
            name = item.get("name")
            if kind == "EnumDecl" and name:
                n = 0
                for entry in children(item):
                    if entry["kind"] != "EnumConstantDecl":
                        continue
                    if children(entry):
                        n = int(children(entry)[0]["value"], 0)
                    self.enums[entry["name"]] = n
                    n += 1
            elif kind == "RecordDecl" and name and item.get("completeDefinition"):
                self.records[f"{item['tagUsed']} {name}"] = {
                    field["name"]: ctype(field)
                    for field in children(item) if field["kind"] == "FieldDecl"
                }
            elif kind == "TypedefDecl" and name:
                self.aliases[name] = ctype(item)
            elif kind == "FunctionDecl" and name and children(item):
                if children(item)[-1]["kind"] == "CompoundStmt":
                    self.functions[name] = item
            elif kind == "VarDecl" and name and children(item):
                self.globals[item["id"]] = item

    def canonical(self, kind):
        return self.aliases.get(kind, kind)

    def integer(self, kind):
        kind = self.canonical(kind)
        if kind.startswith("enum "):
            return 32, False
        need(kind in INTEGER_TYPES, f"unsupported C arithmetic type {kind}")
        return INTEGER_TYPES[kind]

    def as_int(self, value, kind="int"):
        width, _ = self.integer(kind)
        return self.bits.constant(value, width)

    def check(self, frame, test, description):
        need(self.bits.both(frame.guard, self.bits.neg(test)) == 0, description)

    def layout(self, kind):
        kind = self.canonical(kind)
        indexed = array_type(kind)
        if indexed:
            base, length = indexed
            size, align = self.layout(base)
            return length * size, align
        if "*" in kind:
            return 8, 8
        if kind in self.records:
            offset, maximum = 0, 1
            for field_type in self.records[kind].values():
                size, align = self.layout(field_type)
                maximum = max(maximum, align)
                if kind.startswith("union "):
                    offset = max(offset, size)
                else:
                    offset = (offset + align - 1) // align * align + size
            return (offset + maximum - 1) // maximum * maximum, maximum
        width, _ = self.integer(kind)
        size = max(1, width // 8)
        return size, size

    def zero(self, kind):
        kind = self.canonical(kind)
        indexed = array_type(kind)
        if indexed:
            base, length = indexed
            return Array(tuple(self.zero(base) for _ in range(length)))
        if kind in self.records:
            fields = self.records[kind].items()
            if kind.startswith("union "):
                fields = list(fields)[:1]
            return Record({name: self.zero(base) for name, base in fields})
        if "*" in kind:
            return NIL
        return self.as_int(0, kind)

    def truth(self, value):
        if isinstance(value, tuple):
            return self.bits.any(value)
        if isinstance(value, Pointer):
            return self.bits.any(guard for guard, target in value.targets if target is not None)
        raise Unsupported(f"C truth value of {type(value).__name__}")

    def select(self, cond, yes, no):
        if not cond or yes == no:
            return no
        if cond == 1:
            return yes
        if isinstance(yes, tuple) and isinstance(no, tuple):
            return self.bits.select(cond, yes, no)
        if isinstance(yes, Record) and isinstance(no, Record):
            need(yes.fields.keys() == no.fields.keys(), "mismatched C aggregate fields")
            return Record({key: self.select(cond, value, no.fields[key])
                           for key, value in yes.fields.items()})
        if isinstance(yes, Array) and isinstance(no, Array):
            return Array(tuple(self.select(cond, a, b)
                               for a, b in zip(yes.elements, no.elements, strict=True)))
        if isinstance(yes, Pointer) and isinstance(no, Pointer):
            return Pointer(self.pointer_cases(
                (self.bits.both(guard, c), target)
                for pointer, c in ((yes, cond), (no, self.bits.neg(cond)))
                for guard, target in pointer.targets
            ))
        raise Unsupported(f"unsupported C merge {type(yes).__name__}, {type(no).__name__}")

    def combine(self, alternatives):
        pairs = [(guard, value) for guard, value in alternatives if guard]
        need(pairs, "empty C selection")
        result = pairs[0][1]
        for guard, value in pairs[1:]:
            result = self.select(guard, value, result)
        return result

    def merge(self, frames, keys):
        frames = [frame for frame in frames if frame.guard]
        if not frames:
            return Frame(0, {})
        return Frame(
            self.bits.any(frame.guard for frame in frames),
            {name: self.combine((frame.guard, frame.ram[name]) for frame in frames)
             for name in keys},
        )

    def pointer_cases(self, cases):
        merged = {}
        for guard, ref in cases:
            if guard:
                merged[ref] = self.bits.either(merged.get(ref, 0), guard)
        return tuple((guard, ref) for ref, guard in merged.items())

    def root_value(self, ref, frame):
        if ref.root in frame.ram:
            return frame.ram[ref.root]
        need(ref.root in self.globals, f"untracked declaration {ref.root}")
        if ref.root not in self.global_values:
            declaration = self.globals[ref.root]
            need(declaration["type"]["qualType"].startswith("const "),
                 "mutable global C state")
            self.global_values[ref.root] = self.expr(children(declaration)[0], Frame(1, {}))
        return self.global_values[ref.root]

    def at(self, ref, frame):
        value = self.root_value(ref, frame)
        for index in ref.path:
            if isinstance(value, Record):
                need(index in value.fields, "uninitialized/inactive C aggregate member")
                value = value.fields[index]
            elif isinstance(value, Array):
                value = value.elements[index]
            else:
                raise Unsupported("C pointer target does not have the selected field")
        need(value is not UNDEFINED, "uninitialized local read")
        return value

    def read(self, pointer, frame):
        need(isinstance(pointer, Pointer), "expected C lvalue pointer")
        active = [(self.bits.both(guard, frame.guard), ref) for guard, ref in pointer.targets]
        self.check(frame, self.bits.any(guard for guard, ref in active if ref is not None),
                   "possible null or out-of-bounds read")
        return self.combine((guard, self.at(ref, frame)) for guard, ref in active if guard)

    def write(self, pointer, frame, value):
        def move(old, path, guard):
            if not path:
                if old is UNDEFINED:
                    need(guard == frame.guard, "partial write to uninitialized storage")
                    return value
                return value if guard == frame.guard else self.select(guard, value, old)
            index, *tail = path
            if isinstance(old, Record):
                need(index in old.fields, "write across inactive C union member")
                fields = old.fields | {index: move(old.fields[index], tail, guard)}
                return Record(fields)
            if isinstance(old, Array):
                elements = list(old.elements)
                elements[index] = move(elements[index], tail, guard)
                return Array(tuple(elements))
            raise Unsupported("invalid C store path")

        paths = [(self.bits.both(guard, frame.guard), ref) for guard, ref in pointer.targets]
        self.check(frame, self.bits.any(guard for guard, ref in paths if ref is not None),
                   "possible null or out-of-bounds write")
        for guard, ref in paths:
            if not guard:
                continue
            need(ref.root in frame.ram, "write to global C storage")
            frame.ram[ref.root] = move(frame.ram[ref.root], ref.path, guard)

    def member(self, pointer, key):
        need(isinstance(pointer, Pointer), "member of nonpointer C data")
        return Pointer(tuple((guard, None if ref is None else Ref(ref.root, ref.path + (key,)))
                             for guard, ref in pointer.targets))

    def index(self, pointer, index, frame):
        need(isinstance(pointer, Pointer), "C subscript on non-array pointer")
        out = []
        for guard, ref in pointer.targets:
            if ref is None or not self.bits.both(guard, frame.guard):
                continue
            array = self.at(ref, frame)
            need(isinstance(array, Array), "pointer arithmetic outside array subscript")
            for i in range(len(array.elements)):
                match = self.bits.eq(index, self.bits.constant(i, len(index)))
                out.append((self.bits.both(guard, match), Ref(ref.root, ref.path + (i,))))
        return Pointer(self.pointer_cases(out))

    def lvalue(self, node, frame):
        args = children(node)
        kind = node["kind"]
        if kind == "DeclRefExpr":
            ref = node["referencedDecl"]
            return Pointer(((1, Ref(ref["id"])),))
        if kind == "MemberExpr":
            base = self.expr(args[0], frame) if node.get("isArrow") else self.lvalue(args[0], frame)
            return self.member(base, node["name"])
        if kind == "ArraySubscriptExpr":
            return self.index(self.expr(args[0], frame), self.expr(args[1], frame), frame)
        if kind == "UnaryOperator" and node["opcode"] == "*":
            return self.expr(args[0], frame)
        if kind == "ParenExpr":
            return self.lvalue(args[0], frame)
        raise Unsupported(f"unsupported C lvalue {kind}")

    def initialization(self, node, frame):
        args = children(node)
        kind = self.canonical(ctype(node))
        indexed = array_type(kind)
        if indexed:
            target, length = indexed
            if "array_filler" in node:
                args = node["array_filler"][1:]
            need(len(args) <= length, "invalid C array initializer")
            values = tuple(self.expr(value, frame) for value in args)
            return Array(values + tuple(self.zero(target) for _ in range(length - len(values))))
        need(kind in self.records, f"unsupported C aggregate initializer {kind}")
        keys = tuple(self.records[kind])
        if kind.startswith("union "):
            key = node.get("field", {}).get("name", keys[0])
            need(key in keys, f"unknown C union member {key}")
            keys = (key,)
        need(len(args) == len(keys), f"invalid C record initializer {kind}")
        return Record(dict(zip(keys, (self.expr(value, frame) for value in args), strict=True)))

    def branch_expr(self, frame, condition, yes, no):
        # Only the selected side executes, including writes by a helper.
        arms = []
        for test, action in ((condition, yes), (self.bits.neg(condition), no)):
            guard = self.bits.both(frame.guard, test)
            if guard:
                branch = frame.fork(guard)
                arms.append((branch, action(branch)))
        result = self.combine((f.guard, value) for f, value in arms)
        frame.ram = self.merge([f for f, _ in arms], frame.ram).ram
        return result

    def pure_expr(self, node, frame):
        probe = frame.fork(frame.guard)
        value = self.expr(node, probe)
        need(probe.ram == frame.ram, "side effects in an unsequenced C operand")
        return value

    def binary(self, node, frame):
        left, right = children(node)
        op = node["opcode"]
        if op in ("=", "&="):
            target = self.lvalue(left, frame)
            rv = self.pure_expr(right, frame)
            if op == "&=":
                old = self.read(target, frame)
                need(isinstance(old, tuple) and len(old) == len(rv), "unsupported compound assignment")
                rv = tuple(self.bits.both(a, b) for a, b in zip(old, rv, strict=True))
            self.write(target, frame, rv)
            return rv

        if op in ("&&", "||"):
            test = self.truth(self.expr(left, frame))
            yes, no = (right, 0) if op == "&&" else (1, right)

            def action(item):
                def run(f):
                    truth = item if isinstance(item, int) else self.truth(self.expr(item, f))
                    return self.bits.cast((truth,), self.integer(ctype(node))[0])
                return run

            return self.branch_expr(frame, test, action(yes), action(no))
        lv = self.pure_expr(left, frame)
        rv = self.pure_expr(right, frame)
        bits = self.bits
        if isinstance(lv, Pointer) and isinstance(rv, Pointer):
            need(op in ("==", "!="), f"unsupported C pointer comparison {op}")
            eq = bits.any(bits.both(a, b) for a, u in lv.targets for b, v in rv.targets if u == v)
            return bits.cast((eq if op == "==" else bits.neg(eq),), self.integer(ctype(node))[0])
        width, signed = self.integer(ctype(left))
        need(isinstance(lv, tuple) and isinstance(rv, tuple) and len(lv) == width,
             "noninteger operands in C arithmetic")
        if op in ("<<", ">>"):
            self.check(frame, bits.less(rv, bits.constant(width, len(rv))),
                       "C shift count may exceed the operand width")
            result = bits.shift(lv, rv, right=op == ">>", signed=signed)
            if signed and op == "<<":
                restored = bits.shift(result, rv, right=True, signed=True)
                self.check(frame, bits.both(bits.neg(lv[-1]), bits.eq(restored, lv)),
                           "possible undefined signed left shift")
            return result
        need(len(rv) == width, "missing C arithmetic conversion")
        if op in ("|", "&", "^"):
            fn = {"|": bits.either, "&": bits.both, "^": bits.xor}[op]
            return tuple(fn(a, b) for a, b in zip(lv, rv, strict=True))
        if op in ("+", "-", "*"):
            fn = {"+": bits.add, "-": bits.sub, "*": bits.multiply}[op]
            if signed:
                wide = 2 * width if op == "*" else width + 1
                result = fn(bits.cast(lv, wide, True), bits.cast(rv, wide, True))
                self.check(frame, bits.eq(result, bits.cast(result[:width], wide, True)),
                           "possible C signed arithmetic overflow")
                return result[:width]
            return fn(lv, rv)
        if op == "/":
            l, r = bits.number(lv, signed), bits.number(rv, signed)
            need(l is not None and r is not None and r != 0 and (l >= 0 and r > 0),
                 "symbolic or signed C division")
            return bits.constant(l // r, width)
        if op in ("==", "!=", "<", "<=", ">", ">="):
            if op in ("==", "!="):
                eq = bits.eq(lv, rv)
                result = eq if op == "==" else bits.neg(eq)
            elif op in ("<", ">="):
                less = bits.less(lv, rv, signed)
                result = less if op == "<" else bits.neg(less)
            else:
                less = bits.less(rv, lv, signed)
                result = less if op == ">" else bits.neg(less)
            return bits.cast((result,), self.integer(ctype(node))[0])
        raise Unsupported(f"unsupported C binary {op}")

    def expr(self, node, frame):
        kind = node["kind"]
        args = children(node)
        if kind == "IntegerLiteral":
            return self.as_int(int(node["value"], 0), ctype(node))
        if kind == "StringLiteral":
            # Template labels are opaque. They cannot be dereferenced or
            # converted to numeric observations by the selected C subset.
            return node["value"]
        if kind in ("ParenExpr", "ConstantExpr"):
            return self.expr(args[0], frame)
        if kind == "ImplicitValueInitExpr":
            return self.zero(ctype(node))
        if kind == "InitListExpr":
            return self.initialization(node, frame)
        if kind in ("ImplicitCastExpr", "CStyleCastExpr"):
            mode = node["castKind"]
            if mode == "ArrayToPointerDecay":
                if args[0]["kind"] == "StringLiteral":
                    return self.expr(args[0], frame)
                return self.lvalue(args[0], frame)
            if mode == "FunctionToPointerDecay":
                fn = args[0]["referencedDecl"]
                need(fn["kind"] == "FunctionDecl", "indirect C call")
                return fn["name"]
            if mode == "NullToPointer":
                value = self.expr(args[0], frame)
                need(value == NIL or (isinstance(value, tuple) and self.bits.number(value) == 0),
                     "nonnull pointer cast")
                return NIL
            value = self.expr(args[0], frame)
            if mode in ("NoOp", "LValueToRValue") or (mode == "BitCast" and isinstance(value, Pointer)):
                return value
            if mode == "IntegralToBoolean":
                return (self.truth(value),)
            if mode == "IntegralCast":
                width, _ = self.integer(ctype(node))
                return self.bits.cast(value, width, self.integer(ctype(args[0]))[1])
            raise Unsupported(f"unsupported C cast {mode}")
        if kind == "UnaryExprOrTypeTraitExpr":
            need(node["name"] == "sizeof", "unsupported C type operator")
            measured = ctype(args[0]) if args else ctype({"type": node["argType"]})
            size, _ = self.layout(measured)
            return self.as_int(size, ctype(node))
        if kind in ("DeclRefExpr", "MemberExpr", "ArraySubscriptExpr"):
            if kind == "DeclRefExpr":
                decl = node["referencedDecl"]
                if decl["kind"] == "EnumConstantDecl":
                    return self.as_int(self.enums[decl["name"]], ctype(node))
            return self.read(self.lvalue(node, frame), frame)
        if kind in ("BinaryOperator", "CompoundAssignOperator"):
            return self.binary(node, frame)
        if kind == "UnaryOperator":
            op = node["opcode"]
            if op == "&":
                return self.lvalue(args[0], frame)
            if op == "*":
                return self.read(self.lvalue(node, frame), frame)
            left = self.expr(args[0], frame)
            if op == "!":
                return self.bits.cast((self.bits.neg(self.truth(left)),), self.integer(ctype(node))[0])
            if op == "~":
                return tuple(map(self.bits.neg, left))
            if op == "-":
                zero = (0,) * len(left)
                result = self.bits.sub(zero, left)
                if self.integer(ctype(node))[1]:
                    signed_min = self.bits.constant(1 << (len(left) - 1), len(left))
                    self.check(frame, self.bits.neg(self.bits.eq(left, signed_min)),
                               "possible signed negation overflow")
                return result
            if op == "++":
                width, signed = self.integer(ctype(node))
                if signed:
                    maximum = self.bits.constant((1 << (width - 1)) - 1, width)
                    self.check(frame, self.bits.neg(self.bits.eq(left, maximum)),
                               "possible signed increment overflow")
                result = self.bits.add(left, self.bits.constant(1, width))
                self.write(self.lvalue(args[0], frame), frame, result)
                return left if node["isPostfix"] else result
            raise Unsupported(f"unsupported C unary {op}")
        if kind == "ConditionalOperator":
            test = self.truth(self.expr(args[0], frame))
            return self.branch_expr(frame, test, lambda f: self.expr(args[1], f),
                                    lambda f: self.expr(args[2], f))
        if kind == "CallExpr":
            fn = self.expr(args[0], frame)
            parameters = [self.pure_expr(arg, frame) for arg in args[1:]]
            return self.call(fn, parameters, frame)
        raise Unsupported(f"unsupported C expression {kind}")

    def sequence(self, nodes, frame):
        live = frame
        exits = []
        for node in nodes:
            if not live.guard:
                break
            keep = list(live.ram)
            if node["kind"] == "DeclStmt":
                keep.extend(decl["id"] for decl in children(node))
            outcomes = self.statement(node, live)
            exits.extend(item for item in outcomes if item.kind != "next")
            live = self.merge([item.frame for item in outcomes if item.kind == "next"], keep)
        if live.guard:
            exits.append(Exit(live, "next"))
        return exits

    def switch(self, node, frame):
        value_node, scope = children(node)
        need(scope["kind"] == "CompoundStmt", "unstructured C switch")
        value = self.expr(value_node, frame)
        chunks = []
        for stmt in children(scope):
            while stmt["kind"] in ("CaseStmt", "DefaultStmt"):
                args = children(stmt)
                label = (self.bits.eq(value, self.expr(args[0], frame))
                         if stmt["kind"] == "CaseStmt" else None)
                chunks.append((label, []))
                stmt = args[-1]
            need(chunks, "C switch body before first case")
            chunks[-1][1].append(stmt)
        tests = [label for label, _ in chunks if label is not None]
        unlisted = self.bits.neg(self.bits.any(tests))
        live, exits = Frame(0, {}), []
        keys = frame.ram.keys()
        for condition, statements in chunks:
            g = self.bits.both(frame.guard, unlisted if condition is None else condition)
            if g:
                live = self.merge([live, frame.fork(g)], keys)
            if live.guard:
                outcomes = self.sequence(statements, live)
                for item in outcomes:
                    if item.kind == "break":
                        exits.append(Exit(item.frame, "next"))
                    elif item.kind != "next":
                        exits.append(item)
                live = self.merge([item.frame for item in outcomes if item.kind == "next"], keys)
        if live.guard:
            exits.append(Exit(live, "next"))
        if all(label is not None for label, _ in chunks):
            g = self.bits.both(frame.guard, unlisted)
            if g:
                exits.append(Exit(frame.fork(g), "next"))
        return exits

    def for_loop(self, node, frame):
        init, cond_decl, test, increment, scope = children(node)
        need(not cond_decl, "C loop with condition declaration")
        initialized = self.statement(init, frame)
        need(len(initialized) == 1 and initialized[0].kind == "next", "C loop initializer")
        live = initialized[0].frame
        exits = []
        for _ in range(256):
            if not live.guard:
                return exits
            guard = self.truth(self.expr(test, live))
            need(guard in (0, 1), "symbolic C loop bound")
            if not guard:
                return exits + [Exit(live, "next")]
            outcomes = self.statement(scope, live)
            exits.extend(Exit(item.frame, "next" if item.kind == "break" else item.kind, item.value)
                         for item in outcomes if item.kind != "next")
            live = self.merge([item.frame for item in outcomes if item.kind == "next"], live.ram)
            if live.guard:
                self.expr(increment, live)
        raise Unsupported("C loop unrolling bound exceeded")

    def statement(self, node, frame):
        kind = node["kind"]
        args = children(node)
        self.blocks.add(node["id"])
        if kind == "CompoundStmt":
            return self.sequence(args, frame)
        if kind == "DeclStmt":
            for declaration in args:
                need(declaration["kind"] == "VarDecl", "unsupported C declaration block")
                values = children(declaration)
                need(len(values) <= 1, "unrecognized local C declaration")
                if declaration.get("storageClass") == "static":
                    need(declaration["type"]["qualType"].startswith("const "),
                         "mutable C static local")
                frame.ram[declaration["id"]] = self.expr(values[0], frame) if values else UNDEFINED
            return [Exit(frame, "next")]
        if kind == "IfStmt":
            need(len(args) in (2, 3), "unsupported C if scope")
            condition = self.truth(self.expr(args[0], frame))
            result = []
            for arm, test in enumerate((condition, self.bits.neg(condition)), 1):
                guard = self.bits.both(frame.guard, test)
                if guard:
                    branch = frame.fork(guard)
                    result.extend(self.statement(args[arm], branch) if arm < len(args)
                                  else [Exit(branch, "next")])
            return result
        if kind == "SwitchStmt":
            return self.switch(node, frame)
        if kind == "ForStmt":
            return self.for_loop(node, frame)
        if kind == "BreakStmt":
            return [Exit(frame, "break")]
        if kind == "ReturnStmt":
            return [Exit(frame, "return", self.expr(args[0], frame) if args else None)]
        if kind == "NullStmt":
            return [Exit(frame, "next")]
        self.expr(node, frame)
        return [Exit(frame, "next")]

    def call(self, fn, values, frame):
        need(fn in self.functions and fn not in self.calls, f"unsupported C function {fn}")
        self.calls.add(fn)
        self.called.add(fn)
        proc = self.functions[fn]
        args = [arg for arg in children(proc) if arg["kind"] == "ParmVarDecl"]
        need(len(args) == len(values), f"invalid C arity for {fn}")
        inner = frame.fork(frame.guard)
        for argument, value in zip(args, values, strict=True):
            inner.ram[argument["id"]] = value
        try:
            outcomes = self.statement(children(proc)[-1], inner)
            exits = [item.frame for item in outcomes]
            self.check(frame, self.bits.any(item.guard for item in exits), f"partial C execution in {fn}")
            need(all(item.kind in ("return", "next") for item in outcomes), f"escaping control in {fn}")
            void = proc["type"]["qualType"].startswith("void (")
            need(void or all(item.kind == "return" for item in outcomes), f"missing C return in {fn}")
            result = None if void else self.combine((e.frame.guard, e.value) for e in outcomes)
            frame.ram = self.merge(exits, frame.ram.keys()).ram
            return result
        except Unsupported as exc:
            raise Unsupported(f"{fn}: {exc}") from exc
        finally:
            self.calls.remove(fn)
