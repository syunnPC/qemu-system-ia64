# SPDX-License-Identifier: GPL-2.0-or-later
"""Create the full decoder lane's source bindings and contract statements."""

from hashlib import sha256
from check import FORMS, HERE, REFUTED_FORMS, ROOT
from forms import (ATOMICS16, COMPARE_ALIASES, INTEGER_BITFIELD_ALIASES, INTEGER_FEATURES,
                   INTEGER_TEST_UPDATE_ALIASES)
from long import LONG_FORMS, LONG_REFUTATIONS

DECODER_SOURCES = (
    "target/ia64/decode/opcode.inc",
    "target/ia64/decode/opcode.h",
    "target/ia64/decode/operand.h",
    "target/ia64/decode/decode.h",
    "target/ia64/decode/decode.c",
    "target/ia64/decoder.h",
    "target/ia64/decoder.c",
    "target/ia64/translate/translate.c",
)
MODEL_SOURCES = ("target/ia64/cpu.h", "target/ia64/arch/system.c")
LONG_SOURCES = DECODER_SOURCES + MODEL_SOURCES + (
    "target/ia64/cpu.c", "target/ia64/translate/gen-branch.c",
)
SOURCES = LONG_SOURCES


def main():
    units = " ".join(f"FullDecodeImplementation.{unit}Unit"
                     for unit in ("reserved", "m", "i", "b", "f", "l", "x"))
    prefix = "# SPDX-License-Identifier: GPL-2.0-or-later\n"
    contracts, proofs = [prefix], [prefix]
    source_hashes = "\n".join(
        f'"{source}" = "{sha256((ROOT / source).read_bytes()).hexdigest()}"'
        for source in SOURCES
    )
    proofs.append("[source]\n" + source_hashes + "\n")
    for opcode, form in (FORMS | REFUTED_FORMS).items():
        negation = "¬ " if opcode in REFUTED_FORMS else ""
        kind = "AoBundleContract" if opcode in ATOMICS16 else "BundleContract"
        args = f"({form}) FullDecodeImplementation.bundle{opcode}"
        if opcode in ATOMICS16:
            args += f" FullDecodeImplementation.aoFault{opcode}"
        if opcode in INTEGER_FEATURES:
            kind = "IntegerFeatureBundleContract"
            args = (f"({form}) .{INTEGER_FEATURES[opcode].lower()} "
                    f"FullDecodeImplementation.bundle{opcode} FullDecodeImplementation.integerFeatureFault{opcode}")
        contracts.append(f'''[contract."decode:{opcode}"]
check = "full-decode"
module = "FullDecode.Implementation"
statement = """
{negation}FullDecodeSpec.{kind} {units}
  {args}
"""\n''')
        table = "counterexample" if opcode in REFUTED_FORMS else "proof"
        with_feature = opcode in ATOMICS16 or opcode in INTEGER_FEATURES
        theorem = "refuted" if opcode in REFUTED_FORMS else "withFeature" if with_feature else "complete"
        paths = DECODER_SOURCES + MODEL_SOURCES if with_feature else DECODER_SOURCES
        bindings = ", ".join(f'"{source}"' for source in paths)
        proofs.append(f"""[{table}."decode:{opcode}"]
theorem = "FullDecodeImplementation.{opcode}_{theorem}"
sources = [ {bindings} ]
""")
    bindings = ", ".join(f'"{source}"' for source in DECODER_SOURCES)
    for alias, (rel, width, reg, imm) in COMPARE_ALIASES.items():
        contracts.append(f'''[contract."decode:{alias}"]
check = "full-decode"
module = "FullDecode.Implementation"
statement = """
FullDecodeSpec.CompareAliasContract {units}
  .{rel} {width} FullDecodeImplementation.bundle{reg} FullDecodeImplementation.bundle{imm}
  FullDecodeImplementation.cCompareAliasTag
"""
''')
        proofs.append(f"""[proof."decode:{alias}"]
theorem = "FullDecodeImplementation.{alias}_canonical"
sources = [ {bindings} ]
""")
    for opcode, form in LONG_FORMS.items():
        args = f"FullDecodeImplementation.lUnit FullDecodeImplementation.xUnit {form}\n  " \
               f"FullDecodeImplementation.bundle{opcode}"
        if opcode in LONG_REFUTATIONS:
            statement = f"¬ FullDecodeSpec.LongBundleContract {args}"
            table, theorem, paths = "counterexample", "refuted", DECODER_SOURCES
        else:
            args += (f" FullDecodeImplementation.cLongCompletion FullDecodeImplementation.cLongStop\n  "
                     f"FullDecodeImplementation.lbRequest{opcode} "
                     "FullDecodeImplementation.cLongBranchTarget FullDecodeImplementation.cLongBranchHints")
            statement = f"FullDecodeSpec.LongBranchContract {args}"
            table, theorem, paths = "proof", "withFeature", LONG_SOURCES
        contracts.append(f'''[contract."decode:{opcode}"]
check = "full-decode"
module = "FullDecode.Implementation"
statement = """
{statement}
"""
''')
        bindings = ", ".join(f'"{source}"' for source in paths)
        proofs.append(f"""[{table}."decode:{opcode}"]
theorem = "FullDecodeImplementation.{opcode}_{theorem}"
sources = [ {bindings} ]
""")
    bindings = ", ".join(f'"{source}"' for source in DECODER_SOURCES)
    for aliases, kind in ((INTEGER_BITFIELD_ALIASES, "ShiftAlias"),
                          (INTEGER_TEST_UPDATE_ALIASES, "PredicateUpdateAlias")):
        for alias, (variant, physical) in aliases.items():
            contracts.append(f'''[contract."decode:{alias}"]
check = "full-decode"
module = "FullDecode.Implementation"
statement = """
FullDecodeSpec.{kind}Contract (FullDecodeSpec.templateUnit {units})
  FullDecodeImplementation.iUnit .{variant} FullDecodeImplementation.bundle{physical}
  FullDecodeImplementation.cBitfieldAliasTag
"""
''')
            proofs.append(f"""[proof."decode:{alias}"]
theorem = "FullDecodeImplementation.{alias}_canonical"
sources = [ {bindings} ]
""")
    for filename, rows in (("contracts.toml", contracts), ("proofs.toml", proofs)):
        (HERE / filename).write_text("\n".join(rows), encoding="utf-8")


if __name__ == "__main__":
    main()
