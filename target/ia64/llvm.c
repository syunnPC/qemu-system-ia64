/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qemu/atomic.h"
#include "qemu/error-report.h"
#include "hw/core/cpu.h"
#include "cpu.h"
#include "exec/cpu-common.h"
#include "exec/target_page.h"
#include "exec/helper-proto.h"
#include "accel/tcg/probe.h"
#include "llvm.h"
#include "arch/fp.h"
#if defined(CONFIG_MODULES) || defined(CONFIG_PLUGIN)
#include <gmodule.h>
#endif

struct IA64LLVMRegion {
    unsigned state;
    uint16_t bundles;
    bool stop;
    uint64_t pc;
    unsigned barrier;
    uint32_t samples;
    IA64LLVMCode code;
    uint64_t words[];
};

enum { COLD, QUEUED, READY, UNSUPPORTED };
enum { HOT_THRESHOLD = 1024, MAX_KEYS = 32768, MAX_COMPILES = 2048 };
bool ia64_llvm_enabled;
static IA64LLVMCompile compile_region;
static GHashTable *regions;
static GThreadPool *pool;
static GMutex region_lock;
static unsigned submissions;
static const char *stats_file;
/* Each vCPU writes its own cache line. Statistics are optional. */
uint64_t ia64_llvm_stats[256][8] QEMU_ALIGNED(64);

static void compile_task(gpointer data, gpointer unused)
{
    IA64LLVMRegion *r = data;
    bool ok = compile_region(IA64_LLVM_ABI, r->words, r->bundles, &r->code);
    qatomic_store_release(&r->state, ok ? READY : UNSUPPORTED);
}
static void finish(void)
{
    g_thread_pool_free(pool, true, true);
    if (stats_file) {
        FILE *f = fopen(stats_file, "w");
        if (f) {
            uint64_t total[8] = { 0 };
            for (unsigned c = 0; c < 256; c++) {
                for (unsigned i = 0; i < G_N_ELEMENTS(total); i++) {
                    total[i] += ia64_llvm_stats[c][i];
                }
            }
            fprintf(f, "keys %u submitted %u\n", g_hash_table_size(regions),
                    submissions);
            for (unsigned i = 0; i < G_N_ELEMENTS(total); i++) {
                fprintf(f, "%" PRIu64 "%c", total[i], i == 7 ? '\n' : ' ');
            }
            GHashTableIter iter;
            gpointer item;
            g_hash_table_iter_init(&iter, regions);
            while (g_hash_table_iter_next(&iter, NULL, &item)) {
                IA64LLVMRegion *r = item;
                if (r->samples) {
                    fprintf(f, "sample %u %u %u 0x%" PRIx64 " %u\n", r->samples,
                            r->bundles, r->code.mixes, r->pc, r->barrier);
                }
            }
            fclose(f);
        }
    }
}

void ia64_llvm_init(void)
{
    const char *name = getenv("IA64_LLVM_LIBRARY");
    if (!name || ia64_llvm_enabled) {
        return;
    }
#if defined(CONFIG_MODULES) || defined(CONFIG_PLUGIN)
    GModule *m = g_module_open(name, G_MODULE_BIND_LOCAL);
    if (!m ||
        !g_module_symbol(m, "ia64_llvm_compile", (gpointer *)&compile_region)) {
        error_report("IA-64 LLVM initialization failed: %s", g_module_error());
        exit(EXIT_FAILURE);
    }
    g_module_make_resident(m);
    g_module_close(m);
#else
    error_report("IA-64 LLVM requires a QEMU build with GModule support");
    exit(EXIT_FAILURE);
#endif
    regions = g_hash_table_new_full(g_bytes_hash, g_bytes_equal,
                                    (GDestroyNotify)g_bytes_unref, g_free);
    pool = g_thread_pool_new(compile_task, NULL, 1, false, NULL);
    stats_file = getenv("IA64_LLVM_STATS");
    atexit(finish);
    ia64_llvm_enabled = true;
}

IA64LLVMRegion *ia64_llvm_register(const uint64_t *words, unsigned bundles,
                                   bool stop, uint64_t pc, unsigned barrier)
{
    size_t len = bundles * 16;
    GBytes *key = g_bytes_new(words, len);
    g_mutex_lock(&region_lock);
    IA64LLVMRegion *r = g_hash_table_lookup(regions, key);
    if (!r && g_hash_table_size(regions) < MAX_KEYS) {
        r = g_malloc0(sizeof(*r) + len);
        r->bundles = bundles;
        r->stop = stop;
        r->pc = pc;
        r->barrier = barrier;
        memcpy(r->words, words, len);
        g_hash_table_insert(regions, g_bytes_ref(key), r);
    }
    g_mutex_unlock(&region_lock);
    g_bytes_unref(key);
    return r;
}

static void count_hit(IA64LLVMRegion *r)
{
    typedef struct {
        IA64LLVMRegion *r;
        unsigned n;
    } Bucket;
    static __thread Bucket counter[2048];
    Bucket *p = &counter[((uintptr_t)r >> 5) % G_N_ELEMENTS(counter)];
    if (p->r != r) {
        p->r = r;
        p->n = 0;
    }
    if (++p->n == HOT_THRESHOLD &&
        qatomic_cmpxchg(&r->state, COLD, QUEUED) == COLD) {
        g_mutex_lock(&region_lock);
        if (submissions < MAX_COMPILES) {
            submissions++;
            g_thread_pool_push(pool, r, NULL);
        } else {
            qatomic_set(&r->state, UNSUPPORTED);
        }
        g_mutex_unlock(&region_lock);
    }
}

static void *read_page(void *opaque, uint64_t addr, uint32_t mmu)
{
    CPUTLBEntryFull *full;
    bool victim;
    void *host;
    if (!tlb_lookup_full_no_fill(opaque, addr, MMU_DATA_LOAD, mmu, &full,
                                 &victim, &host) ||
        !host || full->lg_page_size < TARGET_PAGE_BITS) {
        return NULL;
    }
    return (void *)((uintptr_t)host - (addr & ~TARGET_PAGE_MASK));
}

static uint64_t read_sig(void *opaque, uint32_t reg)
{
    CPUIA64State *env = opaque;
    if (env->fp.fr_sig[reg / 64] & (UINT64_C(1) << (reg % 64))) {
        return env->fp.fr[reg];
    }
    return ia64_fp_getf(env, reg, 2);
}

static bool fp_inputs_ready(CPUIA64State *env, const uint64_t fr[2])
{
    bool low = fr[0] & UINT32_MAX;
    bool high = (fr[0] >> 32) || fr[1];
    return !(fr[0] & env->fp.fr_nat[0]) && !(fr[1] & env->fp.fr_nat[1]) &&
           !(low && (env->psr & IA64_PSR_DFL)) &&
           !(high && ((env->psr & IA64_PSR_DFH) || env->cfm_rrb_fr ||
                      env->fp.rotating_fr_materialized_rrb));
}

static const IA64LLVMAccess accessors = { read_page, read_sig };

uint32_t helper_llvm_region(CPUIA64State *env, void *opaque, uint64_t pc,
                            uint32_t mmu)
{
    IA64LLVMRegion *r = opaque;
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
    uint64_t psr = IA64_PSR_IC | IA64_PSR_BE | IA64_PSR_DB | IA64_PSR_SS |
                   IA64_PSR_TB | IA64_PSR_IS | IA64_PSR_FAULT_SUPPRESS_MASK;
    if (env->cfm_sof < r->code.sof || env->cfm_rrb_gr || env->cfm_rrb_pr ||
        (env->psr & psr) != IA64_PSR_IC || env->nat[0] || env->nat[1] ||
        cpu_loop_exit_requested(cpu)) {
        if (s) {
            s[5]++;
        }
        return 0;
    }
    if ((r->code.fr[0] || r->code.fr[1]) && !fp_inputs_ready(env, r->code.fr)) {
        if (s) {
            s[5]++;
        }
        return 0;
    }
    if (!r->code.run(env->gr, env->pr, env, mmu, &accessors)) {
        if (s) {
            s[4]++;
        }
        return 0;
    }
    env->rse.rse_gr_dirty[0] |= r->code.dirty[0];
    env->rse.rse_gr_dirty[1] |= r->code.dirty[1];
    env->ip = pc + 16 * r->bundles;
    env->last_successful_bundle = env->ip - 16;
    env->psr &= ~IA64_PSR_RI_MASK;
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
