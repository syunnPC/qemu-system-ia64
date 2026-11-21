/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef TARGET_IA64_LLVM_H
#define TARGET_IA64_LLVM_H

#include "contrib/ia64-llvm/region.h"

typedef struct IA64LLVMRegion IA64LLVMRegion;
extern bool ia64_llvm_enabled;
IA64LLVMRegion *ia64_llvm_register(const uint64_t *words, unsigned bundles,
                                   bool stop, uint64_t pc, unsigned barrier,
                                   bool full_alat);
#endif
