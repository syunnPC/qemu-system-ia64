/* SPDX-License-Identifier: LGPL-2.1-or-later */
/*
 * Definition of TCGTBCPUState.
 */

#ifndef EXEC_TB_CPU_STATE_H
#define EXEC_TB_CPU_STATE_H

#include "exec/vaddr.h"

typedef struct TCGTBCPUState {
    vaddr pc;
    uint64_t cs_base;
    uint32_t flags;
    uint32_t cflags;
} TCGTBCPUState;

#endif
