/* IA-64 GDB remote-register wire format. */

#include "qemu/osdep.h"
#include "cpu.h"
#include "arch/arch.h"
#include "arch/system.h"
#include "debug.h"
#include "fpreg.h"
#include "gdbstub/helpers.h"

/*
 * Keep this layout in sync with GDB's regformats/reg-ia64.dat.  IA-64 GDB
 * predates target-description XML and expects this 462-register raw layout.
 * In particular, floating-point registers occupy 128 bits on the wire even
 * though only 82 bits are architecturally significant.
 */
enum {
    IA64_GDB_GR0_REGNUM = 0,
    IA64_GDB_FR0_REGNUM = 128,
    IA64_GDB_PR0_REGNUM = 256,
    IA64_GDB_BR0_REGNUM = 320,
    IA64_GDB_VFP_REGNUM = 328,
    IA64_GDB_VRAP_REGNUM = 329,
    IA64_GDB_PR_REGNUM = 330,
    IA64_GDB_IP_REGNUM = 331,
    IA64_GDB_PSR_REGNUM = 332,
    IA64_GDB_CFM_REGNUM = 333,
    IA64_GDB_AR0_REGNUM = 334,
};

static uint64_t ia64_gdb_read_pr(const CPUIA64State *env)
{
    uint64_t value = 1;

    for (unsigned logical = 1; logical < IA64_PR_COUNT; logical++) {
        unsigned physical = logical;

        if (logical >= 16) {
            physical = 16 + ((logical - 16 + env->cfm_rrb_pr) % 48);
        }
        value |= (uint64_t)(env->pr[logical] != 0) << physical;
    }
    return value;
}

static void ia64_gdb_write_pr(CPUIA64State *env, uint64_t value)
{
    for (unsigned logical = 1; logical < IA64_PR_COUNT; logical++) {
        unsigned physical = logical;

        if (logical >= 16) {
            physical = 16 + ((logical - 16 + env->cfm_rrb_pr) % 48);
        }
        env->pr[logical] = (value >> physical) & 1;
    }
    env->pr[IA64_PR_TRUE] = 1;
}

static uint64_t ia64_gdb_read_cfm(const CPUIA64State *env)
{
    return env->cfm_sof | ((uint64_t)env->cfm_sol << IA64_CFM_SOL_SHIFT) |
           ((uint64_t)env->cfm_sor << IA64_CFM_SOR_SHIFT) |
           ((uint64_t)env->cfm_rrb_gr << IA64_CFM_RRB_GR_SHIFT) |
           ((uint64_t)env->cfm_rrb_fr << IA64_CFM_RRB_FR_SHIFT) |
           ((uint64_t)env->cfm_rrb_pr << IA64_CFM_RRB_PR_SHIFT);
}

static void ia64_gdb_write_cfm(CPUIA64State *env, uint64_t value)
{
    unsigned sof = value & IA64_CFM_SOF_MASK;
    unsigned sol = (value & IA64_CFM_SOL_MASK) >> IA64_CFM_SOL_SHIFT;
    unsigned sor = (value & IA64_CFM_SOR_MASK) >> IA64_CFM_SOR_SHIFT;
    unsigned rrb_gr = (value & IA64_CFM_RRB_GR_MASK) >> IA64_CFM_RRB_GR_SHIFT;
    unsigned rrb_fr = (value & IA64_CFM_RRB_FR_MASK) >> IA64_CFM_RRB_FR_SHIFT;
    unsigned rrb_pr = (value & IA64_CFM_RRB_PR_MASK) >> IA64_CFM_RRB_PR_SHIFT;
    uint64_t physical_pr;

    /*
     * Changing the frame shape or RRB.GR also changes the RSE partitions and
     * its logical stacked-register view.  There is no fault-reporting channel
     * in the GDB callback, so reject such writes instead of leaving an
     * internally inconsistent RSE.  RRB.FR and RRB.PR have complete rebasing
     * setters and can safely be changed independently.
     */
    if (sof != env->cfm_sof || sol != env->cfm_sol || sor != env->cfm_sor ||
        rrb_gr != env->cfm_rrb_gr || rrb_fr >= 96 || rrb_pr >= 48) {
        return;
    }

    physical_pr = ia64_gdb_read_pr(env);
    ia64_set_cfm_rrb_fr(env, rrb_fr);
    env->cfm_rrb_pr = rrb_pr;
    /* env->pr[] is a logical view; preserve the raw physical PR file. */
    ia64_gdb_write_pr(env, physical_pr);
}

int ia64_cpu_gdb_read_register(CPUState *cs, GByteArray *buf, int reg)
{
    IA64CPU *cpu = ia64_cpu_from_cpu_state(cs);
    CPUIA64State *env = &cpu->env;
    uint64_t val = 0;

    if (reg >= IA64_GDB_GR0_REGNUM && reg < IA64_GDB_FR0_REGNUM) {
        val = reg == 0 ? 0 : env->gr[reg];
    } else if (reg < IA64_GDB_PR0_REGNUM) {
        uint64_t low, high;
        unsigned freg = reg - IA64_GDB_FR0_REGNUM;

        if (freg >= IA64_FR_ROTATING_BASE) {
            ia64_sync_rotating_fr(env);
        }
        ia64_fpreg_to_spill(env, freg, &low, &high);
        return gdb_get_reg128(buf, high, low);
    } else if (reg < IA64_GDB_BR0_REGNUM) {
        val = (ia64_gdb_read_pr(env) >> (reg - IA64_GDB_PR0_REGNUM)) & 1;
    } else if (reg < IA64_GDB_VFP_REGNUM) {
        val = env->br[reg - IA64_GDB_BR0_REGNUM];
    } else if (reg == IA64_GDB_VFP_REGNUM || reg == IA64_GDB_VRAP_REGNUM) {
        /* GDB computes these virtual registers itself. */
    } else if (reg == IA64_GDB_PR_REGNUM) {
        val = ia64_gdb_read_pr(env);
    } else if (reg == IA64_GDB_IP_REGNUM) {
        val = env->ip;
    } else if (reg == IA64_GDB_PSR_REGNUM) {
        val = env->psr;
    } else if (reg == IA64_GDB_CFM_REGNUM) {
        val = ia64_gdb_read_cfm(env);
    } else if (reg >= IA64_GDB_AR0_REGNUM && reg < IA64_GDB_NUM_CORE_REGS) {
        val = ia64_system_read_ar(env, reg - IA64_GDB_AR0_REGNUM);
    } else {
        return 0;
    }

    return gdb_get_reg64(buf, val);
}

int ia64_cpu_gdb_write_register(CPUState *cs, uint8_t *buf, int reg)
{
    IA64CPU *cpu = ia64_cpu_from_cpu_state(cs);
    CPUIA64State *env = &cpu->env;
    uint64_t val = ldq_p(buf);

    if (reg >= IA64_GDB_GR0_REGNUM && reg < IA64_GDB_FR0_REGNUM) {
        if (reg != 0) {
            env->gr[reg] = val;
            ia64_rse_mark_gr_dirty(env, reg);
            ia64_alat_invalidate_reg(env, reg);
        }
    } else if (reg < IA64_GDB_PR0_REGNUM) {
        unsigned freg = reg - IA64_GDB_FR0_REGNUM;

        if (freg >= IA64_FR_ROTATING_BASE) {
            ia64_sync_rotating_fr(env);
        }
        ia64_fpreg_from_spill(env, freg, val, ldq_p(buf + 8));
        ia64_alat_invalidate_fp_reg(env, freg);
        return 16;
    } else if (reg < IA64_GDB_BR0_REGNUM) {
        unsigned predicate = reg - IA64_GDB_PR0_REGNUM;
        uint64_t physical_pr = ia64_gdb_read_pr(env);

        if (predicate != 0) {
            if (val) {
                physical_pr |= 1ULL << predicate;
            } else {
                physical_pr &= ~(1ULL << predicate);
            }
        }
        ia64_gdb_write_pr(env, physical_pr);
    } else if (reg < IA64_GDB_VFP_REGNUM) {
        env->br[reg - IA64_GDB_BR0_REGNUM] = val;
    } else if (reg == IA64_GDB_VFP_REGNUM || reg == IA64_GDB_VRAP_REGNUM) {
        /* Read-only virtual registers. */
    } else if (reg == IA64_GDB_PR_REGNUM) {
        ia64_gdb_write_pr(env, val);
    } else if (reg == IA64_GDB_IP_REGNUM) {
        env->ip = val;
    } else if (reg == IA64_GDB_PSR_REGNUM) {
        ia64_system_mov_psr_write(env, val, 0);
    } else if (reg == IA64_GDB_CFM_REGNUM) {
        ia64_gdb_write_cfm(env, val);
    } else if (reg >= IA64_GDB_AR0_REGNUM && reg < IA64_GDB_NUM_CORE_REGS) {
        unsigned ar = reg - IA64_GDB_AR0_REGNUM;

        if (ar != 17 && (ar != 18 || val != env->ar_bspstore)) {
            /*
             * AR.BSP is derived and read-only.  Reapplying the current
             * BSPSTORE is not inert: it makes RNAT undefined and discards
             * cached backing-store state.  A GDB g/G echo must bypass it.
             */
            ia64_system_write_ar(env, ar, val);
        }
    } else {
        return 0;
    }

    return 8;
}
