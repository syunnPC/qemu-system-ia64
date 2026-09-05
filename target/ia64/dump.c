/* IA-64 human-readable and machine-readable CPU state dumps. */

#include "qemu/osdep.h"
#include "qemu/qemu-print.h"
#include "arch/arch.h"
#include "cpu.h"
#include "debug.h"
#include "fpreg.h"

static void ia64_dump_tlb(FILE *f, const char *name, const IA64TlbEntry *tlb,
                          uint16_t count)
{
    uint16_t i;

    for (i = 0; i < count; i++) {
        const IA64TlbEntry *entry = &tlb[i];

        if (!entry->valid) {
            continue;
        }
        qemu_fprintf(f, "%s[%u]%s va=0x%016" PRIx64
                     " pa=0x%016" PRIx64 " ps=0x%016" PRIx64
                     " rid=0x%06" PRIx32 " key=0x%06" PRIx32
                     " ar=%u pl=%u perm=0x%x pte=0x%016" PRIx64 "\n",
                     name, i, entry->is_tr ? " TR" : " TC",
                     entry->va, entry->pa, entry->ps, entry->rid,
                     entry->key, entry->ar, entry->pl, entry->perm,
                     entry->pte);
    }
}

static void ia64_dump_machine_state(CPUState *cs, FILE *f, int flags)
{
    IA64CPU *cpu = ia64_cpu_from_cpu_state(cs);
    CPUIA64State *env = &cpu->env;
    uint64_t pr_mask = 1;
    unsigned i;

    for (i = 1; i < IA64_PR_COUNT; i++) {
        pr_mask |= (env->pr[i] != 0) ? 1ULL << i : 0;
    }

    qemu_fprintf(f, "IA64STATE SCHEMA version=%016x\n",
                 IA64_STATE_SCHEMA_VERSION);
    qemu_fprintf(f, "IA64STATE META ip=%016" PRIx64
                 " psr=%016" PRIx64 " halted=%u\n",
                 env->ip, env->psr, cs->halted ? 1 : 0);
    qemu_fprintf(f, "IA64STATE EXCEPTION code=%016" PRIx64
                 " fault_code=%016" PRIx64
                 " fault_ip=%016" PRIx64 " fault_imm=%016" PRIx64 "\n",
                 (uint64_t)env->exception_state.exception,
                 (uint64_t)env->exception_state.fault_exception,
                 env->exception_state.fault_ip, env->exception_state.fault_imm);
    qemu_fprintf(f, "IA64STATE GRNAT lo=%016" PRIx64
                 " hi=%016" PRIx64 "\n", env->nat[0], env->nat[1]);

    for (i = 0; i < IA64_GR_COUNT; i++) {
        qemu_fprintf(f, "IA64STATE GR index=%03x value=%016" PRIx64
                     " nat=%u\n", i, i == 0 ? 0 : env->gr[i],
                     (unsigned)((env->nat[i / 64] >> (i % 64)) & 1));
    }
    for (i = 0; i < IA64_BR_COUNT; i++) {
        qemu_fprintf(f, "IA64STATE BR index=%03x value=%016" PRIx64 "\n",
                     i, env->br[i]);
    }

    qemu_fprintf(f, "IA64STATE PR value=%016" PRIx64 "\n", pr_mask);
    qemu_fprintf(f, "IA64STATE AR ccv=%016" PRIx64
                 " unat=%016" PRIx64 " fpsr=%016" PRIx64
                 " csd=%016" PRIx64 " ssd=%016" PRIx64
                 " rsc=%016" PRIx64 " rnat=%016" PRIx64
                 " pfs=%016" PRIx64 "\n",
                 env->ar_ccv, env->ar_unat, env->ar_fpsr,
                 env->ar_csd, env->ar_ssd, env->ar_rsc,
                 ia64_rse_read_rnat(env), env->ar_pfs);
    qemu_fprintf(f, "IA64STATE CFM sof=%016" PRIx64
                 " sol=%016" PRIx64 " sor=%016" PRIx64
                 " rrb_gr=%016" PRIx64 " rrb_fr=%016" PRIx64
                 " rrb_pr=%016" PRIx64 "\n",
                 (uint64_t)env->cfm_sof, (uint64_t)env->cfm_sol,
                 (uint64_t)env->cfm_sor, (uint64_t)env->cfm_rrb_gr,
                 (uint64_t)env->cfm_rrb_fr, (uint64_t)env->cfm_rrb_pr);

    if (flags & CPU_DUMP_FPU) {
        for (i = 0; i < IA64_FR_COUNT; i++) {
            uint64_t low;
            uint64_t high;

            ia64_fpreg_to_spill(env, i, &low, &high);
            qemu_fprintf(f, "IA64STATE FR index=%03x low=%016" PRIx64
                         " high=%016" PRIx64 " nat=%u\n",
                         i, low, high,
                         ia64_fpreg_is_nat(env, i) ? 1 : 0);
        }
    }
}

void ia64_cpu_dump_state(CPUState *cs, FILE *f, int flags)
{
    IA64CPU *cpu = ia64_cpu_from_cpu_state(cs);
    int i;

    if (flags & CPU_DUMP_FPU) {
        ia64_sync_rotating_fr(&cpu->env);
    }

    qemu_fprintf(f, "IP: 0x%016" PRIx64 "  PSR: 0x%016" PRIx64
                 "  HALTED: %u\n",
                 cpu->env.ip, cpu->env.psr, cs->halted);
    qemu_fprintf(f, "exception: %" PRIu32 " fault_ip: 0x%016" PRIx64
                 " fault_imm: 0x%016" PRIx64 " fault_tmpl: 0x%016" PRIx64 "\n",
                 cpu->env.exception_state.exception,
                 cpu->env.exception_state.fault_ip,
                 cpu->env.exception_state.fault_imm,
                 cpu->env.exception_state.fault_tmpl);
    for (i = 0; i < IA64_GR_COUNT; i += 4) {
        qemu_fprintf(f,
                     "r%-3d 0x%016" PRIx64 " r%-3d 0x%016" PRIx64
                     " r%-3d 0x%016" PRIx64 " r%-3d 0x%016" PRIx64 "\n",
                     i, cpu->env.gr[i], i + 1, cpu->env.gr[i + 1],
                     i + 2, cpu->env.gr[i + 2], i + 3,
                     cpu->env.gr[i + 3]);
    }
    for (i = 0; i < IA64_PR_ROTATING_BASE; i += 4) {
        qemu_fprintf(f,
                     "p%d: %" PRIu64 " p%d: %" PRIu64
                     " p%d: %" PRIu64 " p%d: %" PRIu64 "\n",
                     i, cpu->env.pr[i], i + 1, cpu->env.pr[i + 1],
                     i + 2, cpu->env.pr[i + 2], i + 3,
                     cpu->env.pr[i + 3]);
    }
    for (i = 0; i < IA64_BR_COUNT; i += 4) {
        qemu_fprintf(f,
                     "b%d: 0x%016" PRIx64 " b%d: 0x%016" PRIx64
                     " b%d: 0x%016" PRIx64 " b%d: 0x%016" PRIx64 "\n",
                     i, cpu->env.br[i], i + 1, cpu->env.br[i + 1],
                     i + 2, cpu->env.br[i + 2], i + 3,
                     cpu->env.br[i + 3]);
    }
    qemu_fprintf(f, "AR.ITC: 0x%016" PRIx64 " AR.LC: 0x%016" PRIx64
                 " AR.EC: 0x%016" PRIx64 "\n",
                 ia64_itc_read(&cpu->env),
                 cpu->env.ar_lc, cpu->env.ar_ec);
    qemu_fprintf(f, "CR.DCR: 0x%016" PRIx64
                 " CR.ITM: 0x%016" PRIx64 " ITV: 0x%016" PRIx64
                 " TPR: 0x%016" PRIx64 " EOI: 0x%016" PRIx64 "\n",
                 cpu->env.cr_dcr, cpu->env.cr_itm, cpu->env.cr[IA64_CR_ITV],
                 cpu->env.cr[IA64_CR_SAPIC_TPR],
                 cpu->env.cr[IA64_CR_SAPIC_EOI]);
    qemu_fprintf(f, "SAPIC XTP: %02x\n",
                 ia64_sapic_get_xtp(CPU(cpu)));
    qemu_fprintf(f, "SAPIC IRR: %016" PRIx64 " %016" PRIx64
                 " %016" PRIx64 " %016" PRIx64 "\n",
                 cpu->env.interrupt.sapic_irr[0],
                 cpu->env.interrupt.sapic_irr[1],
                 cpu->env.interrupt.sapic_irr[2],
                 cpu->env.interrupt.sapic_irr[3]);
    qemu_fprintf(f, "SAPIC ISR: %016" PRIx64 " %016" PRIx64
                 " %016" PRIx64 " %016" PRIx64 "\n",
                 cpu->env.interrupt.sapic_isr[0],
                 cpu->env.interrupt.sapic_isr[1],
                 cpu->env.interrupt.sapic_isr[2],
                 cpu->env.interrupt.sapic_isr[3]);
    qemu_fprintf(f, "IIP: 0x%016" PRIx64 " IFA: 0x%016" PRIx64
                 " IPSR: 0x%016" PRIx64 "\n",
                 cpu->env.cr_iip, cpu->env.cr_ifa, cpu->env.cr_ipsr);
    qemu_fprintf(f, "ISR: 0x%016" PRIx64 " IFS: 0x%016" PRIx64
                 " IIM: 0x%016" PRIx64 "\n",
                 cpu->env.cr_isr, cpu->env.cr_ifs, cpu->env.cr_iim);
    qemu_fprintf(f, "IVA: 0x%016" PRIx64 " IIPA: 0x%016" PRIx64
                 " ITIR: 0x%016" PRIx64 "\n",
                 cpu->env.cr_iva, cpu->env.cr_iipa, cpu->env.cr_itir);
    qemu_fprintf(f, "IHA: 0x%016" PRIx64 " PTA: 0x%016" PRIx64
                 " RR0: 0x%016" PRIx64 " RR5: 0x%016" PRIx64
                 " RR6: 0x%016" PRIx64 " RR7: 0x%016" PRIx64 "\n",
                 cpu->env.cr_iha, cpu->env.cr_pta,
                 cpu->env.rr[IA64_RR_REGION0],
                 cpu->env.rr[IA64_RR_REGION5],
                 cpu->env.rr[IA64_RR_REGION6],
                 cpu->env.rr[IA64_RR_REGION7]);
    qemu_fprintf(f, "PKR0: 0x%016" PRIx64 " PKR1: 0x%016" PRIx64
                 " PKR2: 0x%016" PRIx64 " PKR3: 0x%016" PRIx64 "\n",
                 cpu->env.pkr[IA64_PKR_0], cpu->env.pkr[IA64_PKR_1],
                 cpu->env.pkr[IA64_PKR_2], cpu->env.pkr[IA64_PKR_3]);
    qemu_fprintf(f, "CFM: sof=%u sol=%u sor=%u rrb.gr=%u rrb.fr=%u"
                 " rrb.pr=%u AR.PFS=0x%016"
                 PRIx64 " BSP=0x%016" PRIx64 " BSPSTORE=0x%016" PRIx64 "\n",
                 cpu->env.cfm_sof, cpu->env.cfm_sol,
                 cpu->env.cfm_sor, cpu->env.cfm_rrb_gr,
                 cpu->env.cfm_rrb_fr, cpu->env.cfm_rrb_pr,
                 cpu->env.ar_pfs, cpu->env.ar_bsp, cpu->env.ar_bspstore);
    qemu_fprintf(f, "RSE: bol=%u dirty=%d/%d clean=%d/%d invalid=%d"
                 " RNAT=0x%016" PRIx64 " RNAT_ADDR=0x%016" PRIx64
                 " RNAT_DEFINED=0x%016" PRIx64
                 " RSC=0x%016" PRIx64 "\n",
                 cpu->env.rse.rse_bol, cpu->env.rse.rse_dirty,
                 cpu->env.rse.rse_dirty_nat, cpu->env.rse.rse_clean,
                 cpu->env.rse.rse_clean_nat, cpu->env.rse.rse_invalid,
                 ia64_rse_read_rnat(&cpu->env),
                 cpu->env.rse.rse_rnat_addr,
                 ia64_rse_read_rnat_defined(&cpu->env), cpu->env.ar_rsc);
    qemu_fprintf(f, "RSE-GR-DIRTY: 0x%016" PRIx64 "/0x%016" PRIx64
                 " PGRNAT=0x%016" PRIx64 "/0x%016" PRIx64 "\n",
                 cpu->env.rse.rse_gr_dirty[0],
                 cpu->env.rse.rse_gr_dirty[1],
                 cpu->env.rse.rse_pgr_nat[0],
                 cpu->env.rse.rse_pgr_nat[1]);
    for (i = 0; i < IA64_STACKED_GR_COUNT; i++) {
        qemu_fprintf(f, "RSE-PGR[%u]: 0x%016" PRIx64 " nat=%u\n", i,
                     cpu->env.rse.rse_pgr[i],
                     (unsigned)((cpu->env.rse.rse_pgr_nat[i / 64] >>
                                 (i % 64)) & 1));
    }
    qemu_fprintf(f, "RSE-FILL: RNAT=0x%016" PRIx64
                 " ADDR=0x%016" PRIx64 " DEFINED=0x%016" PRIx64
                 " VALID=%u\n",
                 cpu->env.rse.rse_load_rnat,
                 cpu->env.rse.rse_load_rnat_addr,
                 cpu->env.rse.rse_load_rnat_defined,
                 cpu->env.rse.rse_load_rnat_valid);
    qemu_fprintf(f, "RSE-WRITEBACK: RNAT=0x%016" PRIx64
                 " ADDR=0x%016" PRIx64 " DEFINED=0x%016" PRIx64
                 " VALID=%u\n",
                 cpu->env.rse.rse_writeback_rnat.value,
                 cpu->env.rse.rse_writeback_rnat.addr,
                 cpu->env.rse.rse_writeback_rnat.defined,
                 cpu->env.rse.rse_writeback_rnat.valid);
    for (i = 0; i < cpu->env.rse.rse_rnat_shadow_count; i++) {
        const IA64RnatShadowEntry *entry =
            &cpu->env.rse.rse_rnat_shadow[i];

        if (entry->valid) {
            qemu_fprintf(f, "RSE-RNAT-SHADOW[%d]: RNAT=0x%016" PRIx64
                         " ADDR=0x%016" PRIx64
                         " DEFINED=0x%016" PRIx64 "\n",
                         i, entry->value, entry->addr, entry->defined);
        }
    }
    if (cpu->firmware_debug.rse_valid) {
        const IA64FirmwareDebugRseState *rse =
            &cpu->firmware_debug.rse;

        qemu_fprintf(f, "DEBUG-SAVED-RSE: BOL=%u SOF=%u SOL=%u"
                     " BSP=0x%016" PRIx64 " BSPSTORE=0x%016" PRIx64
                     " DIRTY=%d/%d CLEAN=%d/%d INVALID=%d"
                     " GRDIRTY=0x%016" PRIx64 "/0x%016" PRIx64
                     " PGRNAT=0x%016" PRIx64 "/0x%016" PRIx64 "\n",
                     rse->bol, rse->cfm_sof, rse->cfm_sol,
                     rse->bsp, rse->bspstore, rse->dirty, rse->dirty_nat,
                     rse->clean, rse->clean_nat, rse->invalid,
                     rse->gr_dirty[0], rse->gr_dirty[1],
                     rse->pgr_nat[0], rse->pgr_nat[1]);
        qemu_fprintf(f, "DEBUG-SAVED-RNAT: RNAT=0x%016" PRIx64
                     " ADDR=0x%016" PRIx64
                     " DEFINED=0x%016" PRIx64
                     " LOAD=0x%016" PRIx64 "@0x%016" PRIx64
                     "/0x%016" PRIx64 "/%u"
                     " WRITEBACK=0x%016" PRIx64 "@0x%016" PRIx64
                     "/0x%016" PRIx64 "/%u SHADOW=%u\n",
                     rse->rnat, rse->rnat_addr, rse->rnat_defined,
                     rse->load_rnat, rse->load_rnat_addr,
                     rse->load_rnat_defined, rse->load_rnat_valid,
                     rse->writeback_rnat.value,
                     rse->writeback_rnat.addr,
                     rse->writeback_rnat.defined,
                     rse->writeback_rnat.valid,
                     rse->rnat_shadow_count);
        for (i = 0; i < rse->rnat_shadow_count; i++) {
            const IA64RnatShadowEntry *entry = &rse->rnat_shadow[i];

            if (entry->valid) {
                qemu_fprintf(f, "DEBUG-SAVED-RNAT-SHADOW[%d]:"
                             " RNAT=0x%016" PRIx64
                             " ADDR=0x%016" PRIx64
                             " DEFINED=0x%016" PRIx64 "\n",
                             i, entry->value, entry->addr, entry->defined);
            }
        }
    }
    ia64_dump_tlb(f, "ITLB", cpu->env.mmu.tlb_inst,
                  cpu->env.mmu.tlb_inst_count);
    ia64_dump_tlb(f, "DTLB", cpu->env.mmu.tlb_data,
                  cpu->env.mmu.tlb_data_count);
    if (ia64_env_cpu_class(&cpu->env)->model == IA64_CPU_MODEL_MERCED) {
        ia64_dump_tlb(f, "DTLB1", cpu->env.mmu.tlb_data_l1,
                      IA64_DTLB1_MAX);
    }
    ia64_dump_machine_state(cs, f, flags);
}
