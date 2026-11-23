/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qemu/atomic.h"
#include "qemu/accel.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/qemu-print.h"
#include "hw/core/cpu.h"
#include "cpu.h"
#include "exec/cpu-common.h"
#include "exec/tb-flush.h"
#include "exec/target_page.h"
#include "exec/helper-proto.h"
#include "accel/accel-cpu-ops.h"
#include "accel/accel-ops.h"
#include "accel/tcg/probe.h"
#include "llvm.h"
#include "arch/fp.h"
#include "arch/arch.h"
#include "decoder.h"
#if defined(CONFIG_MODULES) || defined(CONFIG_PLUGIN)
#include <gmodule.h>
#endif

#define TYPE_TCG_LLVM_ACCEL ACCEL_CLASS_NAME("tcg-llvm")

struct IA64LLVMRegion {
    unsigned state;
    unsigned hits;
    unsigned tb_generation;
    uint16_t bundles;
    bool stop;
    uint8_t flags;
    uint64_t pc;
    unsigned barrier;
    uint32_t samples;
    IA64LLVMCode code;
    const uint64_t *words;
    IA64LLVMRegion *next_queued;
};

enum { COLD, QUEUED, READY, UNSUPPORTED };
enum { HOT_THRESHOLD = 1024 };
bool ia64_llvm_enabled;
static IA64LLVMCompile compile_region;
static IA64LLVMCompileBatch compile_batch;
static IA64LLVMRegion *pending_head, *pending_tail;
static unsigned pending_count, scheduled_batches, compiler_threads;
static GHashTable *regions;
static unsigned last_tb_flush;
static GThreadPool *pool;
static GMutex region_lock;
static unsigned submissions;
static IA64LLVMCache cache;
static FILE *stats_file;
static unsigned compiling, peak_compiling, compiled;
static int64_t first_compile_us, last_compile_us;
/* Each vCPU writes its own cache line. Statistics are optional. */
uint64_t ia64_llvm_stats[256][8] QEMU_ALIGNED(64);

static const IA64LLVMLayout layout = {
    .gr = offsetof(CPUIA64State, gr),
    .pr = offsetof(CPUIA64State, pr),
    .fr = offsetof(CPUIA64State, fp.fr),
    .fr_sig = offsetof(CPUIA64State, fp.fr_sig),
    .tlb_mask = offsetof(CPUTLBDescFast, mask),
    .tlb_table = offsetof(CPUTLBDescFast, table),
    .read_tag = offsetof(CPUTLBEntry, addr_read),
    .write_tag = offsetof(CPUTLBEntry, addr_write),
    .dirty = offsetof(CPUIA64State, rse.rse_gr_dirty),
    .addend = offsetof(CPUTLBEntry, addend),
    .page_bits = TARGET_PAGE_BITS,
    .entry_bits = CPU_TLB_ENTRY_BITS,
};

static bool cache_has_space(void)
{
    return cache.limit - qatomic_read(&cache.used) >=
           qemu_real_host_page_size();
}

/* The compiler entry points can belong to an explicitly loaded backend. */
static void QEMU_DISABLE_CFI compile_task(gpointer data, gpointer unused)
{
    IA64LLVMRegion *head = data;
    IA64LLVMInput inputs[IA64_LLVM_MAX_BATCH];
    unsigned count = 0, successes = 0;

    g_mutex_lock(&region_lock);
    if (head == pending_head) {
        pending_head = NULL;
    }
    if (stats_file) {
        if (!first_compile_us) {
            first_compile_us = g_get_monotonic_time();
        }
        compiling++;
        peak_compiling = MAX(peak_compiling, compiling);
    }
    g_mutex_unlock(&region_lock);

    for (IA64LLVMRegion *r = head; r; r = r->next_queued) {
        inputs[count++] =
            (IA64LLVMInput) { r->words, r->bundles, r->flags, &r->code };
    }
    bool packed = count > 1 && cache_has_space() &&
                  compile_batch(IA64_LLVM_ABI, inputs, count, &layout, &cache);
    while (head) {
        IA64LLVMRegion *r = head;
        /* A failed region can be reclaimed after we publish its state. */
        head = r->next_queued;
        /* Individual regions may fit when the batch exceeds the cap. */
        bool ok = packed
                      ? r->code.run != NULL
                      : cache_has_space() &&
                            compile_region(IA64_LLVM_ABI, r->words, r->bundles,
                                           &r->code, &layout, r->flags, &cache);
        qatomic_store_release(&r->state, ok ? READY : UNSUPPORTED);
        successes += ok;
    }

    g_mutex_lock(&region_lock);
    scheduled_batches--;
    if (stats_file) {
        compiling--;
        compiled += successes;
        last_compile_us = g_get_monotonic_time();
    }
    g_mutex_unlock(&region_lock);
}

static void tcg_llvm_shutdown(AccelState *accel)
{
    uint64_t total[8] = { 0 };
    GHashTableIter iter;
    gpointer item;

    g_thread_pool_free(pool, true, true);
    if (!stats_file) {
        return;
    }
    for (unsigned c = 0; c < G_N_ELEMENTS(ia64_llvm_stats); c++) {
        for (unsigned i = 0; i < G_N_ELEMENTS(total); i++) {
            total[i] += ia64_llvm_stats[c][i];
        }
    }
    qemu_fprintf(stats_file,
                 "keys %u submitted %u cache-used %" PRIu64
                 " cache-limit %" PRIu64 " compiled %u peak-compiling %u"
                 " compile-span-us %" PRId64 "\n",
                 g_hash_table_size(regions), qatomic_read(&submissions),
                 cache.used, cache.limit, compiled, peak_compiling,
                 last_compile_us - first_compile_us);
    for (unsigned i = 0; i < G_N_ELEMENTS(total); i++) {
        qemu_fprintf(stats_file, "%" PRIu64 "%c", total[i],
                     i == 7 ? '\n' : ' ');
    }
    g_hash_table_iter_init(&iter, regions);
    while (g_hash_table_iter_next(&iter, NULL, &item)) {
        IA64LLVMRegion *r = item;
        if (r->samples) {
            qemu_fprintf(stats_file, "sample %u %u %u 0x%" PRIx64 " %u\n",
                         r->samples, r->bundles, r->code.mixes, r->pc,
                         r->barrier);
        }
    }
    fclose(stats_file);
}

#if defined(CONFIG_MODULES) || defined(CONFIG_PLUGIN)
static typeof(ia64_llvm_compile_v5) *legacy_compile;

static bool QEMU_DISABLE_CFI
compile_legacy(unsigned abi, const uint64_t *words, unsigned bundles,
               IA64LLVMCode *out, const IA64LLVMLayout *layout, unsigned flags,
               IA64LLVMCache *shared)
{
    static IA64LLVMCache legacy_cache;
    legacy_cache.limit = shared->limit;
    bool ok = !(flags & IA64_LLVM_MERCED_DTLB1) &&
              legacy_compile(5, words, bundles, out, layout,
                             flags & IA64_LLVM_FULL_ALAT, &legacy_cache);
    qatomic_set(&shared->used, legacy_cache.used);
    return ok;
}
#endif

static int tcg_llvm_init_machine(AccelState *accel, MachineState *ms)
{
    Object *tcg = OBJECT(accel);
    g_autofree char *library =
        object_property_get_str(tcg, "llvm-library", &error_abort);
    g_autofree char *stats =
        object_property_get_str(tcg, "llvm-stats", &error_abort);
    unsigned threads =
        object_property_get_uint(tcg, "llvm-thread", &error_abort);
    IA64LLVMCompile compiler;
    IA64LLVMCompileBatch batch = NULL;
#if defined(CONFIG_MODULES) || defined(CONFIG_PLUGIN)
    GModule *module = NULL;
#endif

    if (*library) {
#if defined(CONFIG_MODULES) || defined(CONFIG_PLUGIN)
        g_autofree char *path = g_canonicalize_filename(library, NULL);
        module = g_module_open(path, G_MODULE_BIND_LOCAL);
        if (!module) {
            error_report("IA-64 LLVM initialization failed: %s",
                         g_module_error());
            return -ENOENT;
        }
        if (!g_module_symbol(module, IA64_LLVM_COMPILE_SYMBOL,
                             (gpointer *)&compiler)) {
            if (!g_module_symbol(module, "ia64_llvm_compile_v5",
                                 (gpointer *)&legacy_compile)) {
                error_report(
                    "IA-64 LLVM backend provides no compatible compiler");
                g_module_close(module);
                return -EINVAL;
            }
            if (threads != 1) {
                error_report("IA-64 LLVM backend ABI 5 requires llvm-thread=1");
                g_module_close(module);
                return -EINVAL;
            }
            compiler = compile_legacy;
        } else {
            g_module_symbol(module, IA64_LLVM_COMPILE_BATCH_SYMBOL,
                            (gpointer *)&batch);
        }
#else
        error_report(
            "Loading an external IA-64 LLVM backend requires GModule support");
        return -ENOSYS;
#endif
    } else {
#ifdef CONFIG_IA64_LLVM_BUILTIN
        compiler = ia64_llvm_compile_v6;
        batch = ia64_llvm_compile_batch_v6;
#else
        error_report("This QEMU build has no built-in IA-64 LLVM backend");
        return -ENOSYS;
#endif
    }
    if (*stats && !(stats_file = fopen(stats, "w"))) {
        int err = errno;
        error_report("Cannot open LLVM statistics file '%s': %s", stats,
                     strerror(err));
#if defined(CONFIG_MODULES) || defined(CONFIG_PLUGIN)
        if (module) {
            g_module_close(module);
        }
#endif
        return -err;
    }
#if defined(CONFIG_MODULES) || defined(CONFIG_PLUGIN)
    if (module) {
        g_module_make_resident(module);
        g_module_close(module);
    }
#endif
    compile_region = compiler;
    compile_batch = batch;
    compiler_threads = threads;
    cache.limit = object_property_get_uint(tcg, "cachesize", &error_abort);
    regions = g_hash_table_new_full(g_bytes_hash, g_bytes_equal,
                                    (GDestroyNotify)g_bytes_unref, g_free);
    pool = g_thread_pool_new(compile_task, NULL, (int)threads, false, NULL);
    ia64_llvm_enabled = true;
    return accel_find("tcg")->init_machine(accel, ms);
}

static void tcg_llvm_class_init(ObjectClass *oc, const void *data)
{
    AccelClass *ac = ACCEL_CLASS(oc);
    ac->name = "tcg-llvm";
    ac->init_machine = tcg_llvm_init_machine;
    ac->shutdown = tcg_llvm_shutdown;
}

static const TypeInfo tcg_llvm_types[] = {
    {
        .name = TYPE_TCG_LLVM_ACCEL,
        .parent = ACCEL_CLASS_NAME("tcg"),
        .class_init = tcg_llvm_class_init,
    },
    {
        .name = ACCEL_OPS_NAME("tcg-llvm"),
        .parent = ACCEL_OPS_NAME("tcg"),
        .abstract = true,
    },
};
DEFINE_TYPES(tcg_llvm_types)

static gboolean remove_stale_region(gpointer key, gpointer value,
                                    gpointer previous_generation)
{
    IA64LLVMRegion *r = value;
    unsigned state = qatomic_load_acquire(&r->state);

    /* Keep recent hit counts across retranslations and keep all JIT code. */
    return r->tb_generation != GPOINTER_TO_UINT(previous_generation) &&
           (state == COLD || state == UNSUPPORTED);
}

IA64LLVMRegion *ia64_llvm_register(const uint64_t *words, unsigned bundles,
                                   bool stop, uint64_t pc, unsigned barrier,
                                   uint8_t flags)
{
    size_t len = (size_t)bundles * 16;
    uint8_t *bytes = g_malloc(len + 1);
    IA64LLVMRegion *r;
    GBytes *key;
    unsigned generation;

    memcpy(bytes, words, len);
    bytes[len] = flags;
    key = g_bytes_new_take(bytes, len + 1);
    g_mutex_lock(&region_lock);
    generation = tcg_tb_flush_count();
    if (generation != last_tb_flush) {
        /* Only a full TB flush retires every embedded region pointer. */
        g_hash_table_foreach_remove(regions, remove_stale_region,
                                    GUINT_TO_POINTER(generation - 1));
        last_tb_flush = generation;
    }
    r = g_hash_table_lookup(regions, key);
    if (!r && cache_has_space()) {
        r = g_new0(IA64LLVMRegion, 1);
        r->bundles = bundles;
        r->stop = stop;
        r->flags = flags;
        if (stats_file) {
            r->pc = pc;
            r->barrier = barrier;
        }
        r->words = (const uint64_t *)bytes; /* Owned by the table key. */
        g_hash_table_insert(regions, g_bytes_ref(key), r);
    }
    if (r) {
        r->tb_generation = generation;
    }
    g_mutex_unlock(&region_lock);
    g_bytes_unref(key);
    return r;
}

static void count_hit(IA64LLVMRegion *r)
{
    if (qatomic_fetch_inc(&r->hits) != HOT_THRESHOLD - 1 ||
        qatomic_cmpxchg(&r->state, COLD, QUEUED) != COLD) {
        return;
    }
    if (!cache_has_space()) {
        qatomic_set(&r->state, UNSUPPORTED);
        return;
    }
    if (stats_file) {
        qatomic_inc(&submissions);
    }
    g_mutex_lock(&region_lock);
    if (compile_batch && pending_head &&
        scheduled_batches >= compiler_threads &&
        pending_count < IA64_LLVM_MAX_BATCH) {
        pending_tail->next_queued = r;
        pending_tail = r;
        pending_count++;
    } else {
        pending_head = pending_tail = r;
        pending_count = 1;
        scheduled_batches++;
        g_thread_pool_push(pool, r, NULL);
    }
    g_mutex_unlock(&region_lock);
}

static void *read_page(void *opaque, uint64_t addr, uint32_t mmu)
{
    CPUTLBEntryFull *full;
    bool victim;
    void *host;
    if (!tlb_lookup_full_no_fill(opaque, addr, MMU_DATA_LOAD, (int)mmu, &full,
                                 &victim, &host) ||
        !host || full->lg_page_size < TARGET_PAGE_BITS) {
        return NULL;
    }
    return (void *)((uintptr_t)host - (addr & ~TARGET_PAGE_MASK));
}

static uint64_t read_sig(void *opaque, uint32_t reg)
{
    return ia64_fp_getf(opaque, reg, 2);
}

static void dtlb1_touch(void *opaque, uint64_t addr, uint32_t size)
{
    helper_merced_dtlb1_touch(opaque, addr, size);
}

static bool fp_inputs_ready(CPUIA64State *env, const uint64_t fr_inputs[2])
{
    bool low = fr_inputs[0] & UINT32_MAX;
    bool high = (fr_inputs[0] >> 32) || fr_inputs[1];
    return !(fr_inputs[0] & env->fp.fr_nat[0]) &&
           !(fr_inputs[1] & env->fp.fr_nat[1]) &&
           !(low && (env->psr & IA64_PSR_DFL)) &&
           !(high && ((env->psr & IA64_PSR_DFH) || env->cfm_rrb_fr ||
                      env->fp.rotating_fr_materialized_rrb));
}

static void observed_store(void *opaque, void *host, uint64_t addr,
                           uint64_t value, unsigned size)
{
    CPUIA64State *env = opaque;
    ia64_alat_write_begin(env);
    switch (size) {
    case 1:
        qatomic_store_release((uint8_t *)host, (uint8_t)value);
        break;
    case 2:
        qatomic_store_release((uint16_t *)host, (uint16_t)value);
        break;
    case 4:
        qatomic_store_release((uint32_t *)host, (uint32_t)value);
        break;
    case 8:
        qatomic_store_release((uint64_t *)host, value);
        break;
    default:
        g_assert_not_reached();
    }
    ia64_alat_write_end(env, addr, size);
}

static const IA64LLVMAccess accessors = {
    .read_page = read_page,
    .read_sig = read_sig,
    .observed_store = observed_store,
    .dtlb1_touch = dtlb1_touch,
};

static bool region_inputs_ready(CPUIA64State *env, IA64LLVMRegion *r,
                                bool tb_entry)
{
    const uint64_t psr = IA64_PSR_IC | IA64_PSR_BE | IA64_PSR_DB | IA64_PSR_SS |
                         IA64_PSR_TB | IA64_PSR_IS |
                         IA64_PSR_FAULT_SUPPRESS_MASK;
    if (!tb_entry &&
        ((env->psr & psr) != IA64_PSR_IC || env->nat[0] || env->nat[1])) {
        return false;
    }
    return env->cfm_sof >= r->code.sof && !env->cfm_rrb_gr &&
           !env->cfm_rrb_pr &&
           !!(r->flags & IA64_LLVM_FULL_ALAT) == env->alat_state.alat_full &&
           (!(r->code.fr_inputs[0] | r->code.fr_inputs[1]) ||
            fp_inputs_ready(env, r->code.fr_inputs));
}

/* JIT entry points are created after the host binary's CFI tables are linked. */
static uint32_t QEMU_DISABLE_CFI run_native(IA64LLVMRun run, CPUIA64State *env,
                                            uint32_t mmu)
{
    return run(env, mmu, &accessors, cpu_tlb_fast(env_cpu(env), (int)mmu));
}

static inline QEMU_ALWAYS_INLINE uint32_t run_region(CPUIA64State *env,
                                                     IA64LLVMRegion *r,
                                                     uint64_t pc, uint32_t mmu,
                                                     bool fixed, bool tb_entry)
{
    CPUState *cpu = env_cpu(env);
    unsigned state = qatomic_load_acquire(&r->state);
    uint64_t *s =
        stats_file && (unsigned)cpu->cpu_index < G_N_ELEMENTS(ia64_llvm_stats)
            ? ia64_llvm_stats[cpu->cpu_index]
            : NULL;
    if (state != READY) {
        if (state == COLD) {
            count_hit(r);
        }
        if (s) {
            s[state == UNSUPPORTED ? 7 : 6]++;
        }
        return 0;
    }
    if (s) {
        s[0]++;
    }
    if (cpu_loop_exit_requested(cpu) ||
        (!fixed && !region_inputs_ready(env, r, tb_entry))) {
        if (s) {
            s[5]++;
        }
        return 0;
    }
    unsigned result = run_native(r->code.run, env, mmu);
    if (result != IA64_LLVM_COMPLETE) {
        if (s) {
            s[4]++;
        }
        if (result >= IA64_LLVM_RESUME) {
            /* Earlier memory accesses retired. Restart at the precise slot. */
            uint64_t index = result - IA64_LLVM_RESUME;
            unsigned slot = index % 3;
            uint64_t prev = index - 1;
            uint8_t tmpl;

            g_assert(index > 0 &&
                     index < (uint64_t)IA64_BUNDLE_SLOTS * r->bundles);
            tmpl = ia64_bundle_template_code(r->words[2 * (prev / 3)]);
            env->ip = pc + (index / 3) * 16;
            env->psr = (env->psr & ~IA64_PSR_RI_MASK) |
                       ((uint64_t)slot << IA64_PSR_RI_SHIFT);
            env->instruction_group_start =
                ia64_template_info(tmpl)->stop_after[prev % 3];
            env->last_successful_bundle = pc + (prev / 3) * 16;
            env->exception_state.fault_slot = slot;
            cpu_loop_exit(cpu);
        }
        return 0;
    }
    if (!fixed) {
        env->rse.rse_gr_dirty[0] |= r->code.dirty[0];
        env->rse.rse_gr_dirty[1] |= r->code.dirty[1];
    }
    env->ip = pc + UINT64_C(16) * r->bundles;
    env->last_successful_bundle = env->ip - 16;
    /* A complete region preserves an established RI of zero. */
    if (!tb_entry) {
        env->psr &= ~IA64_PSR_RI_MASK;
    }
    env->instruction_group_start = r->stop;
    env->exception_state.fault_slot = 0;
    if (s) {
        s[1]++;
        if (!(s[1] & 8191)) {
            qatomic_inc(&r->samples);
        }
        s[2] += r->bundles;
        s[3] += r->code.mixes;
    }
    return 1;
}

uint32_t helper_llvm_region(CPUIA64State *env, void *opaque, uint64_t pc,
                            uint32_t mmu, uint32_t tb_entry)
{
    return run_region(env, opaque, pc, mmu, false, tb_entry);
}

uint32_t helper_llvm_region_fixed(CPUIA64State *env, void *opaque, uint64_t pc,
                                  uint32_t mmu)
{
    return run_region(env, opaque, pc, mmu, true, true);
}
