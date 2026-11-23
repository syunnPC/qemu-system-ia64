# SPDX-License-Identifier: GPL-2.0-or-later
"""Project the CMP8XCHG16 helper's half selection, packing and CAS loop."""

import re
from pathlib import Path

from Memory.source import body, compact, require
from Ordering.check import cases, function, stripped, switches
from TcgMemory.atomic import GEN_MEMORY, TRANSLATOR
from TcgMemory.compare import ARCH_MEMORY, EXEC_ACCESS, HELPER_MEMORY
from TcgMemory.primitive import check_body

BODIES = {
    ARCH_MEMORY: ("ia64_memory_cmp8xchg16", "ia64_ldq_data_ra", "ia64_stq_data_ra",
                  "ia64_runtime_data_memop"),
    EXEC_ACCESS: ("ia64_exec_load_16", "ia64_exec_cmpxchg_16", "ia64_exec_probe_write",
                  "ia64_exec_is_parallel", "ia64_exec_exit_atomic"),
    HELPER_MEMORY: ("helper_cmp8xchg16",),
    TRANSLATOR: ("ia64_insn_needs_16byte_atomics", "ia64_gen_prepare_insn",
                 "ia64_gen_check_alignment_only"),
    Path("target/ia64/arch/system.c"): ("ia64_system_read_ar",),
    Path("target/ia64/helper/helper-system.c"): ("helper_read_ar",),
    Path("accel/tcg/cputlb.c"): ("do_ld16_mmu",),
    Path("accel/tcg/ldst_common.c.inc"): ("cpu_ld16_mmu",),
    Path("accel/tcg/ldst_atomicity.c.inc"): ("load_atom_16",),
}
DIGESTS = {
    "ia64_memory_cmp8xchg16": "2dec07da67686667fc11313541f0e68c9233c971ce3280d8a7906685c2e08fe2",
    "ia64_ldq_data_ra": "a86705ac83070cbafa8a12cd48f8fa80dfa3457cf5fe831954b95e4aaf82d2f0",
    "ia64_stq_data_ra": "779ae08f3db25ef904df6cc7412572a57ae41f01079d4200d679f913a5468106",
    "ia64_runtime_data_memop": "32adedca373932eef81935a79481b87e1ea750f30c7cb52bec3093887a0a16e5",
    "ia64_exec_load_16": "61d5d24b48696507b2b07efeef595bec22487c6b78bf3814f97e464faa537664",
    "ia64_exec_cmpxchg_16": "c9a1b24ea64f129cf80ac565412758670a53c0ea1e617be6e43ba2b8ac6ad996",
    "ia64_exec_probe_write": "d2804e37f6d84fc77898adaebe5010295ab1455ba8c3aec8a15f21f2e29c33a7",
    "ia64_exec_is_parallel": "0c8649b79fe203329a34d91cf4acb3f73383b688d13399f459579a6e8bd37642",
    "ia64_exec_exit_atomic": "b437b5c08eb06163cb8b21c606f76c0374cf1a62f7e83679b415488ba6ad4ab4",
    "helper_cmp8xchg16": "51fc7ba0c97fcd4b400aa003deaa4e82a88c4b2e286b97df02bea225ec04c326",
    "ia64_insn_needs_16byte_atomics": "423c5dd7cd14046830cd6858252cb66d54b97f64f4da4d1e7762a99af9277303",
    "ia64_gen_prepare_insn": "0e4c08174cef80aa042f7113ee5322250e92b3da80876b4cc48dcae3062ae703",
    "ia64_gen_check_alignment_only": "02cc4f9098f349b35a90b34955398780814e0496cba9a97d8948e609913f9a9f",
    "ia64_system_read_ar": "2a38f9370c458975e03f4c79c08315157e610e2c7f1420e9fff6363fc11c2042",
    "helper_read_ar": "f84ab41f8e564d18125f5553f9229e88f28771f7da8bfb7b3ee3186227ce78f7",
    "do_ld16_mmu": "24a870488b08fcd1715fd17d34f4061ece138f3b035a5716fc246d840220161a",
    "cpu_ld16_mmu": "9412a64b70c2aff9e73e278c6ed01896186ea24afc73dbba57247caba1330585",
    "load_atom_16": "8e6e8f37f736f3e65cb8314a6c2a28275e2ce6a82d4a5b6fca1f55c64ca017e8",
    "CMP8XCHG16": "2c8b4472f0055f035c76b6128c42da438531f99bfc795087eb61748505a14095",
}


def selected(root, mutation):
    for path, names in BODIES.items():
        source = (root / path).read_text()
        for name in names:
            value = body(source, name)
            if name == "ia64_memory_cmp8xchg16":
                layout = re.search(
                    r"big_endian\?int128_make128\((csd|val),(csd|val)\)"
                    r":int128_make128\((csd|val),(csd|val)\);", value)
                require(layout is not None, "unsupported CMP8XCHG16 Int128 layout")
                yield "layout", layout.groups()
                half = re.search(
                    r"if\(big_endian\)\{old=\(addr&8\)\?"
                    r"int128_get(lo|hi)\(expected\):int128_get(lo|hi)\(expected\);\}"
                    r"else\{old=\(addr&8\)\?int128_get(lo|hi)\(expected\)"
                    r":int128_get(lo|hi)\(expected\);\}", value)
                require(half is not None, "unsupported CMP8XCHG16 compare half")
                hi_be, lo_be, hi_le, lo_le = half.groups()
                if mutation:
                    hi_be = "hi" if hi_be == "lo" else "lo"
                yield "half", (hi_be, lo_be, hi_le, lo_le)
                value = value.replace(half[0], "COMPARE_HALF;").replace(layout[0], "PACK_WORDS;")
            yield name, value
    generator = function(stripped((root / GEN_MEMORY).read_text()), "ia64_gen_memory")
    dispatch = cases(switches(generator)[0][2])
    yield "CMP8XCHG16", compact(dispatch["CMP8XCHG16"])


def generate(root, mutation=False):
    source = dict(selected(root, mutation))
    half = [".1" if key == "lo" else ".2" for key in source.pop("half")]
    hi_be, lo_be, hi_le, lo_le = half
    be_lo, be_hi, le_lo, le_hi = source.pop("layout")
    for name, value in source.items():
        check_body(value, DIGESTS[name], name)
    return "\n".join((
        "namespace TcgMemory.CompareWide",
        "def sourceHalf (big upper : Bool) (expected : Word × Word) : Word :=",
        f"  if big then (if upper then expected{hi_be} else expected{lo_be})",
        f"  else (if upper then expected{hi_le} else expected{lo_le})",
        "def sourceDesired (big : Bool) (val csd : Word) : Word × Word :=",
        f"  if big then ({be_lo}, {be_hi}) else ({le_lo}, {le_hi})",
        "end TcgMemory.CompareWide",
    ))
