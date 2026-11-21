/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <assert.h>
#include <dlfcn.h>
#include <stdio.h>
#include <string.h>
#include "region.h"

typedef struct Probe {
    uint64_t ram[1024];
    unsigned pages, calls;
    uint64_t sig;
} Probe;

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
    return ((Probe *)opaque)->sig;
}

static uint64_t random_word(uint64_t *s)
{
    *s ^= *s << 13;
    *s ^= *s >> 7;
    *s ^= *s << 17;
    return *s;
}

int main(int argc, char **argv)
{
    IA64LLVMCode code;
    uint64_t words[2 * IA64_LLVM_MAX_BUNDLES];
    static const IA64LLVMAccess access = { map, sig };
    static const uint64_t shifts[] = { 0, 1, 31, 63, 64, 65, UINT64_MAX };
    assert(argc == 3);
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
    IA64LLVMCompile compile = (IA64LLVMCompile)dlsym(lib, "ia64_llvm_compile");
    assert(compile && !compile(0, words, n, &code) &&
           !compile(IA64_LLVM_ABI, words, 0, &code));
    assert(compile(IA64_LLVM_ABI, words, n, &code));
    assert(code.dirty[0] == 3 && code.dirty[1] == 0 && code.sof >= 2);
    assert(code.fr[0] == (UINT64_C(1) << 38) && code.fr[1] == 0 &&
           code.mixes == 2);
    uint64_t seed = 0xb1035f8701ULL;
    Probe p = { 0 };
    for (unsigned trial = 0; trial < 512; trial++) {
        uint64_t gr[128], pr[64], wanted[128], predicate[64];
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
        memcpy(wanted, gr, sizeof(gr));
        memcpy(predicate, pr, sizeof(pr));
        p.pages = trial % 4;
        p.calls = 0;
        bool ok = code.run(gr, pr, &p, 3, &access);
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
        assert(!memcmp(wanted, gr, sizeof(gr)) &&
               !memcmp(predicate, pr, sizeof(pr)));
    }
    puts("512 region executions passed");
    return 0;
}
