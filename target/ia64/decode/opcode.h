/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * IA-64 decoded opcode identifiers.
 */

#ifndef TARGET_IA64_DECODE_OPCODE_H
#define TARGET_IA64_DECODE_OPCODE_H

typedef enum Ia64Opcode {
#define IA64_OPCODE(name, generator) IA64_OP_ ## name,
#include "opcode.inc"
#undef IA64_OPCODE
    IA64_OP_COUNT,
} Ia64Opcode;

#endif /* TARGET_IA64_DECODE_OPCODE_H */
