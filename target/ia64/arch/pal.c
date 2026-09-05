/*
 * IA-64 Processor Abstraction Layer services.
 *
 * PAL calls update architected CPU state and return through the firmware
 * portal.  The helper ABI supplies only the translated-code return address.
 */

#include "qemu/osdep.h"
#include "qemu/units.h"
#include "cpu.h"
#include "arch/arch.h"
#include "exec-access.h"
#include "exec/cpu-common.h"
#include "exec/cputlb.h"
#include "trace.h"

/*
 * PAL function indices (Intel IA-64 PAL specification).
 * GR28 holds the function index on entry.
 * Results are returned in GR8 (status), GR9-GR11 (outputs).
 */
#define PAL_CACHE_FLUSH     0x0001
#define PAL_CACHE_INFO      0x0002
#define PAL_CACHE_INIT      0x0003
#define PAL_CACHE_SUMMARY   0x0004
#define PAL_MEM_ATTRIB      0x0005
#define PAL_PTCE_INFO       0x0006
#define PAL_VM_INFO         0x0007
#define PAL_VM_SUMMARY      0x0008
#define PAL_BUS_GET_FEATURES 0x0009
#define PAL_BUS_SET_FEATURES 0x000A
#define PAL_DEBUG_INFO      0x000B
#define PAL_FIXED_ADDR      0x000C
#define PAL_FREQ_BASE       0x000D
#define PAL_FREQ_RATIOS     0x000E
#define PAL_PERF_MON_INFO   0x000F
#define PAL_PLATFORM_ADDR   0x0010
#define PAL_PROC_GET_FEATURES 0x0011
#define PAL_PROC_SET_FEATURES 0x0012
#define PAL_RSE_INFO        0x0013
#define PAL_VERSION         0x0014
#define PAL_MC_CLEAR_LOG    0x0015
#define PAL_MC_DRAIN        0x0016
#define PAL_MC_EXPECTED     0x0017
#define PAL_MC_DYNAMIC_STATE 0x0018
#define PAL_MC_ERROR_INFO   0x0019
#define PAL_MC_RESUME       0x001A
#define PAL_MC_REGISTER_MEM 0x001B
#define PAL_HALT            0x001C
#define PAL_HALT_LIGHT      0x001D
#define PAL_COPY_INFO       0x001E
#define PAL_CACHE_LINE_INIT 0x001F
#define PAL_PMI_ENTRYPOINT  0x0020
#define PAL_VM_PAGE_SIZE    0x0022
#define PAL_MEM_FOR_TEST    0x0025
#define PAL_CACHE_PROT_INFO 0x0026
#define PAL_REGISTER_INFO   0x0027
#define PAL_PREFETCH_VIS    0x0029
#define PAL_LOGICAL_TO_PHYSICAL 0x002A
#define PAL_CACHE_SHARED_INFO 0x002B

#define PAL_COPY_PAL       0x0100
#define PAL_HALT_INFO       0x0101
#define PAL_TEST_PROC       0x0102
#define PAL_VM_TR_READ      0x0105
#define PAL_BRAND_INFO      0x0112

#define PAL_COPY_BUFFER_SIZE  0x1000ULL
#define PAL_COPY_BUFFER_ALIGN 0x1000ULL
#define PAL_COPY_PROC_OFFSET  0
#define PAL_COPY_CODE_SIZE    0x20ULL
#define PAL_COPY_TARGET_CACHE_ATTR (1ULL << 63)
#define PAL_MIN_STATE_SAVE_SIZE 0x1000ULL
#define PAL_SELF_TEST_STATE_TESTED (1ULL << 2)
#define PAL_MEM_ATTR_WB            (1ULL << 0)
#define PAL_MEM_ATTR_VALID_MASK    0xffffULL
#define PAL_PERF_MON_INFO_BASE     0x08120004ULL
#define PAL_BUS_LOCK_MASK          (1ULL << 30)
#define PAL_BUS_REQUEST_PARKING    (1ULL << 29)
#define PAL_BUS_REQUIRED_FEATURES  \
    (PAL_BUS_LOCK_MASK | PAL_BUS_REQUEST_PARKING)

#define PAL_CACHE_FLUSH_OPERATION_MASK 0x3ULL
#define PAL_HALT_STATE_COUNT       8
#define PAL_HALT_STATE_IMPLEMENTED (1ULL << 60)
#define PAL_HALT_STATE_COHERENT    (1ULL << 61)
#define PAL_HALT_IO_TYPE_NONE      0
#define PAL_HALT_IO_TYPE_LOAD      1
#define PAL_HALT_IO_TYPE_STORE     2
#define PAL_HALT_IO_PHYS_ADDR_MASK (~(1ULL << 63))
#define IA64_L0_CACHE_LINE_SIZE    64ULL
#define IA64_L1_CACHE_LINE_SIZE    128ULL
#define IA64_L2_CACHE_LINE_SIZE    128ULL
static bool pal_reserved_args_are_zero(CPUIA64State *env);

static bool pal_physical_range_is_implemented(CPUIA64State *env,
                                              uint64_t address,
                                              uint64_t size)
{
    uint64_t limit =
        1ULL << ia64_env_cpu_class(env)->phys_addr_bits;
    uint64_t pa = ia64_physical_address(address);

    return ia64_pa_is_implemented(env, address) &&
           size <= limit && pa <= limit - size;
}

static uint64_t pal_stacked_arg(CPUIA64State *env, uint32_t arg)
{
    return env->gr[IA64_STACKED_GR_BASE + 1 + arg];
}

#define PAL_STATUS_SUCCESS         0
#define PAL_STATUS_NOT_IMPLEMENTED (-1)
#define PAL_STATUS_INVALID_ARGUMENT (-2)
#define PAL_STATUS_ERROR           (-3)
#define PAL_STATUS_NO_INFORMATION  (-6)
#define PAL_STATUS_BEYOND_MAX      (-8)
#define PAL_STATUS_NEXT_HIGHER     1
#define PAL_PREFETCH_VIS_OK        1

static void pal_get_version(CPUIA64State *env)
{
    if (pal_reserved_args_are_zero(env)) {
        /*
         * QEMU provides one built-in PAL implementation rather than
         * independently replaceable PAL_A and PAL_B images.  The CPU model
         * therefore identifies the documented PAL release used as its
         * compatibility target and reports that value as both the minimum
         * and current version.  This is emulator-specific: there is no
         * separate loaded image from which a different current version
         * could be obtained.
         */
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_SUCCESS;
        env->gr[IA64_PAL_GR_RESULT1] =
            ia64_env_cpu_class(env)->pal_version;
        env->gr[IA64_PAL_GR_RESULT2] = env->gr[IA64_PAL_GR_RESULT1];
    } else {
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_INVALID_ARGUMENT;
        env->gr[IA64_PAL_GR_RESULT1] = 0;
        env->gr[IA64_PAL_GR_RESULT2] = 0;
    }
    env->gr[IA64_PAL_GR_RESULT3] = 0;
}

static void pal_rse_info(CPUIA64State *env)
{
    if (pal_reserved_args_are_zero(env)) {
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_SUCCESS;
        env->gr[IA64_PAL_GR_RESULT1] = 96;
        /*
         * PAL_RSE_INFO.result2 is a two-bit eager-load/eager-store hint
         * bitmap, not a clean-partition size.  The relevant product manuals
         * specify enforced-lazy RSE operation, and the Montecito update does
         * not replace that inherited RSE mode.
         * Therefore all three models advertise only the required lazy mode
         * as 0.  Whether the implementation maintains a clean partition is
         * separate model state and must not leak into these reserved bits.
         */
        env->gr[IA64_PAL_GR_RESULT2] = 0;
    } else {
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_INVALID_ARGUMENT;
        env->gr[IA64_PAL_GR_RESULT1] = 0;
        env->gr[IA64_PAL_GR_RESULT2] = 0;
    }
    env->gr[IA64_PAL_GR_RESULT3] = 0;
}

static void pal_vm_summary(CPUIA64State *env)
{
    IA64CPUClass *icc = ia64_env_cpu_class(env);

    g_assert(icc->itr_count > 0 && icc->itr_count <= IA64_TR_MAX);
    g_assert(icc->dtr_count > 0 && icc->dtr_count <= IA64_TR_MAX);
    if (pal_reserved_args_are_zero(env)) {
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_SUCCESS;
        env->gr[IA64_PAL_GR_RESULT1] = 1ULL |
                     ((uint64_t)icc->phys_addr_bits << 1) |
                     ((uint64_t)icc->key_bits << 8) |
                     (((uint64_t)IA64_PKR_COUNT - 1ULL) << 16) |
                     ((uint64_t)icc->hash_tag_id << 24) |
                     ((icc->dtr_count - 1ULL) << 32) |
                     ((icc->itr_count - 1ULL) << 40) |
                     ((uint64_t)icc->unique_tcs << 48) |
                     ((uint64_t)icc->tc_levels << 56);
        env->gr[IA64_PAL_GR_RESULT2] = icc->impl_va_msb |
                      ((uint64_t)icc->rid_bits << 8);
    } else {
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_INVALID_ARGUMENT;
        env->gr[IA64_PAL_GR_RESULT1] = 0;
        env->gr[IA64_PAL_GR_RESULT2] = 0;
    }
    env->gr[IA64_PAL_GR_RESULT3] = 0;
}

static bool pal_halt_light(CPUIA64State *env)
{
    CPUState *cs = env_cpu(env);

    if (!pal_reserved_args_are_zero(env)) {
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_INVALID_ARGUMENT;
        env->gr[IA64_PAL_GR_RESULT1] = 0;
        env->gr[IA64_PAL_GR_RESULT2] = 0;
        env->gr[IA64_PAL_GR_RESULT3] = 0;
        return false;
    }

    env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_SUCCESS;
    env->gr[IA64_PAL_GR_RESULT1] = 0;
    env->gr[IA64_PAL_GR_RESULT2] = 0;
    env->gr[IA64_PAL_GR_RESULT3] = 0;
    /*
     * PAL defines an unmasked external interrupt using TPR.mic/TPR.mmi,
     * independently of PSR.i.  Static PAL call wrappers mask PSR.i while
     * entering physical PAL and restore the caller state after this call
     * resumes, so remember that this halt has PAL wake semantics.
     */
    env->interrupt.pal_halt_wake = true;
    ia64_itc_enter_halt(env);
    /*
     * Leave cpu_exec only after the PAL break instruction has completed and
     * the translator has committed its continuation IP.  Setting halted here
     * merely ended the current translation block; cpu_exec could then run on
     * with a stale halted flag and stop later at an unrelated instruction.
     */
    cpu_set_interrupt(cs, CPU_INTERRUPT_HALT);
    return true;
}

static bool pal_halt_valid_io_size(uint64_t io_size)
{
    return io_size == 1 || io_size == 2 || io_size == 4 || io_size == 8;
}

static bool pal_halt_io_transaction(uint64_t io_detail_ptr,
                                    CPUIA64State *env,
                                    uint64_t *load_return)
{
    uint64_t info;
    uint64_t io_type;
    uint64_t io_size;
    uint64_t addr;
    uint64_t data;
    uint64_t phys_addr;

    *load_return = 0;
    if (io_detail_ptr == 0) {
        return true;
    }
    if ((io_detail_ptr & 7) != 0 ||
        !pal_physical_range_is_implemented(env, io_detail_ptr, 24)) {
        return false;
    }
    io_detail_ptr = ia64_physical_address(io_detail_ptr);

    (void)ia64_exec_physical_rw(io_detail_ptr, &info, sizeof(info), false);
    info = le64_to_cpu(info);
    if ((info & ~0xffffULL) != 0) {
        return false;
    }
    io_type = info & 0xff;
    io_size = (info >> 8) & 0xff;
    if (io_type == PAL_HALT_IO_TYPE_NONE) {
        return io_size == 0;
    }
    if ((io_type != PAL_HALT_IO_TYPE_LOAD &&
         io_type != PAL_HALT_IO_TYPE_STORE) ||
        !pal_halt_valid_io_size(io_size)) {
        return false;
    }

    (void)ia64_exec_physical_rw(io_detail_ptr + 8, &addr, sizeof(addr),
                                false);
    addr = le64_to_cpu(addr);
    phys_addr = addr & PAL_HALT_IO_PHYS_ADDR_MASK;
    if ((phys_addr & (io_size - 1)) != 0 ||
        !pal_physical_range_is_implemented(env, addr, io_size)) {
        return false;
    }

    if (io_type == PAL_HALT_IO_TYPE_LOAD) {
        uint8_t buf[8] = { 0 };
        uint64_t value = 0;
        int i;

        (void)ia64_exec_physical_rw(phys_addr, buf, io_size, false);
        for (i = 0; i < io_size; i++) {
            value |= (uint64_t)buf[i] << (i * 8);
        }
        *load_return = value;
    } else {
        uint8_t buf[8];
        uint64_t store_value;
        int i;

        (void)ia64_exec_physical_rw(io_detail_ptr + 16, &data,
                                    sizeof(data), false);
        store_value = le64_to_cpu(data);
        for (i = 0; i < io_size; i++) {
            buf[i] = store_value >> (i * 8);
        }
        (void)ia64_exec_physical_rw(phys_addr, buf, io_size, true);
    }
    return true;
}

static bool pal_halt(CPUIA64State *env)
{
    CPUState *cs = env_cpu(env);
    uint64_t halt_state = env->gr[IA64_PAL_GR_ARG1];
    uint64_t io_detail_ptr = env->gr[IA64_PAL_GR_ARG2];
    uint64_t load_return = 0;

    if (halt_state != 1 || env->gr[IA64_PAL_GR_ARG3] != 0 ||
        !pal_halt_io_transaction(io_detail_ptr, env, &load_return)) {
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_INVALID_ARGUMENT;
        env->gr[IA64_PAL_GR_RESULT1] = 0;
        env->gr[IA64_PAL_GR_RESULT2] = 0;
        env->gr[IA64_PAL_GR_RESULT3] = 0;
        return false;
    }

    env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_SUCCESS;
    env->gr[IA64_PAL_GR_RESULT1] = load_return;
    env->gr[IA64_PAL_GR_RESULT2] = 0;
    env->gr[IA64_PAL_GR_RESULT3] = 0;
    env->interrupt.pal_halt_wake = true;
    ia64_itc_enter_halt(env);
    cpu_set_interrupt(cs, CPU_INTERRUPT_HALT);
    return true;
}

static void pal_prefetch_vis(CPUIA64State *env)
{
    IA64CPUClass *icc = ia64_env_cpu_class(env);
    uint64_t trans_type = env->gr[IA64_PAL_GR_ARG1];
    bool valid_trans_type;

    /*
     * The first-generation PAL interface reserves argument 1 and requires
     * zero.  The later interface names it trans_type and also accepts one
     * for a physical-mode transition.
     */
    valid_trans_type = trans_type == 0 ||
        (icc->model != IA64_CPU_MODEL_MERCED && trans_type == 1);

    if (valid_trans_type &&
        env->gr[IA64_PAL_GR_ARG2] == 0 &&
        env->gr[IA64_PAL_GR_ARG3] == 0) {
        /*
         * TCG has no outstanding hardware prefetches.  Therefore the
         * operation is complete locally and need not be repeated on remote
         * processors; PAL specifies that condition as positive status 1.
         * All three result registers are reserved for this procedure.
         */
        env->gr[IA64_PAL_GR_STATUS] = PAL_PREFETCH_VIS_OK;
    } else {
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_INVALID_ARGUMENT;
    }
    env->gr[IA64_PAL_GR_RESULT1] = 0;
    env->gr[IA64_PAL_GR_RESULT2] = 0;
    env->gr[IA64_PAL_GR_RESULT3] = 0;
}

static bool pal_cache_flush(CPUIA64State *env)
{
    uint64_t cache_type = env->gr[IA64_PAL_GR_ARG1];
    uint64_t operation = env->gr[IA64_PAL_GR_ARG2];
    bool serialize_execution = false;

    if (cache_type < 1 || cache_type > 4 ||
        (operation & ~PAL_CACHE_FLUSH_OPERATION_MASK) != 0) {
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_INVALID_ARGUMENT;
    } else {
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_SUCCESS;
        /*
         * TCG has no data-cache lines or dirty writeback state.  Its
         * instruction view is already coherent: CPU stores and address-space
         * writes invalidate translated code by physical RAM range, while
         * fc.i performs the same precise invalidation explicitly.  Thus all
         * effects requested here are complete without discarding unrelated
         * translated code.
         */
        serialize_execution = cache_type == 1 || cache_type == 3 ||
                              cache_type == 4;
    }
    env->gr[IA64_PAL_GR_RESULT1] = 0;
    env->gr[IA64_PAL_GR_RESULT2] = 0;
    env->gr[IA64_PAL_GR_RESULT3] = 0;
    return serialize_execution;
}

static void pal_cache_init(CPUIA64State *env)
{
    uint64_t level = env->gr[IA64_PAL_GR_ARG1];
    uint64_t cache_type = env->gr[IA64_PAL_GR_ARG2];
    uint64_t restrict_side_effects = env->gr[IA64_PAL_GR_ARG3];

    if (level != UINT64_MAX &&
        (level >= 3 || cache_type < 1 || cache_type > 3 ||
         restrict_side_effects > 1)) {
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_INVALID_ARGUMENT;
    } else {
        /*
         * Cache contents are not target state in TCG, so there are no tags
         * or data arrays to initialize and no cross-level side effects to
         * reject.  A valid request therefore completes immediately.
         */
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_SUCCESS;
    }
    env->gr[IA64_PAL_GR_RESULT1] = 0;
    env->gr[IA64_PAL_GR_RESULT2] = 0;
    env->gr[IA64_PAL_GR_RESULT3] = 0;
}

static void pal_mem_attrib(CPUIA64State *env)
{
    if (pal_reserved_args_are_zero(env)) {
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_SUCCESS;
        env->gr[IA64_PAL_GR_RESULT1] =
            ia64_env_cpu_class(env)->memory_attribute_mask;
    } else {
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_INVALID_ARGUMENT;
        env->gr[IA64_PAL_GR_RESULT1] = 0;
    }
    env->gr[IA64_PAL_GR_RESULT2] = 0;
    env->gr[IA64_PAL_GR_RESULT3] = 0;
}

static void pal_vm_page_size(CPUIA64State *env)
{
    if (pal_reserved_args_are_zero(env)) {
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_SUCCESS;
        env->gr[IA64_PAL_GR_RESULT1] = ia64_cpu_page_size_mask(env);
        env->gr[IA64_PAL_GR_RESULT2] =
            ia64_cpu_purge_page_size_mask(env);
    } else {
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_INVALID_ARGUMENT;
        env->gr[IA64_PAL_GR_RESULT1] = 0;
        env->gr[IA64_PAL_GR_RESULT2] = 0;
    }
    env->gr[IA64_PAL_GR_RESULT3] = 0;
}

typedef struct IA64PalCacheInfo {
    bool unified;
    uint8_t attribute;
    uint8_t associativity;
    uint8_t line_shift;
    uint8_t stride_shift;
    uint8_t store_latency;
    uint8_t load_latency;
    uint8_t store_hints;
    uint8_t load_hints;
    uint8_t alias_boundary;
    uint8_t tag_lsb;
    uint8_t tag_msb;
    uint32_t size;
} IA64PalCacheInfo;

static bool pal_cache_info_for_model(CPUIA64State *env, uint64_t level,
                                     uint64_t type, IA64PalCacheInfo *info)
{
    IA64CPUClass *icc = ia64_env_cpu_class(env);
    bool montecito = icc->is_montecito;

    if (type < 1 || type > 2 || level >= 3) {
        return false;
    }

    *info = (IA64PalCacheInfo) {
        /*
         * PAL reports the minimum alias separation, not the cache-line or
         * flush-stride size.  The modeled processors use physically indexed,
         * physically tagged caches and support 4 KiB as their minimum page,
         * so distinct virtual aliases already meet the 2^12-byte boundary.
         */
        .alias_boundary = 12,
        .tag_msb = ia64_env_cpu_class(env)->phys_addr_bits - 1,
    };

    if (icc->model == IA64_CPU_MODEL_MERCED) {
        if (type == 2) {
            /*
             * The first-generation product manual specifies t1/nt1/nt2/nta
             * locality support.  PAL_CACHE_INFO describes only load and store
             * instructions: stores implement t1/nta and loads implement
             * t1/nt1/nta (nt2 is an lfetch hint).  TCG does not model cache
             * allocation or replacement state, but these hints are explicitly
             * non-functional, so reporting the documented capability remains
             * architecturally observable even though it cannot affect timing.
             */
            info->store_hints = (1 << 0) | (1 << 3);
            info->load_hints = (1 << 0) | (1 << 1) | (1 << 3);
        }
        switch (level) {
        case 0:
            info->attribute = 0;
            info->associativity = 4;
            info->line_shift = 5;
            info->stride_shift = 5;
            info->store_latency = type == 1 ? 0xff : 1;
            info->load_latency = type == 1 ? 1 : 2;
            info->tag_lsb = 12;
            info->size = 16 * KiB;
            return true;
        case 1:
            if (type != 2) {
                return false;
            }
            info->unified = true;
            info->attribute = 1;
            info->associativity = 6;
            info->line_shift = 6;
            info->stride_shift = 6;
            info->store_latency = 1;
            info->load_latency = 6;
            info->tag_lsb = 14;
            info->size = 96 * KiB;
            return true;
        case 2:
            if (type != 2) {
                return false;
            }
            info->unified = true;
            info->attribute = 1;
            info->associativity = 4;
            info->line_shift = 6;
            info->stride_shift = 6;
            info->store_latency = 1;
            info->load_latency = 21;
            info->tag_lsb = 20;
            info->size = 4 * MiB;
            return true;
        default:
            g_assert_not_reached();
        }
    }

    switch (level) {
    case 0:
        info->attribute = 0;
        info->associativity = 4;
        info->line_shift = 6;
        info->stride_shift = 6;
        info->store_latency = type == 1 ? 0xff : 1;
        info->load_latency = 1;
        info->tag_lsb = 12;
        info->size = 16 * KiB;
        return true;
    case 1:
        if (!montecito && type != 2) {
            return false;
        }
        info->unified = !montecito;
        info->attribute = type == 1 ? 0 : 1;
        info->associativity = 8;
        info->line_shift = 7;
        info->stride_shift = 7;
        info->store_latency = type == 1 ? 0xff : 1;
        info->load_latency = type == 1 ? 7 : 5;
        info->tag_lsb = type == 1 ? 17 : 15;
        info->size = type == 1 ? 1 * MiB : 256 * KiB;
        return true;
    case 2:
        if (type != 2) {
            return false;
        }
        info->unified = true;
        info->attribute = 1;
        info->associativity = icc->pal_l3_associativity;
        info->line_shift = 7;
        info->stride_shift = 7;
        info->store_latency = 1;
        info->load_latency = icc->pal_l3_load_latency;
        info->tag_lsb = icc->pal_l3_tag_lsb;
        info->size = icc->pal_l3_cache_size;
        return true;
    default:
        g_assert_not_reached();
    }
}

static void pal_cache_summary(CPUIA64State *env)
{
    if (pal_reserved_args_are_zero(env)) {
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_SUCCESS;
        env->gr[IA64_PAL_GR_RESULT1] = 3;
        env->gr[IA64_PAL_GR_RESULT2] =
            ia64_env_cpu_class(env)->is_montecito ? 5 : 4;
    } else {
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_INVALID_ARGUMENT;
        env->gr[IA64_PAL_GR_RESULT1] = 0;
        env->gr[IA64_PAL_GR_RESULT2] = 0;
    }
    env->gr[IA64_PAL_GR_RESULT3] = 0;
}

static void pal_copy_info(CPUIA64State *env)
{
    uint64_t copy_type = env->gr[IA64_PAL_GR_ARG1];
    uint64_t platform_info = env->gr[IA64_PAL_GR_ARG2];

    if (copy_type == 0 && platform_info == 0) {
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_SUCCESS;
        env->gr[IA64_PAL_GR_RESULT1] = PAL_COPY_BUFFER_SIZE;
        env->gr[IA64_PAL_GR_RESULT2] = PAL_COPY_BUFFER_ALIGN;
    } else if (copy_type == 1) {
        /*
         * copy_type 1 supplies PAL support for a complete IA-32 operating
         * system environment and is paired with PAL_ENTER_IA_32_ENV.  QEMU
         * implements IA-32 application execution on the native Merced and
         * Madison models, but not that separate system environment or its
         * PAL transition procedure.  Report PAL's defined generic error
         * instead of returning a buffer for code that cannot be supplied.
         */
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_ERROR;
        env->gr[IA64_PAL_GR_RESULT1] = 0;
        env->gr[IA64_PAL_GR_RESULT2] = 0;
    } else {
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_INVALID_ARGUMENT;
        env->gr[IA64_PAL_GR_RESULT1] = 0;
        env->gr[IA64_PAL_GR_RESULT2] = 0;
    }
    env->gr[IA64_PAL_GR_RESULT3] = 0;
}

static void pal_copy_pal(CPUIA64State *env)
{
    static const uint64_t pal_proc_words[] = {
        0x000002000000000aULL,
        0x0004000000000200ULL,
        0x0000000100000010ULL,
        0x0084000080000200ULL,
    };
    uint64_t target_addr = pal_stacked_arg(env, 0);
    uint64_t alloc_size = pal_stacked_arg(env, 1);
    uint64_t processor = pal_stacked_arg(env, 2);
    uint64_t target_pa = target_addr & ~PAL_COPY_TARGET_CACHE_ATTR;

    if (processor > 1 ||
        alloc_size < PAL_COPY_BUFFER_SIZE ||
        (target_pa & (PAL_COPY_BUFFER_ALIGN - 1)) != 0 ||
        !pal_physical_range_is_implemented(env, target_pa, alloc_size)) {
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_INVALID_ARGUMENT;
        env->gr[IA64_PAL_GR_RESULT1] = 0;
        env->gr[IA64_PAL_GR_RESULT2] = 0;
        env->gr[IA64_PAL_GR_RESULT3] = 0;
        return;
    }

    if (processor == 0) {
        uint64_t le_words[ARRAY_SIZE(pal_proc_words)];
        int i;

        for (i = 0; i < ARRAY_SIZE(pal_proc_words); i++) {
            le_words[i] = cpu_to_le64(pal_proc_words[i]);
        }
        (void)ia64_exec_physical_rw(target_pa, le_words,
                                    sizeof(le_words), true);
        ia64_exec_invalidate_phys_range(env, target_pa,
                                        PAL_COPY_CODE_SIZE);
    }

    /*
     * An application-processor call does not repeat the memory copy, but it
     * still installs the relocated procedure and PMI entry points in that
     * processor (SDM Vol. 2, PAL_COPY_PAL).  Keep this state per CPU so a
     * subsequent break in the shared PAL image is dispatched as a PAL call.
     */
    env->pal.pal_proc_copy_addr = target_pa + PAL_COPY_PROC_OFFSET;
    env->pal.pal_proc_copy_valid = true;
    env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_SUCCESS;
    env->gr[IA64_PAL_GR_RESULT1] = PAL_COPY_PROC_OFFSET;
    env->gr[IA64_PAL_GR_RESULT2] = 0;
    env->gr[IA64_PAL_GR_RESULT3] = 0;
}

static void pal_brand_info(CPUIA64State *env, uintptr_t ra)
{
    IA64CPUClass *icc = ia64_env_cpu_class(env);
    bool montecito = icc->is_montecito;
    uint64_t request = pal_stacked_arg(env, 0);
    uint64_t address = pal_stacked_arg(env, 1);
    uint64_t reserved = pal_stacked_arg(env, 2);
    const char *brand = icc->pal_brand;
    size_t length;
    size_t i;

    env->gr[IA64_PAL_GR_RESULT1] = 0;
    env->gr[IA64_PAL_GR_RESULT2] = 0;
    env->gr[IA64_PAL_GR_RESULT3] = 0;

    if (reserved != 0) {
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_INVALID_ARGUMENT;
        return;
    }

    switch (request) {
    case 0:
        if (address == 0) {
            env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_INVALID_ARGUMENT;
            return;
        }
        length = strlen(brand);
        for (i = 0; i <= length; i++) {
            ia64_exec_store_data(env, address + i, brand[i], 1, false, ra);
        }
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_SUCCESS;
        env->gr[IA64_PAL_GR_RESULT1] = length;
        break;
    case 16:
        /*
         * The Montecito reference defines requests 16-18.  It explicitly
         * says earlier second-generation processors cannot report validated
         * processor or bus frequencies, while their cache size is
         * available.  Preserve that distinction for Madison.
         */
        env->gr[IA64_PAL_GR_STATUS] = montecito ? PAL_STATUS_SUCCESS :
                     PAL_STATUS_NO_INFORMATION;
        env->gr[IA64_PAL_GR_RESULT1] =
            montecito ? icc->pal_processor_frequency_hz : 0;
        break;
    case 17:
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_SUCCESS;
        env->gr[IA64_PAL_GR_RESULT1] = icc->pal_package_cache_size;
        break;
    case 18:
        env->gr[IA64_PAL_GR_STATUS] = montecito ? PAL_STATUS_SUCCESS :
                     PAL_STATUS_NO_INFORMATION;
        env->gr[IA64_PAL_GR_RESULT1] =
            montecito ? icc->pal_bus_frequency_hz : 0;
        break;
    default:
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_INVALID_ARGUMENT;
        break;
    }
}

typedef struct IA64PalTopology {
    uint32_t socket;
    uint32_t core;
    uint32_t thread;
    uint32_t cores_per_socket;
    uint32_t threads_per_core;
    uint32_t package_base;
    uint32_t package_cpus;
} IA64PalTopology;

static IA64PalTopology pal_cpu_topology(CPUIA64State *env)
{
    CPUState *cs = env_cpu(env);
    IA64CPU *cpu = ia64_cpu_from_cpu_state(cs);

    return (IA64PalTopology) {
        .socket = cpu->socket_id,
        .core = cpu->core_id,
        .thread = cpu->thread_id,
        .cores_per_socket = MAX(cpu->cores_per_socket, 1U),
        .threads_per_core = MAX(cpu->threads_per_core, 1U),
        .package_base = cpu->package_base,
        .package_cpus = MAX(cpu->package_cpus, 1U),
    };
}

static void pal_logical_to_physical(CPUIA64State *env)
{
    IA64PalTopology topology;
    int64_t requested = env->gr[IA64_PAL_GR_ARG1];
    uint32_t number;
    uint32_t target;
    uint32_t target_core;
    uint32_t target_thread;

    env->gr[IA64_PAL_GR_RESULT1] = 0;
    env->gr[IA64_PAL_GR_RESULT2] = 0;
    env->gr[IA64_PAL_GR_RESULT3] = 0;

    if (!ia64_env_cpu_class(env)->is_montecito) {
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_NOT_IMPLEMENTED;
        return;
    }
    if (env->gr[IA64_PAL_GR_ARG2] != 0 || env->gr[IA64_PAL_GR_ARG3] != 0) {
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_INVALID_ARGUMENT;
        return;
    }

    topology = pal_cpu_topology(env);
    if (requested == -1) {
        number = env_cpu(env)->cpu_index - topology.package_base;
    } else if (requested >= 0 && requested < topology.package_cpus) {
        number = requested;
    } else {
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_INVALID_ARGUMENT;
        return;
    }

    target = topology.package_base + number;
    target_core = (target / topology.threads_per_core) %
                  topology.cores_per_socket;
    target_thread = target % topology.threads_per_core;

    env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_SUCCESS;
    env->gr[IA64_PAL_GR_RESULT1] = topology.package_cpus |
                 ((uint64_t)topology.threads_per_core << 16) |
                 ((uint64_t)topology.cores_per_socket << 32) |
                 ((uint64_t)topology.socket << 48);
    env->gr[IA64_PAL_GR_RESULT2] =
        target_thread | ((uint64_t)target_core << 32);
    env->gr[IA64_PAL_GR_RESULT3] = target;
}

static void pal_cache_shared_info(CPUIA64State *env)
{
    IA64PalTopology topology;
    IA64PalCacheInfo cache;
    uint64_t number = env->gr[IA64_PAL_GR_ARG3];
    uint32_t core_base;
    uint32_t shared;
    uint32_t target;

    env->gr[IA64_PAL_GR_RESULT1] = 0;
    env->gr[IA64_PAL_GR_RESULT2] = 0;
    env->gr[IA64_PAL_GR_RESULT3] = 0;

    if (!ia64_env_cpu_class(env)->is_montecito) {
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_NOT_IMPLEMENTED;
        return;
    }
    if (!pal_cache_info_for_model(env, env->gr[IA64_PAL_GR_ARG1],
                                  env->gr[IA64_PAL_GR_ARG2], &cache)) {
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_INVALID_ARGUMENT;
        return;
    }

    topology = pal_cpu_topology(env);
    core_base = topology.package_base +
                topology.core * topology.threads_per_core;
    shared = MIN(topology.threads_per_core,
                 topology.package_cpus -
                 topology.core * topology.threads_per_core);
    if (number >= shared) {
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_INVALID_ARGUMENT;
        return;
    }

    target = core_base + number;
    env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_SUCCESS;
    env->gr[IA64_PAL_GR_RESULT1] = shared;
    env->gr[IA64_PAL_GR_RESULT2] = (target % topology.threads_per_core) |
                  ((uint64_t)topology.core << 32);
    env->gr[IA64_PAL_GR_RESULT3] = target;
}

static void pal_halt_info(CPUIA64State *env, uintptr_t ra)
{
    uint64_t power_buffer = pal_stacked_arg(env, 0);
    uint64_t reserved1 = pal_stacked_arg(env, 1);
    uint64_t reserved2 = pal_stacked_arg(env, 2);
    uint64_t power_states[PAL_HALT_STATE_COUNT] = { 0 };
    int i;

    if ((power_buffer & 7) != 0 || reserved1 != 0 || reserved2 != 0) {
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_INVALID_ARGUMENT;
        env->gr[IA64_PAL_GR_RESULT1] = 0;
        env->gr[IA64_PAL_GR_RESULT2] = 0;
        env->gr[IA64_PAL_GR_RESULT3] = 0;
        return;
    }

    /*
     * QEMU models the scheduling-visible running and halted states, but has
     * no electrical power or cycle-accurate transition model.  Keep the two
     * architected halt states usable and give them conservative nonzero
     * synthetic power and latency values; these are not measurements of a
     * physical processor.
     */
    power_states[0] = PAL_HALT_STATE_IMPLEMENTED | PAL_HALT_STATE_COHERENT |
                      (1000ULL << 32) | (1ULL << 16) | 1ULL;
    power_states[1] = PAL_HALT_STATE_IMPLEMENTED |
                      (1000ULL << 32) | (1ULL << 16) | 1ULL;

    for (i = 0; i < PAL_HALT_STATE_COUNT; i++) {
        ia64_exec_store_data(env, power_buffer + i * 8, power_states[i],
                             8, false, ra);
    }

    env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_SUCCESS;
    env->gr[IA64_PAL_GR_RESULT1] = 0;
    env->gr[IA64_PAL_GR_RESULT2] = 0;
    env->gr[IA64_PAL_GR_RESULT3] = 0;
}

static void pal_mc_drain(CPUIA64State *env)
{
    /*
     * TCG completes memory transactions synchronously and has no deferred
     * physical machine-check source.  There is therefore nothing additional
     * to drain once the PAL call is reached.
     */
    env->gr[IA64_PAL_GR_STATUS] = pal_reserved_args_are_zero(env) ?
        PAL_STATUS_SUCCESS : PAL_STATUS_INVALID_ARGUMENT;
    env->gr[IA64_PAL_GR_RESULT1] = 0;
    env->gr[IA64_PAL_GR_RESULT2] = 0;
    env->gr[IA64_PAL_GR_RESULT3] = 0;
}

static bool pal_reserved_args_are_zero(CPUIA64State *env)
{
    return env->gr[IA64_PAL_GR_ARG1] == 0 &&
           env->gr[IA64_PAL_GR_ARG2] == 0 &&
           env->gr[IA64_PAL_GR_ARG3] == 0;
}

static void pal_mc_clear_log(CPUIA64State *env)
{
    if (pal_reserved_args_are_zero(env)) {
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_SUCCESS;
        env->pal.pal_mc_log_valid = false;
        env->pal.pal_mc_error_map = 0;
        env->pal.pal_mc_state_parameter = 0;
        env->pal.pal_mc_status = 0;
        env->pal.pal_mc_address = 0;
        env->pal.pal_mc_information = 0;
        env->pal.pal_mc_ip = 0;
        env->gr[IA64_PAL_GR_RESULT1] =
            (env->pal.pal_mca_pending ? BIT_ULL(0) : 0) |
            (env->interrupt.sapic_init_pending ? BIT_ULL(1) : 0);
    } else {
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_INVALID_ARGUMENT;
        env->gr[IA64_PAL_GR_RESULT1] = 0;
    }
    env->gr[IA64_PAL_GR_RESULT2] = 0;
    env->gr[IA64_PAL_GR_RESULT3] = 0;
}

static void pal_mc_expected(CPUIA64State *env)
{
    uint64_t expected = env->gr[IA64_PAL_GR_ARG1];

    if (expected > 1 || env->gr[IA64_PAL_GR_ARG2] != 0 ||
        env->gr[IA64_PAL_GR_ARG3] != 0) {
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_INVALID_ARGUMENT;
        env->gr[IA64_PAL_GR_RESULT1] = 0;
    } else {
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_SUCCESS;
        env->gr[IA64_PAL_GR_RESULT1] = env->pal.pal_mc_expected ? 1 : 0;
        env->pal.pal_mc_expected = expected != 0;
    }
    env->gr[IA64_PAL_GR_RESULT2] = 0;
    env->gr[IA64_PAL_GR_RESULT3] = 0;
}

static void pal_mc_dynamic_state(CPUIA64State *env)
{
    uint64_t offset = env->gr[IA64_PAL_GR_ARG1];

    if ((offset & 7) != 0 || env->gr[IA64_PAL_GR_ARG2] != 0 ||
        env->gr[IA64_PAL_GR_ARG3] != 0) {
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_INVALID_ARGUMENT;
    } else {
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_SUCCESS;
    }
    env->gr[IA64_PAL_GR_RESULT1] = 0;
    env->gr[IA64_PAL_GR_RESULT2] = 0;
    env->gr[IA64_PAL_GR_RESULT3] = 0;
}

static bool pal_mc_level_index_valid(CPUIA64State *env,
                                     uint64_t level_index)
{
    IA64CPU *cpu = env_archcpu(env);
    uint64_t structure_bits = (level_index >> 8) & ((1ULL << 40) - 1);

    if ((level_index >> 48) != 0 ||
        (level_index & 0xf) != (cpu->core_id & 0xf) ||
        ((level_index >> 4) & 0xf) != (cpu->thread_id & 0xf)) {
        return false;
    }

    return structure_bits != 0 && (structure_bits & (structure_bits - 1)) == 0;
}

static void pal_mc_error_info(CPUIA64State *env)
{
    uint64_t info_index = env->gr[IA64_PAL_GR_ARG1];
    uint64_t level_index = env->gr[IA64_PAL_GR_ARG2];
    uint64_t err_type_index = env->gr[IA64_PAL_GR_ARG3];
    uint64_t value = 0;
    bool valid = false;

    switch (info_index) {
    case 0:
        value = env->pal.pal_mc_error_map;
        valid = true;
        break;
    case 1:
        value = env->pal.pal_mc_state_parameter;
        valid = true;
        break;
    case 2:
        valid = pal_mc_level_index_valid(env, level_index) &&
                (!env->pal.pal_mc_log_valid ||
                 (level_index & ~UINT64_C(0xff)) ==
                    (env->pal.pal_mc_error_map & ~UINT64_C(0xff))) &&
                (err_type_index & 7) <= 4;
        if (valid) {
            switch (err_type_index & 7) {
            case 0:
                value = env->pal.pal_mc_status;
                break;
            case 1:
                value = env->pal.pal_mc_address;
                break;
            case 2:
                value = env->pal.pal_mc_information;
                break;
            case 3:
                value = 0;
                break;
            case 4:
                value = env->pal.pal_mc_ip;
                break;
            }
        }
        break;
    default:
        valid = false;
        break;
    }

    if (!valid) {
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_INVALID_ARGUMENT;
    } else if (!env->pal.pal_mc_log_valid) {
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_NO_INFORMATION;
    } else {
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_SUCCESS;
    }
    env->gr[IA64_PAL_GR_RESULT1] =
        env->gr[IA64_PAL_GR_STATUS] == PAL_STATUS_SUCCESS ? value : 0;
    env->gr[IA64_PAL_GR_RESULT2] = 0;
    env->gr[IA64_PAL_GR_RESULT3] = 0;
}

static bool pal_mc_resume(CPUIA64State *env)
{
    uint64_t set_cmci = env->gr[IA64_PAL_GR_ARG1];
    uint64_t save_ptr = env->gr[IA64_PAL_GR_ARG2];
    uint64_t new_context = env->gr[IA64_PAL_GR_ARG3];
    int64_t status;

    if (set_cmci > 1 || new_context > 1 ||
        !(save_ptr & IA64_PHYS_UC_BIT) || (save_ptr & 0x1ff) != 0 ||
        !pal_physical_range_is_implemented(env, save_ptr,
                                           PAL_MIN_STATE_SAVE_SIZE)) {
        status = PAL_STATUS_INVALID_ARGUMENT;
    } else {
        if (ia64_ras_restore_min_state(
                ia64_cpu_from_cpu_state(env_cpu(env)), save_ptr,
                new_context != 0)) {
            env->pal.pal_mca_active = false;
            env->pal.pal_mca_active_record_id = 0;
            env->pal.pal_init_active = false;
            if (set_cmci) {
                env->pal.pal_cmc_pending = true;
            }
            ia64_ras_update_cmc(env);
            ia64_sapic_update_interrupt(env);
            return true;
        }
        status = PAL_STATUS_ERROR;
    }
    env->gr[IA64_PAL_GR_STATUS] = status;
    env->gr[IA64_PAL_GR_RESULT1] = 0;
    env->gr[IA64_PAL_GR_RESULT2] = 0;
    env->gr[IA64_PAL_GR_RESULT3] = 0;
    return false;
}

static void pal_mc_register_mem(CPUIA64State *env)
{
    uint64_t address = env->gr[IA64_PAL_GR_ARG1];

    /*
     * The architected min-state area is a 4 KiB uncacheable region even
     * though its base alignment is only 512 bytes.  In physical addressing
     * the uncacheable attribute is carried in address bit 63, which is not
     * part of the implemented physical-address-width check.
     */
    if (!(address & IA64_PHYS_UC_BIT) || (address & 0x1ff) != 0 ||
        !pal_physical_range_is_implemented(env, address,
                                           PAL_MIN_STATE_SAVE_SIZE) ||
        env->gr[IA64_PAL_GR_ARG2] != 0 || env->gr[IA64_PAL_GR_ARG3] != 0) {
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_INVALID_ARGUMENT;
    } else {
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_SUCCESS;
        env->pal.pal_mc_save_addr = address;
    }
    env->gr[IA64_PAL_GR_RESULT1] = 0;
    env->gr[IA64_PAL_GR_RESULT2] = 0;
    env->gr[IA64_PAL_GR_RESULT3] = 0;
}

static void pal_cache_line_init(CPUIA64State *env)
{
    uint64_t address = env->gr[IA64_PAL_GR_ARG1];

    if ((address >> 63) != 0 ||
        !pal_physical_range_is_implemented(env, address, 1) ||
        env->gr[IA64_PAL_GR_ARG3] != 0) {
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_INVALID_ARGUMENT;
    } else {
        /*
         * There is no target cache-array state to seed with data_value.
         * Validation is still architecturally visible, while a successful
         * request has no timing or memory side effect in the cacheless model.
         */
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_SUCCESS;
    }
    env->gr[IA64_PAL_GR_RESULT1] = 0;
    env->gr[IA64_PAL_GR_RESULT2] = 0;
    env->gr[IA64_PAL_GR_RESULT3] = 0;
}

static void pal_pmi_entrypoint(CPUIA64State *env)
{
    uint64_t entry = env->gr[IA64_PAL_GR_ARG1];

    if ((entry & 0xff) != 0 ||
        !pal_physical_range_is_implemented(env, entry, 1) ||
        env->gr[IA64_PAL_GR_ARG2] != 0 || env->gr[IA64_PAL_GR_ARG3] != 0) {
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_INVALID_ARGUMENT;
    } else {
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_SUCCESS;
        env->pal.pal_pmi_entry = entry;
        ia64_sapic_update_interrupt(env);
    }
    env->gr[IA64_PAL_GR_RESULT1] = 0;
    env->gr[IA64_PAL_GR_RESULT2] = 0;
    env->gr[IA64_PAL_GR_RESULT3] = 0;
}

static void pal_mem_for_test(CPUIA64State *env)
{
    /*
     * The virtual processor has no electrical, cache-array, or datapath
     * failure state for late self-test to diagnose.  It therefore needs no
     * scratch memory; alignment one is the neutral value for the resulting
     * zero-byte requirement.  This is emulator-specific rather than a claim
     * about the requirement of a physical processor.
     */
    env->gr[IA64_PAL_GR_STATUS] = pal_reserved_args_are_zero(env) ?
        PAL_STATUS_SUCCESS : PAL_STATUS_INVALID_ARGUMENT;
    env->gr[IA64_PAL_GR_RESULT1] = 0;
    env->gr[IA64_PAL_GR_RESULT2] =
        env->gr[IA64_PAL_GR_STATUS] == PAL_STATUS_SUCCESS ? 1 : 0;
    env->gr[IA64_PAL_GR_RESULT3] = 0;
}

static int64_t pal_proc_feature_set_status(CPUIA64State *env,
                                           uint64_t feature_set)
{
    IA64CPUModel model = ia64_env_cpu_class(env)->model;

    if (feature_set == 0) {
        return PAL_STATUS_SUCCESS;
    }
    if (feature_set < 16) {
        return PAL_STATUS_INVALID_ARGUMENT;
    }

    if (model == IA64_CPU_MODEL_MERCED) {
        return PAL_STATUS_BEYOND_MAX;
    }

    if (model == IA64_CPU_MODEL_MADISON) {
        /* Madison recognizes feature set 16 and exposes no feature bits. */
        return feature_set == 16 ?
            PAL_STATUS_SUCCESS : PAL_STATUS_BEYOND_MAX;
    }

    if (feature_set > 18) {
        return PAL_STATUS_BEYOND_MAX;
    }
    return feature_set < 18 ?
        PAL_STATUS_NEXT_HIGHER : PAL_STATUS_SUCCESS;
}

static void pal_proc_get_features(CPUIA64State *env)
{
    IA64CPUClass *icc = ia64_env_cpu_class(env);
    uint64_t feature_set = env->gr[IA64_PAL_GR_ARG2];

    env->gr[IA64_PAL_GR_RESULT1] = 0;
    env->gr[IA64_PAL_GR_RESULT2] = 0;
    env->gr[IA64_PAL_GR_RESULT3] = 0;

    if (env->gr[IA64_PAL_GR_ARG1] != 0 || env->gr[IA64_PAL_GR_ARG3] != 0) {
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_INVALID_ARGUMENT;
        return;
    }

    env->gr[IA64_PAL_GR_STATUS] =
        pal_proc_feature_set_status(env, feature_set);
    if (env->gr[IA64_PAL_GR_STATUS] == PAL_STATUS_SUCCESS &&
        feature_set == 18) {
        /*
         * Feature set 18 contains instruction-cache coherence filtering,
         * enhanced exclusive prefetch, and optional Hyper-Threading bits.
         */
        env->gr[IA64_PAL_GR_RESULT1] =
            icc->pal_proc_feature_available;
        env->gr[IA64_PAL_GR_RESULT2] =
            env->pal.pal_proc_feature_status;
        env->gr[IA64_PAL_GR_RESULT3] =
            icc->pal_proc_feature_controllable;
    }
}

static void pal_cache_info(CPUIA64State *env)
{
    uint64_t level = env->gr[IA64_PAL_GR_ARG1];
    uint64_t cache_type = env->gr[IA64_PAL_GR_ARG2];
    IA64PalCacheInfo info;

    if (env->gr[IA64_PAL_GR_ARG3] != 0 ||
        !pal_cache_info_for_model(env, level, cache_type, &info)) {
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_INVALID_ARGUMENT;
        env->gr[IA64_PAL_GR_RESULT1] = 0;
        env->gr[IA64_PAL_GR_RESULT2] = 0;
        env->gr[IA64_PAL_GR_RESULT3] = 0;
        return;
    }

    env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_SUCCESS;
    env->gr[IA64_PAL_GR_RESULT1] = (info.unified ? 1ULL : 0ULL) |
                 ((uint64_t)info.attribute << 1) |
                 ((uint64_t)info.associativity << 8) |
                 ((uint64_t)info.line_shift << 16) |
                 ((uint64_t)info.stride_shift << 24) |
                 ((uint64_t)info.store_latency << 32) |
                 ((uint64_t)info.load_latency << 40) |
                 ((uint64_t)info.store_hints << 48) |
                 ((uint64_t)info.load_hints << 56);
    env->gr[IA64_PAL_GR_RESULT2] = info.size |
                  ((uint64_t)info.alias_boundary << 32) |
                  ((uint64_t)info.tag_lsb << 40) |
                  ((uint64_t)info.tag_msb << 48);
    env->gr[IA64_PAL_GR_RESULT3] = 0;
}

static void pal_cache_prot_info(CPUIA64State *env)
{
    uint64_t level = env->gr[IA64_PAL_GR_ARG1];
    uint64_t cache_type = env->gr[IA64_PAL_GR_ARG2];
    uint64_t reserved = env->gr[IA64_PAL_GR_ARG3];
    uint32_t data_none = 64;
    uint32_t tag_none;
    IA64PalCacheInfo info;

    if (reserved != 0 ||
        !pal_cache_info_for_model(env, level, cache_type, &info)) {
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_INVALID_ARGUMENT;
        env->gr[IA64_PAL_GR_RESULT1] = 0;
        env->gr[IA64_PAL_GR_RESULT2] = 0;
        env->gr[IA64_PAL_GR_RESULT3] = 0;
        return;
    }

    /*
     * Physical processors protect cache data and tag arrays with parity or
     * ECC, but TCG has neither cache-array state nor cache error injection
     * and correction semantics.  Public product documentation also does not
     * define enough PAL protection-group geometry to construct truthful
     * descriptors for every modeled cache.  Report method zero for the
     * protection that is actually modeled instead of inventing prot_bits or
     * grouping values.  This is an intentional emulator-specific result.
     */
    tag_none = (1U << 30) | ((uint32_t)info.tag_lsb << 8) |
               ((uint32_t)info.tag_msb << 14);
    env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_SUCCESS;
    env->gr[IA64_PAL_GR_RESULT1] = data_none | ((uint64_t)tag_none << 32);
    env->gr[IA64_PAL_GR_RESULT2] = 0;
    env->gr[IA64_PAL_GR_RESULT3] = 0;
}

static void pal_vm_info(CPUIA64State *env)
{
    IA64CPUClass *icc = ia64_env_cpu_class(env);
    uint64_t level = env->gr[IA64_PAL_GR_ARG1];
    uint64_t tc_type = env->gr[IA64_PAL_GR_ARG2];
    uint64_t tc_info;
    uint64_t tc_pages;

    if (level >= icc->tc_levels || env->gr[IA64_PAL_GR_ARG3] != 0 ||
        tc_type < 1 || tc_type > 2) {
        goto invalid;
    }

    if (icc->model == IA64_CPU_MODEL_MERCED) {
        if (tc_type == 1) {
            if (level != 0) {
                goto invalid;
            }
            tc_info = 1ULL | ((uint64_t)icc->itlb_entries << 8) |
                      ((uint64_t)icc->itlb_entries << 16) |
                      (1ULL << 34);
        } else if (level == 0) {
            tc_info = 1ULL | (32ULL << 8) | (32ULL << 16);
        } else {
            tc_info = 1ULL | ((uint64_t)icc->dtlb_entries << 8) |
                      ((uint64_t)icc->dtlb_entries << 16) |
                      (1ULL << 34);
        }
        tc_pages = ia64_cpu_page_size_mask(env);
    } else if (level == 0) {
        tc_info = 1ULL | (32ULL << 8) | (32ULL << 16);
        tc_pages = 1ULL << 12;
    } else {
        uint64_t entries = tc_type == 1 ? icc->itlb_entries :
                                          icc->dtlb_entries;

        tc_info = 1ULL | (entries << 8) | (entries << 16) |
                  (1ULL << 32) | (1ULL << 34);
        tc_pages = ia64_cpu_page_size_mask(env);
    }

    env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_SUCCESS;
    env->gr[IA64_PAL_GR_RESULT1] = tc_info;
    env->gr[IA64_PAL_GR_RESULT2] = tc_pages;
    env->gr[IA64_PAL_GR_RESULT3] = 0;
    return;

invalid:
    env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_INVALID_ARGUMENT;
    env->gr[IA64_PAL_GR_RESULT1] = 0;
    env->gr[IA64_PAL_GR_RESULT2] = 0;
    env->gr[IA64_PAL_GR_RESULT3] = 0;
}

static uint64_t pal_page_shift(uint64_t page_size)
{
    uint64_t shift = 0;

    while ((1ULL << shift) < page_size && shift < 63) {
        shift++;
    }
    return shift;
}

static void pal_vm_tr_read(CPUIA64State *env, uintptr_t ra)
{
    uint64_t reg_num = pal_stacked_arg(env, 0);
    uint64_t tr_type = pal_stacked_arg(env, 1);
    uint64_t tr_buffer = pal_stacked_arg(env, 2);
    const IA64TlbEntry *tlb;
    const IA64TlbEntry *entry;
    uint64_t pte = 0;
    uint64_t itir = 0;
    uint64_t ifa = 0;
    uint64_t rr = 0;
    uint64_t tr_valid = 0;
    uint64_t ps_shift;

    if (tr_type > 1 ||
        reg_num >= (tr_type == 0 ? ia64_env_cpu_class(env)->itr_count :
                                  ia64_env_cpu_class(env)->dtr_count) ||
        (tr_buffer & 7) != 0) {
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_INVALID_ARGUMENT;
        env->gr[IA64_PAL_GR_RESULT1] = 0;
        env->gr[IA64_PAL_GR_RESULT2] = 0;
        env->gr[IA64_PAL_GR_RESULT3] = 0;
        return;
    }

    tlb = tr_type == 0 ? env->mmu.tlb_inst : env->mmu.tlb_data;
    entry = &tlb[reg_num];
    if (entry->valid && entry->is_tr) {
        ps_shift = pal_page_shift(entry->ps);
        pte = entry->pte;
        itir = (ps_shift << IA64_ITIR_PS_SHIFT) |
               ((uint64_t)entry->key << IA64_ITIR_KEY_SHIFT);
        ifa = entry->va | 1;
        rr = ((uint64_t)entry->rid << IA64_RR_RID_SHIFT) |
             (ps_shift << IA64_ITIR_PS_SHIFT);
        tr_valid = 0xf;
    }

    ia64_exec_store_data(env, tr_buffer, pte, 8, false, ra);
    ia64_exec_store_data(env, tr_buffer + 8, itir, 8, false, ra);
    ia64_exec_store_data(env, tr_buffer + 16, ifa, 8, false, ra);
    ia64_exec_store_data(env, tr_buffer + 24, rr, 8, false, ra);

    env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_SUCCESS;
    env->gr[IA64_PAL_GR_RESULT1] = tr_valid;
    env->gr[IA64_PAL_GR_RESULT2] = 0;
    env->gr[IA64_PAL_GR_RESULT3] = 0;
}

static void pal_freq_base(CPUIA64State *env)
{
    if (pal_reserved_args_are_zero(env)) {
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_SUCCESS;
        env->gr[IA64_PAL_GR_RESULT1] =
            ia64_env_cpu_class(env)->frequency_base_hz;
    } else {
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_INVALID_ARGUMENT;
        env->gr[IA64_PAL_GR_RESULT1] = 0;
    }
    env->gr[IA64_PAL_GR_RESULT2] = 0;
    env->gr[IA64_PAL_GR_RESULT3] = 0;
}

static void pal_freq_ratios(CPUIA64State *env)
{
    if (pal_reserved_args_are_zero(env)) {
        IA64CPUClass *icc = ia64_env_cpu_class(env);

        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_SUCCESS;
        env->gr[IA64_PAL_GR_RESULT1] = icc->processor_frequency_ratio;
        env->gr[IA64_PAL_GR_RESULT2] = icc->bus_frequency_ratio;
        env->gr[IA64_PAL_GR_RESULT3] = icc->itc_frequency_ratio;
    } else {
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_INVALID_ARGUMENT;
        env->gr[IA64_PAL_GR_RESULT1] = 0;
        env->gr[IA64_PAL_GR_RESULT2] = 0;
        env->gr[IA64_PAL_GR_RESULT3] = 0;
    }
}

static void pal_ptce_info(CPUIA64State *env)
{
    if (pal_reserved_args_are_zero(env)) {
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_SUCCESS;
        env->gr[IA64_PAL_GR_RESULT1] = 0;
        env->gr[IA64_PAL_GR_RESULT2] = (1ULL << 32) | 1ULL;
        env->gr[IA64_PAL_GR_RESULT3] = 0;
    } else {
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_INVALID_ARGUMENT;
        env->gr[IA64_PAL_GR_RESULT1] = 0;
        env->gr[IA64_PAL_GR_RESULT2] = 0;
        env->gr[IA64_PAL_GR_RESULT3] = 0;
    }
}

static void pal_bus_get_features(CPUIA64State *env)
{
    if (pal_reserved_args_are_zero(env)) {
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_SUCCESS;
        /*
         * PAL requires both defined features to be reported as available
         * and software-controllable.
         */
        env->gr[IA64_PAL_GR_RESULT1] = PAL_BUS_REQUIRED_FEATURES;
        env->gr[IA64_PAL_GR_RESULT2] =
            env->pal.pal_bus_feature_status;
        env->gr[IA64_PAL_GR_RESULT3] = PAL_BUS_REQUIRED_FEATURES;
    } else {
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_INVALID_ARGUMENT;
        env->gr[IA64_PAL_GR_RESULT1] = 0;
        env->gr[IA64_PAL_GR_RESULT2] = 0;
        env->gr[IA64_PAL_GR_RESULT3] = 0;
    }
}

static void pal_bus_set_features(CPUIA64State *env)
{
    uint64_t requested = env->gr[IA64_PAL_GR_ARG1];
    uint64_t controllable = PAL_BUS_REQUIRED_FEATURES;

    if (env->gr[IA64_PAL_GR_ARG2] != 0 ||
        env->gr[IA64_PAL_GR_ARG3] != 0) {
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_INVALID_ARGUMENT;
    } else {
        /*
         * PAL requires selections for unavailable or uncontrollable
         * features to be ignored, not rejected.
         */
        env->pal.pal_bus_feature_status =
            (env->pal.pal_bus_feature_status & ~controllable) |
            (requested & controllable);
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_SUCCESS;
    }
    env->gr[IA64_PAL_GR_RESULT1] = 0;
    env->gr[IA64_PAL_GR_RESULT2] = 0;
    env->gr[IA64_PAL_GR_RESULT3] = 0;
}

static void pal_proc_set_features(CPUIA64State *env)
{
    IA64CPUClass *icc = ia64_env_cpu_class(env);
    uint64_t requested = env->gr[IA64_PAL_GR_ARG1];
    uint64_t feature_set = env->gr[IA64_PAL_GR_ARG2];

    if (env->gr[IA64_PAL_GR_ARG3] != 0) {
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_INVALID_ARGUMENT;
    } else {
        env->gr[IA64_PAL_GR_STATUS] =
            pal_proc_feature_set_status(env, feature_set);
        if (env->gr[IA64_PAL_GR_STATUS] == PAL_STATUS_SUCCESS &&
            feature_set == 18) {
            /* Unavailable and uncontrollable feature selections are ignored. */
            env->pal.pal_proc_feature_status =
                (env->pal.pal_proc_feature_status &
                 ~icc->pal_proc_feature_controllable) |
                (requested & icc->pal_proc_feature_controllable);
        }
    }
    env->gr[IA64_PAL_GR_RESULT1] = 0;
    env->gr[IA64_PAL_GR_RESULT2] = 0;
    env->gr[IA64_PAL_GR_RESULT3] = 0;
}

static void pal_register_info(CPUIA64State *env)
{
    uint64_t info_type = env->gr[IA64_PAL_GR_ARG1];

    if (env->gr[IA64_PAL_GR_ARG2] != 0 ||
        env->gr[IA64_PAL_GR_ARG3] != 0 || info_type > 3) {
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_INVALID_ARGUMENT;
        env->gr[IA64_PAL_GR_RESULT1] = 0;
        env->gr[IA64_PAL_GR_RESULT2] = 0;
        env->gr[IA64_PAL_GR_RESULT3] = 0;
        return;
    }

    env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_SUCCESS;
    switch (info_type) {
    case 0:
        env->gr[IA64_PAL_GR_RESULT1] = 0x000011117f2f00ffULL;
        env->gr[IA64_PAL_GR_RESULT2] = 0x7;
        break;
    case 1:
        env->gr[IA64_PAL_GR_RESULT1] = 0;
        env->gr[IA64_PAL_GR_RESULT2] = 0;
        break;
    case 2:
        env->gr[IA64_PAL_GR_RESULT1] = 0x0000000003fb0107ULL;
        env->gr[IA64_PAL_GR_RESULT2] = 0x307ff;
        break;
    case 3:
        env->gr[IA64_PAL_GR_RESULT1] = 0;
        env->gr[IA64_PAL_GR_RESULT2] = 0x2;
        break;
    default:
        g_assert_not_reached();
        break;
    }
    env->gr[IA64_PAL_GR_RESULT3] = 0;
}

static void pal_perf_mon_info(CPUIA64State *env, uintptr_t ra)
{
    IA64CPUClass *icc = ia64_env_cpu_class(env);
    uint64_t pm_buffer = env->gr[IA64_PAL_GR_ARG1];
    int i;

    if (pm_buffer == 0 || (pm_buffer & 7) != 0 ||
        env->gr[IA64_PAL_GR_ARG2] != 0 || env->gr[IA64_PAL_GR_ARG3] != 0) {
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_INVALID_ARGUMENT;
        env->gr[IA64_PAL_GR_RESULT1] = 0;
        env->gr[IA64_PAL_GR_RESULT2] = 0;
        env->gr[IA64_PAL_GR_RESULT3] = 0;
        return;
    }

    for (i = 0; i < 16; i++) {
        ia64_exec_store_data(env, pm_buffer + i * 8, 0, 8, false, ra);
    }

    /*
     * The later-generation PAL table specifies four generic counter pairs
     * and the masks returned below, although the PMU chapter in the same
     * processor manual describes twelve physical counters.  The published
     * material does not explain how those two descriptions are reconciled.
     * Follow the callable PAL contract here; TCG exposes the corresponding
     * register/control subset but does not synthesize model-specific event
     * accumulation.
     */
    ia64_exec_store_data(env, pm_buffer, icc->implemented_pmc_mask,
                         8, false, ra);
    ia64_exec_store_data(env, pm_buffer + 0x20, icc->implemented_pmd_mask,
                         8, false, ra);
    ia64_exec_store_data(env, pm_buffer + 0x40, icc->perf_cycles_mask,
                         8, false, ra);
    ia64_exec_store_data(env, pm_buffer + 0x60, icc->perf_retired_mask,
                         8, false, ra);

    env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_SUCCESS;
    env->gr[IA64_PAL_GR_RESULT1] = PAL_PERF_MON_INFO_BASE |
        ((uint64_t)icc->perf_counter_width << 8);
    env->gr[IA64_PAL_GR_RESULT2] = 0;
    env->gr[IA64_PAL_GR_RESULT3] = 0;
}

static bool pal_addr_overlaps_fw_update(uint64_t address, uint64_t alignment)
{
    uint64_t fw_base = 0xff000000ULL;
    uint64_t fw_limit = 0x100000000ULL;
    uint64_t block_end;

    if (address >= fw_limit) {
        return false;
    }

    block_end = address + alignment;
    return block_end > fw_base && address < fw_limit;
}

static void pal_platform_addr(CPUIA64State *env)
{
    IA64CPUClass *icc = ia64_env_cpu_class(env);
    uint64_t block_type = env->gr[IA64_PAL_GR_ARG1];
    uint64_t address = env->gr[IA64_PAL_GR_ARG2] & ~(1ULL << 63);
    uint64_t alignment;
    uint64_t implemented_limit = 1ULL << icc->phys_addr_bits;

    if (env->gr[IA64_PAL_GR_ARG3] != 0 || block_type > 1) {
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_INVALID_ARGUMENT;
        env->gr[IA64_PAL_GR_RESULT1] = 0;
        env->gr[IA64_PAL_GR_RESULT2] = 0;
        env->gr[IA64_PAL_GR_RESULT3] = 0;
        return;
    }

    if (block_type == 0) {
        alignment = 2ULL << 20;
    } else {
        alignment = 64ULL << 20;
    }

    if ((address & (alignment - 1)) != 0 ||
        !ia64_pa_is_implemented(env, address) ||
        address > implemented_limit - alignment ||
        pal_addr_overlaps_fw_update(address, alignment)) {
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_ERROR;
    } else {
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_SUCCESS;
        if (block_type == 0) {
            env->pal.pal_interrupt_block_addr = address;
        } else {
            env->pal.pal_io_block_addr = address;
        }
    }
    env->gr[IA64_PAL_GR_RESULT1] = 0;
    env->gr[IA64_PAL_GR_RESULT2] = 0;
    env->gr[IA64_PAL_GR_RESULT3] = 0;
}

static void pal_test_proc(CPUIA64State *env)
{
    uint64_t test_address = pal_stacked_arg(env, 0);
    uint64_t test_size = pal_stacked_arg(env, 1);
    uint64_t attributes = pal_stacked_arg(env, 2);

    if ((test_address >> 63) != 0 ||
        !pal_physical_range_is_implemented(env, test_address, test_size) ||
        (attributes & ~PAL_MEM_ATTR_VALID_MASK) != 0 ||
        (attributes & PAL_MEM_ATTR_WB) == 0) {
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_INVALID_ARGUMENT;
        env->gr[IA64_PAL_GR_RESULT1] = 0;
    } else {
        /*
         * No model-specific physical failure sources exist to exercise.
         * After validating the callable memory contract, report the
         * architected healthy state with the testing-performed bit set.
         */
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_SUCCESS;
        env->gr[IA64_PAL_GR_RESULT1] = PAL_SELF_TEST_STATE_TESTED;
    }
    env->gr[IA64_PAL_GR_RESULT2] = 0;
    env->gr[IA64_PAL_GR_RESULT3] = 0;
}

static void pal_debug_info(CPUIA64State *env)
{
    if (pal_reserved_args_are_zero(env)) {
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_SUCCESS;
        env->gr[IA64_PAL_GR_RESULT1] = IA64_IBR_IMPLEMENTED_COUNT / 2;
        env->gr[IA64_PAL_GR_RESULT2] = IA64_DBR_IMPLEMENTED_COUNT / 2;
    } else {
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_INVALID_ARGUMENT;
        env->gr[IA64_PAL_GR_RESULT1] = 0;
        env->gr[IA64_PAL_GR_RESULT2] = 0;
    }
    env->gr[IA64_PAL_GR_RESULT3] = 0;
}

static void pal_fixed_addr(CPUIA64State *env)
{
    CPUState *cs = env_cpu(env);

    if (pal_reserved_args_are_zero(env)) {
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_SUCCESS;
        env->gr[IA64_PAL_GR_RESULT1] = cs->cpu_index & 0xffff;
    } else {
        env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_INVALID_ARGUMENT;
        env->gr[IA64_PAL_GR_RESULT1] = 0;
    }
    env->gr[IA64_PAL_GR_RESULT2] = 0;
    env->gr[IA64_PAL_GR_RESULT3] = 0;
}

static void pal_not_implemented(CPUIA64State *env)
{
    env->gr[IA64_PAL_GR_STATUS] = PAL_STATUS_NOT_IMPLEMENTED;
    env->gr[IA64_PAL_GR_RESULT1] = 0;
    env->gr[IA64_PAL_GR_RESULT2] = 0;
    env->gr[IA64_PAL_GR_RESULT3] = 0;
}

uint32_t ia64_pal_dispatch(CPUIA64State *env, uintptr_t ra)
{
    uint64_t index = env->gr[IA64_PAL_GR_INDEX];
    uint32_t flags = 0;

    trace_ia64_pal_call(env_cpu(env)->cpu_index, index,
                        env->gr[IA64_PAL_GR_ARG1],
                        env->gr[IA64_PAL_GR_ARG2],
                        env->gr[IA64_PAL_GR_ARG3]);

    /*
     * These procedures post-date this model's PAL revision.  Earlier
     * required procedures, including prefetch visibility, remain available.
     */
    if (ia64_env_cpu_class(env)->model == IA64_CPU_MODEL_MERCED &&
        (index == PAL_LOGICAL_TO_PHYSICAL ||
         index == PAL_CACHE_SHARED_INFO ||
         index == PAL_BRAND_INFO)) {
        pal_not_implemented(env);
        goto complete_call;
    }

    switch (index) {
    case PAL_VERSION:
        pal_get_version(env);
        break;
    case PAL_RSE_INFO:
        pal_rse_info(env);
        break;
    case PAL_VM_SUMMARY:
        pal_vm_summary(env);
        break;
    case PAL_HALT_LIGHT:
        if (pal_halt_light(env)) {
            flags |= IA64_PAL_DISPATCH_HALTED;
        }
        break;
    case PAL_PREFETCH_VIS:
        pal_prefetch_vis(env);
        break;
    case PAL_CACHE_FLUSH:
        if (pal_cache_flush(env)) {
            /* Preserve the instruction-cache serialization boundary. */
            flags |= IA64_PAL_DISPATCH_EXIT_TB;
        }
        break;
    case PAL_CACHE_INIT:
        pal_cache_init(env);
        break;
    case PAL_CACHE_LINE_INIT:
        pal_cache_line_init(env);
        break;
    case PAL_CACHE_SUMMARY:
        pal_cache_summary(env);
        break;
    case PAL_MEM_ATTRIB:
        pal_mem_attrib(env);
        break;
    case PAL_PROC_GET_FEATURES:
        pal_proc_get_features(env);
        break;
    case PAL_PROC_SET_FEATURES:
        pal_proc_set_features(env);
        break;
    case PAL_CACHE_INFO:
        pal_cache_info(env);
        break;
    case PAL_CACHE_PROT_INFO:
        pal_cache_prot_info(env);
        break;
    case PAL_CACHE_SHARED_INFO:
        pal_cache_shared_info(env);
        break;
    case PAL_VM_INFO:
        pal_vm_info(env);
        break;
    case PAL_VM_PAGE_SIZE:
        pal_vm_page_size(env);
        break;
    case PAL_VM_TR_READ:
        pal_vm_tr_read(env, ra);
        break;
    case PAL_FREQ_BASE:
        pal_freq_base(env);
        break;
    case PAL_FREQ_RATIOS:
        pal_freq_ratios(env);
        break;
    case PAL_PTCE_INFO:
        pal_ptce_info(env);
        break;
    case PAL_BUS_GET_FEATURES:
        pal_bus_get_features(env);
        break;
    case PAL_BUS_SET_FEATURES:
        pal_bus_set_features(env);
        break;
    case PAL_REGISTER_INFO:
        pal_register_info(env);
        break;
    case PAL_PERF_MON_INFO:
        pal_perf_mon_info(env, ra);
        break;
    case PAL_PLATFORM_ADDR:
        pal_platform_addr(env);
        break;
    case PAL_TEST_PROC:
        pal_test_proc(env);
        break;
    case PAL_DEBUG_INFO:
        pal_debug_info(env);
        break;
    case PAL_FIXED_ADDR:
        pal_fixed_addr(env);
        break;
    case PAL_LOGICAL_TO_PHYSICAL:
        pal_logical_to_physical(env);
        break;
    case PAL_MC_CLEAR_LOG:
        pal_mc_clear_log(env);
        break;
    case PAL_COPY_INFO:
        pal_copy_info(env);
        break;
    case PAL_COPY_PAL:
        pal_copy_pal(env);
        break;
    case PAL_BRAND_INFO:
        pal_brand_info(env, ra);
        break;
    case PAL_HALT_INFO:
        pal_halt_info(env, ra);
        break;
    case PAL_MC_DRAIN:
        pal_mc_drain(env);
        break;
    case PAL_MC_EXPECTED:
        pal_mc_expected(env);
        break;
    case PAL_MC_DYNAMIC_STATE:
        pal_mc_dynamic_state(env);
        break;
    case PAL_MC_ERROR_INFO:
        pal_mc_error_info(env);
        break;
    case PAL_MC_RESUME:
        if (pal_mc_resume(env)) {
            flags |= IA64_PAL_DISPATCH_RESUMED;
        }
        break;
    case PAL_MC_REGISTER_MEM:
        pal_mc_register_mem(env);
        break;
    case PAL_HALT:
        if (pal_halt(env)) {
            flags |= IA64_PAL_DISPATCH_HALTED;
        }
        break;
    case PAL_MEM_FOR_TEST:
        pal_mem_for_test(env);
        break;
    case PAL_PMI_ENTRYPOINT:
        pal_pmi_entrypoint(env);
        break;
    default:
        pal_not_implemented(env);
        break;
    }

    if (flags & IA64_PAL_DISPATCH_RESUMED) {
        return flags;
    }

complete_call:
    for (uint32_t reg = IA64_PAL_GR_STATUS;
         reg <= IA64_PAL_GR_RESULT3; reg++) {
        ia64_gr_nat_set(env, reg, false);
    }

    /*
     * PAL_PROC is a firmware portal, not a normal C function.  Static
     * calls arrive with a plain branch, stacked calls with br.call; the
     * PAL trampoline returns with a plain branch to b0 in both cases.
     * Stacked-convention indices are exactly those with bit 8 set
     * (256-511 and 768-1023, SDM Vol.2 table 11-11); complete such a
     * call's frame here before the trampoline branches back.
     */
    if (index & 0x100) {
        ia64_rse_pop_return_frame(env, env->ar_pfs);
    }

    trace_ia64_pal_return(env_cpu(env)->cpu_index, index,
                          (int64_t)env->gr[IA64_PAL_GR_STATUS],
                          env->gr[IA64_PAL_GR_RESULT1], flags);
    return flags;
}
