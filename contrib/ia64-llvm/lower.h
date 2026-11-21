/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef IA64_LLVM_LOWER_H
#define IA64_LLVM_LOWER_H

#include "region.h"

struct t1_ir;
bool ia64_llvm_lower(const struct t1_ir *ir, IA64LLVMCode *out);

#endif
