/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef IA64_LLVM_REGION_H
#define IA64_LLVM_REGION_H

#include <stdbool.h>
#include <stdint.h>

/* A region contains whole bundles from a single QEMU code page. */
#define IA64_LLVM_MAX_BUNDLES 64
#define IA64_LLVM_ABI 2

typedef void *(*IA64LLVMReadPage)(void *env, uint64_t addr, uint32_t mmu);
typedef uint64_t (*IA64LLVMReadSig)(void *env, uint32_t reg);
typedef struct IA64LLVMAccess {
    IA64LLVMReadPage read_page;
    IA64LLVMReadSig read_sig;
} IA64LLVMAccess;
typedef uint32_t (*IA64LLVMRun)(uint64_t *gr, uint64_t *pr, void *env,
                                uint32_t mmu, const IA64LLVMAccess *access);
typedef struct IA64LLVMCode {
    IA64LLVMRun run;
    uint64_t dirty[2]; /* QEMU's stacked GR dirty bit numbers */
    uint64_t fr[2];    /* FR inputs of getf.sig */
    uint8_t sof;
    uint8_t mixes;
} IA64LLVMCode;

/* QEMU submits immutable bundle copies from a single compiler thread. */
typedef bool (*IA64LLVMCompile)(unsigned abi, const uint64_t *words,
                                unsigned bundles, IA64LLVMCode *out);
#endif
