# SPDX-License-Identifier: GPL-2.0-or-later
"""Project frame bookkeeping from the ALLOC and COVER helper calls."""

import re

from Decode.check import c_function, get_match, uncomment


def compact(text):
    return re.sub(r"\s+", "", text)


def function(source, name):
    return compact(c_function(source, name)[1])


def require(text, fragment, name, *, full=False):
    return get_match(re.escape(compact(fragment)), text, name, full=full)


def frame_updates(root, source, header):
    def read(path):
        return uncomment((root / f"target/ia64/{path}").read_text(encoding="utf-8"))

    translators = compact(read("translate/gen-system.c"))
    (pack,) = get_match(
        r"caseIA64_OP_ALLOC:\{uint64_tpacked=\(uint64_t\)op->immediate;"
        r"uint32_tsof=\(packed>>0\)&0x7f;uint32_tsol=\(packed>>7\)&0x7f;"
        r"uint32_tsor=\(packed>>14\)&0x0f;"
        r"if\(!ia64_cfm_frame_fields_valid\(sof,sol,sor\)\)\{"
        r"ia64_gen_raise_exception\(IA64_EXCP_ILLEGAL,insn->address,insn->raw,insn->slot\);"
        r"returnIA64_GEN_NORETURN;\}gen_helper_alloc_rse\(tcg_env,"
        r"tcg_constant_i32\(op->destination\),tcg_constant_i32\((.+?)\),"
        r"tcg_constant_i64\(insn->address\),tcg_constant_i32\(insn->slot\)\);break;\}",
        translators, "ALLOC TCG emission",
    ).groups()
    require(translators, "case IA64_OP_COVER: ia64_gen_check_cover_virtualization(insn);"
            "gen_helper_cover_rse(tcg_env); break;", "COVER TCG emission")
    (sol_shift, sor_shift) = get_match(r"sof\|\(sol<<(\d+)\)\|\(sor<<(\d+)\)",
                                      pack, "ALLOC TCG operands", full=True).groups()

    wrappers = read("helper/helper-rse.c")
    require(function(wrappers, "helper_alloc_rse"),
            "ia64_rse_alloc(env, r1, pfm, fault_ip, slot, GETPC());",
            "ALLOC TCG helper", full=True)
    require(function(wrappers, "helper_cover_rse"), "ia64_rse_cover(env);",
            "COVER TCG helper", full=True)

    alloc = function(source, "ia64_rse_alloc")
    (sol_extract, sor_extract) = get_match(
        r"uint32_told_sof=env->cfm_sof;uint32_tnew_sof=pfm&0x7f;"
        r"uint32_tnew_sol=\(pfm>>(\d+)\)&0x7f;uint32_tnew_sor=\(pfm>>(\d+)\)&0x0f;",
        alloc, "ALLOC field extraction",
    ).groups()
    require(alloc, f"""
        uint32_t old_sof = env->cfm_sof;
        uint32_t new_sof = pfm & 0x7f;
        uint32_t new_sol = (pfm >> {sol_extract}) & 0x7f;
        uint32_t new_sor = (pfm >> {sor_extract}) & 0x0f;
        int32_t growth = (int32_t)new_sof - (int32_t)env->cfm_sof;
        if (!ia64_cfm_frame_fields_valid(new_sof, new_sol, new_sor)) {{
            env->cr_isr = 0;
            ia64_raise_exception(env, IA64_EXCP_ILLEGAL, fault_ip, 0, slot);
        }}
        if (new_sor != env->cfm_sor &&
            (env->cfm_rrb_gr || env->cfm_rrb_fr || env->cfm_rrb_pr)) {{
            env->cr_isr = 0x30;
            ia64_raise_exception(env, IA64_EXCP_RESERVED_REG_FIELD, fault_ip, 0, slot);
        }}
        if (growth < 0 || growth > env->rse.rse_invalid || env->cfm_rrb_gr) {{
            ia64_rse_sync_frame_out(env);
        }}
        ia64_rse_new_frame(env, growth, ra);
        env->cfm_sof = new_sof;
        env->cfm_sol = new_sol;
        env->cfm_sor = new_sor;
        if (growth > 0) {{
            if (likely((!new_sor || !env->cfm_rrb_gr) &&
                       !(env->rse.rse_pgr_nat[0] | env->rse.rse_pgr_nat[1]))) {{
                ia64_rse_load_linear(env, old_sof, growth);
                if (unlikely((env->nat[0] >> IA64_STACKED_GR_BASE) | env->nat[1])) {{
                    ia64_clear_bit_range(env->nat, IA64_STACKED_GR_BASE + old_sof, growth);
                }}
            }} else {{
                ia64_rse_sync_frame_in_range(env, old_sof, growth);
            }}
        }}
        ia64_rse_invalidate_stacked_alat(env);
        if (r1 != 0) {{
            env->gr[r1] = env->ar_pfs;
            ia64_gr_nat_set(env, r1, false);
            ia64_rse_mark_gr_dirty(env, r1);
        }}
        ia64_rse_check(env, "alloc");
        IA64_TRACE_RSE_STATE(env, "alloc");
    """, "ALLOC projected helper body", full=True)
    require(function(source, "ia64_rse_new_frame"), """
        if (growth <= env->rse.rse_invalid) {
            env->rse.rse_invalid -= growth;
            return;
        }
        growth -= env->rse.rse_invalid;
        if (growth <= env->rse.rse_clean) {
            env->rse.rse_invalid = 0;
            env->rse.rse_clean -= growth;
            env->rse.rse_clean_nat =
                ia64_rse_nat_words_shrink(env->ar_bsp, env->rse.rse_clean +
                                                           env->rse.rse_dirty + 1) -
                env->rse.rse_dirty_nat;
            return;
        }
    """, "ALLOC resident partition cases")
    require(function(source, "ia64_gr_nat_set"), """
        if (reg == 0) { return; }
        if (nat) {
            env->nat[reg / 64] |= (1ULL << (reg % 64));
        } else {
            env->nat[reg / 64] &= ~(1ULL << (reg % 64));
        }
    """, "ALLOC destination NaT", full=True)
    require(function(source, "ia64_rse_cover"), """
        if (!(env->psr & IA64_PSR_IC)) {
            env->cr_ifs = IA64_IFS_V | ia64_rse_current_cfm(env);
        }
        ia64_rse_sync_frame_out(env);
        ia64_rse_preserve_frame(env, env->cfm_sof);
        env->cfm_sof = 0;
        env->cfm_sol = 0;
        env->cfm_sor = 0;
        ia64_rse_reset_rotations(env);
        ia64_rse_invalidate_stacked_alat(env);
        ia64_rse_check(env, "cover");
        IA64_TRACE_RSE_STATE(env, "cover");
    """, "COVER projected helper body", full=True)
    require(function(source, "ia64_rse_preserve_frame"), """
        uint32_t nats;
        if (nregs == 0) { return; }
        nats = ia64_rse_nat_words_grow(env->ar_bsp, nregs);
        env->rse.rse_bol = ia64_rse_wrap_phys(env->rse.rse_bol + nregs);
        env->ar_bsp += (uint64_t)(nregs + nats) * 8;
        env->rse.rse_dirty += nregs;
        env->rse.rse_dirty_nat += nats;
    """, "COVER frame preservation", full=True)
    require(function(source, "ia64_rse_reset_rotations"), """
        env->cfm_rrb_gr = 0;
        if (unlikely(env->cfm_rrb_fr != 0)) { ia64_set_cfm_rrb_fr(env, 0); }
        if (unlikely(env->cfm_rrb_pr != 0)) { ia64_set_cfm_rrb_pr(env, 0); }
    """, "COVER rotation reset", full=True)
    require(function(read("arch/fp.c"), "ia64_set_cfm_rrb_fr"),
            "env->cfm_rrb_fr = new_rrb;", "floating rotation assignment")
    system = read("arch/system.c")
    require(function(system, "ia64_set_cfm_rrb_pr"),
            "ia64_set_cfm_rrb_pr_slow(env, new_rrb, old_rrb);", "predicate rotation setter")
    require(function(system, "ia64_set_cfm_rrb_pr_slow"),
            "env->cfm_rrb_pr = new_rrb;", "predicate rotation assignment")
    ic = get_match(r"#define IA64_PSR_IC\s+\(1ULL << (\d+)\)", header, "PSR.ic bit")[1]
    valid = get_match(r"#define IA64_IFS_V\s+\(1ULL << (\d+)\)", header, "IFS.v bit")[1]

    return f"""
def packAllocC (sof sol : BitVec 7) (sor : BitVec 4) : U32 :=
  sof.zeroExtend 32 ||| (sol.zeroExtend 32 <<< {sol_shift}) |||
    (sor.zeroExtend 32 <<< {sor_shift})

def allocFieldsC (pfm : U32) : BitVec 7 × BitVec 7 × BitVec 4 :=
  (pfm.extractLsb' 0 7, pfm.extractLsb' {sol_extract} 7, pfm.extractLsb' {sor_extract} 4)

def allocResidentC (s : FrameState) (pfm : U32) (r : BitVec 7)
    (old : Word) (nat : Bool) : FrameState × (Word × Bool) :=
  let (sof, sol, sor) := allocFieldsC pfm
  let growth : Int := (sof.toNat : Int) - s.cfm.sof.toNat
  let resized :=
    if growth ≤ s.invalid then {{ s with invalid := s.invalid - growth }}
    else
      let clean := s.clean - (growth - s.invalid)
      {{ s with
        invalid := 0
        clean := clean
        cleanNat := natShrinkC s.bsp (BitVec.ofInt 32 (clean + s.dirty + 1)) - s.dirtyNat }}
  ({{ resized with cfm := {{ s.cfm with sof := sof, sol := sol, sor := sor }} }},
    if r ≠ 0 then (s.pfs, false) else (old, nat))

def icMaskC : Word := 1 <<< {ic}
def ifsValidC : Word := 1 <<< {valid}

def coverFrameC (s : FrameState) : FrameState :=
  let f := s.cfm
  let n := f.sof.zeroExtend 32
  let saved := {{ s with ifs := if s.psr &&& icMaskC = 0 then
    ifsValidC ||| frameMarkerC f.sof f.sol f.sor f.gr f.fr f.pr else s.ifs }}
  let moved := if n = 0 then saved else
    {{ saved with
       bol := wrapPhysC (s.bol + (n.toNat : Int))
       bsp := advanceC s.bsp n
       dirty := s.dirty + (n.toNat : Int)
       dirtyNat := s.dirtyNat + natGrowC s.bsp n }}
  {{ moved with cfm := ⟨0, 0, 0, 0, 0, 0⟩ }}
"""
