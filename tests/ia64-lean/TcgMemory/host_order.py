# SPDX-License-Identifier: GPL-2.0-or-later

import re
from pathlib import Path

from Memory.source import body, compact, require
from Ordering.check import conventional_masks, function, mask_value, stripped
from TcgMemory.atomic import CPU, MASKS, SOURCE_BODIES, TCG_OP
from TcgMemory.primitive import LDST, check_body

HERE = Path(__file__).resolve().parent
HOST_HEADERS = {
    "x86Order": Path("tcg/x86_64/tcg-target-mo.h"),
    "armOrder": Path("tcg/aarch64/tcg-target-mo.h"),
}
TRANSLATE_ALL = Path("accel/tcg/translate-all.c")


def generate(root):
    definitions = {key: int(value, 16) for key, value in re.findall(
        r"(TCG_(?:MO|BAR)_\w+)\s*=\s*(0x[\da-fA-F]+)",
        (root / MASKS).read_text(encoding="utf-8"),
    )}
    guest = re.search(r"\.guest_default_memory_order\s*=\s*([^,]+),",
                      stripped((root / CPU).read_text(encoding="utf-8")))
    require(guest is not None, "missing IA-64 guest order")
    route = (root / TRANSLATE_ALL).read_text(encoding="utf-8")
    require("tcg_ctx->gen_tb=tb;tcg_ctx->guest_mo=cpu->cc->tcg_ops->guest_default_memory_order;"
            in compact(function(route, "tb_gen_code")), "guest order is not carried into TCG translation")
    ldst = (root / LDST).read_text(encoding="utf-8")
    request = conventional_masks(ldst, definitions)
    emitter = body((root / TCG_OP).read_text(encoding="utf-8"), "tcg_gen_mb")
    check_body(emitter, SOURCE_BODIES[TCG_OP]["tcg_gen_mb"], "parallel TCG mb emission")
    values = {
        "guestOrder": mask_value(guest[1], definitions),
        "loadRequest": request["ld"],
        "storeRequest": request["st"],
        "mbKind": definitions["TCG_BAR_SC"],
        "pairMask": definitions["TCG_MO_ALL"],
    }
    for name, path in HOST_HEADERS.items():
        text = stripped((root / path).read_text(encoding="utf-8"))
        default = re.search(r"(?m)^#define[ \t]+TCG_TARGET_DEFAULT_MO[ \t]+([^\n]+)$", text)
        require(default is not None, f"missing target order in {path}")
        values[name] = mask_value(default[1], definitions)
    lean = "\n".join(f"def {name} : BitVec 8 := {value}" for name, value in values.items())
    return ("namespace TcgMemory.HostOrder\n\n" + lean + "\n\nend TcgMemory.HostOrder\n\n" +
            (HERE / "HostOrder.lean.inc").read_text(encoding="utf-8"))
