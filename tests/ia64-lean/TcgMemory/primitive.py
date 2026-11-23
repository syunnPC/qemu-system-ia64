# SPDX-License-Identifier: GPL-2.0-or-later
"""Project the generic TCG memory operations and optional byte swaps."""

import hashlib
import re
from pathlib import Path

from Memory.source import body, require

LDST = Path("tcg/tcg-op-ldst.c")
MEMOP_ROOTS = {
    "tcg_canonicalize_memop": "151686b07c0501096575577acdc309b65cb32c390cdc4ebefb0f52346388d3ee",
    "gen_ldst1": "1a168f677ca0cbb8ea41fb5d0b4a2b75e56fba64eb33fb4a07c0abacd4119b06",
    "gen_ld_i64": "b30f4467497e6717558275f61268afdb31e439db7e0ef02a6bfc9b45fa131596",
    "gen_st_i64": "e815d294043da800e3e6de320b340f7ee3b58c123ed986e5607fcb7b92a2f8a3",
    "tcg_gen_qemu_ld_i64_chk": "0cea9d41dda9f9f4e9731ea0af42c8e1cb2c9bde3258f9412f4c4fb521d3efb0",
    "tcg_gen_qemu_st_i64_chk": "516dde71165f0f6c4bde4b04cb60a9c4b9f5e0056560d6c6afdc565ec7ee8868",
}
EMITTER_HASHES = {
    "ld": "762f45c7429a83229411eb9832b4aa990ba5b29605cb513463dc1cb1b71b2f0e",
    "st": "c3861c6195bc7c0588f9f6fbc7e82d3d411ae7cf9b80701fde63de9b8fe11bc2",
}
SWAPS = re.compile(
    r"caseMO_(16|32|64):tcg_gen_bswap(16|32|64)_i64\("
    r"([^;]+)\);break;"
)


def check_body(source, digest, description):
    require(hashlib.sha256(source.encode()).hexdigest() == digest,
            f"{description} changed; review the generic TCG memory projection")


def project_emitter(source, kind):
    name = f"tcg_gen_qemu_{kind}_i64_int"
    emitter = body(source, name)
    swaps = {}
    for match in SWAPS.finditer(emitter):
        logical_width, swap_width, args = match.groups()
        require(logical_width not in swaps, f"duplicate byte swap for {logical_width} bits")
        expected_args = ("val,val" if kind == "ld" else "val,orig_val")
        if swap_width != "64":
            expected_args += ",flags" if kind == "ld" else ",0"
        require(args == expected_args, f"unexpected {name} byte swap operands for {logical_width}")
        swaps[logical_width] = int(swap_width) // 8
    require(swaps.keys() == {"16", "32", "64"}, f"unrecognized {name} swap dispatch")
    # The normalized body retains the capability test, descriptor rewrite,
    # qemu_ld/st operand order and the before/after placement of the swap.
    normalized = SWAPS.sub(lambda match: f"caseMO_{match[1]}:byte_swap;break;", emitter)
    return swaps, normalized


def generate(root, source=None):
    if source is None:
        source = (root / LDST).read_text()
    for name, digest in MEMOP_ROOTS.items():
        check_body(body(source, name), digest, name)
    result = ["namespace TcgMemory"]
    for kind, call in (("ld", "load"), ("st", "store")):
        swaps, normalized = project_emitter(source, kind)
        check_body(normalized, EMITTER_HASHES[kind], f"tcg_gen_qemu_{kind}_i64_int")
        result += [
            f"def {call}NeedsSoftwareSwap (endian hostBig hasMemBswap : Bool) : Bool :=",
            "  endian != hostBig && !hasMemBswap",
            f"def {call}SwapWidth : Width → Width",
            "  | .w1 => .w1",
        ]
        result += [f"  | .w{int(width) // 8} => .w{swap}" for width, swap in swaps.items()]
    result += ["end TcgMemory"]
    return "\n".join(result)
