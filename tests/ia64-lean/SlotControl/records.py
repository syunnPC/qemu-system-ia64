# SPDX-License-Identifier: GPL-2.0-or-later
"""Bind the selected control context and lift M/I NOP decode masks."""

import re

from Decode.check import expression
from Integer.check import cases, compact, require, switches
from SlotControl.reify import fixed, function, recognize


# The enclosing callers establish scope: how the decoded record reaches the
# lifted subeffect emitters and how the current slot's stop becomes the next
# slot's group status. Their other operations are outside this projection.
CALLERS = {
    "ia64_tr_translate_insn": "20bcdf87f0506b2138d5b738068091c1edb0625ebab566ba3c0c32dd5380cc38",
    "ia64_gen_insn": "340706a890cd42d117ccc06055de41fd16c333d5294b2912fc5e189ad229ac6d",
    "ia64_gen_goto_tb_group": "c5f02d9adf59a9366f9c908fc61e58339673fb74aa6c53d9ec92e1fbe328042c",
    "ia64_tr_tb_stop": "aed42659ddf17fcf739e5df9b2ee6165dcc12eac80da0df91126c8c45eee4a4e",
}


def check_callers(translator, system, table, helpers):
    for name, digest in CALLERS.items():
        fixed(translator, name, digest)
    require("IA64_OPCODE(NOP,SYSTEM,false,UNKNOWN)" in compact(table)
            and "IA64_OPCODE(ADD,INTEGER,true,SOURCES_R2_R3)" in compact(table),
            "changed ADD/NOP opcode properties")
    require("DEF_HELPER_5(raise_exception,noreturn,env,i32,i64,i64,i32)" in compact(helpers),
            "raise_exception must stop the TCG path")
    require("caseIA64_GENERATOR_SYSTEM:returnia64_gen_system"
            in compact(function(translator, "ia64_gen_dispatch")),
            "NOP no longer enters the system emitter")
    require("caseIA64_OP_NOP:caseIA64_OP_HINT_I:"
            in compact(function(translator, "ia64_insn_is_empty_hint")),
            "NOP no longer has the empty-hint path")
    gen = function(system, "ia64_gen_system")
    ((start, end, body),) = switches(gen)
    require(compact(gen[:start]) ==
            "constIA64SystemOperands*op=&insn->operands.system;"
            "constuint64_tnext_ip=insn->address+16;switch(insn->opcode)"
            and compact(gen[end + 1:]) == "returnIA64_GEN_CONTINUE;"
            and compact(cases(body)["NOP"]) == "break;",
            "NOP no longer has an empty system-emitter body")


def lift_nop_decoder(decoder):
    paths = []
    for unit in ("m", "i"):
        result = recognize(decoder, f"ia64_is_{unit}_nop", """
            const uint64_t mask = @mask@;
            const uint64_t value = @value@;
            return (raw & mask) == value;
        """, mask=r"[^;]+", value=r"[^;]+")
        mask = expression(result["mask"], {})
        value = expression(result["value"], {})
        paths.append(f"""
def cNop_{unit} (raw : Word) : Bool := (raw &&& {mask}) == {value}

theorem cNop_{unit}_matches (raw : Word) : cNop_{unit} raw = SDM.nopMI raw := by
  apply Bool.eq_iff_iff.mpr
  simp only [cNop_{unit}, SDM.nopMI, Bool.and_eq_true, beq_iff_eq]
  bv_decide
""")
    lifted = recognize(decoder, "ia64_base_insn", """
        Ia64Instruction insn = {
            .opcode = opcode, .unit = unit, .raw = raw, .address = address,
            .slot = slot, .qp = ia64_bits(raw, @offset@, @width@),
            .operands.decoder.sf = unit == IA64_UNIT_F ? ia64_bits(raw, 34, 2) : 0,
            .valid = true,
        };
        return insn;
    """, offset=r"[0-9]+", width=r"[1-7]")
    recognize(decoder, "ia64_bits", "return (value >> low) & ((1ULL << width) - 1);")
    full_decoder = compact(function(decoder, "ia64_decode_insn"))
    for unit in ("m", "i"):
        require(re.search(
            rf"if\(ia64_is_{unit}_nop\(raw\)\){{Ia64Instructioninsn="
            r"ia64_base_insn\(IA64_OP_NOP,unit,raw,address,slot\);"
            r"insn\.operands\.decoder\.imm=ia64_immu21\(raw\);returninsn;}",
            full_decoder),
            f"projected NOP {unit.upper()} decode arm changed")
    return "\n".join([*paths, f"""
def cDecodedQualifier (raw : Word) : Word :=
  (raw >>> {lifted['offset']}#64) &&& ((1#64 <<< {lifted['width']}#64) - 1)

theorem cDecodedQualifier_matches (raw : Word) : cDecodedQualifier raw = SDM.qualifier raw := by
  simp only [cDecodedQualifier, SDM.qualifier]
  bv_decide
"""])
