/* SPDX-License-Identifier: GPL-2.0-or-later */
/* State shim and finite input driver for selected production MMU functions. */

#define QEMU_ALWAYS_INLINE

typedef struct IA64TlbEntry {
    uint64_t va, pa, ps, pte, page_mask;
    uint32_t rid;
    uint8_t ar, pl;
    bool valid;
} IA64TlbEntry;

typedef struct IA64DTlb1Lookup {
    uint64_t page;
    uint32_t rid;
    uint8_t slot;
    bool valid;
} IA64DTlb1Lookup;

typedef struct CPUIA64State {
    uint64_t psr;
    struct {
        IA64TlbEntry tlb_data_l1[IA64_DTLB1_MAX];
        IA64DTlb1Lookup tlb_data_l1_lookup[IA64_DTLB1_LOOKUP_SIZE];
    } mmu;
} CPUIA64State;

/* OBSERVED C */

int main(void)
{
    CPUIA64State env = { 0 };
    const uint64_t va = UINT64_C(0x6123456789abc001);
    const uint64_t offset = va & 4095;
    const uint32_t rid = 17;

    for (unsigned ar = 0; ar < 8; ar++) {
        for (unsigned pl = 0; pl < 4; pl++) {
            for (unsigned cpl = 0; cpl < 4; cpl++) {
                for (unsigned present = 0; present < 2; present++) {
                    /* SDM Vol. 2 Figure 4-5 encodes P, PL and AR here. */
                    IA64TlbEntry entry = {
                        .pte = present | (pl << 7) | (ar << 9),
                        .ar = ar, .pl = pl,
                        .ps = 4096, .pa = 8192,
                    };
                    uint64_t pa;
                    uint8_t permission;

                    ia64_tlb_entry_translate(&entry, va, cpl, &pa, &permission);
                    if (pa != 8192 + offset) {
                        return 1;
                    }
                    printf("%u %u ", ia64_pte_perm(entry.pte, cpl), permission);
                }
            }
        }
    }
    puts("");
    for (unsigned cpl = 0; cpl < 4; cpl++) {
        for (unsigned request = 0; request < 16; request++) {
            env.psr = (uint64_t)cpl << 32;
            printf("%u ", ia64_probe_access_level(&env, request));
        }
    }
    puts("");
    for (unsigned flags = 0; flags < 256; flags++) {
        uint64_t page = ia64_merced_dtlb1_lookup_page(va);
        uint32_t index = ia64_merced_dtlb1_lookup_index(page, rid);
        IA64DTlb1Lookup *lookup = &env.mmu.tlb_data_l1_lookup[index];
        IA64TlbEntry *entry = &env.mmu.tlb_data_l1[0];

        *lookup = (IA64DTlb1Lookup) {
            .valid = flags & 1,
            .page = flags & 2 ? page : page ^ 1,
            .rid = flags & 4 ? rid : rid ^ 1,
            .slot = flags & 8 ? IA64_DTLB1_MAX : 0,
        };
        *entry = (IA64TlbEntry) {
            .valid = flags & 16,
            .ps = flags & 32 ? 0 : 4096,
            .rid = flags & 64 ? rid ^ 1 : rid,
            .va = flags & 128 ? va ^ (1 << 20) : va,
            .page_mask = UINT64_C(0x1ffffffffffff000),
        };
        printf("%d ", ia64_merced_dtlb1_lookup(&env, va, rid) + 1);
        printf("%u ", (unsigned)lookup->valid);
    }
    puts("");
    return 0;
}
