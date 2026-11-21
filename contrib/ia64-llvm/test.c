/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stddef.h>
#include <string.h>
#include "insn.h"
#include "region.h"

static IA64LLVMCache cache = { .limit = UINT64_C(1) << 30 };

typedef struct TestEntry {
    uintptr_t read, write, unused, addend;
} TestEntry;

typedef struct TestTLB {
    uintptr_t mask;
    TestEntry *table;
} TestTLB;

typedef struct Probe {
    uint64_t gr[128], pr[64], fr[128], fr_sig[2], dirty[2];
    uint64_t ram[1024];
    unsigned pages, calls, sig_calls, observed_stores;
    uint64_t sig;
} Probe;

static const IA64LLVMLayout layout = {
    .dirty = offsetof(Probe, dirty),
    .write_tag = offsetof(TestEntry, write),
    .gr = offsetof(Probe, gr),
    .pr = offsetof(Probe, pr),
    .fr = offsetof(Probe, fr),
    .fr_sig = offsetof(Probe, fr_sig),
    .tlb_mask = offsetof(TestTLB, mask),
    .tlb_table = offsetof(TestTLB, table),
    .read_tag = offsetof(TestEntry, read),
    .addend = offsetof(TestEntry, addend),
    .page_bits = 12,
    .entry_bits = 5,
};

static void *map(void *opaque, uint64_t addr, uint32_t mmu)
{
    Probe *p = opaque;
    p->calls++;
    assert(mmu == 3);
    return addr / 4096 < p->pages ? &p->ram[(addr / 4096) * 512] : NULL;
}

static uint64_t sig(void *opaque, uint32_t fr)
{
    assert(fr == 38);
    Probe *p = opaque;
    p->sig_calls++;
    return p->sig;
}

static uint64_t random_word(uint64_t *s)
{
    *s ^= *s << 13;
    *s ^= *s >> 7;
    *s ^= *s << 17;
    return *s;
}

static void test_application_register_moves(void)
{
    enum { AR_RUC = 45 };
    Ia64Instruction insn = {
        .valid = true, .unit = IA64_UNIT_M,
        .operands.common = { .destination = 8, .source1 = AR_RUC },
    };
    const Ia64Opcode moves[] = {
        IA64_OP_MOV_ARGR, IA64_OP_MOV_GRAR, IA64_OP_MOV_IMMAR,
    };

    for (size_t i = 0; i < sizeof(moves) / sizeof(moves[0]); i++) {
        insn.opcode = moves[i];
        assert(!ia64_llvm_insn_supported(&insn));
    }
}

static void observed_store(void *opaque, void *host, uint64_t addr,
                           uint64_t value, unsigned size)
{
    Probe *p = opaque;
    /* The second virtual page aliases the first. */
    assert(host == (uint8_t *)p->ram + (addr & 4095));
    memcpy(host, &value, size);
    p->observed_stores++;
}

static void test_late_store_fault(IA64LLVMCompile compile,
                                  const uint64_t *arithmetic,
                                  const uint64_t *store_load)
{
    uint64_t words[2 * IA64_LLVM_MAX_BUNDLES];
    size_t tail = IA64_LLVM_MAX_BUNDLES - 2;
    for (size_t i = 0; i < tail; i++) {
        memcpy(&words[i * 2], arithmetic, 16);
    }
    memcpy(&words[tail * 2], store_load, 32);

    IA64LLVMCode code;
    Probe p = { .gr = { [13] = 1, [18] = 7, [19] = 5, [21] = 13, [24] = 3 } };
    TestEntry entry = { .addend = (uintptr_t)p.ram };
    TestTLB tlb = { 0, &entry };
    static const IA64LLVMAccess access = { .read_page = map, .read_sig = sig };
    assert(compile(IA64_LLVM_ABI, words, IA64_LLVM_MAX_BUNDLES, &code, &layout,
                   false, &cache));
    assert(code.run(&p, 3, &access, &tlb) == IA64_LLVM_RESUME + 3 * (tail + 1));
    assert(p.ram[0] == 7 && p.gr[32] == 12 && p.gr[33] == 14);
    assert(p.dirty[0] == 3);
}

static void test_memory(IA64LLVMCompile compile, const char *image,
                        const uint64_t *arithmetic)
{
    uint64_t words[10];
    IA64LLVMCode code;
    FILE *f = fopen(image, "rb");
    assert(f && fread(words, 16, 5, f) == 5);
    fclose(f);
    for (unsigned test = 0; test < 16; test++) {
        if (test % 8 == 0) {
            assert(compile(IA64_LLVM_ABI, words, 5, &code, &layout, test >= 8,
                           &cache));
            assert(code.sof == 5 && code.dirty[0] == 27 && !code.dirty[1]);
        }
        Probe p = { 0 };
        TestEntry entries[2];
        TestTLB tlb = { sizeof(TestEntry), entries };
        IA64LLVMAccess access = { map, sig, observed_store };
        for (size_t i = 0; i < 2; i++) {
            uintptr_t page = i * 4096;
            entries[i] = (TestEntry) { .read = page,
                                       .write = page,
                                       .addend = (uintptr_t)p.ram - page };
        }
        unsigned fault = test % 8;
        p.ram[0] = UINT64_C(0x12345678fbcd9876);
        p.gr[10] = 0;
        p.gr[12] = 8;
        p.gr[35] = 16;
        p.gr[11] = 4096 + 16;
        p.gr[9] = 4096 + 24;
        p.gr[13] = 24;
        p.pr[0] = p.pr[7] = 1;
        switch (fault) {
        case 1:
            p.gr[10]++;
            break; /* first load */
        case 2:
            entries[0].write |= 1;
            break; /* first store */
        case 3:
            p.gr[11]++;
            break; /* load after store */
        case 4:
            entries[1].write |= 1;
            break; /* second store */
        case 5:
            p.gr[13]++;
            break; /* final load */
        case 6:
            entries[1].read |= 1;
            break; /* withheld mapping */
        case 7:
            p.gr[9]++;
            break; /* unaligned store */
        default:
            assert(fault == 0);
            break;
        }
        uint32_t status = code.run(&p, 3, &access, &tlb);
        unsigned resume[] = { 1, 0, 0, 2 + 6, 2 + 9, 2 + 12, 2 + 6, 2 + 9 };
        assert(status == resume[fault]);
        bool store1 = !fault || fault >= 3;
        bool load2 = !fault || fault == 4 || fault == 5 || fault == 7;
        bool store2 = !fault || fault == 5;
        assert(p.ram[2] == (store1 ? (uint32_t)p.ram[0] : 0));
        assert(p.ram[3] == (store2 ? (uint32_t)p.ram[0] : 0));
        assert(p.gr[35] == (store1 ? 20 : 16));
        assert(p.gr[36] == store1 && p.gr[33] == store1);
        assert(p.pr[6] == store1 && p.pr[7] == !store1);
        assert(p.gr[10] == (store1 ? 8 : fault == 1));
        assert(p.gr[32] == (store1 ? p.ram[0] : 0));
        assert(p.gr[11] == 4112u + (fault == 3) + 4 * load2);
        assert(p.gr[18] == (load2 ? (uint32_t)p.ram[0] : 0));
        assert(p.gr[22] == (!fault ? (uint32_t)p.ram[0] : 0));
        unsigned observed = test & 8 ? store1 + store2 : 0;
        assert(p.observed_stores == observed);
        assert(p.dirty[0] == (status > 1 ? 27 : 0));
    }
    test_late_store_fault(compile, arithmetic, &words[6]);
}

static void test_integer(IA64LLVMCompile compile, const char *image)
{
    uint64_t words[12], seed = 0xd7b3b689;
    const uint64_t ignored_b = UINT64_C(3) << 37;
    IA64LLVMCode code;
    static const IA64LLVMAccess access = { .read_page = map, .read_sig = sig };
    FILE *f = fopen(image, "rb");
    assert(f && fread(words, 16, 5, f) == 5);
    fclose(f);
    /* A BBB bundle of ignored B-unit opcodes must preserve every register. */
    words[10] = 0x17 | (ignored_b << 5) | (ignored_b << 46);
    words[11] = (ignored_b >> 18) | (ignored_b << 23);
    assert(compile(IA64_LLVM_ABI, words, 6, &code, &layout, false, &cache));
    for (unsigned i = 0; i < 128; i++) {
        uint64_t x = random_word(&seed), y = random_word(&seed);
        uint64_t region = ((y >> 30) & 3) << 61;
        uint64_t mask = UINT64_C(0x1ff) << 48;
        Probe p = { .gr = { [8] = x, [9] = y }, .pr = { 1 } };
        assert(code.run(&p, 3, &access, NULL) == IA64_LLVM_COMPLETE);
        assert(p.gr[16] == (region | (uint32_t)(x + y)));
        assert(p.gr[17] == (region | (uint32_t)((x << 3) + y)));
        assert(p.gr[18] == x - 33 && p.gr[19] == (~UINT64_C(7) & ~x));
        assert(p.gr[20] == (UINT64_C(125) << 57) && p.gr[21] == (y | mask));
        assert(p.gr[22] == x - y - 1);
        assert(p.gr[23] == ((x << 61) | (y & (UINT64_MAX >> 3))));
        assert(p.gr[24] == (uint64_t)((int64_t)x >> 57));
        assert(p.gr[25] == (uint64_t)(int64_t)(int16_t)x);
        assert(p.gr[26] == (uint32_t)y && p.gr[27] == (UINT64_C(1) << 63));
        assert(p.pr[0] == 1 && p.pr[62] == 1 && p.pr[60] == (-17 < (int64_t)y));
        assert(p.gr[8] == x);
    }
}

static void test_cache_limit(IA64LLVMCompile compile, const uint64_t *words)
{
    const IA64LLVMAccess access = { .read_page = map, .read_sig = sig };
    IA64LLVMCode first, other;
    Probe p = { .gr = { [8] = 1, [9] = 2 } };

    cache.limit = 0;
    assert(!compile(IA64_LLVM_ABI, words, 1, &first, &layout, false, &cache));
    assert(!first.run && cache.used == 0);
    cache.limit = 1;
    assert(!compile(IA64_LLVM_ABI, words, 1, &first, &layout, false, &cache));
    assert(!first.run && cache.used == 0);

    cache.limit = UINT64_C(1) << 30;
    assert(compile(IA64_LLVM_ABI, words, 1, &first, &layout, false, &cache));
    uint64_t size = cache.used;
    assert(size && size < cache.limit / 2);

    cache.limit = 2 * size - 1;
    assert(!compile(IA64_LLVM_ABI, words, 1, &other, &layout, false, &cache));
    assert(!other.run && cache.used == size);
    cache.limit++;
    assert(compile(IA64_LLVM_ABI, words, 1, &other, &layout, false, &cache));
    assert(cache.used == cache.limit);
    assert(!compile(IA64_LLVM_ABI, words, 1, &other, &layout, false, &cache));
    assert(!other.run && cache.used == cache.limit);
    assert(first.run(&p, 0, &access, NULL) == IA64_LLVM_COMPLETE);
    assert(p.gr[16] == (UINT64_C(1) << 32 | 2));

    cache.limit = UINT64_C(1) << 30;
}

int main(int argc, char **argv)
{
    IA64LLVMCode code;
    uint64_t words[2 * IA64_LLVM_MAX_BUNDLES];
    static const IA64LLVMAccess access = { .read_page = map, .read_sig = sig };
    static const uint64_t shifts[] = { 0, 1, 31, 63, 64, 65, UINT64_MAX };
    assert(argc == 5);
    test_application_register_moves();
    FILE *f = fopen(argv[2], "rb");
    assert(f != NULL);
    size_t n = fread(words, 16, IA64_LLVM_MAX_BUNDLES, f);
    assert(n == 5 && feof(f));
    fclose(f);
    void *lib = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    if (!lib) {
        fprintf(stderr, "%s\n", dlerror());
        return 1;
    }
    IA64LLVMCompile compile =
        (IA64LLVMCompile)dlsym(lib, IA64_LLVM_COMPILE_SYMBOL);
    assert(compile && !compile(0, words, n, &code, &layout, false, &cache) &&
           !compile(IA64_LLVM_ABI, words, 0, &code, &layout, false, &cache));
    test_cache_limit(compile, words);
    assert(compile(IA64_LLVM_ABI, words, n, &code, &layout, false, &cache));
    assert(code.dirty[0] == 3 && code.dirty[1] == 0 && code.sof >= 2);
    assert(code.fr_inputs[0] == (UINT64_C(1) << 38) && code.fr_inputs[1] == 0 &&
           code.mixes == 2);
    uint64_t seed = 0xb1035f8701ULL;
    Probe p = { 0 };
    TestEntry entries[2];
    TestTLB tlb = { sizeof(TestEntry), entries };
    for (unsigned trial = 0; trial < 512; trial++) {
        uint64_t *gr = p.gr, *pr = p.pr, wanted[128], predicate[64];
        for (unsigned i = 0; i < 128; i++) {
            gr[i] = random_word(&seed);
        }
        for (unsigned i = 0; i < 64; i++) {
            pr[i] = random_word(&seed) & 1;
        }
        gr[0] = 0;
        pr[0] = 1;
        gr[10] = 16;
        gr[11] = 4104;
        gr[13] = shifts[trial % (sizeof(shifts) / sizeof(shifts[0]))];
        p.ram[2] = random_word(&seed);
        p.ram[513] = random_word(&seed);
        p.sig = random_word(&seed);
        if (trial % 4 == 3) {
            gr[11]++;
        }
        memcpy(wanted, gr, sizeof(wanted));
        memcpy(predicate, pr, sizeof(predicate));
        p.pages = trial % 4;
        p.calls = 0;
        bool direct = (trial & 4) != 0;
        for (size_t i = 0; i < 2; i++) {
            uintptr_t page = i * 4096;
            entries[i].read = direct && i < p.pages ? page : page | 1;
            entries[i].addend = (uintptr_t)&p.ram[i * 512] - page;
        }
        p.fr_sig[0] = direct ? UINT64_C(1) << 38 : 0;
        p.fr[38] = direct ? p.sig : ~p.sig;
        p.sig_calls = 0;
        bool ok = code.run(&p, 3, &access, &tlb);
        if (ok) {
            assert(p.calls == (direct ? 0 : 2));
            assert(p.sig_calls == (direct ? 0 : 1));
        }
        assert(ok == (trial % 4 == 2));
        if (ok) {
            uint64_t a = wanted[8], b = wanted[9], shift = wanted[13];
            wanted[16] = (a << 32) | (uint32_t)b;
            wanted[17] = (a & ~UINT64_C(0xffffffff)) | (b >> 32);
            wanted[18] = p.ram[2];
            wanted[19] = wanted[16] ^ wanted[17];
            wanted[20] = (a << 53) | (b >> 11);
            wanted[21] = (uint32_t)p.ram[513];
            wanted[22] = shift < 64 ? a >> shift : 0;
            wanted[23] = (int64_t)a >> (shift < 64 ? shift : 63);
            wanted[24] = p.sig;
            wanted[25] = shift < 64 ? b << shift : 0;
            predicate[6] = (int32_t)a < (int32_t)b;
            predicate[7] = !predicate[6];
            wanted[32] = wanted[19] + wanted[18];
            wanted[33] = wanted[21] ^ wanted[24];
        }
        assert(!memcmp(wanted, gr, sizeof(wanted)) &&
               !memcmp(predicate, pr, sizeof(predicate)));
    }
    test_memory(compile, argv[3], &words[8]);
    test_integer(compile, argv[4]);
    puts("658 region executions and cache limit checks passed");
    return 0;
}
