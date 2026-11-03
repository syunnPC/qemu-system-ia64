/* IA-64 data-endian and execution-memory access primitives. */

#include "qemu/osdep.h"
#include "qemu/atomic128.h"
#include "cpu.h"
#include "arch/arch.h"
#include "exec-access.h"
#include "exec/cpu-common.h"

MemOp ia64_memop_for_opcode(Ia64Opcode opcode)
{
    switch (opcode) {
    case IA64_OP_LD1:
    case IA64_OP_LD1S:
    case IA64_OP_LD1A:
    case IA64_OP_LD1SA:
    case IA64_OP_LD1C_CLR:
    case IA64_OP_LD1C_NC:
    case IA64_OP_ST1:
    case IA64_OP_ST1REL:
    case IA64_OP_XCHG1:
    case IA64_OP_CMPXCHG1:
        return MO_UB;
    case IA64_OP_LD2:
    case IA64_OP_LD2S:
    case IA64_OP_LD2A:
    case IA64_OP_LD2SA:
    case IA64_OP_LD2C_CLR:
    case IA64_OP_LD2C_NC:
    case IA64_OP_ST2:
    case IA64_OP_ST2REL:
    case IA64_OP_XCHG2:
    case IA64_OP_CMPXCHG2:
        return MO_LEUW;
    case IA64_OP_LD4:
    case IA64_OP_LD4S:
    case IA64_OP_LD4A:
    case IA64_OP_LD4SA:
    case IA64_OP_LD4C_CLR:
    case IA64_OP_LD4C_NC:
    case IA64_OP_ST4:
    case IA64_OP_ST4REL:
    case IA64_OP_XCHG4:
    case IA64_OP_CMPXCHG4:
    case IA64_OP_FETCHADD4:
        return MO_LEUL;
    case IA64_OP_LD8:
    case IA64_OP_LD8S:
    case IA64_OP_LD8A:
    case IA64_OP_LD8SA:
    case IA64_OP_LD8FILL:
    case IA64_OP_LD8C_CLR:
    case IA64_OP_LD8C_NC:
    case IA64_OP_ST8:
    case IA64_OP_ST8REL:
    case IA64_OP_ST8SPILL:
    case IA64_OP_XCHG8:
    case IA64_OP_CMPXCHG8:
    case IA64_OP_FETCHADD8:
        return MO_LEUQ;
    default:
        break;
    }
    g_assert_not_reached();
}

uint32_t ia64_memop_size(MemOp memop)
{
    return 1u << (memop & MO_SIZE);
}

bool ia64_data_big_endian(CPUIA64State *env)
{
    return (env->psr & IA64_PSR_BE) != 0;
}

MemOp ia64_runtime_data_memop(CPUIA64State *env, MemOp memop)
{
    if ((memop & MO_SIZE) == MO_8) {
        return memop & ~MO_BSWAP;
    }
    return (memop & ~MO_BSWAP) | (ia64_data_big_endian(env) ? MO_BE : MO_LE);
}

uint32_t ia64_lduw_data_ra(CPUIA64State *env, uint64_t addr, uintptr_t ra)
{
    return ia64_exec_load_data(env, addr, 2, ia64_data_big_endian(env), ra);
}

uint32_t ia64_ldl_data_ra(CPUIA64State *env, uint64_t addr, uintptr_t ra)
{
    return ia64_exec_load_data(env, addr, 4, ia64_data_big_endian(env), ra);
}

uint64_t ia64_ldq_data_ra(CPUIA64State *env, uint64_t addr, uintptr_t ra)
{
    return ia64_exec_load_data(env, addr, 8, ia64_data_big_endian(env), ra);
}

void ia64_stq_data_ra(CPUIA64State *env, uint64_t addr, uint64_t val,
                      uintptr_t ra)
{
    ia64_exec_store_data(env, addr, val, 8, ia64_data_big_endian(env), ra);
}

static bool ia64_cmpxchg_compare_representable(uint64_t cmp, uint32_t size)
{
    switch (size) {
    case 1:
        return cmp <= UINT8_MAX;
    case 2:
        return cmp <= UINT16_MAX;
    case 4:
        return cmp <= UINT32_MAX;
    case 8:
        return true;
    default:
        g_assert_not_reached();
    }
}

uint64_t ia64_memory_cmpxchg(CPUIA64State *env, uint64_t addr, uint64_t cmp,
                             uint64_t val, uint32_t size, uintptr_t ra)
{
    int mmu_idx = ia64_exec_mmu_index(env, false);
    uint64_t old;
    bool cmp_representable = ia64_cmpxchg_compare_representable(cmp, size);

    switch (size) {
    case 1:
        if (cmp_representable) {
            old = ia64_exec_cmpxchg(env, addr, cmp, val, 1, false,
                                    make_memop_idx(MO_UB, mmu_idx), ra);
        } else {
            old = ia64_exec_load_data(env, addr, 1, false, ra);
        }
        return old;
    case 2:
        if (cmp_representable) {
            MemOpIdx oi =
                make_memop_idx(ia64_runtime_data_memop(env, MO_LEUW), mmu_idx);

            old = ia64_exec_cmpxchg(env, addr, cmp, val, 2,
                                    ia64_data_big_endian(env), oi, ra);
        } else {
            old = ia64_lduw_data_ra(env, addr, ra);
        }
        return old;
    case 4:
        if (cmp_representable) {
            MemOpIdx oi =
                make_memop_idx(ia64_runtime_data_memop(env, MO_LEUL), mmu_idx);

            old = ia64_exec_cmpxchg(env, addr, cmp, val, 4,
                                    ia64_data_big_endian(env), oi, ra);
        } else {
            old = ia64_ldl_data_ra(env, addr, ra);
        }
        return old;
    case 8: {
        MemOpIdx oi =
            make_memop_idx(ia64_runtime_data_memop(env, MO_LEUQ), mmu_idx);

        old = ia64_exec_cmpxchg(env, addr, cmp, val, 8,
                                ia64_data_big_endian(env), oi, ra);
        return old;
    }
    default:
        g_assert_not_reached();
    }
}

uint64_t ia64_memory_cmp8xchg16(CPUIA64State *env, uint64_t addr, uint64_t cmp,
                                uint64_t val, uint64_t csd, uintptr_t ra)
{
    uint64_t base = addr & ~8ULL;
    uint64_t old;
    int mmu_idx = ia64_exec_mmu_index(env, false);

    ia64_exec_probe_write(env, base, 16, mmu_idx, ra);
    if (ia64_exec_is_parallel(env)) {
#if HAVE_CMPXCHG128
        bool big_endian = ia64_data_big_endian(env);
        MemOpIdx oi = make_memop_idx(
            ia64_runtime_data_memop(env, MO_UO | MO_ALIGN_16), mmu_idx);
        Int128 desired =
            big_endian ? int128_make128(csd, val) : int128_make128(val, csd);
        Int128 observed = ia64_exec_load_16(env, base, oi, ra);

        for (;;) {
            Int128 expected = observed;

            if (big_endian) {
                old = (addr & 8) ? int128_getlo(expected)
                                 : int128_gethi(expected);
            } else {
                old = (addr & 8) ? int128_gethi(expected)
                                 : int128_getlo(expected);
            }
            if (old != cmp) {
                return old;
            }
            observed = ia64_exec_cmpxchg_16(env, base, expected, desired,
                                            big_endian, 16, oi, ra);
            if (int128_eq(observed, expected)) {
                return old;
            }
        }
#else
        ia64_exec_exit_atomic(env, ra);
#endif
    }

    old = ia64_ldq_data_ra(env, addr, ra);
    if (old == cmp) {
        ia64_stq_data_ra(env, base, val, ra);
        ia64_stq_data_ra(env, base + 8, csd, ra);
    }
    return old;
}
