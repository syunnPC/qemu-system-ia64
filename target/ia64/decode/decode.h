/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * TCG-independent IA-64 instruction decoder interface.
 */

#ifndef TARGET_IA64_DECODE_DECODE_H
#define TARGET_IA64_DECODE_DECODE_H

#include "target/ia64/decode/operand.h"

Ia64Instruction ia64_decode_insn(IA64SlotUnit unit, uint64_t raw,
                                 uint64_t address, uint8_t slot);
void ia64_apply_mlx_long_fixup(uint8_t template_code,
                               const uint64_t slots[IA64_BUNDLE_SLOTS],
                               int slot, Ia64Instruction *insn,
                               bool *skip_x_slot);

int64_t ia64_sign_extend(uint64_t value, unsigned width);
uint64_t ia64_low_mask(uint64_t len);
uint64_t ia64_bitfield_effective_len(uint64_t pos, uint64_t len);
uint64_t ia64_deposit_mask(uint64_t pos, uint64_t len);

bool ia64_opcode_is_store(Ia64Opcode opcode);
bool ia64_opcode_is_release_store(Ia64Opcode opcode);
bool ia64_opcode_is_load(Ia64Opcode opcode);

#endif /* TARGET_IA64_DECODE_DECODE_H */
