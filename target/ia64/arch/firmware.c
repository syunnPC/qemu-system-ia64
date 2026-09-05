/*
 * IA-64 native firmware-assist bridges.
 */

#include "qemu/osdep.h"
#include "arch/arch.h"
#include "arch/system.h"
#include "cpu.h"
#include "decode/decode.h"
#include "decoder.h"
#include "exec-access.h"
#include "fpreg.h"
#include "hw/ia64/ia64_vpc_abi.h"
#include "qemu/atomic.h"

#define FW_BOOT_NONSTANDARD_DIRECT_RR \
    ((1ULL << IA64_RR_RID_SHIFT) | \
     (13ULL << IA64_ITIR_PS_SHIFT) | IA64_RR_VE)
#define FW_BOOT_NONSTANDARD_DIRECT_RR_MASK \
    (IA64_RR_RID_MASK | \
     (IA64_ITIR_PS_MASK << IA64_ITIR_PS_SHIFT) | IA64_RR_VE)

bool ia64_firmware_boot_miss_mapping(IA64CPU *cpu, uint64_t va,
                                     uint64_t *pa, uint8_t *page_shift,
                                     bool *direct)
{
    CPUIA64State *env = &cpu->env;
    uint64_t phys = va & IA64_REGION7_PHYS_MASK;
    uint64_t rr = env->rr[ia64_rr_index(va)];

    if (!ia64_sal_boot_environment_active(env)) {
        return false;
    }

    /*
     * Non-standard loader compatibility:
     *
     * SAL 3.0, sections 3.3.2.1 and 3.3.2.2, specify firmware-assisted
     * identity mappings (VA == PA).  They do not specify this PA + 2 GiB
     * alias.  The compatibility option deliberately provides behavior
     * outside the published loader and platform ABIs.
     *
     * Keep the compatibility behavior narrowly scoped to its exact RR state
     * (region 7, RID 1, 8 KiB pages, VHPT disabled), only while the firmware
     * IVT owns misses, and only for installed low RAM.  The caller installs
     * an ordinary TC entry so architectural replacement and purge rules
     * still apply; this must never become a persistent address-space bypass.
     */
    if (ia64_rr_index(va) == 7 &&
        (cpu->firmware_compat_flags &
         IA64_FW_COMPAT_LOADER_DIRECT_ALIAS) &&
        (rr & FW_BOOT_NONSTANDARD_DIRECT_RR_MASK) ==
            FW_BOOT_NONSTANDARD_DIRECT_RR &&
        !(env->cr_pta & IA64_PTA_VE) &&
        phys >= IA64_FW_LOADER_DIRECT_ALIAS_OFFSET) {
        uint64_t direct_pa = phys - IA64_FW_LOADER_DIRECT_ALIAS_OFFSET;

        if (!cpu->boot_info_valid ||
            direct_pa >= cpu->boot_info.low_ram_size) {
            return false;
        }
        *pa = direct_pa;
        *page_shift = 13;
        *direct = true;
        return true;
    }

    if (!ia64_pa_is_implemented(env, phys)) {
        return false;
    }
    *pa = phys;
    *page_shift = 12;
    *direct = false;
    return true;
}

/* ---- Native EFI Debug Support context bridge -------------------------- */

#define FW_DEBUG_CTX_R1         8U
#define FW_DEBUG_CTX_F2         256U
#define FW_DEBUG_CTX_PR         736U
#define FW_DEBUG_CTX_B0         744U
#define FW_DEBUG_CTX_AR_RSC     808U
#define FW_DEBUG_CTX_AR_BSP     816U
#define FW_DEBUG_CTX_AR_BSPSTORE 824U
#define FW_DEBUG_CTX_AR_RNAT    832U
#define FW_DEBUG_CTX_AR_FCR     840U
#define FW_DEBUG_CTX_AR_EFLAG   848U
#define FW_DEBUG_CTX_AR_CSD     856U
#define FW_DEBUG_CTX_AR_SSD     864U
#define FW_DEBUG_CTX_AR_CFLG    872U
#define FW_DEBUG_CTX_AR_FSR     880U
#define FW_DEBUG_CTX_AR_FIR     888U
#define FW_DEBUG_CTX_AR_FDR     896U
#define FW_DEBUG_CTX_AR_CCV     904U
#define FW_DEBUG_CTX_AR_UNAT    912U
#define FW_DEBUG_CTX_AR_FPSR    920U
#define FW_DEBUG_CTX_AR_PFS     928U
#define FW_DEBUG_CTX_AR_LC      936U
#define FW_DEBUG_CTX_AR_EC      944U
#define FW_DEBUG_CTX_CR_DCR     952U
#define FW_DEBUG_CTX_CR_ITM     960U
#define FW_DEBUG_CTX_CR_IVA     968U
#define FW_DEBUG_CTX_CR_PTA     976U
#define FW_DEBUG_CTX_CR_IPSR    984U
#define FW_DEBUG_CTX_CR_ISR     992U
#define FW_DEBUG_CTX_CR_IIP     1000U
#define FW_DEBUG_CTX_CR_IFA     1008U
#define FW_DEBUG_CTX_CR_ITIR    1016U
#define FW_DEBUG_CTX_CR_IIPA    1024U
#define FW_DEBUG_CTX_CR_IFS     1032U
#define FW_DEBUG_CTX_CR_IIM     1040U
#define FW_DEBUG_CTX_CR_IHA     1048U
#define FW_DEBUG_CTX_DBR0       1056U
#define FW_DEBUG_CTX_IBR0       1120U
#define FW_DEBUG_CTX_INT_NAT    1184U

QEMU_BUILD_BUG_ON(FW_DEBUG_CTX_INT_NAT + sizeof(uint64_t) !=
                  IA64_FW_DEBUG_CONTEXT_SIZE);

static void ia64_fw_debug_putq(CPUIA64State *env, size_t offset,
                               uint64_t value)
{
    stq_le_p(ia64_firmware_debug_state(env)->context + offset, value);
}

static uint64_t ia64_fw_debug_getq(const CPUIA64State *env, size_t offset)
{
    return ldq_le_p(ia64_firmware_debug_state_const(env)->context + offset);
}

static uint64_t ia64_fw_debug_pr(const CPUIA64State *env)
{
    return ia64_system_read_pr(env);
}

static uint64_t ia64_fw_debug_int_nat(const CPUIA64State *env)
{
    uint64_t value = 0;
    unsigned i;

    for (i = 1; i < 32; i++) {
        value |= ((env->nat[i / 64] >> (i % 64)) & 1) << i;
    }
    return value;
}

static uint64_t ia64_fw_debug_current_cfm(const CPUIA64State *env)
{
    return env->cfm_sof
        | ((uint64_t)env->cfm_sol << IA64_CFM_SOL_SHIFT)
        | ((uint64_t)env->cfm_sor << IA64_CFM_SOR_SHIFT)
        | ((uint64_t)env->cfm_rrb_gr << IA64_CFM_RRB_GR_SHIFT)
        | ((uint64_t)env->cfm_rrb_fr << IA64_CFM_RRB_FR_SHIFT)
        | ((uint64_t)env->cfm_rrb_pr << IA64_CFM_RRB_PR_SHIFT);
}

void ia64_firmware_debug_capture(CPUIA64State *env, uint16_t vector,
                                 bool collected)
{
    IA64FirmwareDebugState *debug = ia64_firmware_debug_state(env);
    uint64_t low;
    uint64_t high;
    unsigned i;

    if (debug->handler_active) {
        return;
    }
    debug->context_valid = false;
    debug->rse_valid = false;
    if (!collected || env->cr_iva != IA64_FIRMWARE_IVT_BASE) {
        return;
    }

    memset(debug->context, 0, sizeof(debug->context));
    for (i = 1; i < 32; i++) {
        ia64_fw_debug_putq(env, FW_DEBUG_CTX_R1 + (i - 1) * 8,
                           env->gr[i]);
    }
    for (i = 2; i < 32; i++) {
        ia64_fpreg_to_spill(env, i, &low, &high);
        ia64_fw_debug_putq(env, FW_DEBUG_CTX_F2 + (i - 2) * 16, low);
        ia64_fw_debug_putq(env, FW_DEBUG_CTX_F2 + (i - 2) * 16 + 8,
                           high);
    }
    ia64_fw_debug_putq(env, FW_DEBUG_CTX_PR, ia64_fw_debug_pr(env));
    for (i = 0; i < IA64_BR_COUNT; i++) {
        ia64_fw_debug_putq(env, FW_DEBUG_CTX_B0 + i * 8, env->br[i]);
    }

    ia64_fw_debug_putq(env, FW_DEBUG_CTX_AR_RSC, env->ar_rsc);
    ia64_fw_debug_putq(env, FW_DEBUG_CTX_AR_BSP, env->ar_bsp);
    ia64_fw_debug_putq(env, FW_DEBUG_CTX_AR_BSPSTORE, env->ar_bspstore);
    ia64_fw_debug_putq(env, FW_DEBUG_CTX_AR_RNAT,
                       ia64_rse_read_rnat(env));
    ia64_fw_debug_putq(env, FW_DEBUG_CTX_AR_FCR, env->ar_fcr);
    ia64_fw_debug_putq(env, FW_DEBUG_CTX_AR_EFLAG, env->ar_eflag);
    ia64_fw_debug_putq(env, FW_DEBUG_CTX_AR_CSD, env->ar_csd);
    ia64_fw_debug_putq(env, FW_DEBUG_CTX_AR_SSD, env->ar_ssd);
    ia64_fw_debug_putq(env, FW_DEBUG_CTX_AR_CFLG, env->ar_cflg);
    ia64_fw_debug_putq(env, FW_DEBUG_CTX_AR_FSR, env->ar_fsr);
    ia64_fw_debug_putq(env, FW_DEBUG_CTX_AR_FIR, env->ar_fir);
    ia64_fw_debug_putq(env, FW_DEBUG_CTX_AR_FDR, env->ar_fdr);
    ia64_fw_debug_putq(env, FW_DEBUG_CTX_AR_CCV, env->ar_ccv);
    ia64_fw_debug_putq(env, FW_DEBUG_CTX_AR_UNAT, env->ar_unat);
    ia64_fw_debug_putq(env, FW_DEBUG_CTX_AR_FPSR, env->ar_fpsr);
    ia64_fw_debug_putq(env, FW_DEBUG_CTX_AR_PFS, env->ar_pfs);
    ia64_fw_debug_putq(env, FW_DEBUG_CTX_AR_LC, env->ar_lc);
    ia64_fw_debug_putq(env, FW_DEBUG_CTX_AR_EC, env->ar_ec);

    ia64_fw_debug_putq(env, FW_DEBUG_CTX_CR_DCR, env->cr_dcr);
    ia64_fw_debug_putq(env, FW_DEBUG_CTX_CR_ITM, env->cr_itm);
    ia64_fw_debug_putq(env, FW_DEBUG_CTX_CR_IVA, env->cr_iva);
    ia64_fw_debug_putq(env, FW_DEBUG_CTX_CR_PTA, env->cr_pta);
    ia64_fw_debug_putq(env, FW_DEBUG_CTX_CR_IPSR, env->cr_ipsr);
    ia64_fw_debug_putq(env, FW_DEBUG_CTX_CR_ISR, env->cr_isr);
    ia64_fw_debug_putq(env, FW_DEBUG_CTX_CR_IIP, env->cr_iip);
    ia64_fw_debug_putq(env, FW_DEBUG_CTX_CR_IFA, env->cr_ifa);
    ia64_fw_debug_putq(env, FW_DEBUG_CTX_CR_ITIR, env->cr_itir);
    ia64_fw_debug_putq(env, FW_DEBUG_CTX_CR_IIPA, env->cr_iipa);
    ia64_fw_debug_putq(env, FW_DEBUG_CTX_CR_IFS,
                       IA64_IFS_V | ia64_fw_debug_current_cfm(env));
    ia64_fw_debug_putq(env, FW_DEBUG_CTX_CR_IIM, env->cr_iim);
    ia64_fw_debug_putq(env, FW_DEBUG_CTX_CR_IHA, env->cr_iha);
    for (i = 0; i < 8; i++) {
        ia64_fw_debug_putq(env, FW_DEBUG_CTX_DBR0 + i * 8, env->dbr[i]);
    }
    for (i = 0; i < 8; i++) {
        ia64_fw_debug_putq(env, FW_DEBUG_CTX_IBR0 + i * 8, env->ibr[i]);
    }
    ia64_fw_debug_putq(env, FW_DEBUG_CTX_INT_NAT,
                       ia64_fw_debug_int_nat(env));
    debug->vector = vector;
    debug->context_valid = true;
}

static unsigned ia64_fw_debug_exception_type(uint16_t vector)
{
    if (vector < 0x5000 && (vector & 0x3ff) == 0) {
        return vector >> 10;
    }
    if (vector >= 0x5000 && vector <= 0x6b00 &&
        (vector & 0xff) == 0) {
        return 20 + ((vector - 0x5000) >> 8);
    }
    return 64;
}

static unsigned ia64_fw_debug_cpu_index(CPUIA64State *env)
{
    CPUState *cs = env_cpu(env);
    unsigned index = cs->cpu_index < 0 ? 0 : cs->cpu_index;

    return MIN(index, IA64_VPC_MAX_CPUS - 1);
}

static hwaddr ia64_fw_cpu_assist_base(const CPUIA64State *env)
{
    const IA64CPU *cpu =
        ia64_cpu_from_cpu_state(env_cpu((CPUIA64State *)env));
    uint64_t low_ram_size = cpu->boot_info_valid ?
        cpu->boot_info.low_ram_size : IA64_FW_MIN_LOW_RAM_SIZE;

    return low_ram_size - IA64_FW_BOOT_STACK_SIZE;
}

static hwaddr ia64_fw_debug_context_pa(CPUIA64State *env)
{
    unsigned index = ia64_fw_debug_cpu_index(env);

    return ia64_fw_cpu_assist_base(env) + IA64_FW_DEBUG_CONTEXT_OFFSET +
           (hwaddr)index * IA64_FW_DEBUG_CONTEXT_STRIDE;
}

uint32_t ia64_firmware_debug_enter(CPUIA64State *env, uint64_t address)
{
    IA64FirmwareDebugState *debug = ia64_firmware_debug_state(env);
    uint64_t vector_base = env->cr_iva & ~0x7fffULL;
    uint64_t vector_address = vector_base | debug->vector;
    uint64_t handler = env->gr[IA64_FW_DEBUG_GR_HANDLER];
    unsigned exception_type;

    if (!debug->context_valid || debug->handler_active ||
        vector_base != IA64_FIRMWARE_IVT_BASE ||
        address < vector_address || address >= vector_address + 0x100 ||
        handler < IA64_FW_IDENTITY_BASE ||
        handler >= IA64_FW_IDENTITY_BASE + IA64_FW_IDENTITY_SIZE ||
        (handler & (IA64_BUNDLE_SIZE - 1))) {
        return 0;
    }

    exception_type = ia64_fw_debug_exception_type(debug->vector);
    if (exception_type >= 64) {
        return 0;
    }
    debug->handler_active = true;
    env->gr[IA64_FW_DEBUG_GR_EXCEPTION] = exception_type;
    env->gr[IA64_FW_DEBUG_GR_CONTEXT] = ia64_fw_debug_context_pa(env);
    env->gr[IA64_FW_DEBUG_GR_CPU] = ia64_fw_debug_cpu_index(env);
    env->gr[IA64_GR_STACK_POINTER] = ia64_fw_cpu_assist_base(env) +
                  IA64_FW_DEBUG_STACK_OFFSET +
                  (ia64_fw_debug_cpu_index(env) + 1) *
                  IA64_FW_DEBUG_STACK_SIZE - 16;
    env->nat[0] &= ~((1ULL << IA64_GR_STACK_POINTER) |
                     (1ULL << IA64_FW_DEBUG_GR_EXCEPTION) |
                     (1ULL << IA64_FW_DEBUG_GR_CONTEXT) |
                     (1ULL << IA64_FW_DEBUG_GR_CPU));
    ia64_set_psr(env, env->psr & ~(IA64_PSR_DT | IA64_PSR_RT |
                                   IA64_PSR_IT | IA64_PSR_RI_MASK));
    ia64_tlb_serialize(env, 1, 1);
    env->ip = handler;
    env->exception_state.fault_slot = 0;
    env->instruction_group_start = true;
    return 1;
}

static void ia64_fw_debug_save_rse(CPUIA64State *env)
{
    IA64FirmwareDebugState *debug = ia64_firmware_debug_state(env);

    ia64_rse_save_context(env, &debug->rse);
    debug->rse_valid = true;
}

static void ia64_fw_debug_restore_rse(CPUIA64State *env)
{
    ia64_rse_restore_context(env, &ia64_firmware_debug_state(env)->rse);
}

uint32_t ia64_firmware_debug_save(CPUIA64State *env)
{
    IA64FirmwareDebugState *debug = ia64_firmware_debug_state(env);

    if (!debug->handler_active || !debug->context_valid) {
        return 0;
    }
    ia64_fw_debug_putq(env, FW_DEBUG_CTX_CR_IFS, env->cr_ifs);
    ia64_fw_debug_save_rse(env);
    (void)ia64_exec_physical_rw(
        ia64_fw_debug_context_pa(env),
        debug->context, sizeof(debug->context), true);
    return 1;
}

static void ia64_fw_debug_restore_static_gr(CPUIA64State *env,
                                            uint64_t ipsr,
                                            uint64_t int_nat)
{
    unsigned i;

    for (i = 1; i < 16; i++) {
        env->gr[i] = ia64_fw_debug_getq(env,
                                       FW_DEBUG_CTX_R1 + (i - 1) * 8);
        if (int_nat & (1ULL << i)) {
            env->nat[0] |= 1ULL << i;
        } else {
            env->nat[0] &= ~(1ULL << i);
        }
    }
    for (i = 16; i < 32; i++) {
        uint64_t value = ia64_fw_debug_getq(
            env, FW_DEBUG_CTX_R1 + (i - 1) * 8);
        bool nat = (int_nat >> i) & 1;

        if (ipsr & IA64_PSR_BN) {
            env->banked_gr[i - 16] = value;
            if (nat) {
                env->banked_nat |= 1U << (i - 16);
            } else {
                env->banked_nat &= ~(1U << (i - 16));
            }
        } else {
            env->gr[i] = value;
            if (nat) {
                env->nat[0] |= 1ULL << i;
            } else {
                env->nat[0] &= ~(1ULL << i);
            }
        }
    }
}

uint32_t ia64_firmware_debug_restore(CPUIA64State *env)
{
    IA64FirmwareDebugState *debug = ia64_firmware_debug_state(env);
    uint64_t ipsr;
    uint64_t int_nat;
    uint64_t pr;
    uint64_t original_bsp;
    uint64_t original_bspstore;
    uint64_t restored_bsp;
    uint64_t restored_bspstore;
    unsigned i;

    if (!debug->handler_active || !debug->context_valid ||
        !debug->rse_valid) {
        return 0;
    }
    original_bsp = ia64_fw_debug_getq(env, FW_DEBUG_CTX_AR_BSP);
    original_bspstore = ia64_fw_debug_getq(env,
                                            FW_DEBUG_CTX_AR_BSPSTORE);
    (void)ia64_exec_physical_rw(
        ia64_fw_debug_context_pa(env),
        debug->context, sizeof(debug->context), false);
    ia64_fw_debug_restore_rse(env);
    restored_bsp = ia64_fw_debug_getq(env, FW_DEBUG_CTX_AR_BSP);
    restored_bspstore = ia64_fw_debug_getq(env,
                                            FW_DEBUG_CTX_AR_BSPSTORE);
    env->ar_bsp += restored_bsp - original_bsp;
    env->ar_bspstore += restored_bspstore - original_bspstore;
    /* The handler supplies the NaT collection along with the pointers. */
    env->ar_rnat = ia64_fw_debug_getq(env, FW_DEBUG_CTX_AR_RNAT);
    ia64_rse_rnat_reloaded(env);

    ipsr = ia64_fw_debug_getq(env, FW_DEBUG_CTX_CR_IPSR);
    int_nat = ia64_fw_debug_getq(env, FW_DEBUG_CTX_INT_NAT);
    ia64_fw_debug_restore_static_gr(env, ipsr, int_nat);
    for (i = 2; i < 32; i++) {
        uint64_t low = ia64_fw_debug_getq(
            env, FW_DEBUG_CTX_F2 + (i - 2) * 16);
        uint64_t high = ia64_fw_debug_getq(
            env, FW_DEBUG_CTX_F2 + (i - 2) * 16 + 8);

        ia64_fpreg_from_spill(env, i, low, high);
    }
    pr = ia64_fw_debug_getq(env, FW_DEBUG_CTX_PR) | 1;
    ia64_system_write_pr(env, pr, UINT64_MAX);
    for (i = 0; i < IA64_BR_COUNT; i++) {
        env->br[i] = ia64_fw_debug_getq(env, FW_DEBUG_CTX_B0 + i * 8);
    }

    env->ar_rsc = ia64_fw_debug_getq(env, FW_DEBUG_CTX_AR_RSC);
    env->ar_fcr = ia64_fw_debug_getq(env, FW_DEBUG_CTX_AR_FCR);
    env->ar_eflag = ia64_fw_debug_getq(env, FW_DEBUG_CTX_AR_EFLAG);
    env->ar_csd = ia64_fw_debug_getq(env, FW_DEBUG_CTX_AR_CSD);
    env->ar_ssd = ia64_fw_debug_getq(env, FW_DEBUG_CTX_AR_SSD);
    env->ar_cflg = ia64_fw_debug_getq(env, FW_DEBUG_CTX_AR_CFLG);
    env->ar_fsr = ia64_fw_debug_getq(env, FW_DEBUG_CTX_AR_FSR);
    env->ar_fir = ia64_fw_debug_getq(env, FW_DEBUG_CTX_AR_FIR);
    env->ar_fdr = ia64_fw_debug_getq(env, FW_DEBUG_CTX_AR_FDR);
    env->ar_ccv = ia64_fw_debug_getq(env, FW_DEBUG_CTX_AR_CCV);
    env->ar_unat = ia64_fw_debug_getq(env, FW_DEBUG_CTX_AR_UNAT);
    env->ar_fpsr = ia64_fw_debug_getq(env, FW_DEBUG_CTX_AR_FPSR);
    env->ar_pfs = ia64_fw_debug_getq(env, FW_DEBUG_CTX_AR_PFS);
    env->ar_lc = ia64_fw_debug_getq(env, FW_DEBUG_CTX_AR_LC);
    env->ar_ec = ia64_fw_debug_getq(env, FW_DEBUG_CTX_AR_EC);

    env->cr_dcr = ia64_fw_debug_getq(env, FW_DEBUG_CTX_CR_DCR);
    ia64_write_cr(env, IA64_CR_ITM,
                  ia64_fw_debug_getq(env, FW_DEBUG_CTX_CR_ITM));
    ia64_write_cr(env, IA64_CR_IVA,
                  ia64_fw_debug_getq(env, FW_DEBUG_CTX_CR_IVA));
    ia64_write_cr(env, IA64_CR_PTA,
                  ia64_fw_debug_getq(env, FW_DEBUG_CTX_CR_PTA));
    env->cr_ipsr = ipsr;
    env->cr_isr = ia64_fw_debug_getq(env, FW_DEBUG_CTX_CR_ISR);
    env->cr_iip = ia64_fw_debug_getq(env, FW_DEBUG_CTX_CR_IIP);
    env->cr_ifa = ia64_fw_debug_getq(env, FW_DEBUG_CTX_CR_IFA);
    env->cr_itir = ia64_fw_debug_getq(env, FW_DEBUG_CTX_CR_ITIR);
    env->cr_iipa = ia64_fw_debug_getq(env, FW_DEBUG_CTX_CR_IIPA);
    env->cr_ifs = ia64_fw_debug_getq(env, FW_DEBUG_CTX_CR_IFS);
    env->cr_iim = ia64_fw_debug_getq(env, FW_DEBUG_CTX_CR_IIM);
    env->cr_iha = ia64_fw_debug_getq(env, FW_DEBUG_CTX_CR_IHA);
    for (i = 0; i < 8; i++) {
        env->dbr[i] = ia64_fw_debug_getq(env, FW_DEBUG_CTX_DBR0 + i * 8);
    }
    for (i = 0; i < 8; i++) {
        env->ibr[i] = ia64_fw_debug_getq(env, FW_DEBUG_CTX_IBR0 + i * 8);
    }

    ia64_firmware_debug_state(env)->handler_active = false;
    ia64_firmware_debug_state(env)->context_valid = false;
    ia64_firmware_debug_state(env)->rse_valid = false;
    ia64_rfi(env, env->ip, 0);
    return 1;
}

static bool ia64_opcode_has_firmware_unaligned_load_assist(Ia64Opcode opcode)
{
    switch (opcode) {
    case IA64_OP_LD1:
    case IA64_OP_LD2:
    case IA64_OP_LD4:
    case IA64_OP_LD8:
    case IA64_OP_LD1S:
    case IA64_OP_LD2S:
    case IA64_OP_LD4S:
    case IA64_OP_LD8S:
    case IA64_OP_LD1A:
    case IA64_OP_LD2A:
    case IA64_OP_LD4A:
    case IA64_OP_LD8A:
    case IA64_OP_LD1SA:
    case IA64_OP_LD2SA:
    case IA64_OP_LD4SA:
    case IA64_OP_LD8SA:
    case IA64_OP_LD8FILL:
    case IA64_OP_LD1C_CLR:
    case IA64_OP_LD2C_CLR:
    case IA64_OP_LD4C_CLR:
    case IA64_OP_LD8C_CLR:
    case IA64_OP_LD1C_NC:
    case IA64_OP_LD2C_NC:
    case IA64_OP_LD4C_NC:
    case IA64_OP_LD8C_NC:
        return true;
    default:
        return false;
    }
}

static bool ia64_opcode_has_firmware_unaligned_store_assist(Ia64Opcode opcode)
{
    switch (opcode) {
    case IA64_OP_ST1:
    case IA64_OP_ST2:
    case IA64_OP_ST4:
    case IA64_OP_ST8:
    case IA64_OP_ST1REL:
    case IA64_OP_ST2REL:
    case IA64_OP_ST4REL:
    case IA64_OP_ST8REL:
    case IA64_OP_ST8SPILL:
        return true;
    default:
        return false;
    }
}

static bool ia64_opcode_is_control_speculative_load(Ia64Opcode opcode)
{
    switch (opcode) {
    case IA64_OP_LD1S:
    case IA64_OP_LD2S:
    case IA64_OP_LD4S:
    case IA64_OP_LD8S:
    case IA64_OP_LD1SA:
    case IA64_OP_LD2SA:
    case IA64_OP_LD4SA:
    case IA64_OP_LD8SA:
        return true;
    default:
        return false;
    }
}

static bool ia64_opcode_is_data_speculative_load(Ia64Opcode opcode)
{
    switch (opcode) {
    case IA64_OP_LD1A:
    case IA64_OP_LD2A:
    case IA64_OP_LD4A:
    case IA64_OP_LD8A:
    case IA64_OP_LD1SA:
    case IA64_OP_LD2SA:
    case IA64_OP_LD4SA:
    case IA64_OP_LD8SA:
        return true;
    default:
        return false;
    }
}

static bool ia64_opcode_is_check_load_clear(Ia64Opcode opcode)
{
    switch (opcode) {
    case IA64_OP_LD1C_CLR:
    case IA64_OP_LD2C_CLR:
    case IA64_OP_LD4C_CLR:
    case IA64_OP_LD8C_CLR:
        return true;
    default:
        return false;
    }
}

static bool ia64_opcode_is_check_load_no_clear(Ia64Opcode opcode)
{
    switch (opcode) {
    case IA64_OP_LD1C_NC:
    case IA64_OP_LD2C_NC:
    case IA64_OP_LD4C_NC:
    case IA64_OP_LD8C_NC:
        return true;
    default:
        return false;
    }
}

static bool ia64_opcode_is_check_load(Ia64Opcode opcode)
{
    return ia64_opcode_is_check_load_clear(opcode) ||
           ia64_opcode_is_check_load_no_clear(opcode);
}

static bool ia64_opcode_is_fill_load(Ia64Opcode opcode)
{
    switch (opcode) {
    case IA64_OP_LD8FILL:
        return true;
    default:
        return false;
    }
}

static bool ia64_debug_read_code(CPUState *cs, uint64_t addr, void *buf,
                                 size_t size)
{
    return ia64_exec_debug_read(cs, addr, buf, size);
}

static bool ia64_firmware_data_access(CPUIA64State *env, uint64_t addr,
                                      void *buf, size_t size, bool is_write,
                                      bool ordered)
{
    uint64_t pa;

    if (!ia64_translate_data_access(env, addr, is_write, &pa)) {
        return false;
    }
    /*
     * The translated access was abandoned when its alignment helper raised
     * the fault, before qemu_ld/st could emit its ordinary memory-ordering
     * barrier.  Fully order this rare firmware-emulated reference so that UC
     * and UCE mappings remain sequential on weakly ordered hosts as well as
     * TSO hosts.  This also supplies the pre-store release ordering for an
     * ordered store; an acquire takes effect only after a load has completed
     * successfully.
     */
    smp_mb(); /* Order every assisted reference against prior accesses. */
    if (!ia64_exec_physical_rw(pa, buf, size, is_write)) {
        return false;
    }
    if (ordered && !is_write) {
        smp_mb_acquire();
    }
    return true;
}

static void ia64_resume_after_instruction(CPUIA64State *env, uint64_t ip,
                                          uint8_t slot)
{
    env->psr &= ~IA64_PSR_RI_MASK;
    if (slot >= 2) {
        env->ip = ip + 16;
    } else {
        env->ip = ip;
        env->psr |= (uint64_t)(slot + 1) << IA64_PSR_RI_SHIFT;
    }
}

static void ia64_firmware_complete_instruction(CPUIA64State *env,
                                               uint64_t source_ip,
                                               uint8_t source_slot,
                                               bool group_start)
{
    uint8_t target_slot =
        (env->psr & IA64_PSR_RI_MASK) >> IA64_PSR_RI_SHIFT;

    /*
     * The translated instruction exited on its Unaligned fault, so none of
     * the ordinary successful-retirement bookkeeping was emitted.  Once the
     * firmware assist has completed it, perform that bookkeeping before any
     * completion trap exactly as the translated path would.
     */
    if (env->psr & IA64_PSR_IC) {
        env->last_successful_bundle = ia64_ip_bundle_addr(source_ip);
    }
    ia64_system_clear_psr_fault_suppression(env);
    env->instruction_group_start = group_start;
    env->exception_state.exception = IA64_EXCP_NONE;
    ia64_check_native_traps(
        env, env->ip, source_ip,
        IA64_NATIVE_TRAP_SLOTS(target_slot, source_slot), 0, 0, env->psr);
}

static void ia64_gr_nat_clear_runtime(CPUIA64State *env, uint8_t reg)
{
    if (reg == IA64_GR_ZERO) {
        return;
    }

    env->nat[reg / 64] &= ~(1ULL << (reg % 64));
    ia64_rse_mark_gr_dirty(env, reg);
}

static bool ia64_gr_nat_get_runtime(CPUIA64State *env, uint8_t reg)
{
    if (reg == IA64_GR_ZERO) {
        return false;
    }

    return (env->nat[reg / 64] >> (reg % 64)) & 1;
}

static void ia64_gr_nat_set_runtime(CPUIA64State *env, uint8_t reg, bool nat)
{
    if (reg == IA64_GR_ZERO) {
        return;
    }

    if (nat) {
        env->nat[reg / 64] |= 1ULL << (reg % 64);
    } else {
        env->nat[reg / 64] &= ~(1ULL << (reg % 64));
    }
    ia64_rse_mark_gr_dirty(env, reg);
}

typedef struct Ia64UnalignedBaseUpdate {
    uint64_t increment;
    bool base_nat;
    bool increment_nat;
} Ia64UnalignedBaseUpdate;

static Ia64UnalignedBaseUpdate
ia64_unaligned_base_update_snapshot(CPUIA64State *env,
                                    const Ia64Instruction *insn)
{
    Ia64UnalignedBaseUpdate update = { 0 };

    if (insn->reg_base_update) {
        update.increment = env->gr[insn->operands.common.source1];
        update.base_nat = ia64_gr_nat_get_runtime(
            env, insn->operands.common.source2);
        update.increment_nat = ia64_gr_nat_get_runtime(
            env, insn->operands.common.source1);
    }
    return update;
}

static void ia64_unaligned_base_update(
    CPUIA64State *env, const Ia64Instruction *insn, uint64_t addr,
    const Ia64UnalignedBaseUpdate *update)
{
    if (insn->operands.common.source2 == IA64_GR_ZERO) {
        return;
    }

    if (insn->reg_base_update) {
        env->gr[insn->operands.common.source2] =
            addr + update->increment;
        ia64_gr_nat_set_runtime(env, insn->operands.common.source2,
                                update->base_nat || update->increment_nat);
    } else if (insn->imm_base_update) {
        env->gr[insn->operands.common.source2] =
            addr + insn->operands.common.immediate;
        ia64_rse_mark_gr_dirty(env, insn->operands.common.source2);
    }
}

static void ia64_firmware_defer_speculative_load(CPUIA64State *env,
                                                 const Ia64Instruction *insn)
{
    if (insn->operands.common.destination != IA64_GR_ZERO) {
        env->gr[insn->operands.common.destination] = 0;
        ia64_gr_nat_set_runtime(env, insn->operands.common.destination, true);
    }
    if (env->alat_state.alat_full &&
        ia64_opcode_is_data_speculative_load(insn->opcode)) {
        ia64_alat_invalidate_reg(env, insn->operands.common.destination);
    }
}

/*
 * The SAL/firmware unaligned assist emulates a misaligned reference with a
 * single translated access, so it can only service references that stay within
 * one translation.  A reference is serviceable when its first and last bytes
 * map to contiguous physical addresses: this correctly admits large pages,
 * where an access may cross a 4KB sub-boundary yet remain inside one page,
 * while a reference whose bytes fall in different (or unmapped) translations
 * remains an architectural fault.  Using the actual translation instead of a
 * hardcoded 4KB span is required for OS loaders, which map their working set
 * with megabyte-sized translation registers.
 */
static bool ia64_firmware_unaligned_spans_translation(CPUIA64State *env,
                                                      uint64_t addr,
                                                      uint32_t size)
{
    uint64_t pa_first;
    uint64_t pa_last;

    if (size <= 1) {
        return false;
    }
    if (!ia64_data_address_to_phys(env, addr, &pa_first) ||
        !ia64_data_address_to_phys(env, addr + size - 1, &pa_last)) {
        return true;
    }
    return pa_last != pa_first + (size - 1);
}

bool ia64_try_emulate_firmware_unaligned(CPUState *cs,
                                         uint64_t fault_addr,
                                         uint8_t fault_slot)
{
    IA64CPU *cpu = ia64_cpu_from_cpu_state(cs);
    CPUIA64State *env = &cpu->env;
    uint8_t bundle[16];
    uint8_t data[16];
    uint64_t low, high;
    uint8_t template_code;
    const IA64TemplateInfo *template_info;
    Ia64Instruction insn;
    uint64_t slots[3];
    uint64_t addr;
    MemOp memop;
    uint32_t size;

    /*
     * Model the SAL/firmware IVT's unaligned assist only before the guest has
     * installed its own IVA.  Page-spanning and semaphore references remain
     * architectural faults.
     */
    if (env->cr_iva != IA64_FIRMWARE_IVT_BASE ||
        !(env->psr & IA64_PSR_IC) ||
        fault_slot > 2) {
        return false;
    }

    if (!ia64_debug_read_code(cs, env->exception_state.fault_ip,
                              bundle, sizeof(bundle))) {
        return false;
    }

    low = ldq_le_p(bundle);
    high = ldq_le_p(bundle + 8);
    template_code = ia64_bundle_template_code(low);
    template_info = ia64_template_info(template_code);
    if (!template_info->defined) {
        return false;
    }

    slots[0] = ia64_bundle_slot(low, high, 0);
    slots[1] = ia64_bundle_slot(low, high, 1);
    slots[2] = ia64_bundle_slot(low, high, 2);
    insn = ia64_decode_insn(template_info->units[fault_slot],
                            slots[fault_slot],
                            env->exception_state.fault_ip, fault_slot);
    if (!insn.valid) {
        return false;
    }

    if (ia64_opcode_has_firmware_unaligned_load_assist(insn.opcode)) {
        bool check_load_clear = ia64_opcode_is_check_load_clear(insn.opcode);
        bool check_load_no_clear =
            ia64_opcode_is_check_load_no_clear(insn.opcode);
        Ia64UnalignedBaseUpdate base_update =
            ia64_unaligned_base_update_snapshot(env, &insn);
        uint64_t alat_generation = 0;

        memop = ia64_runtime_data_memop(
            env, ia64_memop_for_opcode(insn.opcode));
        size = ia64_memop_size(memop);
        addr = env->gr[insn.operands.common.source2];
        if (addr != fault_addr ||
            ia64_firmware_unaligned_spans_translation(env, addr, size)) {
            return false;
        }

        if (ia64_opcode_is_control_speculative_load(insn.opcode) &&
            (env->cr_isr & IA64_ISR_SP) &&
            (env->cr_isr & IA64_ISR_ED)) {
            ia64_firmware_defer_speculative_load(env, &insn);
            ia64_unaligned_base_update(env, &insn, addr, &base_update);
            ia64_resume_after_instruction(
                env, env->exception_state.fault_ip, fault_slot);
            ia64_firmware_complete_instruction(
                env, env->exception_state.fault_ip, fault_slot,
                template_info->stop_after[fault_slot]);
            return true;
        }

        /*
         * IA-64 SDM Vol. 2, 17.3.1 requires unaligned handlers to force
         * failed data-speculative loads; ALAT cannot track all misalignment
         * sizes for later store-conflict checks.
         */
        if (ia64_opcode_is_data_speculative_load(insn.opcode)) {
            ia64_alat_invalidate_reg(env,
                                     insn.operands.common.destination);
            ia64_unaligned_base_update(env, &insn, addr, &base_update);
            ia64_resume_after_instruction(
                env, env->exception_state.fault_ip, fault_slot);
            ia64_firmware_complete_instruction(
                env, env->exception_state.fault_ip, fault_slot,
                template_info->stop_after[fault_slot]);
            return true;
        }

        if (env->alat_state.alat_full &&
            ia64_opcode_is_check_load(insn.opcode) &&
            ia64_alat_check_load_addr(env,
                                      insn.operands.common.destination,
                                      addr, size, check_load_clear)) {
            /* ld.c.clr.acq orders the ALAT lookup even on an ALAT hit. */
            if (insn.mem_acquire) {
                smp_mb_acquire();
            }
            ia64_unaligned_base_update(env, &insn, addr, &base_update);
            ia64_resume_after_instruction(
                env, env->exception_state.fault_ip, fault_slot);
            ia64_firmware_complete_instruction(
                env, env->exception_state.fault_ip, fault_slot,
                template_info->stop_after[fault_slot]);
            return true;
        }

        if (check_load_no_clear && env->alat_state.alat_full) {
            alat_generation = ia64_alat_load_begin(env);
        }
        if (size > sizeof(data) ||
            !ia64_firmware_data_access(env, addr, data, size, false,
                                       insn.mem_acquire)) {
            return false;
        }
        if (insn.operands.common.destination != IA64_GR_ZERO) {
            env->gr[insn.operands.common.destination] = ldm_p(data, memop);
            if (ia64_opcode_is_fill_load(insn.opcode)) {
                uint64_t nat = (env->ar_unat >> ((addr >> 3) & 0x3f)) & 1;

                if (nat) {
                    ia64_gr_nat_set_runtime(
                        env, insn.operands.common.destination, true);
                } else {
                    ia64_gr_nat_clear_runtime(
                        env, insn.operands.common.destination);
                }
            } else {
                ia64_gr_nat_clear_runtime(
                    env, insn.operands.common.destination);
                if (check_load_no_clear && env->alat_state.alat_full &&
                    ia64_exec_advanced_load_allowed(
                        env, addr, ia64_exec_mmu_index(env, false))) {
                    ia64_alat_set(env, insn.operands.common.destination,
                                  addr, size, alat_generation);
                }
            }
        }
        ia64_unaligned_base_update(env, &insn, addr, &base_update);
        ia64_resume_after_instruction(env, env->exception_state.fault_ip,
                                      fault_slot);
        ia64_firmware_complete_instruction(
            env, env->exception_state.fault_ip, fault_slot,
            template_info->stop_after[fault_slot]);
        return true;
    }

    if (ia64_opcode_has_firmware_unaligned_store_assist(insn.opcode)) {
        Ia64UnalignedBaseUpdate base_update =
            ia64_unaligned_base_update_snapshot(env, &insn);
        uint64_t value = env->gr[insn.operands.common.source1];

        memop = ia64_runtime_data_memop(
            env, ia64_memop_for_opcode(insn.opcode));
        size = ia64_memop_size(memop);
        addr = env->gr[insn.operands.common.source2];
        if (addr != fault_addr ||
            ia64_firmware_unaligned_spans_translation(env, addr, size) ||
            size > sizeof(data)) {
            return false;
        }

        if (insn.opcode == IA64_OP_ST8SPILL &&
            ia64_gr_nat_get(env, insn.operands.common.source1)) {
            value = 0;
        }
        stm_p(data, memop, value);
        if (!ia64_firmware_data_access(env, addr, data, size, true,
                                       insn.mem_release)) {
            return false;
        }
        if (env->alat_state.alat_full) {
            ia64_invalidate_alat_store(env, addr, size);
        }
        if (insn.opcode == IA64_OP_ST8SPILL) {
            ia64_system_st_spill_unat(env,
                                      insn.operands.common.source1, addr);
        }
        ia64_unaligned_base_update(env, &insn, addr, &base_update);
        ia64_resume_after_instruction(env, env->exception_state.fault_ip,
                                      fault_slot);
        ia64_firmware_complete_instruction(
            env, env->exception_state.fault_ip, fault_slot,
            template_info->stop_after[fault_slot]);
        return true;
    }

    return false;
}
