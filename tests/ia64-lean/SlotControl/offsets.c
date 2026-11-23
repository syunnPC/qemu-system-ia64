/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "cpu.h"
#include "decode/decode.h"

int main(void)
{
    printf("%zu %zu %zu %d %d\n",
           offsetof(CPUIA64State, last_successful_bundle),
           offsetof(CPUIA64State, instruction_group_start),
           offsetof(CPUIA64State, exception_state.fault_slot),
           IA64_OP_NOP, IA64_OP_ADD);
    return 0;
}
