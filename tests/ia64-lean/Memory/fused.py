# SPDX-License-Identifier: GPL-2.0-or-later
"""Check the source reachability of the fused release-store counterexample."""

import hashlib
import os
import shlex
import subprocess
import tempfile
from pathlib import Path

from source import body, require

HERE = Path(__file__).resolve().parent


# Bind the witness to the actual recognized path, including its eligibility
# guards. The decoder fields are checked separately by compiling the decoder.
FUSED_BODIES = {
    "translate/translate.c": {
        "ia64_is_fill_st1_postinc":
            "e30485a68f768bb05066970095625466d219d8d828b0761f15c06d9d1bf6d43a",
        "ia64_analyze_self_counted_loop":
            "19f85170ddbb2cb196909d4c049af609e8bc34ae4cd8c76991f03f6bdef4535e",
        "ia64_prepare_self_counted_loop":
            "7edbe09aa728a0627867c677ec2aecb4ce66041d48d023d920ea129a82f4bf76",
        "ia64_gen_fill_st1_cloop":
            "8fca247aa814c6bbd5e0f8199b836bcfe19dbbe342b856bebc0fa4cbfbc44682",
        "ia64_tr_translate_insn":
            "20bcdf87f0506b2138d5b738068091c1edb0625ebab566ba3c0c32dd5380cc38",
    },
    "translate/gen-branch.c": {
        "ia64_gen_branch":
            "0d3111fa3785245f1d17f80be8a80421f6955cec30863b9b6b61fa38341ff45b",
    },
    "helper/helper-rse.c": {
        "helper_cloop_fill_st1":
            "31a44a3321575c9e530eca601e61fc9cf2c4db6a07947cfc8e6bfd7ff0c750c7",
    },
    "arch/rse.c": {
        "ia64_rse_cloop_fill_st1":
            "890225dea519b3e1e0bb89a4851b2251e8282ad15bbdfbbdd16881a5f614c25f",
    },
    "exec-access.c": {
        "ia64_exec_probe_host":
            "018662be966f811c92a19ab9c4c3947863c4e47a305bfdf4176b2733fd395fbc",
    },
}


def check_source(root):
    target = root / "target/ia64"
    for name, functions in FUSED_BODIES.items():
        source = (target / name).read_text(encoding="utf-8")
        for function, expected in functions.items():
            digest = hashlib.sha256(body(source, function).encode()).hexdigest()
            require(digest == expected,
                    f"fused release witness needs review: {function} changed")


def check_decoder(root):
    # SDM v3 formats M5 and B2, plus the MIB template in chapter 4.
    # st1.rel [r8] = r9, 1; nop.i; br.cloop this_bundle
    store = (5 << 37) | (0x34 << 30) | (8 << 20) | (9 << 13) | (1 << 6)
    nop = 1 << 27
    branch = (4 << 37) | (5 << 6)
    requests = f"M 0 {store:x} 0\nI 0 {nop:x} 0\nB 0 {branch:x} 0\n"
    with tempfile.TemporaryDirectory(prefix="ia64-fused-decode-") as temporary:
        executable = Path(temporary) / "observe"
        decoder = root / "target/ia64"
        subprocess.run(
            [*shlex.split(os.environ.get("CC", "cc")), "-std=c11", "-O2",
             "-I", str(root), str(HERE.parent / "Decode/observe.c"),
             str(decoder / "decode/decode.c"), str(decoder / "decoder.c"),
             "-o", str(executable)],
            check=True,
        )
        output = subprocess.check_output([str(executable)], input=requests, text=True)
    decoded = [line.split() for line in output.splitlines()]
    require(len(decoded) == 3, "incomplete fused witness decoder output")
    st, hint, br = decoded
    require(st[:6] == ["ST1REL", "defined", "0", "0", "9", "8"]
            and st[11:17] == ["1", "0", "0", "1", "0", "1"],
            "fused witness requires the post-increment release-store form")
    require(hint[0] == "NOP" and br[:3] == ["BR_CLOOP", "defined", "0"]
            and br[11] == "0", "fused witness requires a nop and self br.cloop")


def check_fused_release_loop(root, check_lean):
    check_source(root)
    check_decoder(root)
    check_lean((HERE / "Fused.lean").read_text(encoding="utf-8"))
    print("COUNTEREXAMPLE ordering:fused-release-loop "
          "The bulk-fill contract admits observing a later byte while an earlier "
          "byte still reads as zero. IA-64 release/acquire forbids that outcome.")
