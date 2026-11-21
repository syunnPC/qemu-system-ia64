/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef IA64_LLVM_JIT_H
#define IA64_LLVM_JIT_H

#include <llvm-c/Core.h>
#include "region.h"

#ifdef __cplusplus
extern "C" {
#endif

/* Takes ownership of the module and its private LLVM context. */
bool ia64_llvm_compile_module(LLVMModuleRef module, const char *name,
                              IA64LLVMCode *out, IA64LLVMCache *cache);
#ifdef __cplusplus
}
#endif
#endif
