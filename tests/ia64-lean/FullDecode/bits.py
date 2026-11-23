# SPDX-License-Identifier: GPL-2.0-or-later
"""Exact finite Boolean functions over the decoder's input bits."""

from functools import lru_cache


ORDER = {name: rank for rank, name in enumerate(
    ("unit", "code", "index", "slot", "raw", "l", "x", "low", "high", "address")
)}


def bit_rank(bit):
    name, index = bit
    return ORDER[name], -index


class Bits:
    def __init__(self):
        self.nodes = [(None, 0, 0), (None, 1, 1)]
        self.unique = {}
        self.ite = lru_cache(maxsize=None)(self._ite)
        self.restrict = lru_cache(maxsize=None)(self._restrict)

    def node(self, variable, lo, hi):
        if lo == hi:
            return lo
        key = (variable, lo, hi)
        if key not in self.unique:
            self.unique[key] = len(self.nodes)
            self.nodes.append(key)
        return self.unique[key]

    def variable(self, name, index):
        return self.node((name, index), 0, 1)

    def _ite(self, select, yes, no):
        if select == 0 or yes == no:
            return no
        if select == 1:
            return yes
        if yes == 1 and no == 0:
            return select
        next_var = min((self.nodes[i][0] for i in (select, yes, no) if i > 1), key=bit_rank)
        operands = [(self.nodes[i][1], self.nodes[i][2])
                    if self.nodes[i][0] == next_var else (i, i)
                    for i in (select, yes, no)]
        lo = self.ite(*(operand[0] for operand in operands))
        hi = self.ite(*(operand[1] for operand in operands))
        return self.node(next_var, lo, hi)

    def _restrict(self, value, guard):
        """Return value on guard. Values outside guard are don't-cares."""
        if guard == 0:
            return 0
        if guard == 1 or value < 2:
            return value
        gv, lv, hv = self.nodes[value]
        gg, lg, hg = self.nodes[guard]
        variable = min(gv, gg, key=bit_rank)
        if gv != variable:
            lv = hv = value
        if gg != variable:
            lg = hg = guard
        if lg == 0:
            return self.restrict(hv, hg)
        if hg == 0:
            return self.restrict(lv, lg)
        return self.node(variable, self.restrict(lv, lg), self.restrict(hv, hg))

    def neg(self, value):
        return self.ite(value, 0, 1)

    def both(self, left, right):
        return self.ite(left, right, 0)

    def either(self, left, right):
        return self.ite(left, 1, right)

    def xor(self, left, right):
        return self.ite(left, self.neg(right), right)

    def any(self, values):
        result = 0
        for value in values:
            result = self.either(result, value)
        return result

    def all(self, values):
        result = 1
        for value in values:
            result = self.both(result, value)
        return result

    def word(self, name, width):
        return tuple(self.variable(name, bit) for bit in range(width))

    @staticmethod
    def constant(value, width):
        return tuple((value >> bit) & 1 for bit in range(width))

    @staticmethod
    def number(value, signed=False):
        if not all(bit in (0, 1) for bit in value):
            return None
        result = sum(bit << i for i, bit in enumerate(value))
        if signed and value[-1]:
            result -= 1 << len(value)
        return result

    @staticmethod
    def cast(value, width, signed=False):
        ext = value[-1] if signed else 0
        return (value + (ext,) * max(0, width - len(value)))[:width]

    def select(self, condition, yes, no):
        return tuple(self.ite(condition, a, b) for a, b in zip(yes, no, strict=True))

    def eq(self, left, right):
        return self.all(self.neg(self.xor(a, b)) for a, b in zip(left, right, strict=True))

    def less(self, left, right, signed=False):
        result = 0
        for a, b in zip(left, right, strict=True):
            result = self.ite(self.xor(a, b), b, result)
        if signed:
            result = self.ite(self.xor(left[-1], right[-1]), left[-1], result)
        return result

    def add(self, left, right, carry=0):
        result = []
        for a, b in zip(left, right, strict=True):
            different = self.xor(a, b)
            result.append(self.xor(different, carry))
            carry = self.ite(different, carry, a)
        return tuple(result)

    def sub(self, left, right):
        return self.add(left, tuple(map(self.neg, right)), 1)

    def shift(self, value, count, *, right=False, signed=False):
        width = len(value)
        pad = value[-1] if signed and right else 0

        def constant_shift(bits, n):
            n = min(n, width)
            if right:
                return bits[n:] + (pad,) * n
            return (0,) * n + bits[:width - n]

        for i, bit in enumerate(count):
            if bit:
                value = self.select(bit, constant_shift(value, 1 << i), value)
        return value

    def multiply(self, left, right):
        if self.number(left) is not None:
            left, right = right, left
        width = len(left)
        result = (0,) * width
        for i, bit in enumerate(right):
            if bit:
                column = (0,) * i + tuple(self.both(bit, b) for b in left[:width - i])
                result = self.add(result, column)
        return result
