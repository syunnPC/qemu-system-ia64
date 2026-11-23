/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 * Minimal CPU state for executing the local SAPIC register operations.
 */

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

typedef struct {
    int cpu_index;
    uint64_t cr[128];
    struct {
        uint64_t sapic_irr[4];
        uint64_t sapic_isr[4];
    } interrupt;
} CPUIA64State;

#define env_cpu(env) (env)
#define trace_ia64_sapic_vector(...) ((void)0)
#define ia64_sapic_update_interrupt(env) ((void)(env))

static unsigned clz64(uint64_t value)
{
    return value ? __builtin_clzll(value) : 64;
}

#include "sapic.c"

int main(void)
{
    CPUIA64State env = { 0 };
    unsigned eoi;
    uint64_t *irr = env.interrupt.sapic_irr;
    uint64_t *isr = env.interrupt.sapic_isr;
    int count;

    while ((count = scanf("%u %" SCNx64 " %" SCNx64 " %" SCNx64
                          " %" SCNx64 " %" SCNx64 " %" SCNx64
                          " %" SCNx64 " %" SCNx64 " %" SCNx64,
                          &eoi, &env.cr[IA64_CR_SAPIC_TPR],
                          &irr[0], &irr[1], &irr[2], &irr[3],
                          &isr[0], &isr[1], &isr[2], &isr[3])) == 10) {
        int vector;
        if (eoi) {
            vector = sapic_find_isr(&env);
            ia64_sapic_eoi(&env);
        } else {
            vector = ia64_sapic_get_ivr(&env);
        }
        printf("%d %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64
               " %" PRIu64 " %" PRIu64 " %" PRIu64 " %" PRIu64 "\n",
               vector, irr[0], irr[1], irr[2], irr[3],
               isr[0], isr[1], isr[2], isr[3]);
    }

    return count == EOF && !ferror(stdin) && !ferror(stdout) ? 0 : 1;
}
