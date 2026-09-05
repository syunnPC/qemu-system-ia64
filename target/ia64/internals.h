/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Internal IA-64 CPU subsystem state.  Include from cpu.h only after the
 * architectural entry types have been declared.
 */

#ifndef TARGET_IA64_INTERNALS_H
#define TARGET_IA64_INTERNALS_H

#define PAL_PROC_MONTECITO_ICACHE_COHERENCE   (1ULL << 5)
#define PAL_PROC_MONTECITO_EXCLUSIVE_PREFETCH (1ULL << 7)
#define PAL_PROC_MONTECITO_HT                  (1ULL << 18)
#define PAL_PROC_MONTECITO_CONTROLLABLE \
    (PAL_PROC_MONTECITO_ICACHE_COHERENCE | \
     PAL_PROC_MONTECITO_EXCLUSIVE_PREFETCH)
#define PAL_PROC_MONTECITO_AVAILABLE \
    (PAL_PROC_MONTECITO_CONTROLLABLE | PAL_PROC_MONTECITO_HT)


typedef struct IA64ExceptionState {
    /* Restart-visible exception and interruption-delivery state. */
    uint64_t fault_ip;
    /* Internal delivery operand kept separate from architected CR.IFA. */
    uint64_t fault_addr;
    uint64_t fault_imm;
    uint64_t fault_tmpl;
    uint32_t exception;
    uint32_t fault_exception;
    uint32_t fault_slot;
    bool ia32_trap;
    /* Trap raised by the IA-32 side of an ISA transition boundary. */
    bool ia32_transition_trap;
    /* Native completion trap: fault_ip is successor, fault_imm is source. */
    bool native_completion_trap;
    /* Trap enables/translation mode as observed before the instruction. */
    uint64_t psr_before_insn;

    /* Transient state spanning one serialization/fault-suppression window. */
    bool psr_ic_inflight;
    uint64_t psr_suppression_before_insn;
    uint64_t suppressed_tlb_pages[IA64_SUPPRESSED_TLB_MAX];
    uint16_t suppressed_tlb_idxmaps[IA64_SUPPRESSED_TLB_MAX];
    uint8_t suppressed_tlb_count;
    bool suppressed_tlb_overflow;
} IA64ExceptionState;

typedef struct IA64MMUState {
    /* Derived translation caches; all entries are reconstructible. */
    IA64TlbEntry tlb_data[IA64_TLB_MAX];
    IA64TlbEntry tlb_inst[IA64_TLB_MAX];
    /*
     * Merced's DTLB1 holds non-inclusive cached copies of DTLB2 entries.
     * A DTLB1 copy can satisfy an access after DTLB2 replacement; keeping the
     * corresponding softmmu translation alive is only a derived fast path.
     * Later models leave this state unused.
     */
    IA64TlbEntry tlb_data_l1[IA64_DTLB1_MAX];
    uint64_t tlb_data_l1_age[IA64_DTLB1_MAX];
    /*
     * Derived direct lookup for the modeled DTLB1.  Entries are always
     * validated against tlb_data_l1, so purge and replacement need no
     * architected bookkeeping here.
     */
    IA64DTlb1Lookup tlb_data_l1_lookup[IA64_DTLB1_LOOKUP_SIZE];
    uint64_t tlb_data_l1_clock;
    uint16_t tlb_data_count;
    uint16_t tlb_inst_count;
    uint8_t tlb_data_l1_count;
    /* One plus the most recently touched DTLB1 slot; zero means unknown. */
    uint8_t tlb_data_l1_last;
    uint16_t tlb_data_replace;
    uint16_t tlb_inst_replace;
    uint32_t tlb_data_generation;
    uint32_t tlb_inst_generation;
    IA64MicroTlbEntry tlb_data_micro[IA64_MICRO_TLB_SIZE];
    IA64MicroTlbEntry tlb_inst_micro[IA64_MICRO_TLB_SIZE];
    IA64CodeTlbEdCache code_tlb_ed;

    /* Transient bookkeeping for architected purge operations. */
    uint16_t pending_purge_data_count;
    uint16_t pending_purge_inst_count;
} IA64MMUState;

typedef struct IA64InterruptState {
    /* Architected Local SAPIC and pending external interrupt state. */
    uint8_t pending_extint;
    uint8_t sapic_xtp;
    uint16_t sapic_pmi_pending;
    bool sapic_init_pending;
    uint8_t sapic_init_reason;
    bool pal_halt_wake;
    uint64_t sapic_irr[4];
    uint64_t sapic_isr[4];

    /*
     * Derived host timer state for architected ITC/ITM registers.
     * itc_fraction is the numerator left after converting virtual
     * nanoseconds to ticks, with NANOSECONDS_PER_SECOND as denominator.
     * A synthetic post-match tick is repaid from later oscillator ticks so
     * reading ITC in an interrupt handler cannot permanently advance time.
     */
    int64_t itc_last_ns;
    uint32_t itc_fraction;
    uint64_t itc_tick_debt;
    uint64_t itm_armed_value;
    uint64_t itm_last_match;
    bool itm_armed;
    bool itm_last_match_valid;
} IA64InterruptState;

typedef struct IA64PalState {
    /* Architected PAL registration and machine-check state. */
    bool pal_mc_expected;
    uint64_t pal_bus_feature_status;
    uint64_t pal_proc_feature_status;
    uint64_t pal_mc_save_addr;
    uint64_t pal_pmi_entry;
    bool pal_proc_copy_valid;
    uint64_t pal_proc_copy_addr;
    uint64_t pal_interrupt_block_addr;
    uint64_t pal_io_block_addr;

    /* Processor RAS log and asynchronous machine-check entry state. */
    bool pal_mc_log_valid;
    bool pal_cmc_pending;
    bool pal_mca_pending;
    bool pal_mca_active;
    bool pal_init_active;
    uint8_t pal_mc_severity;
    uint64_t pal_mc_error_map;
    uint64_t pal_mc_state_parameter;
    uint64_t pal_mc_status;
    uint64_t pal_mc_address;
    uint64_t pal_mc_information;
    uint64_t pal_mc_ip;
    uint64_t pal_mca_entry;
    uint64_t pal_mca_gp;
    uint64_t pal_mca_pending_record_id;
    uint64_t pal_mca_active_record_id;
    uint64_t pal_init_entry;
    uint64_t pal_init_gp;
} IA64PalState;

typedef struct IA64RSEState {
    /* Architected RSE physical register file and partition state. */
    uint64_t rse_pgr[IA64_STACKED_GR_COUNT];
    uint64_t rse_pgr_nat[2];
    uint64_t rse_gr_dirty[2];
    uint32_t rse_bol;
    int32_t rse_dirty;
    int32_t rse_dirty_nat;
    int32_t rse_clean;
    int32_t rse_clean_nat;
    int32_t rse_invalid;
    bool rse_cfle;

    /* Transient marker for one mandatory RSE backing-store access. */
    bool rse_access;

    /*
     * A br.ret has committed and its target frame is being restored.  Keep
     * the completion-trap conditions across an interrupted mandatory fill;
     * the trap becomes eligible only after all target-frame loads complete.
     */
    bool rse_completion_pending;
    bool rse_completion_demoted;
    uint64_t rse_completion_psr;
    uint64_t rse_completion_source_ip;
    uint8_t rse_completion_source_slot;

    /*
     * Derived spill- and fill-side NaT collection state.
     *
     * AR.RNAT holds the spill-side value and rse_rnat_defined identifies
     * exactly which of its low 63 bits have a defined source.  Keep a mask
     * rather than a contiguous range: incomplete-frame movement can make
     * stores non-contiguous.
     *
     * RSE.BspLoad can walk a different collection from AR.BSPSTORE.  The
     * fill-side latch models the NaT dispersal register described by SDM
     * Vol.3, which need not be AR.RNAT.  Internal partition movement can also
     * suspend more than one partial collection.  The shadow entries retain
     * both their values and explicit defined masks until the store pointer
     * returns to them; across loadrs they also hold the partial collection at
     * the tear point that has no complete memory image.  These latches are
     * implementation state, not new architectural registers.  UINT64_MAX
     * rse_rnat_addr denotes an entirely undefined spill-side RNAT after
     * mov-to-BSPSTORE, loadrs, or an RNAT collection spill.
     *
     * loadrs makes AR.RNAT architecturally undefined, but that permission
     * to hide its value does not permit a later partial collection store to
     * corrupt registers already coherent below BSPSTORE (SDM Vol.2 6.10).
     * rse_writeback_rnat retains the explicitly known part of the physical
     * RNAT solely for that later store.  It is intentionally a separate type
     * and is never consulted by the architectural RNAT or fill paths.
     */
    uint64_t rse_rnat_addr;
    uint64_t rse_rnat_defined;
    uint64_t rse_load_rnat;
    uint64_t rse_load_rnat_addr;
    uint64_t rse_load_rnat_defined;
    bool rse_load_rnat_valid;
    IA64RnatWritebackImage rse_writeback_rnat;
    IA64RnatShadowEntry rse_rnat_shadow[IA64_RSE_RNAT_SHADOW_COUNT];
    uint8_t rse_rnat_shadow_count;
} IA64RSEState;

typedef struct IA64AlatState {
    /* Architected ALAT contents; active_count is a derived fast-path cache. */
    IA64AlatEntry alat[IA64_ALAT_ENTRIES];
    uint32_t alat_active_count;
    bool alat_full;
    /* Transient write-scope state for one faultable CPU store. */
    bool write_active;
    bool write_observed;
    uint64_t write_generation;
    /* Sum of host-local CPU store sequences and the external RAM generation. */
    uint64_t memory_write_generation;
} IA64AlatState;

typedef struct IA64FPTransactionState {
    /* Transient rollback snapshot for one faultable FP instruction. */
    uint64_t backup_fr[IA64_FR_COUNT];
    uint64_t backup_fr_nat[2];
    uint64_t backup_fr_sig[2];
    uint64_t backup_fr_ext_mant[IA64_FR_COUNT];
    uint32_t backup_fr_ext_exp[IA64_FR_COUNT];
    uint64_t backup_fr_ext_sign[2];
    uint64_t backup_fr_ext_valid[2];
    uint64_t backup_fr_int_value[IA64_FR_COUNT];
    uint64_t backup_fr_int_origin[2];
    uint64_t backup_pr[IA64_PR_COUNT];
    uint64_t backup_fr_mask[2];
    uint64_t backup_pr_mask;
    uint64_t backup_psr_mf;
    /* ISR.fpa for a scalar completing O/U/I trap. */
    bool trap_fpa;
    /* Predecoded lane-specific O/U/I/FPA ISR.code for a packed operation. */
    uint16_t packed_trap_code;
    bool packed_trap;
    bool active;
} IA64FPTransactionState;

typedef struct IA64FPState {
    /*
     * Authoritative IA-64 register-format representation.  fr is the
     * significand/binary64 execution cache; fr_ext_* and fr_int_* retain
     * values that cannot be represented losslessly by that cache.
     */
    uint64_t fr[IA64_FR_COUNT];
    uint64_t fr_nat[2];
    uint64_t fr_sig[2];
    uint64_t fr_ext_mant[IA64_FR_COUNT];
    uint32_t fr_ext_exp[IA64_FR_COUNT];
    uint64_t fr_ext_sign[2];
    uint64_t fr_ext_valid[2];
    uint64_t fr_int_value[IA64_FR_COUNT];
    uint64_t fr_int_origin[2];
    bool rotating_fr_live;
    /* RRB.FR for which the logical rotating-register arrays are arranged. */
    uint8_t rotating_fr_materialized_rrb;

    IA64FPTransactionState transaction;

    /* Architected software-assist handoff retained until FPSWA consumes it. */
    uint64_t fpswa_result_low;
    uint64_t fpswa_result_high;
    uint64_t fpswa_flags;
    uint8_t fpswa_dest_fr;
    uint8_t fpswa_dest_pr;
    uint8_t fpswa_sf;
    bool fpswa_pending;
    bool fpswa_fpa;

    /* Derived SoftFloat execution status. */
    float_status fp_status;
} IA64FPState;

typedef struct IA64FirmwareDebugState {
    /* Device/firmware bridge state, deliberately outside CPUArchState. */
    uint8_t context[IA64_FW_DEBUG_CONTEXT_SIZE];
    IA64FirmwareDebugRseState rse;
    uint16_t vector;
    bool context_valid;
    bool handler_active;
    bool rse_valid;
} IA64FirmwareDebugState;

#endif /* TARGET_IA64_INTERNALS_H */
