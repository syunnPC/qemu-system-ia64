/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * EFI Byte Code interpreter and IA-64 call bridge.
 *
 * This is an original implementation of chapter 19 of the EFI 1.10
 * specification.  Its execution state, decoder, stack frames, thunk registry,
 * and generated thunk layout are private to this firmware and do not derive
 * from another firmware interpreter.
 */

#include "fw-ebc.h"
#include "fw-services.h"

#define FW_EBC_REVISION             0x00010000ULL
#define FW_EBC_STACK_BYTES          (64U * 1024U)
#define FW_EBC_NATIVE_ARGUMENTS     16U
#define FW_EBC_THUNK_SLOTS          96U
#define FW_EBC_THUNK_CODE_BYTES     64U
#define FW_EBC_THUNK_TOTAL_BYTES    (16U + FW_EBC_THUNK_CODE_BYTES)
#define FW_EBC_THUNK_ALIGNMENT      16U

#define FW_EBC_FLAG_CONDITION       0x01U
#define FW_EBC_FLAG_SINGLE_STEP     0x02U
#define FW_EBC_FLAG_MASK            0x03U

#define FW_EBC_OPCODE_MASK          0x3fU
#define FW_EBC_MODIFIER_HIGH        0x80U
#define FW_EBC_MODIFIER_LOW         0x40U

#define FW_EBC_OP_BREAK             0x00U
#define FW_EBC_OP_JUMP              0x01U
#define FW_EBC_OP_JUMP8             0x02U
#define FW_EBC_OP_CALL              0x03U
#define FW_EBC_OP_RETURN            0x04U
#define FW_EBC_OP_COMPARE_EQ        0x05U
#define FW_EBC_OP_COMPARE_SLE       0x06U
#define FW_EBC_OP_COMPARE_SGE       0x07U
#define FW_EBC_OP_COMPARE_ULE       0x08U
#define FW_EBC_OP_COMPARE_UGE       0x09U
#define FW_EBC_OP_NOT               0x0aU
#define FW_EBC_OP_NEGATE            0x0bU
#define FW_EBC_OP_ADD               0x0cU
#define FW_EBC_OP_SUBTRACT          0x0dU
#define FW_EBC_OP_MULTIPLY          0x0eU
#define FW_EBC_OP_MULTIPLY_UNSIGNED 0x0fU
#define FW_EBC_OP_DIVIDE            0x10U
#define FW_EBC_OP_DIVIDE_UNSIGNED   0x11U
#define FW_EBC_OP_REMAINDER         0x12U
#define FW_EBC_OP_REMAINDER_UNSIGNED 0x13U
#define FW_EBC_OP_AND               0x14U
#define FW_EBC_OP_OR                0x15U
#define FW_EBC_OP_XOR               0x16U
#define FW_EBC_OP_SHIFT_LEFT        0x17U
#define FW_EBC_OP_SHIFT_RIGHT       0x18U
#define FW_EBC_OP_SHIFT_ARITHMETIC  0x19U
#define FW_EBC_OP_EXTEND_BYTE       0x1aU
#define FW_EBC_OP_EXTEND_WORD       0x1bU
#define FW_EBC_OP_EXTEND_DWORD      0x1cU
#define FW_EBC_OP_MOVE_BW           0x1dU
#define FW_EBC_OP_MOVE_WW           0x1eU
#define FW_EBC_OP_MOVE_DW           0x1fU
#define FW_EBC_OP_MOVE_QW           0x20U
#define FW_EBC_OP_MOVE_BD           0x21U
#define FW_EBC_OP_MOVE_WD           0x22U
#define FW_EBC_OP_MOVE_DD           0x23U
#define FW_EBC_OP_MOVE_QD           0x24U
#define FW_EBC_OP_MOVE_SIGNED_W     0x25U
#define FW_EBC_OP_MOVE_SIGNED_D     0x26U
#define FW_EBC_OP_MOVE_QQ           0x28U
#define FW_EBC_OP_LOAD_SPECIAL      0x29U
#define FW_EBC_OP_STORE_SPECIAL     0x2aU
#define FW_EBC_OP_PUSH              0x2bU
#define FW_EBC_OP_POP               0x2cU
#define FW_EBC_OP_COMPARE_IMM_EQ    0x2dU
#define FW_EBC_OP_COMPARE_IMM_SLE   0x2eU
#define FW_EBC_OP_COMPARE_IMM_SGE   0x2fU
#define FW_EBC_OP_COMPARE_IMM_ULE   0x30U
#define FW_EBC_OP_COMPARE_IMM_UGE   0x31U
#define FW_EBC_OP_MOVE_NW           0x32U
#define FW_EBC_OP_MOVE_ND           0x33U
#define FW_EBC_OP_PUSH_NATURAL      0x35U
#define FW_EBC_OP_POP_NATURAL       0x36U
#define FW_EBC_OP_MOVE_IMMEDIATE    0x37U
#define FW_EBC_OP_MOVE_INDEX        0x38U
#define FW_EBC_OP_MOVE_RELATIVE     0x39U

typedef enum {
    FwEbcFaultNone,
    FwEbcFaultDivideByZero,
    FwEbcFaultBreakpoint,
    FwEbcFaultInvalidOpcode,
    FwEbcFaultStack,
    FwEbcFaultAlignment,
    FwEbcFaultEncoding,
    FwEbcFaultBadBreak,
    FwEbcFaultSingleStep,
} FW_EBC_FAULT;

typedef struct {
    UINT8 *pc;
    UINT64 registers[8];
    UINT64 flags;
    UINTN stack_low;
    UINTN stack_high;
    UINTN final_frame;
    UINTN argument_limit;
    EFI_HANDLE image;
    UINT32 compiler_revision;
    FW_EBC_FAULT fault;
    BOOLEAN complete;
} FW_EBC_MACHINE;

typedef struct {
    UINT64 entry;
    UINT64 gp;
} FW_EBC_FUNCTION_DESCRIPTOR;

typedef struct _FW_EBC_PROTOCOL FW_EBC_PROTOCOL;
typedef EFI_STATUS (*FW_EBC_CACHE_FLUSH)(EFI_PHYSICAL_ADDRESS Start,
                                         UINT64 Length);

struct _FW_EBC_PROTOCOL {
    EFI_STATUS (*CreateThunk)(FW_EBC_PROTOCOL *This, EFI_HANDLE ImageHandle,
                              VOID *ByteCodeEntry, VOID **Thunk);
    EFI_STATUS (*UnloadImage)(FW_EBC_PROTOCOL *This, EFI_HANDLE ImageHandle);
    EFI_STATUS (*RegisterICacheFlush)(FW_EBC_PROTOCOL *This,
                                      FW_EBC_CACHE_FLUSH Flush);
    EFI_STATUS (*GetVersion)(FW_EBC_PROTOCOL *This, UINT64 *Version);
};

typedef struct {
    BOOLEAN active;
    BOOLEAN image_entry;
    EFI_HANDLE owner;
    UINT8 *allocation;
    FW_EBC_FUNCTION_DESCRIPTOR *descriptor;
    UINT8 *byte_code;
} FW_EBC_THUNK;

static const UINT8 mEbcProtocolGuid[16] = {
    0xd1, 0x6d, 0xac, 0x13, 0xd0, 0x73, 0xd4, 0x11,
    0xb0, 0x6b, 0x00, 0xaa, 0x00, 0xbd, 0x6d, 0xe7,
};

static FW_EBC_PROTOCOL mEbcProtocol;
static FW_EBC_THUNK mEbcThunks[FW_EBC_THUNK_SLOTS];
static FW_EBC_CACHE_FLUSH mEbcCacheFlush;
static BOOLEAN mEbcProtocolInstalled;

extern UINT64 fw_ebc_capture_context(VOID);
extern UINT64 fw_ebc_call_native(UINTN FunctionDescriptor, UINTN ArgumentBase,
                                 UINTN ArgumentLimit);

static UINT64 fw_ebc_mask(UINTN bytes)
{
    return bytes >= 8U ? ~0ULL : (1ULL << (bytes * 8U)) - 1ULL;
}

static UINT64 fw_ebc_read(UINTN address, UINTN bytes)
{
    const volatile UINT8 *source = (const volatile UINT8 *)address;
    UINT64 value = 0;
    UINTN i;

    for (i = 0; i < bytes; i++) {
        value |= (UINT64)source[i] << (i * 8U);
    }
    return value;
}

static VOID fw_ebc_write(UINTN address, UINT64 value, UINTN bytes)
{
    volatile UINT8 *destination = (volatile UINT8 *)address;
    UINTN i;

    for (i = 0; i < bytes; i++) {
        destination[i] = (UINT8)(value >> (i * 8U));
    }
}

static INT64 fw_ebc_sign_extend(UINT64 value, UINTN bits)
{
    UINT64 sign;

    if (bits == 64U) {
        return (INT64)value;
    }
    sign = 1ULL << (bits - 1U);
    return (INT64)((value ^ sign) - sign);
}

static BOOLEAN fw_ebc_natural_offset(UINT64 encoding, UINTN bytes,
                                     INT64 *offset)
{
    UINTN bits = bytes * 8U;
    UINTN payload_bits;
    UINTN natural_bits;
    UINTN constant_bits;
    UINT64 natural_mask;
    UINT64 constant_mask;
    UINT64 natural_units;
    UINT64 constant_units;
    UINT64 magnitude;

    if (offset == NULL || (bytes != 2U && bytes != 4U && bytes != 8U)) {
        return 0;
    }
    payload_bits = bits - 4U;
    natural_bits = ((encoding >> payload_bits) & 7U) * bytes;
    if (natural_bits > payload_bits) {
        return 0;
    }
    constant_bits = payload_bits - natural_bits;
    natural_mask = natural_bits == 0 ? 0 : (1ULL << natural_bits) - 1ULL;
    constant_mask = constant_bits == 0 ? 0 : (1ULL << constant_bits) - 1ULL;
    natural_units = encoding & natural_mask;
    constant_units = (encoding >> natural_bits) & constant_mask;
    magnitude = constant_units + natural_units * sizeof(UINTN);
    *offset = (encoding & (1ULL << (bits - 1U))) != 0
                  ? (INT64)(0ULL - magnitude)
                  : (INT64)magnitude;
    return 1;
}

static EFI_STATUS fw_ebc_fail(FW_EBC_MACHINE *machine, FW_EBC_FAULT fault,
                              EFI_STATUS status)
{
    machine->fault = fault;
    return status;
}

static BOOLEAN fw_ebc_stack_range(const FW_EBC_MACHINE *machine, UINTN address,
                                  UINTN bytes)
{
    return address >= machine->stack_low && address <= machine->stack_high &&
           bytes <= machine->stack_high - address;
}

static EFI_STATUS fw_ebc_push(FW_EBC_MACHINE *machine, UINT64 value,
                              UINTN bytes)
{
    UINTN sp = (UINTN)machine->registers[0];

    if (sp < machine->stack_low + bytes || sp > machine->stack_high) {
        return fw_ebc_fail(machine, FwEbcFaultStack, EFI_LOAD_ERROR);
    }
    sp -= bytes;
    fw_ebc_write(sp, value, bytes);
    machine->registers[0] = sp;
    return EFI_SUCCESS;
}

static EFI_STATUS fw_ebc_pop(FW_EBC_MACHINE *machine, UINTN bytes,
                             UINT64 *value)
{
    UINTN sp = (UINTN)machine->registers[0];

    if (value == NULL || !fw_ebc_stack_range(machine, sp, bytes)) {
        return fw_ebc_fail(machine, FwEbcFaultStack, EFI_LOAD_ERROR);
    }
    *value = fw_ebc_read(sp, bytes);
    machine->registers[0] = sp + bytes;
    return EFI_SUCCESS;
}

static UINT64 fw_ebc_operand(const FW_EBC_MACHINE *machine, UINT8 reg,
                             BOOLEAN indirect, INT64 displacement, UINTN bytes)
{
    UINT64 location = machine->registers[reg] + (UINT64)displacement;

    return indirect ? fw_ebc_read((UINTN)location, bytes) : location;
}

static VOID fw_ebc_store_operand(FW_EBC_MACHINE *machine, UINT8 reg,
                                 BOOLEAN indirect, INT64 displacement,
                                 UINT64 value, UINTN bytes)
{
    value &= fw_ebc_mask(bytes);
    if (indirect) {
        fw_ebc_write((UINTN)(machine->registers[reg] + (UINT64)displacement),
                     value, bytes);
    } else {
        machine->registers[reg] = value;
    }
}

static BOOLEAN fw_ebc_condition(const FW_EBC_MACHINE *machine, UINT8 encoding,
                                UINT8 conditional_bit, UINT8 desired_bit)
{
    BOOLEAN condition;

    if ((encoding & conditional_bit) == 0) {
        return 1;
    }
    condition = (machine->flags & FW_EBC_FLAG_CONDITION) != 0;
    return condition == ((encoding & desired_bit) != 0);
}

static EFI_STATUS fw_ebc_set_pc(FW_EBC_MACHINE *machine, UINTN target)
{
    if (target == 0 || (target & 1U) != 0) {
        return fw_ebc_fail(machine, FwEbcFaultAlignment, EFI_LOAD_ERROR);
    }
    machine->pc = (UINT8 *)target;
    return EFI_SUCCESS;
}

static BOOLEAN fw_ebc_compare(UINT8 operation, UINT64 left, UINT64 right,
                              UINTN bytes)
{
    UINT64 mask = fw_ebc_mask(bytes);
    INT64 signed_left;
    INT64 signed_right;

    left &= mask;
    right &= mask;
    signed_left = fw_ebc_sign_extend(left, bytes * 8U);
    signed_right = fw_ebc_sign_extend(right, bytes * 8U);
    switch (operation) {
    case FW_EBC_OP_COMPARE_EQ:
    case FW_EBC_OP_COMPARE_IMM_EQ:
        return left == right;
    case FW_EBC_OP_COMPARE_SLE:
    case FW_EBC_OP_COMPARE_IMM_SLE:
        return signed_left <= signed_right;
    case FW_EBC_OP_COMPARE_SGE:
    case FW_EBC_OP_COMPARE_IMM_SGE:
        return signed_left >= signed_right;
    case FW_EBC_OP_COMPARE_ULE:
    case FW_EBC_OP_COMPARE_IMM_ULE:
        return left <= right;
    default:
        return left >= right;
    }
}

static VOID fw_ebc_set_condition(FW_EBC_MACHINE *machine, BOOLEAN value)
{
    machine->flags = (machine->flags & ~FW_EBC_FLAG_CONDITION) |
                     (value ? FW_EBC_FLAG_CONDITION : 0U);
}

static EFI_STATUS fw_ebc_data_instruction(FW_EBC_MACHINE *machine)
{
    UINT8 encoded_opcode = machine->pc[0];
    UINT8 operation = encoded_opcode & FW_EBC_OPCODE_MASK;
    UINT8 operands = machine->pc[1];
    UINTN width = (encoded_opcode & FW_EBC_MODIFIER_LOW) != 0 ? 8U : 4U;
    UINTN length = (encoded_opcode & FW_EBC_MODIFIER_HIGH) != 0 ? 4U : 2U;
    UINT8 destination = operands & 7U;
    UINT8 source = (operands >> 4) & 7U;
    BOOLEAN destination_indirect = (operands & 0x08U) != 0;
    BOOLEAN source_indirect = (operands & 0x80U) != 0;
    INT64 source_offset = 0;
    UINT64 left = 0;
    UINT64 right;
    UINT64 result;

    if ((encoded_opcode & FW_EBC_MODIFIER_HIGH) != 0) {
        UINT64 encoded = fw_ebc_read((UINTN)machine->pc + 2U, 2U);

        if (source_indirect) {
            if (!fw_ebc_natural_offset(encoded, 2U, &source_offset)) {
                return fw_ebc_fail(machine, FwEbcFaultEncoding,
                                   EFI_UNSUPPORTED);
            }
        } else {
            source_offset = fw_ebc_sign_extend(encoded, 16U);
        }
    }

    if (operation >= FW_EBC_OP_EXTEND_BYTE &&
        operation <= FW_EBC_OP_EXTEND_DWORD) {
        UINTN source_width = operation == FW_EBC_OP_EXTEND_BYTE   ? 1U
                             : operation == FW_EBC_OP_EXTEND_WORD ? 2U
                                                                  : 4U;

        right = fw_ebc_operand(machine, source, source_indirect, source_offset,
                               source_width);
        result = (UINT64)fw_ebc_sign_extend(right & fw_ebc_mask(source_width),
                                            source_width * 8U);
    } else {
        right = fw_ebc_operand(machine, source, source_indirect, source_offset,
                               width) &
                fw_ebc_mask(width);
        if (operation != FW_EBC_OP_NOT && operation != FW_EBC_OP_NEGATE) {
            left = fw_ebc_operand(machine, destination, destination_indirect, 0,
                                  width) &
                   fw_ebc_mask(width);
        }
        switch (operation) {
        case FW_EBC_OP_NOT:
            result = ~right;
            break;
        case FW_EBC_OP_NEGATE:
            result = 0ULL - right;
            break;
        case FW_EBC_OP_ADD:
            result = left + right;
            break;
        case FW_EBC_OP_SUBTRACT:
            result = left - right;
            break;
        case FW_EBC_OP_MULTIPLY:
        case FW_EBC_OP_MULTIPLY_UNSIGNED:
            result = left * right;
            break;
        case FW_EBC_OP_DIVIDE:
        case FW_EBC_OP_REMAINDER:
            if (right == 0) {
                return fw_ebc_fail(machine, FwEbcFaultDivideByZero,
                                   EFI_LOAD_ERROR);
            }
            if (width == 4U) {
                INT32 dividend = (INT32)(UINT32)left;
                INT32 divisor = (INT32)(UINT32)right;

                if (divisor == -1 && dividend == (-2147483647 - 1)) {
                    result =
                        operation == FW_EBC_OP_DIVIDE ? (UINT32)dividend : 0U;
                } else {
                    result = operation == FW_EBC_OP_DIVIDE
                                 ? (UINT32)(dividend / divisor)
                                 : (UINT32)(dividend % divisor);
                }
            } else {
                INT64 dividend = (INT64)left;
                INT64 divisor = (INT64)right;

                if (divisor == -1 && dividend == (INT64)(1ULL << 63)) {
                    result = operation == FW_EBC_OP_DIVIDE ? left : 0U;
                } else {
                    result = operation == FW_EBC_OP_DIVIDE
                                 ? (UINT64)(dividend / divisor)
                                 : (UINT64)(dividend % divisor);
                }
            }
            break;
        case FW_EBC_OP_DIVIDE_UNSIGNED:
            if (right == 0) {
                return fw_ebc_fail(machine, FwEbcFaultDivideByZero,
                                   EFI_LOAD_ERROR);
            }
            result = left / right;
            break;
        case FW_EBC_OP_REMAINDER_UNSIGNED:
            if (right == 0) {
                return fw_ebc_fail(machine, FwEbcFaultDivideByZero,
                                   EFI_LOAD_ERROR);
            }
            result = left % right;
            break;
        case FW_EBC_OP_AND:
            result = left & right;
            break;
        case FW_EBC_OP_OR:
            result = left | right;
            break;
        case FW_EBC_OP_XOR:
            result = left ^ right;
            break;
        case FW_EBC_OP_SHIFT_LEFT:
            result = right >= width * 8U ? 0 : left << right;
            break;
        case FW_EBC_OP_SHIFT_RIGHT:
            result = right >= width * 8U ? 0 : left >> right;
            break;
        case FW_EBC_OP_SHIFT_ARITHMETIC:
            if (width == 4U) {
                INT32 signed_value = (INT32)(UINT32)left;

                result = right >= 32U ? (signed_value < 0 ? 0xffffffffU : 0U)
                                      : (UINT32)(signed_value >> right);
            } else {
                INT64 signed_value = (INT64)left;

                result = right >= 64U ? (signed_value < 0 ? ~0ULL : 0U)
                                      : (UINT64)(signed_value >> right);
            }
            break;
        default:
            return fw_ebc_fail(machine, FwEbcFaultInvalidOpcode,
                               EFI_UNSUPPORTED);
        }
    }

    fw_ebc_store_operand(machine, destination, destination_indirect, 0, result,
                         width);
    machine->pc += length;
    return EFI_SUCCESS;
}

static EFI_STATUS fw_ebc_compare_instruction(FW_EBC_MACHINE *machine)
{
    UINT8 encoded_opcode = machine->pc[0];
    UINT8 operation = encoded_opcode & FW_EBC_OPCODE_MASK;
    UINT8 operands = machine->pc[1];
    UINTN width = (encoded_opcode & FW_EBC_MODIFIER_LOW) != 0 ? 8U : 4U;
    UINTN length = (encoded_opcode & FW_EBC_MODIFIER_HIGH) != 0 ? 4U : 2U;
    UINT8 left_register = operands & 7U;
    UINT8 right_register = (operands >> 4) & 7U;
    BOOLEAN right_indirect = (operands & 0x80U) != 0;
    INT64 displacement = 0;
    UINT64 right;

    if ((operands & 0x08U) != 0) {
        return fw_ebc_fail(machine, FwEbcFaultEncoding, EFI_UNSUPPORTED);
    }
    if ((encoded_opcode & FW_EBC_MODIFIER_HIGH) != 0) {
        UINT64 encoded = fw_ebc_read((UINTN)machine->pc + 2U, 2U);

        if (right_indirect) {
            if (!fw_ebc_natural_offset(encoded, 2U, &displacement)) {
                return fw_ebc_fail(machine, FwEbcFaultEncoding,
                                   EFI_UNSUPPORTED);
            }
        } else {
            displacement = fw_ebc_sign_extend(encoded, 16U);
        }
    }
    right = fw_ebc_operand(machine, right_register, right_indirect,
                           displacement, width);
    fw_ebc_set_condition(
        machine, fw_ebc_compare(operation, machine->registers[left_register],
                                right, width));
    machine->pc += length;
    return EFI_SUCCESS;
}

static EFI_STATUS fw_ebc_compare_immediate(FW_EBC_MACHINE *machine)
{
    UINT8 encoded_opcode = machine->pc[0];
    UINT8 operation = encoded_opcode & FW_EBC_OPCODE_MASK;
    UINT8 operands = machine->pc[1];
    UINTN comparison_width =
        (encoded_opcode & FW_EBC_MODIFIER_LOW) != 0 ? 8U : 4U;
    UINTN immediate_width =
        (encoded_opcode & FW_EBC_MODIFIER_HIGH) != 0 ? 4U : 2U;
    UINTN cursor = 2U;
    BOOLEAN indirect = (operands & 0x08U) != 0;
    INT64 displacement = 0;
    INT64 immediate;
    UINT64 left;

    if ((operands & 0xe0U) != 0 || ((operands & 0x10U) != 0 && !indirect)) {
        return fw_ebc_fail(machine, FwEbcFaultEncoding, EFI_UNSUPPORTED);
    }
    if ((operands & 0x10U) != 0) {
        if (!fw_ebc_natural_offset(fw_ebc_read((UINTN)machine->pc + cursor, 2U),
                                   2U, &displacement)) {
            return fw_ebc_fail(machine, FwEbcFaultEncoding, EFI_UNSUPPORTED);
        }
        cursor += 2U;
    }
    immediate = fw_ebc_sign_extend(
        fw_ebc_read((UINTN)machine->pc + cursor, immediate_width),
        immediate_width * 8U);
    cursor += immediate_width;
    left = fw_ebc_operand(machine, operands & 7U, indirect, displacement,
                          comparison_width);
    fw_ebc_set_condition(
        machine,
        fw_ebc_compare(operation, left, (UINT64)immediate, comparison_width));
    machine->pc += cursor;
    return EFI_SUCCESS;
}

static EFI_STATUS fw_ebc_move_instruction(FW_EBC_MACHINE *machine)
{
    UINT8 encoded_opcode = machine->pc[0];
    UINT8 operation = encoded_opcode & FW_EBC_OPCODE_MASK;
    UINT8 operands = machine->pc[1];
    UINTN index_width;
    UINTN move_width;
    UINTN cursor = 2U;
    INT64 destination_offset = 0;
    INT64 source_offset = 0;
    BOOLEAN destination_indirect = (operands & 0x08U) != 0;
    BOOLEAN source_indirect = (operands & 0x80U) != 0;
    UINT64 value;

    if ((operation >= FW_EBC_OP_MOVE_BW && operation <= FW_EBC_OP_MOVE_QW) ||
        operation == FW_EBC_OP_MOVE_NW) {
        index_width = 2U;
    } else if ((operation >= FW_EBC_OP_MOVE_BD &&
                operation <= FW_EBC_OP_MOVE_QD) ||
               operation == FW_EBC_OP_MOVE_ND) {
        index_width = 4U;
    } else {
        index_width = 8U;
    }
    if ((encoded_opcode & FW_EBC_MODIFIER_HIGH) != 0) {
        if (!destination_indirect ||
            !fw_ebc_natural_offset(
                fw_ebc_read((UINTN)machine->pc + cursor, index_width),
                index_width, &destination_offset)) {
            return fw_ebc_fail(machine, FwEbcFaultEncoding, EFI_UNSUPPORTED);
        }
        cursor += index_width;
    }
    if ((encoded_opcode & FW_EBC_MODIFIER_LOW) != 0) {
        if (!fw_ebc_natural_offset(
                fw_ebc_read((UINTN)machine->pc + cursor, index_width),
                index_width, &source_offset)) {
            return fw_ebc_fail(machine, FwEbcFaultEncoding, EFI_UNSUPPORTED);
        }
        cursor += index_width;
    }

    switch (operation) {
    case FW_EBC_OP_MOVE_BW:
    case FW_EBC_OP_MOVE_BD:
        move_width = 1U;
        break;
    case FW_EBC_OP_MOVE_WW:
    case FW_EBC_OP_MOVE_WD:
        move_width = 2U;
        break;
    case FW_EBC_OP_MOVE_DW:
    case FW_EBC_OP_MOVE_DD:
        move_width = 4U;
        break;
    default:
        move_width = sizeof(UINTN);
        break;
    }
    value = fw_ebc_operand(machine, (operands >> 4) & 7U, source_indirect,
                           source_offset, move_width);
    fw_ebc_store_operand(machine, operands & 7U, destination_indirect,
                         destination_offset, value, move_width);
    machine->pc += cursor;
    return EFI_SUCCESS;
}

static EFI_STATUS fw_ebc_move_signed_natural(FW_EBC_MACHINE *machine)
{
    UINT8 encoded_opcode = machine->pc[0];
    UINT8 operands = machine->pc[1];
    UINTN index_width =
        (encoded_opcode & FW_EBC_OPCODE_MASK) == FW_EBC_OP_MOVE_SIGNED_W ? 2U
                                                                         : 4U;
    UINTN cursor = 2U;
    BOOLEAN destination_indirect = (operands & 0x08U) != 0;
    BOOLEAN source_indirect = (operands & 0x80U) != 0;
    INT64 destination_offset = 0;
    INT64 source_offset = 0;
    INT64 value;

    if ((encoded_opcode & FW_EBC_MODIFIER_HIGH) != 0) {
        if (!destination_indirect ||
            !fw_ebc_natural_offset(
                fw_ebc_read((UINTN)machine->pc + cursor, index_width),
                index_width, &destination_offset)) {
            return fw_ebc_fail(machine, FwEbcFaultEncoding, EFI_UNSUPPORTED);
        }
        cursor += index_width;
    }
    if ((encoded_opcode & FW_EBC_MODIFIER_LOW) != 0) {
        UINT64 encoded = fw_ebc_read((UINTN)machine->pc + cursor, index_width);

        if (source_indirect) {
            if (!fw_ebc_natural_offset(encoded, index_width, &source_offset)) {
                return fw_ebc_fail(machine, FwEbcFaultEncoding,
                                   EFI_UNSUPPORTED);
            }
        } else {
            source_offset = fw_ebc_sign_extend(encoded, index_width * 8U);
        }
        cursor += index_width;
    }
    if (source_indirect) {
        value =
            (INTN)fw_ebc_read((UINTN)(machine->registers[(operands >> 4) & 7U] +
                                      (UINT64)source_offset),
                              sizeof(UINTN));
    } else {
        value = (INT64)(machine->registers[(operands >> 4) & 7U] +
                        (UINT64)source_offset);
    }
    if (destination_indirect) {
        fw_ebc_write((UINTN)(machine->registers[operands & 7U] +
                             (UINT64)destination_offset),
                     (UINTN)value, sizeof(UINTN));
    } else {
        machine->registers[operands & 7U] = (UINT64)value;
    }
    machine->pc += cursor;
    return EFI_SUCCESS;
}

static EFI_STATUS fw_ebc_move_immediate(FW_EBC_MACHINE *machine)
{
    UINT8 encoded_opcode = machine->pc[0];
    UINT8 operands = machine->pc[1];
    UINTN immediate_width = 1U << (encoded_opcode >> 6);
    UINTN move_width = 1U << ((operands >> 4) & 3U);
    UINTN cursor = 2U;
    BOOLEAN destination_indirect = (operands & 0x08U) != 0;
    INT64 destination_offset = 0;
    INT64 value;

    if ((encoded_opcode >> 6) == 0 || (operands & 0x80U) != 0 ||
        ((operands & 0x40U) != 0 && !destination_indirect)) {
        return fw_ebc_fail(machine, FwEbcFaultEncoding, EFI_UNSUPPORTED);
    }
    if ((operands & 0x40U) != 0) {
        if (!fw_ebc_natural_offset(fw_ebc_read((UINTN)machine->pc + cursor, 2U),
                                   2U, &destination_offset)) {
            return fw_ebc_fail(machine, FwEbcFaultEncoding, EFI_UNSUPPORTED);
        }
        cursor += 2U;
    }
    value = fw_ebc_sign_extend(
        fw_ebc_read((UINTN)machine->pc + cursor, immediate_width),
        immediate_width * 8U);
    cursor += immediate_width;
    fw_ebc_store_operand(machine, operands & 7U, destination_indirect,
                         destination_offset, (UINT64)value, move_width);
    machine->pc += cursor;
    return EFI_SUCCESS;
}

static EFI_STATUS fw_ebc_move_index(FW_EBC_MACHINE *machine)
{
    UINT8 encoded_opcode = machine->pc[0];
    UINT8 operands = machine->pc[1];
    UINTN encoded_width = 1U << (encoded_opcode >> 6);
    UINTN cursor = 2U;
    BOOLEAN destination_indirect = (operands & 0x08U) != 0;
    INT64 destination_offset = 0;
    INT64 value;

    if ((encoded_opcode >> 6) == 0 || (operands & 0xb0U) != 0 ||
        ((operands & 0x40U) != 0 && !destination_indirect)) {
        return fw_ebc_fail(machine, FwEbcFaultEncoding, EFI_UNSUPPORTED);
    }
    if ((operands & 0x40U) != 0) {
        if (!fw_ebc_natural_offset(fw_ebc_read((UINTN)machine->pc + cursor, 2U),
                                   2U, &destination_offset)) {
            return fw_ebc_fail(machine, FwEbcFaultEncoding, EFI_UNSUPPORTED);
        }
        cursor += 2U;
    }
    if (!fw_ebc_natural_offset(
            fw_ebc_read((UINTN)machine->pc + cursor, encoded_width),
            encoded_width, &value)) {
        return fw_ebc_fail(machine, FwEbcFaultEncoding, EFI_UNSUPPORTED);
    }
    cursor += encoded_width;
    fw_ebc_store_operand(machine, operands & 7U, destination_indirect,
                         destination_offset, (UINT64)value, sizeof(UINTN));
    machine->pc += cursor;
    return EFI_SUCCESS;
}

static EFI_STATUS fw_ebc_move_relative(FW_EBC_MACHINE *machine)
{
    UINT8 encoded_opcode = machine->pc[0];
    UINT8 operands = machine->pc[1];
    UINTN data_width = 1U << (encoded_opcode >> 6);
    UINTN cursor = 2U;
    BOOLEAN destination_indirect = (operands & 0x08U) != 0;
    INT64 destination_offset = 0;
    INT64 relative;
    UINT64 value;

    if ((encoded_opcode >> 6) == 0 || (operands & 0xb0U) != 0 ||
        ((operands & 0x40U) != 0 && !destination_indirect)) {
        return fw_ebc_fail(machine, FwEbcFaultEncoding, EFI_UNSUPPORTED);
    }
    if ((operands & 0x40U) != 0) {
        if (!fw_ebc_natural_offset(fw_ebc_read((UINTN)machine->pc + cursor, 2U),
                                   2U, &destination_offset)) {
            return fw_ebc_fail(machine, FwEbcFaultEncoding, EFI_UNSUPPORTED);
        }
        cursor += 2U;
    }
    relative = fw_ebc_sign_extend(
        fw_ebc_read((UINTN)machine->pc + cursor, data_width), data_width * 8U);
    cursor += data_width;
    value =
        fw_ebc_read((UINTN)machine->pc + cursor + (UINTN)relative, data_width);
    fw_ebc_store_operand(machine, operands & 7U, destination_indirect,
                         destination_offset, value, data_width);
    machine->pc += cursor;
    return EFI_SUCCESS;
}

static EFI_STATUS fw_ebc_stack_instruction(FW_EBC_MACHINE *machine, BOOLEAN pop,
                                           BOOLEAN natural)
{
    UINT8 encoded_opcode = machine->pc[0];
    UINT8 operands = machine->pc[1];
    UINTN width = natural
                      ? sizeof(UINTN)
                      : ((encoded_opcode & FW_EBC_MODIFIER_LOW) != 0 ? 8U : 4U);
    UINTN length = (encoded_opcode & FW_EBC_MODIFIER_HIGH) != 0 ? 4U : 2U;
    BOOLEAN indirect = (operands & 0x08U) != 0;
    INT64 displacement = 0;
    UINT64 value;
    EFI_STATUS status;

    if ((operands & 0xf0U) != 0 ||
        (natural && (encoded_opcode & FW_EBC_MODIFIER_LOW) != 0)) {
        return fw_ebc_fail(machine, FwEbcFaultEncoding, EFI_UNSUPPORTED);
    }
    if ((encoded_opcode & FW_EBC_MODIFIER_HIGH) != 0) {
        UINT64 encoded = fw_ebc_read((UINTN)machine->pc + 2U, 2U);

        if (indirect) {
            if (!fw_ebc_natural_offset(encoded, 2U, &displacement)) {
                return fw_ebc_fail(machine, FwEbcFaultEncoding,
                                   EFI_UNSUPPORTED);
            }
        } else {
            displacement = fw_ebc_sign_extend(encoded, 16U);
        }
    }
    if (!pop) {
        value = fw_ebc_operand(machine, operands & 7U, indirect, displacement,
                               width);
        status = fw_ebc_push(machine, value, width);
    } else {
        status = fw_ebc_pop(machine, width, &value);
        if (status == EFI_SUCCESS) {
            if (indirect) {
                fw_ebc_store_operand(machine, operands & 7U, 1, displacement,
                                     value, width);
            } else {
                if (!natural && width == 4U) {
                    value = (UINT64)(INT64)(INT32)(UINT32)value;
                }
                machine->registers[operands & 7U] =
                    value + (UINT64)displacement;
            }
        }
    }
    if (status == EFI_SUCCESS) {
        machine->pc += length;
    }
    return status;
}

static FW_EBC_THUNK *fw_ebc_find_thunk(UINTN descriptor)
{
    UINTN i;

    for (i = 0; i < FW_EBC_THUNK_SLOTS; i++) {
        if (mEbcThunks[i].active &&
            (UINTN)mEbcThunks[i].descriptor == descriptor) {
            return &mEbcThunks[i];
        }
    }
    return NULL;
}

static EFI_STATUS fw_ebc_enter_byte_code(FW_EBC_MACHINE *machine, UINTN target,
                                         UINTN return_address)
{
    UINTN saved_limit = machine->argument_limit;
    EFI_STATUS status;

    if (target == 0 || (target & 1U) != 0) {
        return fw_ebc_fail(machine, FwEbcFaultAlignment, EFI_LOAD_ERROR);
    }
    status = fw_ebc_push(machine, saved_limit, sizeof(UINTN));
    if (status != EFI_SUCCESS) {
        return status;
    }
    status = fw_ebc_push(machine, return_address, sizeof(UINT64));
    if (status != EFI_SUCCESS) {
        return status;
    }
    machine->argument_limit = (UINTN)machine->registers[0];
    machine->pc = (UINT8 *)target;
    return EFI_SUCCESS;
}

static EFI_STATUS fw_ebc_call_instruction(FW_EBC_MACHINE *machine)
{
    UINT8 encoded_opcode = machine->pc[0];
    UINT8 operands = machine->pc[1];
    UINT8 format = encoded_opcode >> 6;
    UINTN length;
    UINT64 address;
    UINTN target;
    BOOLEAN external = (operands & 0x20U) != 0;

    if ((operands & 0xc0U) != 0 || format == 1U ||
        (format == 3U && (operands & 0x10U) != 0)) {
        return fw_ebc_fail(machine, FwEbcFaultEncoding, EFI_UNSUPPORTED);
    }
    length = format == 0U ? 2U : format == 2U ? 6U : 10U;
    if (format == 3U) {
        address = fw_ebc_read((UINTN)machine->pc + 2U, 8U);
    } else {
        UINT8 reg = operands & 7U;
        UINT64 base = reg == 0 ? 0 : machine->registers[reg];
        INT64 displacement = 0;

        if (format == 2U) {
            UINT64 encoded = fw_ebc_read((UINTN)machine->pc + 2U, 4U);

            if ((operands & 0x08U) != 0) {
                if (!fw_ebc_natural_offset(encoded, 4U, &displacement)) {
                    return fw_ebc_fail(machine, FwEbcFaultEncoding,
                                       EFI_UNSUPPORTED);
                }
            } else {
                displacement = fw_ebc_sign_extend(encoded, 32U);
            }
        }
        address = (operands & 0x08U) != 0
                      ? fw_ebc_read((UINTN)(base + (UINT64)displacement),
                                    sizeof(UINTN))
                      : base + (UINT64)displacement;
    }
    target = (operands & 0x10U) != 0
                 ? (UINTN)machine->pc + length + (UINTN)address
                 : (UINTN)address;

    if (external) {
        FW_EBC_THUNK *thunk = fw_ebc_find_thunk(target);

        if (thunk != NULL) {
            return fw_ebc_enter_byte_code(machine, (UINTN)thunk->byte_code,
                                          (UINTN)machine->pc + length);
        }
        if (target == 0 || (target & 7U) != 0 ||
            (UINTN)machine->registers[0] > machine->argument_limit ||
            !fw_ebc_stack_range(machine, (UINTN)machine->registers[0],
                                machine->argument_limit -
                                    (UINTN)machine->registers[0])) {
            return fw_ebc_fail(machine, FwEbcFaultStack, EFI_LOAD_ERROR);
        }
        machine->registers[7] = fw_ebc_call_native(
            target, (UINTN)machine->registers[0], machine->argument_limit);
        machine->pc += length;
        return EFI_SUCCESS;
    }
    return fw_ebc_enter_byte_code(machine, target, (UINTN)machine->pc + length);
}

static EFI_STATUS fw_ebc_return_instruction(FW_EBC_MACHINE *machine)
{
    UINT64 target;
    UINT64 saved_limit;
    EFI_STATUS status;

    if ((machine->pc[0] & ~FW_EBC_OPCODE_MASK) != 0 || machine->pc[1] != 0) {
        return fw_ebc_fail(machine, FwEbcFaultEncoding, EFI_UNSUPPORTED);
    }
    if ((UINTN)machine->registers[0] == machine->final_frame) {
        machine->complete = 1;
        return EFI_SUCCESS;
    }
    status = fw_ebc_pop(machine, sizeof(UINT64), &target);
    if (status != EFI_SUCCESS) {
        return status;
    }
    status = fw_ebc_pop(machine, sizeof(UINTN), &saved_limit);
    if (status != EFI_SUCCESS) {
        return status;
    }
    machine->argument_limit = (UINTN)saved_limit;
    return fw_ebc_set_pc(machine, (UINTN)target);
}

static EFI_STATUS fw_ebc_jump_instruction(FW_EBC_MACHINE *machine)
{
    UINT8 encoded_opcode = machine->pc[0];
    UINT8 operands = machine->pc[1];
    UINT8 format = encoded_opcode >> 6;
    UINTN length;
    UINT64 address;
    UINTN target;

    if ((operands & 0x20U) != 0 || format == 1U) {
        return fw_ebc_fail(machine, FwEbcFaultEncoding, EFI_UNSUPPORTED);
    }
    length = format == 0U ? 2U : format == 2U ? 6U : 10U;
    if (format == 3U) {
        address = fw_ebc_read((UINTN)machine->pc + 2U, 8U);
    } else {
        UINT8 reg = operands & 7U;
        UINT64 base = reg == 0 ? 0 : machine->registers[reg];
        INT64 displacement = 0;

        if (format == 2U) {
            UINT64 encoded = fw_ebc_read((UINTN)machine->pc + 2U, 4U);

            if ((operands & 0x08U) != 0) {
                if (!fw_ebc_natural_offset(encoded, 4U, &displacement)) {
                    return fw_ebc_fail(machine, FwEbcFaultEncoding,
                                       EFI_UNSUPPORTED);
                }
            } else {
                displacement = fw_ebc_sign_extend(encoded, 32U);
            }
        }
        address = (operands & 0x08U) != 0
                      ? fw_ebc_read((UINTN)(base + (UINT64)displacement),
                                    sizeof(UINTN))
                      : base + (UINT64)displacement;
    }
    if (!fw_ebc_condition(machine, operands, 0x80U, 0x40U)) {
        machine->pc += length;
        return EFI_SUCCESS;
    }
    target = (operands & 0x10U) != 0
                 ? (UINTN)machine->pc + length + (UINTN)address
                 : (UINTN)address;
    return fw_ebc_set_pc(machine, target);
}

static EFI_STATUS fw_ebc_jump8_instruction(FW_EBC_MACHINE *machine)
{
    UINT8 encoded_opcode = machine->pc[0];
    INT8 word_offset = (INT8)machine->pc[1];

    if (fw_ebc_condition(machine, encoded_opcode, 0x80U, 0x40U)) {
        machine->pc += 2 + (INTN)word_offset * 2;
    } else {
        machine->pc += 2;
    }
    return EFI_SUCCESS;
}

static EFI_STATUS fw_ebc_special_instruction(FW_EBC_MACHINE *machine,
                                             BOOLEAN store)
{
    UINT8 encoded_opcode = machine->pc[0];
    UINT8 operands = machine->pc[1];

    if ((encoded_opcode & ~FW_EBC_OPCODE_MASK) != 0 ||
        (operands & 0x88U) != 0) {
        return fw_ebc_fail(machine, FwEbcFaultEncoding, EFI_UNSUPPORTED);
    }
    if (!store) {
        UINT8 special = operands & 7U;
        UINT8 source = (operands >> 4) & 7U;

        if (special != 0) {
            return fw_ebc_fail(machine, FwEbcFaultEncoding, EFI_UNSUPPORTED);
        }
        machine->flags = (machine->flags & ~FW_EBC_FLAG_MASK) |
                         (machine->registers[source] & FW_EBC_FLAG_MASK);
    } else {
        UINT8 destination = operands & 7U;
        UINT8 special = (operands >> 4) & 7U;

        if (special > 1U) {
            return fw_ebc_fail(machine, FwEbcFaultEncoding, EFI_UNSUPPORTED);
        }
        machine->registers[destination] =
            special == 0 ? machine->flags : (UINTN)machine->pc;
    }
    machine->pc += 2;
    return EFI_SUCCESS;
}

static EFI_STATUS fw_ebc_make_thunk(EFI_HANDLE image, UINT8 *byte_code,
                                    BOOLEAN image_entry, VOID **result);

static EFI_STATUS fw_ebc_break_instruction(FW_EBC_MACHINE *machine)
{
    UINT8 code = machine->pc[1];

    if ((machine->pc[0] & ~FW_EBC_OPCODE_MASK) != 0) {
        return fw_ebc_fail(machine, FwEbcFaultEncoding, EFI_UNSUPPORTED);
    }
    switch (code) {
    case 1:
        machine->registers[7] = FW_EBC_REVISION;
        break;
    case 3:
        machine->fault = FwEbcFaultBreakpoint;
        break;
    case 4:
        break;
    case 5: {
        UINTN pointer_address = (UINTN)machine->registers[7];
        INT32 relative;
        UINT8 *byte_code;
        VOID *thunk = NULL;
        EFI_STATUS status;

        if (pointer_address == 0) {
            return fw_ebc_fail(machine, FwEbcFaultBadBreak, EFI_LOAD_ERROR);
        }
        relative = (INT32)(UINT32)fw_ebc_read(pointer_address, 4U);
        byte_code = (UINT8 *)(pointer_address + 4U + (INTN)relative);
        status = fw_ebc_make_thunk(machine->image, byte_code, 0, &thunk);
        if (status != EFI_SUCCESS) {
            return status;
        }
        fw_ebc_write(pointer_address, (UINTN)thunk, sizeof(UINTN));
        break;
    }
    case 6:
        machine->compiler_revision = (UINT32)machine->registers[7];
        break;
    default:
        return fw_ebc_fail(machine, FwEbcFaultBadBreak, EFI_LOAD_ERROR);
    }
    machine->pc += 2;
    return EFI_SUCCESS;
}

static EFI_STATUS fw_ebc_step(FW_EBC_MACHINE *machine)
{
    UINT8 operation;

    if (machine == NULL || machine->pc == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    if (((UINTN)machine->pc & 1U) != 0) {
        return fw_ebc_fail(machine, FwEbcFaultAlignment, EFI_LOAD_ERROR);
    }
    operation = machine->pc[0] & FW_EBC_OPCODE_MASK;
    switch (operation) {
    case FW_EBC_OP_BREAK:
        return fw_ebc_break_instruction(machine);
    case FW_EBC_OP_JUMP:
        return fw_ebc_jump_instruction(machine);
    case FW_EBC_OP_JUMP8:
        return fw_ebc_jump8_instruction(machine);
    case FW_EBC_OP_CALL:
        return fw_ebc_call_instruction(machine);
    case FW_EBC_OP_RETURN:
        return fw_ebc_return_instruction(machine);
    case FW_EBC_OP_COMPARE_EQ:
    case FW_EBC_OP_COMPARE_SLE:
    case FW_EBC_OP_COMPARE_SGE:
    case FW_EBC_OP_COMPARE_ULE:
    case FW_EBC_OP_COMPARE_UGE:
        return fw_ebc_compare_instruction(machine);
    case FW_EBC_OP_NOT:
    case FW_EBC_OP_NEGATE:
    case FW_EBC_OP_ADD:
    case FW_EBC_OP_SUBTRACT:
    case FW_EBC_OP_MULTIPLY:
    case FW_EBC_OP_MULTIPLY_UNSIGNED:
    case FW_EBC_OP_DIVIDE:
    case FW_EBC_OP_DIVIDE_UNSIGNED:
    case FW_EBC_OP_REMAINDER:
    case FW_EBC_OP_REMAINDER_UNSIGNED:
    case FW_EBC_OP_AND:
    case FW_EBC_OP_OR:
    case FW_EBC_OP_XOR:
    case FW_EBC_OP_SHIFT_LEFT:
    case FW_EBC_OP_SHIFT_RIGHT:
    case FW_EBC_OP_SHIFT_ARITHMETIC:
    case FW_EBC_OP_EXTEND_BYTE:
    case FW_EBC_OP_EXTEND_WORD:
    case FW_EBC_OP_EXTEND_DWORD:
        return fw_ebc_data_instruction(machine);
    case FW_EBC_OP_MOVE_BW:
    case FW_EBC_OP_MOVE_WW:
    case FW_EBC_OP_MOVE_DW:
    case FW_EBC_OP_MOVE_QW:
    case FW_EBC_OP_MOVE_BD:
    case FW_EBC_OP_MOVE_WD:
    case FW_EBC_OP_MOVE_DD:
    case FW_EBC_OP_MOVE_QD:
    case FW_EBC_OP_MOVE_QQ:
    case FW_EBC_OP_MOVE_NW:
    case FW_EBC_OP_MOVE_ND:
        return fw_ebc_move_instruction(machine);
    case FW_EBC_OP_MOVE_SIGNED_W:
    case FW_EBC_OP_MOVE_SIGNED_D:
        return fw_ebc_move_signed_natural(machine);
    case FW_EBC_OP_LOAD_SPECIAL:
        return fw_ebc_special_instruction(machine, 0);
    case FW_EBC_OP_STORE_SPECIAL:
        return fw_ebc_special_instruction(machine, 1);
    case FW_EBC_OP_PUSH:
        return fw_ebc_stack_instruction(machine, 0, 0);
    case FW_EBC_OP_POP:
        return fw_ebc_stack_instruction(machine, 1, 0);
    case FW_EBC_OP_COMPARE_IMM_EQ:
    case FW_EBC_OP_COMPARE_IMM_SLE:
    case FW_EBC_OP_COMPARE_IMM_SGE:
    case FW_EBC_OP_COMPARE_IMM_ULE:
    case FW_EBC_OP_COMPARE_IMM_UGE:
        return fw_ebc_compare_immediate(machine);
    case FW_EBC_OP_PUSH_NATURAL:
        return fw_ebc_stack_instruction(machine, 0, 1);
    case FW_EBC_OP_POP_NATURAL:
        return fw_ebc_stack_instruction(machine, 1, 1);
    case FW_EBC_OP_MOVE_IMMEDIATE:
        return fw_ebc_move_immediate(machine);
    case FW_EBC_OP_MOVE_INDEX:
        return fw_ebc_move_index(machine);
    case FW_EBC_OP_MOVE_RELATIVE:
        return fw_ebc_move_relative(machine);
    default:
        return fw_ebc_fail(machine, FwEbcFaultInvalidOpcode, EFI_UNSUPPORTED);
    }
}

static EFI_STATUS fw_ebc_execute(FW_EBC_MACHINE *machine)
{
    EFI_STATUS status = EFI_SUCCESS;

    while (!machine->complete) {
        __sync_synchronize();
        status = fw_ebc_step(machine);
        __sync_synchronize();
        if (status != EFI_SUCCESS) {
            break;
        }
        if ((machine->flags & FW_EBC_FLAG_SINGLE_STEP) != 0) {
            machine->fault = FwEbcFaultSingleStep;
        }
        if (!fw_ebc_stack_range(machine, (UINTN)machine->registers[0], 0)) {
            status = fw_ebc_fail(machine, FwEbcFaultStack, EFI_LOAD_ERROR);
            break;
        }
    }
    return status;
}

static EFI_STATUS fw_ebc_prepare_machine(FW_EBC_MACHINE *machine,
                                         EFI_HANDLE image, UINT8 *byte_code,
                                         const UINT64 *arguments,
                                         UINTN argument_count,
                                         VOID **stack_allocation)
{
    UINT8 *stack;
    UINTN i;
    EFI_STATUS status;

    if (machine == NULL || byte_code == NULL || stack_allocation == NULL ||
        argument_count > FW_EBC_NATIVE_ARGUMENTS) {
        return EFI_INVALID_PARAMETER;
    }
    *stack_allocation = NULL;
    status = bs_allocate_pool(EfiBootServicesData, FW_EBC_STACK_BYTES,
                              (VOID **)&stack);
    if (status != EFI_SUCCESS) {
        return EFI_OUT_OF_RESOURCES;
    }
    fw_set_mem(machine, sizeof(*machine), 0);
    machine->pc = byte_code;
    machine->image = image;
    machine->stack_low = (UINTN)stack;
    machine->stack_high = (UINTN)stack + FW_EBC_STACK_BYTES;
    machine->registers[0] = machine->stack_high;
    for (i = argument_count; i > 0; i--) {
        status = fw_ebc_push(machine, arguments[i - 1U], sizeof(UINTN));
        if (status != EFI_SUCCESS) {
            (void)bs_free_pool(stack);
            return status;
        }
    }
    status = fw_ebc_push(machine, 0, sizeof(UINTN));
    if (status == EFI_SUCCESS) {
        status = fw_ebc_push(machine, 0, sizeof(UINT64));
    }
    if (status != EFI_SUCCESS) {
        (void)bs_free_pool(stack);
        return status;
    }
    machine->final_frame = (UINTN)machine->registers[0];
    machine->argument_limit = machine->final_frame;
    *stack_allocation = stack;
    return EFI_SUCCESS;
}

static UINT64 fw_ebc_run(EFI_HANDLE image, UINT8 *byte_code,
                         const UINT64 *arguments, UINTN argument_count,
                         FW_EBC_MACHINE *completed_machine)
{
    FW_EBC_MACHINE machine;
    VOID *stack = NULL;
    EFI_STATUS status;
    UINT64 result;

    status = fw_ebc_prepare_machine(&machine, image, byte_code, arguments,
                                    argument_count, &stack);
    if (status == EFI_SUCCESS) {
        status = fw_ebc_execute(&machine);
    }
    result = status == EFI_SUCCESS ? machine.registers[7] : status;
    if (completed_machine != NULL) {
        *completed_machine = machine;
    }
    if (stack != NULL) {
        (void)bs_free_pool(stack);
    }
    return result;
}

static UINT64 fw_ebc_protocol_gate(UINT64 first_argument, ...)
{
    FW_EBC_THUNK *context = (FW_EBC_THUNK *)(UINTN)fw_ebc_capture_context();
    UINT64 arguments[FW_EBC_NATIVE_ARGUMENTS];
    __builtin_va_list list;
    UINTN i;

    arguments[0] = first_argument;
    __builtin_va_start(list, first_argument);
    for (i = 1; i < FW_EBC_NATIVE_ARGUMENTS; i++) {
        arguments[i] = __builtin_va_arg(list, UINT64);
    }
    __builtin_va_end(list);
    if (context == NULL || !context->active || context->byte_code == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    return fw_ebc_run(context->owner, context->byte_code, arguments,
                      FW_EBC_NATIVE_ARGUMENTS, NULL);
}

static UINT64 fw_ebc_image_gate(EFI_HANDLE image, VOID *system)
{
    FW_EBC_THUNK *context = (FW_EBC_THUNK *)(UINTN)fw_ebc_capture_context();
    UINT64 arguments[2];

    if (context == NULL || !context->active || context->byte_code == NULL ||
        context->owner != image) {
        return EFI_INVALID_PARAMETER;
    }
    arguments[0] = (UINTN)image;
    arguments[1] = (UINTN)system;
    return fw_ebc_run(image, context->byte_code, arguments, 2U, NULL);
}

static VOID fw_ebc_pack_bundle(UINT8 *destination, UINT8 template_value,
                               UINT64 slot0, UINT64 slot1, UINT64 slot2)
{
    UINT64 low = (slot1 << 46) | (slot0 << 5) | template_value;
    UINT64 high = (slot1 >> 18) | (slot2 << 23);

    fw_ebc_write((UINTN)destination, low, 8U);
    fw_ebc_write((UINTN)destination + 8U, high, 8U);
}

static VOID fw_ebc_emit_movl(UINT8 *destination, UINTN reg, UINT64 value)
{
    UINT64 extension = (value >> 22) & 0x1ffffffffffULL;
    UINT64 base =
        ((value & 0x7fULL) << 13) | (((value >> 21) & 1ULL) << 21) |
        (((value >> 16) & 0x1fULL) << 22) | (((value >> 7) & 0x1ffULL) << 27) |
        (((value >> 63) & 1ULL) << 36) | (0x06ULL << 37) | ((reg & 0x7fU) << 6);

    fw_ebc_pack_bundle(destination, 0x05U, 0x00008000000ULL, extension, base);
}

static EFI_STATUS fw_ebc_make_thunk(EFI_HANDLE image, UINT8 *byte_code,
                                    BOOLEAN image_entry, VOID **result)
{
    FW_EBC_THUNK *slot = NULL;
    FW_EBC_FUNCTION_DESCRIPTOR *descriptor;
    FW_EBC_FUNCTION_DESCRIPTOR *gate;
    UINT8 *allocation;
    UINT8 *code;
    UINTN allocation_size =
        FW_EBC_THUNK_TOTAL_BYTES + FW_EBC_THUNK_ALIGNMENT - 1U;
    UINT64 branch_slot;
    EFI_STATUS status;
    UINTN i;

    if (result == NULL || byte_code == NULL || ((UINTN)byte_code & 1U) != 0) {
        return EFI_INVALID_PARAMETER;
    }
    *result = NULL;
    for (i = 0; i < FW_EBC_THUNK_SLOTS; i++) {
        if (!mEbcThunks[i].active) {
            slot = &mEbcThunks[i];
            break;
        }
    }
    if (slot == NULL) {
        return EFI_OUT_OF_RESOURCES;
    }
    status = bs_allocate_pool(EfiBootServicesData, allocation_size,
                              (VOID **)&allocation);
    if (status != EFI_SUCCESS) {
        return EFI_OUT_OF_RESOURCES;
    }
    fw_set_mem(allocation, allocation_size, 0);
    descriptor =
        (FW_EBC_FUNCTION_DESCRIPTOR *)(VOID *)(((UINTN)allocation +
                                                FW_EBC_THUNK_ALIGNMENT - 1U) &
                                               ~(UINTN)(FW_EBC_THUNK_ALIGNMENT -
                                                        1U));
    code = (UINT8 *)(descriptor + 1);
    if (image_entry) {
        gate = (FW_EBC_FUNCTION_DESCRIPTOR *)(UINTN)fw_ebc_image_gate;
    } else {
        gate = (FW_EBC_FUNCTION_DESCRIPTOR *)(UINTN)fw_ebc_protocol_gate;
    }
    descriptor->entry = (UINTN)code;
    descriptor->gp = gate->gp;

    slot->image_entry = image_entry;
    slot->owner = image;
    slot->allocation = allocation;
    slot->descriptor = descriptor;
    slot->byte_code = byte_code;

    fw_ebc_emit_movl(code, 8U, (UINTN)slot);
    fw_ebc_emit_movl(code + 16U, 31U, gate->entry);
    branch_slot = 0x00e00100000ULL | (6ULL << 6) | (31ULL << 13);
    fw_ebc_pack_bundle(code + 32U, 0x0dU, 0x00008000000ULL, 0x00008000000ULL,
                       branch_slot);
    branch_slot = 0x00100000000ULL | (6ULL << 13);
    fw_ebc_pack_bundle(code + 48U, 0x1dU, 0x00008000000ULL, 0x00008000000ULL,
                       branch_slot);

    if (mEbcCacheFlush != NULL) {
        status = mEbcCacheFlush((EFI_PHYSICAL_ADDRESS)(UINTN)descriptor,
                                FW_EBC_THUNK_TOTAL_BYTES);
        if (status != EFI_SUCCESS) {
            fw_set_mem(slot, sizeof(*slot), 0);
            (void)bs_free_pool(allocation);
            return status;
        }
    } else {
        fw_flush_instruction_cache(descriptor, FW_EBC_THUNK_TOTAL_BYTES);
    }

    slot->active = 1;
    *result = descriptor;
    return EFI_SUCCESS;
}

static EFI_STATUS fw_ebc_protocol_create_thunk(FW_EBC_PROTOCOL *This,
                                               EFI_HANDLE ImageHandle,
                                               VOID *ByteCodeEntry,
                                               VOID **Thunk)
{
    if (This != &mEbcProtocol || ImageHandle == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    return fw_ebc_make_thunk(ImageHandle, (UINT8 *)ByteCodeEntry, 0, Thunk);
}

static EFI_STATUS fw_ebc_protocol_unload(FW_EBC_PROTOCOL *This,
                                         EFI_HANDLE ImageHandle)
{
    BOOLEAN found = 0;
    UINTN i;

    if (This != &mEbcProtocol || ImageHandle == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    for (i = 0; i < FW_EBC_THUNK_SLOTS; i++) {
        if (mEbcThunks[i].active && mEbcThunks[i].owner == ImageHandle) {
            (void)bs_free_pool(mEbcThunks[i].allocation);
            fw_set_mem(&mEbcThunks[i], sizeof(mEbcThunks[i]), 0);
            found = 1;
        }
    }
    return found ? EFI_SUCCESS : EFI_INVALID_PARAMETER;
}

static EFI_STATUS fw_ebc_protocol_register_flush(FW_EBC_PROTOCOL *This,
                                                 FW_EBC_CACHE_FLUSH Flush)
{
    if (This != &mEbcProtocol) {
        return EFI_INVALID_PARAMETER;
    }
    mEbcCacheFlush = Flush;
    return EFI_SUCCESS;
}

static EFI_STATUS fw_ebc_protocol_version(FW_EBC_PROTOCOL *This,
                                          UINT64 *Version)
{
    if (This != &mEbcProtocol || Version == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    *Version = FW_EBC_REVISION;
    return EFI_SUCCESS;
}

EFI_STATUS fw_ebc_create_image_thunk(EFI_HANDLE ImageHandle,
                                     VOID *EbcEntryPoint, VOID **Thunk)
{
    return fw_ebc_make_thunk(ImageHandle, (UINT8 *)EbcEntryPoint, 1, Thunk);
}

EFI_STATUS fw_ebc_unload_for_image(EFI_HANDLE ImageHandle)
{
    return fw_ebc_protocol_unload(&mEbcProtocol, ImageHandle);
}

BOOLEAN fw_ebc_install_protocol(VOID)
{
    EFI_HANDLE handle = NULL;

    fw_set_mem(mEbcThunks, sizeof(mEbcThunks), 0);
    mEbcCacheFlush = NULL;
    mEbcProtocol.CreateThunk = fw_ebc_protocol_create_thunk;
    mEbcProtocol.UnloadImage = fw_ebc_protocol_unload;
    mEbcProtocol.RegisterICacheFlush = fw_ebc_protocol_register_flush;
    mEbcProtocol.GetVersion = fw_ebc_protocol_version;
    if (bs_install_protocol(&handle, (VOID *)mEbcProtocolGuid, 0,
                            &mEbcProtocol) != EFI_SUCCESS) {
        return 0;
    }
    mEbcProtocolInstalled = 1;
    return 1;
}
