/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef IA64_LLVM_LOWER_H
#define IA64_LLVM_LOWER_H

#include <llvm-c/Core.h>
#include "target/ia64/decode/decode.h"

/* Returns i64 for a GR result, i1 for a predicate pair, or NULL. */
LLVMValueRef ia64_llvm_lift_integer(LLVMBuilderRef b, const Ia64Instruction *in,
                                    LLVMValueRef lhs, LLVMValueRef rhs);

#endif
