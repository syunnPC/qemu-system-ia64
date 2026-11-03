/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "ia64-test.h"

#define TEST_SAL_SET_VECTORS           0x01000000ULL
#define TEST_SAL_VECTOR_BOOT_RENDEZ     2U
#define TEST_SAL_SUCCESS                0ULL
#define TEST_LOCAL_SAPIC_BASE           0x00000000fee00000ULL
#define TEST_AP_WAKE_VECTOR             0xffU
#define TEST_PROCESSOR_COUNT             4U
#define TEST_RENDEZVOUS_ROUNDS           8U
#define TEST_ATOMIC_INCREMENTS          128U
#define TEST_SEMAPHORE_INCREMENTS      4096U
#define TEST_GUARDED_INCREMENTS       30000U
#define TEST_BIT_LOCK_INCREMENTS      30000U
#define TEST_SLOW_LOCK_INCREMENTS      4096U
#define TEST_RSE_PRESSURE_CALLS           8U
#define TEST_TRANSLATED_LOCK_INCREMENTS 4095U
#define TEST_HIGH_TRANSLATED_LOCK_INCREMENTS 4095U
#define TEST_PACKED_PFN_INCREMENTS          8192U
#define TEST_QUEUED_INCREMENTS        20000U
#define TEST_WAIT_TICKS        1000000000ULL
#define TEST_RETURN_TICKS        10000000ULL
#define TEST_ATOMIC_HIGH 0xa5a55a5af00f0ff0ULL
#define TEST_TRANSLATION_VA 0x2000010005000000ULL
#define TEST_TRANSLATION_ITIR (13ULL << 2)
#define TEST_TRANSLATION_4K_ITIR (12ULL << 2)
#define TEST_TRANSLATION_PTE_FLAGS 0x661ULL
#define TEST_TRANSLATION_VALUE_A 0x1122334455667788ULL
#define TEST_TRANSLATION_VALUE_B 0x8877665544332211ULL
#define TEST_TRANSLATION_VALUE_A_HIGH 0xa1a2a3a4a5a6a7a8ULL
#define TEST_TRANSLATION_VALUE_B_HIGH 0xb1b2b3b4b5b6b7b8ULL
#define TEST_TRANSLATION_HIGH_OFFSET 4096ULL
#define TEST_TRANSLATION_LOCAL_COMMAND  0U
#define TEST_TRANSLATION_GLOBAL_COMMAND 1U
#define TEST_TRANSLATION_GLOBAL_HIGH_COMMAND 2U
#define TEST_GLOBAL_PURGE_ROUND (TEST_RENDEZVOUS_ROUNDS + 1U)
#define TEST_GLOBAL_HIGH_PURGE_ROUND (TEST_RENDEZVOUS_ROUNDS + 2U)
#define TEST_TRANSLATION_RID_A 0x0003fffeULL
#define TEST_TRANSLATION_RID_B 0x0003fffdULL
#define TEST_RID_SWITCH_ROUNDS 4096U
#define TEST_TLB_CHURN_BASE 0xe000020001000000ULL
#define TEST_TLB_CHURN_ENTRIES 192U
#define TEST_TLB_CHURN_ROUNDS 8U
#define TEST_TRANSLATED_LOCK_VA 0xe000020003000000ULL
#define TEST_HIGH_TRANSLATED_LOCK_VA  0xe00001260268f838ULL
#define TEST_HIGH_TRANSLATED_COUNT_VA 0xe00001260268f84cULL
#define TEST_HIGH_TRANSLATED_LOCK_PA  0x000000010368f838ULL
#define TEST_HIGH_TRANSLATED_COUNT_PA 0x000000010368f84cULL
#define TEST_HIGH_TRANSLATION_ITIR (24ULL << 2)
#define TEST_TRANSLATION_WRITE_VA_BASE 0xe000020005000000ULL
#define TEST_TRANSLATION_WRITE_OFFSET 0x84cULL
#define TEST_SEQUENTIAL_DATA_VA 0xe000020007000000ULL
#define TEST_SEQUENTIAL_SYNC_VA 0xe000020007002000ULL
#define TEST_SEQUENTIAL_ITERATIONS 50000U
#define TEST_ASSISTED_SEQUENTIAL_ITERATIONS 8192U
#define TEST_SEQUENTIAL_ALIGNED_PHASE 0U
#define TEST_SEQUENTIAL_ASSISTED_PHASE 1U
#define TEST_SEQUENTIAL_PHASES 2U
#define TEST_SEQUENTIAL_ASSIST_OFFSET 4U
#define TEST_TRANSLATION_PTE_FLAGS_UC  0x671ULL
#define TEST_TRANSLATION_PTE_FLAGS_UCE 0x675ULL
#define TEST_CPUID_FEATURES_REGISTER 4ULL
#define TEST_CPUID4_AO (1ULL << 2)

typedef struct {
    UINT64 Status;
    UINT64 Value0;
    UINT64 Value1;
    UINT64 Value2;
} TEST_SAL_RETURN;

typedef TEST_SAL_RETURN (*TEST_SAL_PROC)(UINT64, UINT64, UINT64, UINT64, UINT64,
                                         UINT64, UINT64, UINT64);

static UINT8 sal_guid[16] = IA64_GUID_SAL;
/* Each AP updates its own slot; the BSP observes the slots after an mf. */
static volatile UINT64 ap_rendezvous_count[TEST_PROCESSOR_COUNT];
/* All processors contend on this naturally aligned semaphore datum. */
static struct {
    volatile UINT64 Low;
    volatile UINT64 High;
} atomic_pair __attribute__((aligned(16)));
static volatile UINT64 guarded_lock;
static volatile UINT16 guarded_counter;
static volatile UINT32 bit_lock;
static volatile UINT16 bit_lock_counter;
static volatile UINT64 slow_lock;
static volatile UINT16 slow_lock_counter;
static volatile UINT64 slow_path_calls[TEST_PROCESSOR_COUNT];
static volatile UINT64 rse_pressure_sink[TEST_PROCESSOR_COUNT];
static volatile UINT64 slow_lock_ready[TEST_PROCESSOR_COUNT];
static volatile UINT64 slow_lock_waiting[TEST_PROCESSOR_COUNT];
typedef struct {
    volatile UINT32 Lock;
    volatile UINT16 Counter;
    UINT16 Reserved;
    volatile UINT64 Lock8;
    volatile UINT16 Counter8;
    UINT8 Padding[8192 - sizeof(UINT32) - 2 * sizeof(UINT16) - sizeof(UINT64) -
                  sizeof(UINT16)];
} TEST_TRANSLATED_LOCK_PAGE;
static TEST_TRANSLATED_LOCK_PAGE translated_lock_pages[2]
    __attribute__((aligned(8192)));
static volatile UINT32 fetchadd4_counter;
static volatile UINT64 fetchadd8_counter;
static volatile UINT32 cmpxchg4_acq_counter;
static volatile UINT32 cmpxchg4_rel_counter;
static volatile UINT64 cmpxchg8_acq_counter;
static volatile UINT64 cmpxchg8_rel_counter;
typedef union {
    volatile UINT64 Whole;
    struct {
        volatile UINT32 Flags;
        volatile UINT16 Count;
        volatile UINT16 Type;
    } Fields;
} TEST_PACKED_PFN_WORD;
static TEST_PACKED_PFN_WORD packed_pfn_word __attribute__((aligned(8)));
typedef struct {
    volatile UINT64 Next;
    volatile UINT64 Waiting;
    UINT8 Padding[48];
} TEST_QUEUED_LOCK_NODE;
static volatile UINT64 queued_lock_tail;
static volatile UINT16 queued_counter;
static TEST_QUEUED_LOCK_NODE queued_lock_nodes[TEST_PROCESSOR_COUNT]
    __attribute__((aligned(64)));
static volatile UINT64 translation_page_a[1024] __attribute__((aligned(8192)));
static volatile UINT64 translation_page_b[1024] __attribute__((aligned(8192)));
static volatile UINT64 translation_round;
static volatile UINT64 translation_mismatch[TEST_PROCESSOR_COUNT];
static volatile UINT64 translation_churn_mismatch[TEST_PROCESSOR_COUNT];
static volatile UINT64 translation_write_mismatch[TEST_PROCESSOR_COUNT];
static volatile UINT64 translation_command;
static volatile UINT64 global_translation_release;
static volatile UINT64 global_translation_ready[TEST_PROCESSOR_COUNT];
static volatile UINT64 global_translation_mismatch[TEST_PROCESSOR_COUNT];
static volatile UINT64 global_translation_probe[TEST_PROCESSOR_COUNT];
static volatile UINT64 translation_write_pages[TEST_PROCESSOR_COUNT][2][1024]
    __attribute__((aligned(8192)));
typedef volatile UINT64 TEST_SHARED_UINT64; /* Shared by independent vCPUs. */
typedef struct {
    TEST_SHARED_UINT64 Value0;
    UINT8 SeparateCacheLines[120];
    TEST_SHARED_UINT64 Value1;
    UINT8 Padding[8192 - 128 - sizeof(UINT64)];
} TEST_SEQUENTIAL_DATA_PAGE;
typedef struct {
    TEST_SHARED_UINT64 Mapped[TEST_PROCESSOR_COUNT];
    TEST_SHARED_UINT64 Ready[TEST_PROCESSOR_COUNT];
    TEST_SHARED_UINT64 Result[TEST_PROCESSOR_COUNT];
    TEST_SHARED_UINT64 Done[TEST_PROCESSOR_COUNT];
    TEST_SHARED_UINT64 Finish[2];
    TEST_SHARED_UINT64 PhaseDone[TEST_PROCESSOR_COUNT];
    TEST_SHARED_UINT64 Violations[2];
} TEST_SEQUENTIAL_SYNC_PHASE;
typedef struct {
    TEST_SEQUENTIAL_SYNC_PHASE Phase[TEST_SEQUENTIAL_PHASES];
    UINT8 Padding[8192 -
                  TEST_SEQUENTIAL_PHASES * sizeof(TEST_SEQUENTIAL_SYNC_PHASE)];
} TEST_SEQUENTIAL_SYNC_PAGE;
static TEST_SEQUENTIAL_DATA_PAGE sequential_data_pages[2]
    __attribute__((aligned(8192)));
static TEST_SEQUENTIAL_SYNC_PAGE sequential_sync_page
    __attribute__((aligned(8192)));

static BOOLEAN translation_cache_churn_check(VOID);

static VOID install_data_tc_mapping_flags(UINT64 Va, UINT64 Pa, UINT64 Itir,
                                          UINT64 PteFlags)
{
    UINT64 page_shift = (Itir >> 2) & 0x3fULL;
    UINT64 page_mask = (1ULL << page_shift) - 1ULL;
    UINT64 pte = (Pa & ~page_mask) | PteFlags;

    __asm__ volatile("rsm psr.ic;;\n\t"
                     "srlz.d;;\n\t"
                     "mov cr.ifa=%0\n\t"
                     "mov cr.itir=%1;;\n\t"
                     "itc.d %2;;\n\t"
                     "srlz.d;;\n\t"
                     "ssm psr.ic;;\n\t"
                     "srlz.d;;"
                     :
                     : "r"(Va), "r"(Itir), "r"(pte)
                     : "memory");
}

static VOID install_data_tc_mapping(UINT64 Va, UINT64 Pa, UINT64 Itir)
{
    install_data_tc_mapping_flags(Va, Pa, Itir, TEST_TRANSLATION_PTE_FLAGS);
}

static VOID install_data_tc_at(UINT64 Va, const volatile UINT64 *Page)
{
    install_data_tc_mapping(Va, (UINT64)(UINTN)Page, TEST_TRANSLATION_ITIR);
}

static VOID install_data_tc(const volatile UINT64 *Page)
{
    install_data_tc_at(TEST_TRANSLATION_VA, Page);
}

static VOID purge_data_tc_mapping(UINT64 Va, UINT64 Itir)
{
    __asm__ volatile("ptc.l %0,%1;;\n\t"
                     "srlz.d;;"
                     :
                     : "r"(Va), "r"(Itir)
                     : "memory");
}

static VOID purge_data_tc_at(UINT64 Va)
{
    purge_data_tc_mapping(Va, TEST_TRANSLATION_ITIR);
}

static VOID purge_data_tc(VOID)
{
    purge_data_tc_at(TEST_TRANSLATION_VA);
}

static VOID purge_global_data_tc(VOID)
{
    __asm__ volatile("ptc.ga %0,%1;;\n\t"
                     "mf;;\n\t"
                     "srlz.d;;"
                     :
                     : "r"(TEST_TRANSLATION_VA), "r"(TEST_TRANSLATION_ITIR)
                     : "memory");
}

static VOID purge_global_high_data_tc_without_alat(VOID)
{
    __asm__ volatile("ptc.g %0,%1;;\n\t"
                     "mf;;\n\t"
                     "srlz.d;;"
                     :
                     : "r"(TEST_HIGH_TRANSLATED_LOCK_VA),
                       "r"(TEST_HIGH_TRANSLATION_ITIR)
                     : "memory");
}

static UINT64 translation_present_at(UINT64 Va)
{
    UINT64 key;

    /*
     * Use tak to observe whether a translation exists: the architected
     * non-faulting probe forms take TLB-miss faults, while tak simply
     * returns 1 when no matching present translation is found.  Every
     * translation this test inserts carries protection key 0, so any
     * value other than 1 means the translation is still present.
     */
    __asm__ volatile("ssm psr.dt;;\n\t"
                     "srlz.d;;\n\t"
                     "tak %0=%1;;\n\t"
                     "rsm psr.dt;;\n\t"
                     "srlz.d;;"
                     : "=r"(key)
                     : "r"(Va)
                     : "memory");
    return key != 1;
}

static UINT64 translation_present_offset(UINT64 Offset)
{
    return translation_present_at(TEST_TRANSLATION_VA + Offset);
}

static UINT64 translation_present(VOID)
{
    return translation_present_offset(0);
}

static UINT64 load_translated_value(UINT64 Offset)
{
    UINT64 value;

    __asm__ volatile("ssm psr.dt;;\n\t"
                     "srlz.d;;\n\t"
                     "ld8 %0=[%1];;\n\t"
                     "rsm psr.dt;;\n\t"
                     "srlz.d;;"
                     : "=r"(value)
                     : "r"(TEST_TRANSLATION_VA + Offset)
                     : "memory");
    return value;
}

static UINT64 load_translated_value_at(UINT64 Va, UINT64 Offset)
{
    UINT64 value;

    __asm__ volatile("ssm psr.dt;;\n\t"
                     "srlz.d;;\n\t"
                     "ld8 %0=[%1];;\n\t"
                     "rsm psr.dt;;\n\t"
                     "srlz.d;;"
                     : "=r"(value)
                     : "r"(Va + Offset)
                     : "memory");
    return value;
}

static UINT16 translated_word_increment_advanced(UINT64 Va, UINT64 Offset)
{
    UINT64 address = Va + Offset;
    UINT64 value;

    /*
     * SMP machines use the zero-entry ALAT model.  In that model the check
     * load must re-read memory.  Keep the complete advanced-load sequence in
     * one routine so a stale QEMU translation can be observed as a store to
     * the physical page that was mapped previously.
     */
    __asm__ volatile("ssm psr.dt;;\n\t"
                     "srlz.d;;\n\t"
                     "ld2.sa %0=[%1];;\n\t"
                     "ld2.c.clr %0=[%1];;\n\t"
                     "adds %0=1,%0;;\n\t"
                     "st2 [%1]=%0;;\n\t"
                     "rsm psr.dt;;\n\t"
                     "srlz.d;;"
                     : "=&r"(value)
                     : "r"(address)
                     : "memory");
    return value;
}

static BOOLEAN translation_remap_word_store_check(UINTN Id)
{
    volatile UINT16 *word[2];
    UINT64 va = TEST_TRANSLATION_WRITE_VA_BASE + Id * 8192ULL;
    UINTN round;
    UINTN page;
    BOOLEAN valid = 1;

    word[0] = (volatile UINT16
                   *)(UINTN)((UINT64)(UINTN)&translation_write_pages[Id][0][0] +
                             TEST_TRANSLATION_WRITE_OFFSET);
    word[1] = (volatile UINT16
                   *)(UINTN)((UINT64)(UINTN)&translation_write_pages[Id][1][0] +
                             TEST_TRANSLATION_WRITE_OFFSET);

    purge_data_tc_at(va);
    for (round = 0; round < TEST_TLB_CHURN_ROUNDS && valid; round++) {
        for (page = 0; page < 2; page++) {
            UINT16 initial = 0x1000U + (round << 4) + page;
            UINT16 other = 0x5000U + (round << 4) + (page ^ 1U);

            *word[page] = initial;
            *word[page ^ 1U] = other;
            __asm__ volatile("mf;;" : : : "memory");

            /* itc.d itself must remove the overlapping old TC mapping. */
            install_data_tc_at(va, &translation_write_pages[Id][page][0]);
            if (translated_word_increment_advanced(
                    va, TEST_TRANSLATION_WRITE_OFFSET) !=
                (UINT16)(initial + 1U)) {
                valid = 0;
                break;
            }
            __asm__ volatile("mf;;" : : : "memory");
            if (*word[page] != (UINT16)(initial + 1U) ||
                *word[page ^ 1U] != other) {
                valid = 0;
                break;
            }
        }

        /* Force TC replacement before the next A/B remap. */
        if (valid && !translation_cache_churn_check()) {
            valid = 0;
        }
    }
    purge_data_tc_at(va);
    return valid;
}

static BOOLEAN translation_cache_churn_check(VOID)
{
    UINTN round;
    UINTN entry;
    BOOLEAN valid = 1;

    for (round = 0; round < TEST_TLB_CHURN_ROUNDS; round++) {
        for (entry = 0; entry < TEST_TLB_CHURN_ENTRIES; entry++) {
            UINT64 va = TEST_TLB_CHURN_BASE + entry * 8192ULL;
            BOOLEAN use_a = ((entry ^ round) & 1U) == 0;
            const volatile UINT64 *page =
                use_a ? translation_page_a : translation_page_b;
            UINT64 expected =
                use_a ? TEST_TRANSLATION_VALUE_A : TEST_TRANSLATION_VALUE_B;
            UINT64 expected_high = use_a ? TEST_TRANSLATION_VALUE_A_HIGH
                                         : TEST_TRANSLATION_VALUE_B_HIGH;

            purge_data_tc_at(va);
            install_data_tc_at(va, page);
            if (load_translated_value_at(va, 0) != expected ||
                load_translated_value_at(va, TEST_TRANSLATION_HIGH_OFFSET) !=
                    expected_high) {
                valid = 0;
                break;
            }
        }
        if (!valid) {
            break;
        }
    }

    for (entry = 0; entry < TEST_TLB_CHURN_ENTRIES; entry++) {
        purge_data_tc_at(TEST_TLB_CHURN_BASE + entry * 8192ULL);
    }
    return valid;
}

static VOID translation_round_check(UINTN Id)
{
    UINT64 round = translation_round;
    const volatile UINT64 *page =
        (round & 1) ? translation_page_a : translation_page_b;
    UINT64 expected =
        (round & 1) ? TEST_TRANSLATION_VALUE_A : TEST_TRANSLATION_VALUE_B;
    UINT64 expected_high = (round & 1) ? TEST_TRANSLATION_VALUE_A_HIGH
                                       : TEST_TRANSLATION_VALUE_B_HIGH;

    if (round != 1) {
        purge_data_tc();
    }
    install_data_tc(page);
    if (load_translated_value(0) != expected ||
        load_translated_value(TEST_TRANSLATION_HIGH_OFFSET) != expected_high) {
        translation_mismatch[Id]++;
    }
}

static BOOLEAN partial_translation_purge_check(VOID)
{
    BOOLEAN valid;

    purge_data_tc();
    install_data_tc(translation_page_a);
    valid = load_translated_value(0) == TEST_TRANSLATION_VALUE_A &&
            load_translated_value(TEST_TRANSLATION_HIGH_OFFSET) ==
                TEST_TRANSLATION_VALUE_A_HIGH;

    /* A partial overlap must remove the complete 8 KiB TC entry. */
    __asm__ volatile("ptc.l %0,%1;;\n\t"
                     "srlz.d;;"
                     :
                     : "r"(TEST_TRANSLATION_VA + TEST_TRANSLATION_HIGH_OFFSET),
                       "r"(TEST_TRANSLATION_4K_ITIR)
                     : "memory");
    valid = valid && translation_present_offset(0) == 0 &&
            translation_present_offset(TEST_TRANSLATION_HIGH_OFFSET) == 0;

    install_data_tc(translation_page_b);
    valid = valid && load_translated_value(0) == TEST_TRANSLATION_VALUE_B &&
            load_translated_value(TEST_TRANSLATION_HIGH_OFFSET) ==
                TEST_TRANSLATION_VALUE_B_HIGH;
    purge_data_tc();
    return valid;
}

static UINT64 read_test_region_register(VOID)
{
    UINT64 value;

    __asm__ volatile("mov %0=rr[%1];;"
                     : "=r"(value)
                     : "r"(TEST_TRANSLATION_VA)
                     : "memory");
    return value;
}

static VOID write_test_region_register(UINT64 Value)
{
    __asm__ volatile("mov rr[%0]=%1;;\n\t"
                     "srlz.d;;"
                     :
                     : "r"(TEST_TRANSLATION_VA), "r"(Value)
                     : "memory");
}

static BOOLEAN rid_translation_switch_check(VOID)
{
    UINT64 saved_rr = read_test_region_register();
    UINT64 rr_a = (TEST_TRANSLATION_RID_A << 8) | TEST_TRANSLATION_ITIR;
    UINT64 rr_b = (TEST_TRANSLATION_RID_B << 8) | TEST_TRANSLATION_ITIR;
    UINTN round;
    BOOLEAN valid = 1;

    write_test_region_register(rr_a);
    purge_data_tc();
    install_data_tc(translation_page_a);
    write_test_region_register(rr_b);
    purge_data_tc();
    install_data_tc(translation_page_b);

    for (round = 0; round < TEST_RID_SWITCH_ROUNDS; round++) {
        write_test_region_register((round & 1) ? rr_b : rr_a);
        if (load_translated_value(0) != ((round & 1)
                                             ? TEST_TRANSLATION_VALUE_B
                                             : TEST_TRANSLATION_VALUE_A) ||
            load_translated_value(TEST_TRANSLATION_HIGH_OFFSET) !=
                ((round & 1) ? TEST_TRANSLATION_VALUE_B_HIGH
                             : TEST_TRANSLATION_VALUE_A_HIGH)) {
            valid = 0;
            break;
        }
    }

    /* Recycle one RID only after its old translation has been purged. */
    write_test_region_register(rr_a);
    purge_data_tc();
    install_data_tc(translation_page_b);
    valid = valid && load_translated_value(0) == TEST_TRANSLATION_VALUE_B &&
            load_translated_value(TEST_TRANSLATION_HIGH_OFFSET) ==
                TEST_TRANSLATION_VALUE_B_HIGH;
    purge_data_tc();

    write_test_region_register(rr_b);
    purge_data_tc();
    write_test_region_register(saved_rr);
    return valid;
}

static UINT64 xchg8(volatile UINT64 *Address, UINT64 Value)
{
    UINT64 old;

    __asm__ volatile("xchg8 %0=[%1],%2;;"
                     : "=r"(old)
                     : "r"(Address), "r"(Value)
                     : "memory");
    return old;
}

static UINT64 load8_bias(volatile UINT64 *Address)
{
    UINT64 value;

    __asm__ volatile("ld8.bias %0=[%1];;"
                     : "=r"(value)
                     : "r"(Address)
                     : "memory");
    return value;
}

static VOID store8_release(volatile UINT64 *Address, UINT64 Value)
{
    __asm__ volatile("st8.rel [%0]=%1;;"
                     :
                     : "r"(Address), "r"(Value)
                     : "memory");
}

static VOID spin_lock_acquire(VOID)
{
    while (xchg8(&guarded_lock, 1) != 0) {
        while (guarded_lock != 0) {
            __asm__ volatile("hint @pause" : : : "memory");
        }
    }
}

static VOID spin_lock_release(VOID)
{
    __asm__ volatile("st8.rel [%0]=r0;;" : : "r"(&guarded_lock) : "memory");
}

static VOID guarded_increment_batch(VOID)
{
    UINTN i;

    for (i = 0; i < TEST_GUARDED_INCREMENTS; i++) {
        UINT64 value;

        spin_lock_acquire();
        __asm__ volatile("ld2 %0=[%1];;"
                         : "=r"(value)
                         : "r"(&guarded_counter)
                         : "memory");
        value++;
        __asm__ volatile("st2 [%0]=%1;;"
                         :
                         : "r"(&guarded_counter), "r"(value)
                         : "memory");
        spin_lock_release();
    }
}

static VOID bit_lock_acquire(VOID)
{
    for (;;) {
        UINT64 expected;
        UINT64 desired;
        UINT64 previous;

        __asm__ volatile("mf;;\n\t"
                         "ld4.bias %0=[%3];;\n\t"
                         "or %1=1,%0;;\n\t"
                         "mov ar.ccv=%0;;\n\t"
                         "cmpxchg4.acq %2=[%3],%1;;"
                         : "=&r"(expected), "=&r"(desired), "=&r"(previous)
                         : "r"(&bit_lock)
                         : "memory");
        if ((expected & 1U) == 0 && previous == expected) {
            return;
        }
        __asm__ volatile("hint @pause" : : : "memory");
    }
}

static VOID bit_lock_release(VOID)
{
    for (;;) {
        UINT64 expected;
        UINT64 desired;
        UINT64 previous;

        __asm__ volatile("mf;;\n\t"
                         "ld4.bias %0=[%3];;\n\t"
                         "and %1=-2,%0;;\n\t"
                         "mov ar.ccv=%0;;\n\t"
                         "cmpxchg4.acq %2=[%3],%1;;"
                         : "=&r"(expected), "=&r"(desired), "=&r"(previous)
                         : "r"(&bit_lock)
                         : "memory");
        if (previous == expected) {
            return;
        }
    }
}

static VOID bit_lock_increment_batch(VOID)
{
    UINTN i;

    for (i = 0; i < TEST_BIT_LOCK_INCREMENTS; i++) {
        UINT64 value;

        bit_lock_acquire();
        __asm__ volatile("ld2.bias %0=[%1];;"
                         : "=r"(value)
                         : "r"(&bit_lock_counter)
                         : "memory");
        value++;
        __asm__ volatile("st2 [%0]=%1;;"
                         :
                         : "r"(&bit_lock_counter), "r"(value)
                         : "memory");
        bit_lock_release();
    }
}

static __attribute__((noinline)) UINT64 rse_pressure(UINTN Depth, UINT64 A,
                                                     UINT64 B, UINT64 C,
                                                     UINT64 D, UINT64 E,
                                                     UINT64 F, UINT64 G,
                                                     UINT64 H)
{
    UINT64 child;

    __asm__ volatile(""
                     : "+r"(A), "+r"(B), "+r"(C), "+r"(D), "+r"(E), "+r"(F),
                       "+r"(G), "+r"(H));
    if (Depth == 0) {
        return A ^ B ^ C ^ D ^ E ^ F ^ G ^ H;
    }
    child = rse_pressure(Depth - 1U, B + 1U, C + 3U, D + 5U, E + 7U, F + 11U,
                         G + 13U, H + 17U, A + 19U);
    __asm__ volatile(""
                     : "+r"(A), "+r"(B), "+r"(C), "+r"(D), "+r"(E), "+r"(F),
                       "+r"(G), "+r"(H));
    return child ^ A ^ (B << 1) ^ (C << 2) ^ (D << 3) ^ (E << 4) ^ (F << 5) ^
           (G << 6) ^ (H << 7);
}

static __attribute__((noinline)) VOID slow_lock_acquire(UINTN Id)
{
    UINT64 sentinel =
        0x6d2b79f5a4c381e7ULL ^ ((UINT64)Id * 0x0101010101010101ULL);
    UINT64 calls = slow_path_calls[Id] + 1U;

    slow_path_calls[Id] = calls;
    if (calls <= TEST_RSE_PRESSURE_CALLS) {
        UINT64 value = rse_pressure(12U, sentinel, sentinel + 1U, sentinel + 2U,
                                    sentinel + 3U, sentinel + 4U, sentinel + 5U,
                                    sentinel + 6U, sentinel + 7U);

        rse_pressure_sink[Id] ^= value;
    }

    while (xchg8(&slow_lock, 1) != 0) {
        while (load8_bias(&slow_lock) != 0) {
            __asm__ volatile("hint @pause" : : : "memory");
        }
    }
}

static BOOLEAN slow_lock_begin_contended_round(UINTN Id)
{
    UINT64 round = translation_round;
    UINTN peer;

    /*
     * Do not rely on host thread scheduling to happen to create contention.
     * CPU 0 holds the lock until every AP has attempted the same acquisition
     * and entered the RSE-pressure slow path.  This makes the test exercise
     * the intended call/RSE path deterministically on both fast and heavily
     * loaded hosts.
     */
    if (Id == 0) {
        while (xchg8(&slow_lock, 1) != 0) {
            __asm__ volatile("hint @pause" : : : "memory");
        }
        slow_lock_ready[Id] = round;
        __asm__ volatile("mf;;" : : : "memory");
        for (peer = 1; peer < TEST_PROCESSOR_COUNT; peer++) {
            while (slow_lock_ready[peer] != round) {
                __asm__ volatile("hint @pause" : : : "memory");
            }
        }
        for (peer = 1; peer < TEST_PROCESSOR_COUNT; peer++) {
            while (slow_lock_waiting[peer] != round) {
                __asm__ volatile("hint @pause" : : : "memory");
            }
        }
        return 1;
    }

    slow_lock_ready[Id] = round;
    __asm__ volatile("mf;;" : : : "memory");
    while (slow_lock_ready[0] != round) {
        __asm__ volatile("hint @pause" : : : "memory");
    }
    if (xchg8(&slow_lock, 1) == 0) {
        return 1;
    }
    slow_lock_waiting[Id] = round;
    __asm__ volatile("mf;;" : : : "memory");
    slow_lock_acquire(Id);
    return 1;
}

static VOID slow_lock_increment_batch(UINTN Id)
{
    BOOLEAN owns_lock = slow_lock_begin_contended_round(Id);
    UINTN i;

    for (i = 0; i < TEST_SLOW_LOCK_INCREMENTS; i++) {
        UINT64 value;

        if (!owns_lock && xchg8(&slow_lock, 1) != 0) {
            slow_lock_acquire(Id);
        }
        __asm__ volatile("ld2.bias %0=[%1];;"
                         : "=r"(value)
                         : "r"(&slow_lock_counter)
                         : "memory");
        value++;
        __asm__ volatile("st2 [%0]=%1;;"
                         :
                         : "r"(&slow_lock_counter), "r"(value)
                         : "memory");
        store8_release(&slow_lock, 0);
        owns_lock = 0;
    }
}

static VOID translated_lock_increment_one(VOID)
{
    volatile UINT32 *lock = (volatile UINT32 *)(UINTN)TEST_TRANSLATED_LOCK_VA;
    volatile UINT16 *counter =
        (volatile UINT16 *)(UINTN)(TEST_TRANSLATED_LOCK_VA + sizeof(UINT32));
    UINT64 expected;
    UINT64 desired;
    UINT64 previous;
    UINT64 value;

    /*
     * Keep data translation enabled for the complete critical section.
     * This couples a translated bias load, cmpxchg4 lock, and 16-bit RMW in
     * the same way as an operating-system page-table accounting path.
     */
    __asm__ volatile("ssm psr.dt;;\n\t"
                     "srlz.d;;\n"
                     "1:\n\t"
                     "mf;;\n\t"
                     "ld4.bias %0=[%4];;\n\t"
                     "or %1=1,%0;;\n\t"
                     "mov ar.ccv=%0;;\n\t"
                     "cmpxchg4.acq %2=[%4],%1;;\n\t"
                     "cmp.eq.unc p0,p8=%2,%0\n\t"
                     "tbit.z.unc p0,p7=%2,0;;\n\t"
                     "(p8) br.cond.spnt 1b;;\n\t"
                     "(p7) br.cond.spnt 1b;;\n\t"
                     "ld2.bias %3=[%5];;\n\t"
                     "adds %3=1,%3;;\n\t"
                     "st2 [%5]=%3;;\n"
                     "2:\n\t"
                     "mf;;\n\t"
                     "ld4.bias %0=[%4];;\n\t"
                     "and %1=-2,%0;;\n\t"
                     "mov ar.ccv=%0;;\n\t"
                     "cmpxchg4.rel %2=[%4],%1;;\n\t"
                     "cmp.eq.unc p0,p8=%2,%0;;\n\t"
                     "(p8) br.cond.spnt 2b;;\n\t"
                     "rsm psr.dt;;\n\t"
                     "srlz.d;;"
                     : "=&r"(expected), "=&r"(desired), "=&r"(previous),
                       "=&r"(value)
                     : "r"(lock), "r"(counter)
                     : "p7", "p8", "memory");
}

static VOID translated_lock8_increment_one(VOID)
{
    volatile UINT64 *lock =
        (volatile UINT64 *)(UINTN)(TEST_TRANSLATED_LOCK_VA + 8U);
    volatile UINT16 *counter =
        (volatile UINT16 *)(UINTN)(TEST_TRANSLATED_LOCK_VA + 16U);
    UINT64 expected;
    UINT64 desired;
    UINT64 previous;
    UINT64 value;

    __asm__ volatile("ssm psr.dt;;\n\t"
                     "srlz.d;;\n"
                     "1:\n\t"
                     "mf;;\n\t"
                     "ld8.bias %0=[%4];;\n\t"
                     "or %1=1,%0;;\n\t"
                     "mov ar.ccv=%0;;\n\t"
                     "cmpxchg8.acq %2=[%4],%1;;\n\t"
                     "cmp.eq.unc p0,p8=%2,%0\n\t"
                     "tbit.z.unc p0,p7=%2,0;;\n\t"
                     "(p8) br.cond.spnt 1b;;\n\t"
                     "(p7) br.cond.spnt 1b;;\n\t"
                     "ld2.s %3=[%5];;\n\t"
                     "adds %3=1,%3;;\n\t"
                     "chk.s.m %3,3f;;\n\t"
                     "st2 [%5]=%3;;\n\t"
                     "br.cond.sptk 4f;;\n"
                     "3:\n\t"
                     "ld2 %3=[%5];;\n\t"
                     "adds %3=1,%3;;\n\t"
                     "st2 [%5]=%3;;\n"
                     "4:\n"
                     "2:\n\t"
                     "mf;;\n\t"
                     "ld8.bias %0=[%4];;\n\t"
                     "and %1=-2,%0;;\n\t"
                     "mov ar.ccv=%0;;\n\t"
                     "cmpxchg8.rel %2=[%4],%1;;\n\t"
                     "cmp.eq.unc p0,p8=%2,%0;;\n\t"
                     "(p8) br.cond.spnt 2b;;\n\t"
                     "rsm psr.dt;;\n\t"
                     "srlz.d;;"
                     : "=&r"(expected), "=&r"(desired), "=&r"(previous),
                       "=&r"(value)
                     : "r"(lock), "r"(counter)
                     : "p7", "p8", "memory");
}

static VOID translated_lock_increment_batch(VOID)
{
    TEST_TRANSLATED_LOCK_PAGE *page =
        &translated_lock_pages[(translation_round - 1U) & 1U];
    UINTN i;

    purge_data_tc_at(TEST_TRANSLATED_LOCK_VA);
    install_data_tc_at(TEST_TRANSLATED_LOCK_VA, (const volatile UINT64 *)page);
    for (i = 0; i < TEST_TRANSLATED_LOCK_INCREMENTS; i++) {
        translated_lock_increment_one();
        translated_lock8_increment_one();
    }
    purge_data_tc_at(TEST_TRANSLATED_LOCK_VA);
}

static VOID high_translated_lock_increment_one(VOID)
{
    volatile UINT64 *lock =
        (volatile UINT64 *)(UINTN)TEST_HIGH_TRANSLATED_LOCK_VA;
    volatile UINT16 *counter =
        (volatile UINT16 *)(UINTN)TEST_HIGH_TRANSLATED_COUNT_VA;
    UINT64 expected;
    UINT64 desired;
    UINT64 previous;
    UINT64 value;

    /*
     * Exercise a high physical address, 16 MiB mapping, 8-byte
     * interlock, and checked 16-bit page-table accounting update in one
     * contended path.
     */
    __asm__ volatile("ssm psr.dt;;\n\t"
                     "srlz.d;;\n"
                     "1:\n\t"
                     "mf;;\n\t"
                     "ld8.bias %0=[%4];;\n\t"
                     "or %1=1,%0;;\n\t"
                     "mov ar.ccv=%0;;\n\t"
                     "cmpxchg8.acq %2=[%4],%1;;\n\t"
                     "cmp.eq.unc p0,p8=%2,%0\n\t"
                     "tbit.z.unc p0,p7=%2,0;;\n\t"
                     "(p8) br.cond.spnt 1b;;\n\t"
                     "(p7) br.cond.spnt 1b;;\n\t"
                     "ld2.sa %3=[%5];;\n\t"
                     "ld2.c.clr %3=[%5];;\n\t"
                     "adds %3=1,%3;;\n\t"
                     "st2 [%5]=%3;;\n"
                     "2:\n\t"
                     "mf;;\n\t"
                     "ld8.bias %0=[%4];;\n\t"
                     "and %1=-2,%0;;\n\t"
                     "mov ar.ccv=%0;;\n\t"
                     "cmpxchg8.rel %2=[%4],%1;;\n\t"
                     "cmp.eq.unc p0,p8=%2,%0;;\n\t"
                     "(p8) br.cond.spnt 2b;;\n\t"
                     "rsm psr.dt;;\n\t"
                     "srlz.d;;"
                     : "=&r"(expected), "=&r"(desired), "=&r"(previous),
                       "=&r"(value)
                     : "r"(lock), "r"(counter)
                     : "p7", "p8", "memory");
}

static VOID high_translated_lock_increment_batch(VOID)
{
    UINTN i;

    purge_data_tc_mapping(TEST_HIGH_TRANSLATED_LOCK_VA,
                          TEST_HIGH_TRANSLATION_ITIR);
    install_data_tc_mapping(TEST_HIGH_TRANSLATED_LOCK_VA,
                            TEST_HIGH_TRANSLATED_LOCK_PA,
                            TEST_HIGH_TRANSLATION_ITIR);
    for (i = 0; i < TEST_HIGH_TRANSLATED_LOCK_INCREMENTS; i++) {
        high_translated_lock_increment_one();
    }
    purge_data_tc_mapping(TEST_HIGH_TRANSLATED_LOCK_VA,
                          TEST_HIGH_TRANSLATION_ITIR);
}

static VOID fetchadd_increment_batch(VOID)
{
    UINTN i;

    for (i = 0; i < TEST_SEMAPHORE_INCREMENTS; i++) {
        UINT64 old;

        __asm__ volatile("fetchadd4.acq %0=[%1],1;;"
                         : "=r"(old)
                         : "r"(&fetchadd4_counter)
                         : "memory");
        __asm__ volatile("fetchadd8.acq %0=[%1],1;;"
                         : "=r"(old)
                         : "r"(&fetchadd8_counter)
                         : "memory");
    }
}

static UINT64 cmpxchg4(volatile UINT32 *Address, UINT32 Compare, UINT32 Value,
                       BOOLEAN Release)
{
    UINT64 old;

    if (Release) {
        __asm__ volatile("mov ar.ccv=%2;;\n\t"
                         "cmpxchg4.rel %0=[%1],%3;;"
                         : "=r"(old)
                         : "r"(Address), "r"((UINT64)Compare),
                           "r"((UINT64)Value)
                         : "memory");
    } else {
        __asm__ volatile("mov ar.ccv=%2;;\n\t"
                         "cmpxchg4.acq %0=[%1],%3;;"
                         : "=r"(old)
                         : "r"(Address), "r"((UINT64)Compare),
                           "r"((UINT64)Value)
                         : "memory");
    }
    return old;
}

static UINT64 cmpxchg8(volatile UINT64 *Address, UINT64 Compare, UINT64 Value,
                       BOOLEAN Release)
{
    UINT64 old;

    if (Release) {
        __asm__ volatile("mov ar.ccv=%2;;\n\t"
                         "cmpxchg8.rel %0=[%1],%3;;"
                         : "=r"(old)
                         : "r"(Address), "r"(Compare), "r"(Value)
                         : "memory");
    } else {
        __asm__ volatile("mov ar.ccv=%2;;\n\t"
                         "cmpxchg8.acq %0=[%1],%3;;"
                         : "=r"(old)
                         : "r"(Address), "r"(Compare), "r"(Value)
                         : "memory");
    }
    return old;
}

static VOID packed_pfn_update_batch(UINTN Id)
{
    UINTN i;

    if (Id == 0) {
        /*
         * One processor performs the naturally aligned 16-bit accounting
         * update used for UsedPageTableEntries.  The other processors issue
         * full-word compare/exchanges that preserve the observed upper word,
         * matching concurrent bit-field maintenance in the containing PFN
         * record.  A stale or non-atomic cmpxchg8 would erase count updates.
         */
        for (i = 0; i < TEST_PACKED_PFN_INCREMENTS; i++) {
            UINT64 value;

            __asm__ volatile("ld2.bias %0=[%1];;"
                             : "=r"(value)
                             : "r"(&packed_pfn_word.Fields.Count)
                             : "memory");
            value++;
            __asm__ volatile("st2 [%0]=%1;;"
                             :
                             : "r"(&packed_pfn_word.Fields.Count), "r"(value)
                             : "memory");
        }
        return;
    }

    for (i = 0; i < TEST_PACKED_PFN_INCREMENTS; i++) {
        UINT64 observed = packed_pfn_word.Whole;

        for (;;) {
            UINT64 desired =
                (observed & ~0xffffffffULL) | ((observed + 1U) & 0xffffffffULL);
            UINT64 previous =
                cmpxchg8(&packed_pfn_word.Whole, observed, desired, i & 1U);

            if (previous == observed) {
                break;
            }
            observed = previous;
        }
    }
}

static VOID queued_lock_acquire(UINTN Id)
{
    TEST_QUEUED_LOCK_NODE *node = &queued_lock_nodes[Id];
    UINT64 predecessor;

    node->Next = 0;
    node->Waiting = 1;
    __asm__ volatile("mf;;" : : : "memory");
    predecessor = xchg8(&queued_lock_tail, (UINT64)(UINTN)node);
    if (predecessor != 0) {
        TEST_QUEUED_LOCK_NODE *previous =
            (TEST_QUEUED_LOCK_NODE *)(UINTN)predecessor;

        store8_release(&previous->Next, (UINT64)(UINTN)node);
        while (load8_bias(&node->Waiting) != 0) {
            __asm__ volatile("hint @pause" : : : "memory");
        }
    }
}

static VOID queued_lock_release(UINTN Id)
{
    TEST_QUEUED_LOCK_NODE *node = &queued_lock_nodes[Id];
    UINT64 successor = load8_bias(&node->Next);

    if (successor == 0) {
        UINT64 node_address = (UINT64)(UINTN)node;

        if (cmpxchg8(&queued_lock_tail, node_address, 0, 1) == node_address) {
            return;
        }
        do {
            successor = load8_bias(&node->Next);
            __asm__ volatile("hint @pause" : : : "memory");
        } while (successor == 0);
    }
    store8_release(&((TEST_QUEUED_LOCK_NODE *)(UINTN)successor)->Waiting, 0);
}

static VOID queued_increment_batch(UINTN Id)
{
    UINTN i;

    for (i = 0; i < TEST_QUEUED_INCREMENTS; i++) {
        UINT64 value;

        queued_lock_acquire(Id);
        __asm__ volatile("ld2 %0=[%1];;"
                         : "=r"(value)
                         : "r"(&queued_counter)
                         : "memory");
        value++;
        __asm__ volatile("st2 [%0]=%1;;"
                         :
                         : "r"(&queued_counter), "r"(value)
                         : "memory");
        queued_lock_release(Id);
    }
}

static VOID cmpxchg_increment_batch(VOID)
{
    UINTN i;

    for (i = 0; i < TEST_SEMAPHORE_INCREMENTS; i++) {
        UINT32 compare4;
        UINT64 compare8;

        compare4 = cmpxchg4_acq_counter;
        while (cmpxchg4(&cmpxchg4_acq_counter, compare4, compare4 + 1, 0) !=
               compare4) {
            compare4 = cmpxchg4_acq_counter;
        }
        compare4 = cmpxchg4_rel_counter;
        while (cmpxchg4(&cmpxchg4_rel_counter, compare4, compare4 + 1, 1) !=
               compare4) {
            compare4 = cmpxchg4_rel_counter;
        }
        compare8 = cmpxchg8_acq_counter;
        while (cmpxchg8(&cmpxchg8_acq_counter, compare8, compare8 + 1, 0) !=
               compare8) {
            compare8 = cmpxchg8_acq_counter;
        }
        compare8 = cmpxchg8_rel_counter;
        while (cmpxchg8(&cmpxchg8_rel_counter, compare8, compare8 + 1, 1) !=
               compare8) {
            compare8 = cmpxchg8_rel_counter;
        }
    }
}

static UINT64 __attribute__((noinline))
cmp8xchg16(volatile UINT64 *Address, UINT64 Compare, UINT64 Low, UINT64 High)
{
    UINT64 old;

    __asm__ volatile("mov ar.ccv=%2\n\t"
                     "mov ar.csd=%4;;\n\t"
                     "cmp8xchg16.acq %0=[%1],%3;;"
                     : "=r"(old)
                     : "r"(Address), "r"(Compare), "r"(Low), "r"(High)
                     : "memory");
    return old;
}

static UINT64 __attribute__((noinline))
cmp8xchg16_big_endian(volatile UINT64 *Address, UINT64 Compare, UINT64 Low,
                      UINT64 High)
{
    UINT64 old;

    __asm__ volatile("sum 2;;\n\t"
                     "srlz.d;;\n\t"
                     "mov ar.ccv=%2\n\t"
                     "mov ar.csd=%4;;\n\t"
                     "cmp8xchg16.acq %0=[%1],%3;;\n\t"
                     "rum 2;;\n\t"
                     "srlz.d;;"
                     : "=r"(old)
                     : "r"(Address), "r"(Compare), "r"(Low), "r"(High)
                     : "memory");
    return old;
}

static VOID atomic_increment_pair(VOID)
{
    UINT64 compare = atomic_pair.Low;

    for (;;) {
        UINT64 old = cmp8xchg16(&atomic_pair.Low, compare, compare + 1,
                                TEST_ATOMIC_HIGH);

        if (old == compare) {
            return;
        }
        compare = old;
    }
}

static VOID atomic_increment_batch(VOID)
{
    UINTN i;

    for (i = 0; i < TEST_ATOMIC_INCREMENTS; i++) {
        atomic_increment_pair();
    }
}

static VOID *find_config_table(EFI_SYSTEM_TABLE *SystemTable, const UINT8 *Guid)
{
    UINTN i;

    for (i = 0; i < SystemTable->NumberOfTableEntries; i++) {
        if (ia64_bytes_equal(SystemTable->ConfigurationTable[i].VendorGuid,
                             Guid, 16)) {
            return (VOID *)(UINTN)SystemTable->ConfigurationTable[i]
                .VendorTable;
        }
    }
    return NULL;
}

static UINT16 get_u16(const VOID *Address)
{
    const UINT8 *p = (const UINT8 *)Address;

    return (UINT16)p[0] | ((UINT16)p[1] << 8);
}

static UINT32 get_u32(const VOID *Address)
{
    const UINT8 *p = (const UINT8 *)Address;

    return (UINT32)p[0] | ((UINT32)p[1] << 8) | ((UINT32)p[2] << 16) |
           ((UINT32)p[3] << 24);
}

static UINT64 get_u64(const VOID *Address)
{
    const UINT8 *p = (const UINT8 *)Address;

    return (UINT64)get_u32(p) | ((UINT64)get_u32(p + 4) << 32);
}

static UINTN sal_descriptor_size(UINT8 Type)
{
    switch (Type) {
    case 0:
        return 48U;
    case 1:
        return 32U;
    case 2:
        return 16U;
    case 3:
        return 32U;
    case 4:
    case 5:
        return 16U;
    default:
        return 0;
    }
}

static BOOLEAN find_sal_descriptors(UINT8 *Sal, UINT64 *Procedure, UINT64 *Gp,
                                    UINT64 *WakeVector)
{
    UINT32 length;
    UINT16 entries;
    UINTN offset = 96U;
    UINTN i;
    BOOLEAN entrypoint = 0;
    BOOLEAN wake = 0;

    if (Sal == NULL || get_u32(Sal) != 0x5f545353U) {
        return 0;
    }
    length = get_u32(Sal + 4U);
    entries = get_u16(Sal + 10U);
    if (length < 96U || length > 4096U || ia64_checksum8(Sal, length) != 0) {
        return 0;
    }
    for (i = 0; i < entries; i++) {
        UINTN size;

        if (offset >= length) {
            return 0;
        }
        size = sal_descriptor_size(Sal[offset]);
        if (size == 0 || size > length - offset) {
            return 0;
        }
        if (Sal[offset] == 0) {
            *Procedure = get_u64(Sal + offset + 16U);
            *Gp = get_u64(Sal + offset + 24U);
            entrypoint = *Procedure != 0;
        } else if (Sal[offset] == 5 && Sal[offset + 1U] == 0) {
            *WakeVector = get_u64(Sal + offset + 8U);
            wake = *WakeVector >= 0x10U && *WakeVector <= 0xffU;
        }
        offset += size;
    }
    return offset == length && entrypoint && wake;
}

static UINT64 read_lid(void)
{
    UINT64 lid;

    __asm__ volatile("mov %0 = cr.lid;;" : "=r"(lid) : : "memory");
    return lid;
}

static BOOLEAN supports_16_byte_atomics(VOID)
{
    UINT64 index = TEST_CPUID_FEATURES_REGISTER;
    UINT64 features;

    __asm__ volatile("mov %0 = cpuid[%1];;" : "=r"(features) : "r"(index));
    return (features & TEST_CPUID4_AO) != 0;
}

static UINT64 read_itc(void)
{
    UINT64 itc;

    __asm__ volatile("mov %0 = ar.itc;;" : "=r"(itc) : : "memory");
    return itc;
}

static UINT64 sequential_load_acquire(TEST_SHARED_UINT64 *Address)
{
    UINT64 value;

    __asm__ volatile("ld8.acq %0=[%1];;"
                     : "=r"(value)
                     : "r"(Address)
                     : "memory");
    return value;
}

static UINT64 sequential_data_load_acquire(UINT64 Address)
{
    UINT64 value;

    __asm__ volatile("ld8.acq %0=[%1];;"
                     : "=r"(value)
                     : "r"(Address)
                     : "memory");
    return value;
}

static VOID sequential_data_store(UINT64 Address, UINT64 Value)
{
    __asm__ volatile("st8 [%0]=%1;;" : : "r"(Address), "r"(Value) : "memory");
}

static UINT64 sequential_store_load(UINT64 Store, UINT64 Load)
{
    UINT64 value;

    /*
     * This is the operation pair under test.  In particular, do not put an
     * mf, acquire, release, serialization, or guest-side helper call between
     * the two unordered references.
     */
    __asm__ volatile("st8 [%1]=%3;;\n\t"
                     "ld8 %0=[%2];;"
                     : "=r"(value)
                     : "r"(Store), "r"(Load), "r"(1ULL)
                     : "memory");
    return value;
}

static VOID sequential_wait_for(TEST_SHARED_UINT64 *Address, UINT64 Value)
{
    while (sequential_load_acquire(Address) != Value) {
        __asm__ volatile("hint @pause" : : : "memory");
    }
}

static VOID sequential_order_check(UINTN Id, UINTN Phase)
{
    UINTN pair = Id >> 1;
    UINTN side = Id & 1U;
    UINTN peer = Id ^ 1U;
    TEST_SEQUENTIAL_DATA_PAGE *data =
        (TEST_SEQUENTIAL_DATA_PAGE *)(UINTN)TEST_SEQUENTIAL_DATA_VA;
    TEST_SEQUENTIAL_SYNC_PAGE *sync_page =
        (TEST_SEQUENTIAL_SYNC_PAGE *)(UINTN)TEST_SEQUENTIAL_SYNC_VA;
    TEST_SEQUENTIAL_SYNC_PHASE *sync = &sync_page->Phase[Phase];
    UINTN offset = Phase == TEST_SEQUENTIAL_ASSISTED_PHASE
                       ? TEST_SEQUENTIAL_ASSIST_OFFSET
                       : 0;
    UINTN iterations = Phase == TEST_SEQUENTIAL_ASSISTED_PHASE
                           ? TEST_ASSISTED_SEQUENTIAL_ITERATIONS
                           : TEST_SEQUENTIAL_ITERATIONS;
    /*
     * Offset the ordinary st8/ld8 by four bytes in the assisted phase.  With
     * PSR.ac set by the firmware handoff, each reference takes the firmware
     * unaligned assist while remaining wholly inside its 8 KiB translation.
     * Synchronization stays on the distinct WB page below and uses only
     * acquire loads and release stores.
     */
    UINT64 value_address[2] = {
        (UINT64)(UINTN)&data->Value0 + offset,
        (UINT64)(UINTN)&data->Value1 + offset,
    };
    UINT64 data_flags = pair == 0 ? TEST_TRANSLATION_PTE_FLAGS_UC
                                  : TEST_TRANSLATION_PTE_FLAGS_UCE;
    UINTN iteration;

    purge_data_tc_at(TEST_SEQUENTIAL_DATA_VA);
    purge_data_tc_at(TEST_SEQUENTIAL_SYNC_VA);
    install_data_tc_mapping_flags(TEST_SEQUENTIAL_DATA_VA,
                                  (UINT64)(UINTN)&sequential_data_pages[pair],
                                  TEST_TRANSLATION_ITIR, data_flags);
    install_data_tc_mapping_flags(
        TEST_SEQUENTIAL_SYNC_VA, (UINT64)(UINTN)&sequential_sync_page,
        TEST_TRANSLATION_ITIR, TEST_TRANSLATION_PTE_FLAGS);
    __asm__ volatile("ssm psr.dt;;\n\t"
                     "srlz.d;;"
                     :
                     :
                     : "memory");

    /* Warm both data soft-TLB entries before the measured store/load pair. */
    (void)sequential_data_load_acquire(value_address[side]);
    (void)sequential_load_acquire(&sync->Mapped[Id]);
    store8_release(&sync->Mapped[Id], 1);
    for (peer = 0; peer < TEST_PROCESSOR_COUNT; peer++) {
        sequential_wait_for(&sync->Mapped[peer], 1);
    }
    peer = Id ^ 1U;

    for (iteration = 0; iteration < iterations; iteration++) {
        UINT64 generation = iteration + 1U;
        UINT64 observed;

        if (side == 0) {
            sequential_data_store(value_address[0], 0);
            sequential_data_store(value_address[1], 0);
            __asm__ volatile("mf;;" : : : "memory");
        }

        store8_release(&sync->Ready[Id], generation);
        sequential_wait_for(&sync->Ready[peer], generation);
        observed = sequential_store_load(value_address[side],
                                         value_address[side ^ 1U]);
        sync->Result[Id] = observed;
        store8_release(&sync->Done[Id], generation);
        sequential_wait_for(&sync->Done[peer], generation);

        if (side == 0) {
            if (observed == 0 && sync->Result[peer] == 0) {
                sync->Violations[pair]++;
            }
            store8_release(&sync->Finish[pair], generation);
        } else {
            sequential_wait_for(&sync->Finish[pair], generation);
        }
    }

    store8_release(&sync->PhaseDone[Id], 1);
    for (peer = 0; peer < TEST_PROCESSOR_COUNT; peer++) {
        sequential_wait_for(&sync->PhaseDone[peer], 1);
    }
    __asm__ volatile("rsm psr.dt;;\n\t"
                     "srlz.d;;"
                     :
                     :
                     : "memory");
    purge_data_tc_at(TEST_SEQUENTIAL_DATA_VA);
    purge_data_tc_at(TEST_SEQUENTIAL_SYNC_VA);
}

static VOID global_translation_remote(UINTN Id)
{
    if (translation_command == TEST_TRANSLATION_GLOBAL_HIGH_COMMAND) {
        purge_data_tc_mapping(TEST_HIGH_TRANSLATED_LOCK_VA,
                              TEST_HIGH_TRANSLATION_ITIR);
        install_data_tc_mapping(TEST_HIGH_TRANSLATED_LOCK_VA,
                                TEST_HIGH_TRANSLATED_LOCK_PA,
                                TEST_HIGH_TRANSLATION_ITIR);
        if (load_translated_value_at(TEST_HIGH_TRANSLATED_LOCK_VA, 0) != 0) {
            global_translation_mismatch[Id]++;
        }
    } else {
        purge_data_tc();
        install_data_tc(translation_page_a);
        if (load_translated_value(0) != TEST_TRANSLATION_VALUE_A ||
            load_translated_value(TEST_TRANSLATION_HIGH_OFFSET) !=
                TEST_TRANSLATION_VALUE_A_HIGH) {
            global_translation_mismatch[Id]++;
        }
    }
    __asm__ volatile("mf;;" : : : "memory");
    global_translation_ready[Id] = 1;
    while (global_translation_release == 0) {
        __asm__ volatile("hint @pause" : : : "memory");
    }
    __asm__ volatile("mf;;" : : : "memory");
    global_translation_probe[Id] =
        translation_command == TEST_TRANSLATION_GLOBAL_HIGH_COMMAND
            ? translation_present_at(TEST_HIGH_TRANSLATED_LOCK_VA)
            : translation_present();
}

static VOID ap_rendezvous(void)
{
    UINTN id = (read_lid() >> 24) & 0xffU;
    UINT64 masked = 1ULL << 16;

    if (id > 0 && id < TEST_PROCESSOR_COUNT) {
        if (translation_command == TEST_TRANSLATION_GLOBAL_COMMAND ||
            translation_command == TEST_TRANSLATION_GLOBAL_HIGH_COMMAND) {
            global_translation_remote(id);
        } else {
            translation_round_check(id);
            if (translation_round == 1) {
                sequential_order_check(id, TEST_SEQUENTIAL_ALIGNED_PHASE);
                sequential_order_check(id, TEST_SEQUENTIAL_ASSISTED_PHASE);
            }
            if (translation_round == 1 && !translation_cache_churn_check()) {
                translation_churn_mismatch[id]++;
            }
            if (translation_round == 1 &&
                !translation_remap_word_store_check(id)) {
                translation_write_mismatch[id]++;
            }
            if (supports_16_byte_atomics()) {
                atomic_increment_batch();
            }
            fetchadd_increment_batch();
            cmpxchg_increment_batch();
            packed_pfn_update_batch(id);
            queued_increment_batch(id);
            guarded_increment_batch();
            bit_lock_increment_batch();
            slow_lock_increment_batch(id);
            translated_lock_increment_batch();
            high_translated_lock_increment_batch();
        }
        ap_rendezvous_count[id]++;
        __asm__ volatile("mf;;" : : : "memory");
        /* TPR is scratch on return; firmware must reopen the wake vector. */
        __asm__ volatile("mov cr.tpr = %0;;\n\tsrlz.d;;"
                         :
                         : "r"(masked)
                         : "memory");
    }
}

static VOID send_wake_ipi(UINTN Id, UINT64 Vector)
{
    /* The target address is an architected memory-mapped interrupt register. */
    volatile UINT64 *ipi =
        (volatile UINT64 *)(UINTN)(TEST_LOCAL_SAPIC_BASE + (Id << 12));

    __asm__ volatile("mf;;" : : : "memory");
    *ipi = Vector;
}

static BOOLEAN wait_for_round(UINT64 Round)
{
    UINT64 deadline = read_itc() + TEST_WAIT_TICKS;

    for (;;) {
        UINTN id;
        BOOLEAN complete = 1;

        __asm__ volatile("mf;;" : : : "memory");
        for (id = 1; id < TEST_PROCESSOR_COUNT; id++) {
            if (ap_rendezvous_count[id] < Round) {
                complete = 0;
            }
        }
        if (complete) {
            return 1;
        }
        if ((INTN)(read_itc() - deadline) >= 0) {
            return 0;
        }
        __asm__ volatile("hint @pause" : : : "memory");
    }
}

static BOOLEAN wait_for_global_translation_ready(VOID)
{
    UINT64 deadline = read_itc() + TEST_WAIT_TICKS;

    for (;;) {
        UINTN id;
        BOOLEAN complete = 1;

        __asm__ volatile("mf;;" : : : "memory");
        for (id = 1; id < TEST_PROCESSOR_COUNT; id++) {
            if (global_translation_ready[id] == 0) {
                complete = 0;
            }
        }
        if (complete) {
            return 1;
        }
        if ((INTN)(read_itc() - deadline) >= 0) {
            return 0;
        }
        __asm__ volatile("hint @pause" : : : "memory");
    }
}

static VOID wait_for_rendezvous_return(VOID)
{
    UINT64 deadline = read_itc() + TEST_RETURN_TICKS;

    /*
     * A counter update precedes the handler return.  Leave enough time for
     * every AP to reach PAL_HALT_LIGHT before reusing the same SAPIC vector;
     * an interrupt request bit intentionally coalesces duplicate writes.
     */
    while ((INTN)(read_itc() - deadline) < 0) {
        __asm__ volatile("hint @pause" : : : "memory");
    }
}

EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    IA64_TEST_CONTEXT context = {
        .SystemTable = SystemTable,
        .Suite = "smp",
        .Passed = 0,
        .Failed = 0,
        .DirectUart = 0,
    };
    UINT8 *sal = (UINT8 *)find_config_table(SystemTable, sal_guid);
    UINT64 sal_descriptor[2] __attribute__((aligned(16)));
    UINT64 *handler_descriptor = (UINT64 *)(UINTN)ap_rendezvous;
    UINT64 wake_vector = 0;
    TEST_SAL_PROC sal_proc;
    TEST_SAL_RETURN result;
    UINTN id;
    UINT64 round;
    BOOLEAN descriptors;
    BOOLEAN first_round = 0;
    BOOLEAN repeat_rounds = 0;
    BOOLEAN atomic_semantics;
    BOOLEAN guarded_semantics;
    BOOLEAN bit_lock_semantics;
    BOOLEAN slow_lock_semantics;
    UINT64 slow_path_total = 0;
    BOOLEAN translated_lock_semantics;
    BOOLEAN high_translated_lock_semantics;
    BOOLEAN fetchadd_semantics;
    BOOLEAN cmpxchg_semantics;
    BOOLEAN packed_pfn_semantics;
    BOOLEAN queued_semantics;
    BOOLEAN translation_semantics;
    BOOLEAN global_translation_source = 0;
    BOOLEAN global_translation_remote = 0;
    BOOLEAN global_translation_high_source = 0;
    BOOLEAN global_translation_high_remote = 0;
    BOOLEAN partial_translation_purge;
    BOOLEAN rid_translation_switch;
    BOOLEAN uc_sequential;
    BOOLEAN uce_sequential;
    BOOLEAN uc_assisted_sequential;
    BOOLEAN uce_assisted_sequential;
    BOOLEAN big_endian_atomic;
    BOOLEAN atomic_16_supported = supports_16_byte_atomics();

    (void)ImageHandle;
    atomic_pair.Low = 0;
    atomic_pair.High = TEST_ATOMIC_HIGH;
    guarded_lock = 0;
    guarded_counter = 0;
    bit_lock = 0;
    bit_lock_counter = 0;
    slow_lock = 0;
    slow_lock_counter = 0;
    translated_lock_pages[0].Lock = 0;
    translated_lock_pages[0].Counter = 0;
    translated_lock_pages[0].Lock8 = 0;
    translated_lock_pages[0].Counter8 = 0;
    translated_lock_pages[1].Lock = 0;
    translated_lock_pages[1].Counter = 0;
    translated_lock_pages[1].Lock8 = 0;
    translated_lock_pages[1].Counter8 = 0;
    *(volatile UINT64 *)(UINTN)TEST_HIGH_TRANSLATED_LOCK_PA = 0;
    *(volatile UINT16 *)(UINTN)TEST_HIGH_TRANSLATED_COUNT_PA = 0;
    fetchadd4_counter = 0;
    fetchadd8_counter = 0;
    cmpxchg4_acq_counter = 0;
    cmpxchg4_rel_counter = 0;
    cmpxchg8_acq_counter = 0;
    cmpxchg8_rel_counter = 0;
    packed_pfn_word.Whole = 0x0002000000560001ULL;
    queued_lock_tail = 0;
    queued_counter = 0;
    translation_page_a[0] = TEST_TRANSLATION_VALUE_A;
    translation_page_b[0] = TEST_TRANSLATION_VALUE_B;
    translation_page_a[TEST_TRANSLATION_HIGH_OFFSET / sizeof(UINT64)] =
        TEST_TRANSLATION_VALUE_A_HIGH;
    translation_page_b[TEST_TRANSLATION_HIGH_OFFSET / sizeof(UINT64)] =
        TEST_TRANSLATION_VALUE_B_HIGH;
    translation_round = 1;
    translation_command = TEST_TRANSLATION_LOCAL_COMMAND;
    global_translation_release = 0;
    descriptors = find_sal_descriptors(sal, &sal_descriptor[0],
                                       &sal_descriptor[1], &wake_vector);
    ia64_test_check(&context, "sal-ap-wake",
                    descriptors && wake_vector == TEST_AP_WAKE_VECTOR,
                    EFI_DEVICE_ERROR, "missing-rendezvous-descriptor");
    if (descriptors) {
        sal_proc = (TEST_SAL_PROC)(UINTN)sal_descriptor;
        result =
            sal_proc(TEST_SAL_SET_VECTORS, TEST_SAL_VECTOR_BOOT_RENDEZ,
                     handler_descriptor[0], handler_descriptor[1], 0, 0, 0, 0);
        if (result.Status == TEST_SAL_SUCCESS) {
            for (id = 1; id < TEST_PROCESSOR_COUNT; id++) {
                send_wake_ipi(id, wake_vector);
            }
            translation_round_check(0);
            sequential_order_check(0, TEST_SEQUENTIAL_ALIGNED_PHASE);
            sequential_order_check(0, TEST_SEQUENTIAL_ASSISTED_PHASE);
            if (!translation_cache_churn_check()) {
                translation_churn_mismatch[0]++;
            }
            if (!translation_remap_word_store_check(0)) {
                translation_write_mismatch[0]++;
            }
            if (atomic_16_supported) {
                atomic_increment_batch();
            }
            fetchadd_increment_batch();
            cmpxchg_increment_batch();
            packed_pfn_update_batch(0);
            queued_increment_batch(0);
            guarded_increment_batch();
            bit_lock_increment_batch();
            slow_lock_increment_batch(0);
            translated_lock_increment_batch();
            high_translated_lock_increment_batch();
            first_round = wait_for_round(1);
            if (first_round) {
                repeat_rounds = 1;
                wait_for_rendezvous_return();
                for (round = 2; round <= TEST_RENDEZVOUS_ROUNDS; round++) {
                    translation_round = round;
                    __asm__ volatile("mf;;" : : : "memory");
                    for (id = 1; id < TEST_PROCESSOR_COUNT; id++) {
                        send_wake_ipi(id, wake_vector);
                    }
                    translation_round_check(0);
                    if (atomic_16_supported) {
                        atomic_increment_batch();
                    }
                    fetchadd_increment_batch();
                    cmpxchg_increment_batch();
                    packed_pfn_update_batch(0);
                    queued_increment_batch(0);
                    guarded_increment_batch();
                    bit_lock_increment_batch();
                    slow_lock_increment_batch(0);
                    translated_lock_increment_batch();
                    high_translated_lock_increment_batch();
                    if (!wait_for_round(round)) {
                        repeat_rounds = 0;
                        break;
                    }
                    wait_for_rendezvous_return();
                }
            }
            if (repeat_rounds) {
                BOOLEAN global_ready;
                BOOLEAN global_round;

                translation_command = TEST_TRANSLATION_GLOBAL_COMMAND;
                global_translation_release = 0;
                __asm__ volatile("mf;;" : : : "memory");
                for (id = 1; id < TEST_PROCESSOR_COUNT; id++) {
                    send_wake_ipi(id, wake_vector);
                }
                purge_data_tc();
                install_data_tc(translation_page_a);
                if (load_translated_value(0) != TEST_TRANSLATION_VALUE_A ||
                    load_translated_value(TEST_TRANSLATION_HIGH_OFFSET) !=
                        TEST_TRANSLATION_VALUE_A_HIGH) {
                    global_translation_mismatch[0]++;
                }
                global_translation_ready[0] = 1;
                global_ready = wait_for_global_translation_ready();
                if (global_ready) {
                    purge_global_data_tc();
                    global_translation_probe[0] = translation_present();
                    store8_release(&global_translation_release, 1);
                    global_round = wait_for_round(TEST_GLOBAL_PURGE_ROUND);
                    if (global_round) {
                        wait_for_rendezvous_return();
                    }
                    global_translation_source =
                        global_round && global_translation_mismatch[0] == 0 &&
                        global_translation_probe[0] == 0;
                    global_translation_remote = global_round;
                    for (id = 1; id < TEST_PROCESSOR_COUNT; id++) {
                        global_translation_remote =
                            global_translation_remote &&
                            global_translation_mismatch[id] == 0 &&
                            global_translation_probe[id] == 0;
                    }
                } else {
                    store8_release(&global_translation_release, 1);
                }

                if (global_ready && global_round) {
                    for (id = 0; id < TEST_PROCESSOR_COUNT; id++) {
                        global_translation_ready[id] = 0;
                        global_translation_mismatch[id] = 0;
                        global_translation_probe[id] = 0;
                    }
                    global_translation_release = 0;
                    translation_command = TEST_TRANSLATION_GLOBAL_HIGH_COMMAND;
                    __asm__ volatile("mf;;" : : : "memory");
                    for (id = 1; id < TEST_PROCESSOR_COUNT; id++) {
                        send_wake_ipi(id, wake_vector);
                    }
                    purge_data_tc_mapping(TEST_HIGH_TRANSLATED_LOCK_VA,
                                          TEST_HIGH_TRANSLATION_ITIR);
                    install_data_tc_mapping(TEST_HIGH_TRANSLATED_LOCK_VA,
                                            TEST_HIGH_TRANSLATED_LOCK_PA,
                                            TEST_HIGH_TRANSLATION_ITIR);
                    if (load_translated_value_at(TEST_HIGH_TRANSLATED_LOCK_VA,
                                                 0) != 0) {
                        global_translation_mismatch[0]++;
                    }
                    global_translation_ready[0] = 1;
                    global_ready = wait_for_global_translation_ready();
                    if (global_ready) {
                        purge_global_high_data_tc_without_alat();
                        global_translation_probe[0] = translation_present_at(
                            TEST_HIGH_TRANSLATED_LOCK_VA);
                        store8_release(&global_translation_release, 1);
                        global_round =
                            wait_for_round(TEST_GLOBAL_HIGH_PURGE_ROUND);
                        if (global_round) {
                            wait_for_rendezvous_return();
                        }
                        global_translation_high_source =
                            global_round &&
                            global_translation_mismatch[0] == 0 &&
                            global_translation_probe[0] == 0;
                        global_translation_high_remote = global_round;
                        for (id = 1; id < TEST_PROCESSOR_COUNT; id++) {
                            global_translation_high_remote =
                                global_translation_high_remote &&
                                global_translation_mismatch[id] == 0 &&
                                global_translation_probe[id] == 0;
                        }
                    } else {
                        store8_release(&global_translation_release, 1);
                    }
                }
            }
        }
    }
    ia64_test_check(&context, "four-processor-rendezvous", first_round,
                    EFI_TIMEOUT, "secondary-start-timeout");
    ia64_test_check(&context, "repeat-rendezvous", repeat_rounds, EFI_TIMEOUT,
                    "secondary-return-timeout");
    uc_sequential =
        first_round && sequential_sync_page.Phase[TEST_SEQUENTIAL_ALIGNED_PHASE]
                               .Violations[0] == 0;
    ia64_test_check(&context, "uc-store-load-sequentiality", uc_sequential,
                    EFI_DEVICE_ERROR, "uc-store-bypassed-by-load");
    uce_sequential =
        first_round && sequential_sync_page.Phase[TEST_SEQUENTIAL_ALIGNED_PHASE]
                               .Violations[1] == 0;
    ia64_test_check(&context, "uce-store-load-sequentiality", uce_sequential,
                    EFI_DEVICE_ERROR, "uce-store-bypassed-by-load");
    uc_assisted_sequential =
        first_round &&
        sequential_sync_page.Phase[TEST_SEQUENTIAL_ASSISTED_PHASE]
                .Violations[0] == 0;
    ia64_test_check(&context, "uc-unaligned-store-load-sequentiality",
                    uc_assisted_sequential, EFI_DEVICE_ERROR,
                    "uc-assisted-store-bypassed-by-load");
    uce_assisted_sequential =
        first_round &&
        sequential_sync_page.Phase[TEST_SEQUENTIAL_ASSISTED_PHASE]
                .Violations[1] == 0;
    ia64_test_check(&context, "uce-unaligned-store-load-sequentiality",
                    uce_assisted_sequential, EFI_DEVICE_ERROR,
                    "uce-assisted-store-bypassed-by-load");
    translation_semantics = repeat_rounds;
    for (id = 0; id < TEST_PROCESSOR_COUNT; id++) {
        translation_semantics =
            translation_semantics && translation_mismatch[id] == 0;
    }
    ia64_test_check(&context, "local-tc-shootdown", translation_semantics,
                    EFI_DEVICE_ERROR, "stale-local-translation");
    ia64_test_check(&context, "global-tc-source-purge",
                    global_translation_source, EFI_DEVICE_ERROR,
                    "stale-source-translation");
    ia64_test_check(&context, "global-tc-remote-purge",
                    global_translation_remote, EFI_DEVICE_ERROR,
                    "stale-remote-translation");
    ia64_test_check(&context, "global-large-tc-source-purge-no-alat",
                    global_translation_high_source, EFI_DEVICE_ERROR,
                    "stale-high-source-translation");
    ia64_test_check(&context, "global-large-tc-remote-purge-no-alat",
                    global_translation_high_remote, EFI_DEVICE_ERROR,
                    "stale-high-remote-translation");
    partial_translation_purge = partial_translation_purge_check();
    ia64_test_check(&context, "partial-tc-overlap-purge",
                    partial_translation_purge, EFI_DEVICE_ERROR,
                    "partial-overlap-left-stale-translation");
    rid_translation_switch = rid_translation_switch_check();
    ia64_test_check(&context, "rid-translation-switch", rid_translation_switch,
                    EFI_DEVICE_ERROR, "rid-switch-used-stale-translation");
    translation_semantics = repeat_rounds;
    for (id = 0; id < TEST_PROCESSOR_COUNT; id++) {
        translation_semantics =
            translation_semantics && translation_churn_mismatch[id] == 0;
    }
    ia64_test_check(&context, "translation-cache-capacity-churn",
                    translation_semantics, EFI_DEVICE_ERROR,
                    "capacity-churn-used-stale-translation");
    translation_semantics = repeat_rounds;
    for (id = 0; id < TEST_PROCESSOR_COUNT; id++) {
        translation_semantics =
            translation_semantics && translation_write_mismatch[id] == 0;
    }
    ia64_test_check(&context, "translation-remap-word-store",
                    translation_semantics, EFI_DEVICE_ERROR,
                    "remap-stored-to-old-physical-page");
    if (atomic_16_supported) {
        atomic_semantics = repeat_rounds &&
                           atomic_pair.Low == TEST_RENDEZVOUS_ROUNDS *
                                                  TEST_PROCESSOR_COUNT *
                                                  TEST_ATOMIC_INCREMENTS &&
                           atomic_pair.High == TEST_ATOMIC_HIGH;
        ia64_test_check(&context, "atomic-16-byte-semaphore", atomic_semantics,
                        EFI_DEVICE_ERROR, "semaphore-contention-failed");
    }
    fetchadd_semantics =
        repeat_rounds &&
        fetchadd4_counter == TEST_RENDEZVOUS_ROUNDS * TEST_PROCESSOR_COUNT *
                                 TEST_SEMAPHORE_INCREMENTS &&
        fetchadd8_counter == TEST_RENDEZVOUS_ROUNDS * TEST_PROCESSOR_COUNT *
                                 TEST_SEMAPHORE_INCREMENTS;
    ia64_test_check(&context, "fetchadd4-fetchadd8-contention",
                    fetchadd_semantics, EFI_DEVICE_ERROR,
                    "fetchadd-contention-failed");
    cmpxchg_semantics = repeat_rounds &&
                        cmpxchg4_acq_counter == TEST_RENDEZVOUS_ROUNDS *
                                                    TEST_PROCESSOR_COUNT *
                                                    TEST_SEMAPHORE_INCREMENTS &&
                        cmpxchg4_rel_counter == cmpxchg4_acq_counter &&
                        cmpxchg8_acq_counter == cmpxchg4_acq_counter &&
                        cmpxchg8_rel_counter == cmpxchg4_acq_counter;
    ia64_test_check(&context, "cmpxchg4-cmpxchg8-contention", cmpxchg_semantics,
                    EFI_DEVICE_ERROR, "cmpxchg-contention-failed");
    packed_pfn_semantics =
        repeat_rounds &&
        packed_pfn_word.Fields.Flags ==
            (TEST_RENDEZVOUS_ROUNDS * (TEST_PROCESSOR_COUNT - 1U) *
                 TEST_PACKED_PFN_INCREMENTS +
             0x00560001U) &&
        packed_pfn_word.Fields.Count ==
            (UINT16)(TEST_RENDEZVOUS_ROUNDS * TEST_PACKED_PFN_INCREMENTS) &&
        packed_pfn_word.Fields.Type == 2;
    ia64_test_check(&context, "packed-pfn-halfword-cmpxchg8",
                    packed_pfn_semantics, EFI_DEVICE_ERROR,
                    "packed-pfn-update-lost");
    queued_semantics = repeat_rounds && queued_lock_tail == 0 &&
                       queued_counter == (UINT16)(TEST_RENDEZVOUS_ROUNDS *
                                                  TEST_PROCESSOR_COUNT *
                                                  TEST_QUEUED_INCREMENTS);
    ia64_test_check(&context, "queued-lock-guarded-word", queued_semantics,
                    EFI_DEVICE_ERROR, "queued-lock-contention-failed");
    guarded_semantics = repeat_rounds && guarded_lock == 0 &&
                        guarded_counter == (UINT16)(TEST_RENDEZVOUS_ROUNDS *
                                                    TEST_PROCESSOR_COUNT *
                                                    TEST_GUARDED_INCREMENTS);
    ia64_test_check(&context, "xchg8-guarded-word", guarded_semantics,
                    EFI_DEVICE_ERROR, "guarded-word-contention-failed");
    bit_lock_semantics = repeat_rounds && bit_lock == 0 &&
                         bit_lock_counter == (UINT16)(TEST_RENDEZVOUS_ROUNDS *
                                                      TEST_PROCESSOR_COUNT *
                                                      TEST_BIT_LOCK_INCREMENTS);
    ia64_test_check(&context, "cmpxchg4-bit-lock-guarded-word",
                    bit_lock_semantics, EFI_DEVICE_ERROR,
                    "bit-lock-contention-failed");
    slow_lock_semantics =
        repeat_rounds && slow_lock == 0 &&
        slow_lock_counter ==
            (UINT16)(TEST_RENDEZVOUS_ROUNDS * TEST_PROCESSOR_COUNT *
                     TEST_SLOW_LOCK_INCREMENTS);
    for (id = 0; id < TEST_PROCESSOR_COUNT; id++) {
        slow_path_total += slow_path_calls[id];
    }
    slow_lock_semantics = slow_lock_semantics && slow_path_total != 0;
    ia64_test_check(&context, "contended-call-rse-guarded-word",
                    slow_lock_semantics, EFI_DEVICE_ERROR,
                    "slow-lock-rse-contention-failed");
    translated_lock_semantics =
        repeat_rounds && translated_lock_pages[0].Lock == 0 &&
        translated_lock_pages[1].Lock == 0 &&
        translated_lock_pages[0].Lock8 == 0 &&
        translated_lock_pages[1].Lock8 == 0 &&
        translated_lock_pages[0].Counter ==
            (UINT16)((TEST_RENDEZVOUS_ROUNDS / 2U) * TEST_PROCESSOR_COUNT *
                     TEST_TRANSLATED_LOCK_INCREMENTS) &&
        translated_lock_pages[1].Counter == translated_lock_pages[0].Counter &&
        translated_lock_pages[0].Counter8 == translated_lock_pages[0].Counter &&
        translated_lock_pages[1].Counter8 == translated_lock_pages[0].Counter;
    ia64_test_check(&context, "translated-cmpxchg-guarded-word",
                    translated_lock_semantics, EFI_DEVICE_ERROR,
                    "translated-lock-contention-failed");
    high_translated_lock_semantics =
        repeat_rounds &&
        *(volatile UINT64 *)(UINTN)TEST_HIGH_TRANSLATED_LOCK_PA == 0 &&
        *(volatile UINT16 *)(UINTN)TEST_HIGH_TRANSLATED_COUNT_PA ==
            (UINT16)(TEST_RENDEZVOUS_ROUNDS * TEST_PROCESSOR_COUNT *
                     TEST_HIGH_TRANSLATED_LOCK_INCREMENTS);
    ia64_test_check(&context, "high-large-page-translated-guarded-word",
                    high_translated_lock_semantics, EFI_DEVICE_ERROR,
                    "high-large-page-lock-contention-failed");
    if (atomic_16_supported) {
        atomic_pair.Low = __builtin_bswap64(0x0123456789abcdefULL);
        atomic_pair.High = __builtin_bswap64(0xfedcba9876543210ULL);
        __asm__ volatile("mf;;" : : : "memory");
        big_endian_atomic =
            cmp8xchg16_big_endian(
                &atomic_pair.High, 0xfedcba9876543210ULL, 0x1111222233334444ULL,
                0x5555666677778888ULL) == 0xfedcba9876543210ULL &&
            atomic_pair.Low == __builtin_bswap64(0x1111222233334444ULL) &&
            atomic_pair.High == __builtin_bswap64(0x5555666677778888ULL);
        ia64_test_check(&context, "big-endian-16-byte-semaphore",
                        big_endian_atomic, EFI_DEVICE_ERROR,
                        "big-endian-semaphore-failed");
    }
    ia64_test_done(&context);
    return context.Failed == 0 ? EFI_SUCCESS : EFI_DEVICE_ERROR;
}

EFI_STATUS (*efi_entry_descriptor_reference)(EFI_HANDLE, EFI_SYSTEM_TABLE *)
    __attribute__((used)) = efi_main;
