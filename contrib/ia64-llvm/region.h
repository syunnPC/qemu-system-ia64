/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef IA64_LLVM_REGION_H
#define IA64_LLVM_REGION_H

#include <stdbool.h>
#include <stdint.h>

/* A region contains whole bundles from a single QEMU code page. */
#define IA64_LLVM_MAX_BUNDLES 64
#define IA64_LLVM_ABI 4
/* A partial exit encodes the instruction index relative to the entry. */
enum { IA64_LLVM_RETRY, IA64_LLVM_COMPLETE, IA64_LLVM_RESUME };
#define IA64_LLVM_COMPILE_SYMBOL "ia64_llvm_compile_v4"

typedef void *(*IA64LLVMReadPage)(void *env, uint64_t addr, uint32_t mmu);
typedef uint64_t (*IA64LLVMReadSig)(void *env, uint32_t reg);
typedef struct IA64LLVMAccess {
    IA64LLVMReadPage read_page;
    IA64LLVMReadSig read_sig;
    void (*observed_store)(void *env, void *host, uint64_t addr, uint64_t value,
                           unsigned size);
} IA64LLVMAccess;
/*
 * Generated code uses the host layout for QEMU registers and for the
 * unflagged, naturally aligned TCG memory fast path.
 */
typedef struct IA64LLVMLayout {
    uint32_t gr, pr, fr, fr_sig, dirty;
    uint32_t tlb_mask, tlb_table, read_tag, write_tag, addend;
    unsigned page_bits, entry_bits;
} IA64LLVMLayout;
typedef uint32_t (*IA64LLVMRun)(void *env, uint32_t mmu,
                                const IA64LLVMAccess *access, const void *tlb);
typedef struct IA64LLVMCode {
    IA64LLVMRun run;
    uint64_t dirty[2];     /* QEMU's stacked GR dirty bit numbers */
    uint64_t fr_inputs[2]; /* FR inputs of getf.sig */
    uint8_t sof;
    uint8_t mixes;
} IA64LLVMCode;

/* QEMU submits immutable bundle copies from a single compiler thread. */
typedef bool (*IA64LLVMCompile)(unsigned abi, const uint64_t *words,
                                unsigned bundles, IA64LLVMCode *out,
                                const IA64LLVMLayout *layout, bool full_alat);
#endif
