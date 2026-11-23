# SPDX-License-Identifier: GPL-2.0-or-later
"""Bind the native data TLB miss exits and single-page RAM helper sequences."""

import re
from pathlib import Path

from Memory.source import body, require
from TcgMemory.primitive import check_body

CPUTLB = Path("accel/tcg/cputlb.c")
EXEC = Path("accel/tcg/cpu-exec-common.c")
CPU = Path("target/ia64/cpu.c")
HELPERS = Path("accel/tcg/ldst_common.c.inc")
ATOMICITY = Path("accel/tcg/ldst_atomicity.c.inc")
ROOTS = {
    CPUTLB: {
        "mmu_lookup": "09d328a359a1e899ce49525052fb7cca6d3d2af35f34e9f24f84c9f441a4af84",
        "mmu_lookup1": "04fb24df89980dcb3de9af87da104f7a6107a19d9e33a2fef63fc04ca4f6632b",
        "tlb_fill_align": "316c0f42961c17556ffdfc913f51654786cc9b47de857654bf1ac8972a777514",
        "do_ld_1": "ff3223073a1bbfe3b398c6f317b4a0c8339b29552480e540b5a61c0322a9a575",
        "do_ld_2": "f74488e2187fe93f2ddbadc83c2db8e25fb9f53181d463d5a428ac14a0bc4c38",
        "do_ld_4": "78b45bc6cbccfcf833b480e94cb00acc650252aa32f8008abeb0e4eec8acdf2d",
        "do_ld_8": "7706a3ebff3b8cc21d5e3ab95c6cadb8b978ac34e679ab5e168944eb8955d3af",
        "do_st_1": "ceb33d7f7a8936141723b50b530ca153e683d5441005f8caef618f0ab9cce163",
        "do_st_2": "021de21b408867aae6c4577c8677a952a3c68c81a2755a43b12f66abf1ee65c4",
        "do_st_4": "7bce4c283250a8b3bcf975669edf1e5e8838f164ed35298b81f5017635c68925",
        "do_st_8": "8e6633982020cc0dd0c123b28b0962fbb88bf31e8a55eee1d977e3324208c0f9",
    },
    EXEC: {
        "cpu_loop_exit_restore": "b67579c6c5ccf41a861b9ad42d453b78ec2eb312cf8fb458419bb4179c8cd64c",
        "cpu_loop_exit": "1483b39b6314f583560e9ff06bfc9ddb98f095ec23408a16bf183a6875d084ed",
    },
    CPU: {
        "ia64_cpu_tlb_fill": "72e0d9e68b74416753bd77cbc1da824c2c17d3b6d3503e48bf55cc048a5d08d7",
    },
    HELPERS: {
        "helper_ldub_mmu": "4578e2236376c20974ea862eaf317e60775a25b89c683c625e9826fabcef37e9",
        "helper_lduw_mmu": "d047fb7a6b5c0aa231aef44c09a6e222e3dd79c361bceb9bac9d4928f0dbac49",
        "helper_ldul_mmu": "68b1e0d928d454df2f00eb2c5b7a6f8dfab354e62a6ab9f23eb4899b6b064721",
        "helper_ldq_mmu": "f4e3b5b4a491eae093521a58434344e625e26a40574a7e37374330f65dfcaf21",
        "helper_stb_mmu": "dc9bd62fa4611712caf43bd0107c2ad14a94da3c52b3754d49beb191fa4d564c",
        "helper_stw_mmu": "23a8e6afe5cc3137b3db730ecafd205189a7694ef1834f8913e883f927181131",
        "helper_stl_mmu": "3d64f721f4a2a4e8f912722cf462681a8b458bed786371578f04c02397cf41c8",
        "helper_stq_mmu": "5a9469e7e8ab854255aebda2b9a1b066eff5462b2ffe048e9e0cf634decbbb2b",
    },
    ATOMICITY: {
        "load_atom_2": "6d657d84c4051b7feffb578e3b7044803d513da25ec333e5b877a1d4c1379cac",
        "load_atom_4": "2b57f289202ec675704fc3146274f7ab2c20fef4f7adcea4f2a0ffb89cbb66f4",
        "load_atom_8": "ed13eed5f0e11817e0e30a53c165f316429c7e4f11f2e6aa6ad4cc8da166e0cc",
        "store_atom_2": "5797dc5eceeaa704739c32daf57a94d1abe048fb5659860d0b9c393a0781f351",
        "store_atom_4": "9361593bb624b2f2d467103521f7c2a2bae04df679480803614655727a8ceff3",
        "store_atom_8": "81b86cfe5cacf3de4c439c9c187c94b09f9117251ae4dd8530c1109419ca08e7",
    },
}
HELPER_HASHES = {
    ("ld", 1): "518ea4b3764f043d6dd65111a105e2b54c75552597d041f39195f8ddb5a077d0",
    ("ld", 2): "a8b2de5077346cb7015c4f3d3be65c5d78f3245986a9830e2b1f5a59aac472fe",
    ("ld", 4): "219c9e6b60d3978094e84d60aeb865e0dcd7cef25947b5ced0ee231ae25f0f7d",
    ("ld", 8): "f06eaf89db4da8b06a3f52855c42752cbea88de1af3e654d41170a2d67a17202",
    ("st", 1): "a1a49e554a35555f67ccb361dd268a11d5ffdd0f4aff50999379e0f0e1162916",
    ("st", 2): "1c572dd26361da13e588f7952eb1a1e9810e9c33cfca3c815e595728b543bf12",
    ("st", 4): "b447c38af242d41cf06b1dcc21bb72d4a8287ac0b3999fe3a4e42e7c87da3b96",
    ("st", 8): "45545408e86e8ba5168f36b465506c5303aa19d79a1c2c2ef1b2199c49207834",
}
SIZES = (1, 2, 4, 8)


def helper_steps(source, kind, size):
    name = f"do_{kind}{size}_mmu"
    code = body(source, name)
    qualifier = "access_type" if kind == "ld" else "MMU_DATA_STORE"
    lookup = re.escape(f"crosspage=mmu_lookup(cpu,addr,oi,ra,{qualifier},&l);")
    args = "l.mmu_idx,access_type," if kind == "ld" else "val,l.mmu_idx,"
    memop = "l.memop," if size > 1 else ""
    prefix = "return" if kind == "ld" else ""
    commit = re.escape(f"{prefix}do_{kind}_{size}(cpu,&l.page[0],{args}{memop}ra);")
    patterns = {".lookup": lookup, ".access": commit}
    # Count the distinct single-page statements and retain every other
    # token, including the victim and cross-page branches.
    hits = []
    for step, pattern in patterns.items():
        found = list(re.finditer(pattern, code))
        require(len(found) == 1, f"{name} single-page projection changed at {step}")
        position = found[0].start()
        leading = code[:position]
        if step == ".access" and size > 1:
            require(leading.endswith("if(likely(!crosspage)){") and
                    leading.count("{") - leading.count("}") == 1,
                    f"{name} access left the single-page guard")
        else:
            require(leading.count("{") == leading.count("}"),
                    f"{name} {step} moved into a conditional path")
        hits.append((position, step))
    normalized = re.sub("|".join(patterns.values()), "", code)
    return [step for _, step in sorted(hits)], normalized


def generate(root, cputlb=None):
    sources = {path: (root / path).read_text() for path in ROOTS}
    if cputlb is not None:
        sources[CPUTLB] = cputlb
    for path, rows in ROOTS.items():
        for name, digest in rows.items():
            check_body(body(sources[path], name), digest, name)
    cpu = sources[CPU]
    callbacks = re.search(r"static const TCGCPUOps ia64_tcg_ops = \{(.*?)\n\};", cpu, re.DOTALL)
    require(callbacks is not None and ".tlb_fill_align" not in callbacks[1] and
            re.search(r"\.tlb_fill\s*=\s*ia64_cpu_tlb_fill\b", callbacks[1]),
            "native TCG does not use the projected IA-64 TLB fill callback")
    out = ["namespace TcgMemory"]
    for kind, name in (("ld", "load"), ("st", "store")):
        out += [f"def {name}HelperPlan : Width → List HelperStep"]
        for size in SIZES:
            steps, normalized = helper_steps(sources[CPUTLB], kind, size)
            check_body(normalized, HELPER_HASHES[(kind, size)], f"do_{kind}{size}_mmu")
            out += [f"  | .w{size} => [{', '.join(steps)}]"]
    out += ["end TcgMemory"]
    return "\n".join(out)
