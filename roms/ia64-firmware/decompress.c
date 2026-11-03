/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * EFI 1.10 decompression protocol.
 *
 * The decoder uses canonical-code ranges and a bounded bit cursor.
 */

#include "fw-decompress.h"

#define FW_DEC_LITERAL_COUNT       510U
#define FW_DEC_AUXILIARY_COUNT      19U
#define FW_DEC_POSITION_COUNT       14U
#define FW_DEC_MAX_CODE_BITS        16U
#define FW_DEC_LITERAL_COUNT_BITS    9U
#define FW_DEC_AUXILIARY_COUNT_BITS  5U
#define FW_DEC_POSITION_COUNT_BITS   4U
#define FW_DEC_MATCH_BIAS          253U

typedef struct {
    UINT16 count[FW_DEC_MAX_CODE_BITS + 1U];
    UINT32 first_code[FW_DEC_MAX_CODE_BITS + 1U];
    UINT16 first_symbol[FW_DEC_MAX_CODE_BITS + 1U];
    UINT16 ordered[FW_DEC_LITERAL_COUNT];
    UINT16 sole_symbol;
    BOOLEAN is_singleton;
} FW_DEC_CODEBOOK;

typedef struct {
    const UINT8 *payload;
    UINT32 payload_size;
    UINT32 bit_cursor;
    UINT8 *output;
    UINT32 output_size;
    UINT32 output_cursor;
    UINT16 block_symbols;
    BOOLEAN invalid;
    UINT8 lengths[FW_DEC_LITERAL_COUNT];
    FW_DEC_CODEBOOK auxiliary;
    FW_DEC_CODEBOOK literal;
    FW_DEC_CODEBOOK position;
} FW_DEC_WORKSPACE;

static UINT32 fw_dec_u32(const UINT8 *bytes)
{
    return (UINT32)bytes[0] | ((UINT32)bytes[1] << 8) |
           ((UINT32)bytes[2] << 16) | ((UINT32)bytes[3] << 24);
}

static BOOLEAN fw_dec_take_bits(FW_DEC_WORKSPACE *work, UINTN width,
                                UINT32 *value)
{
    UINT32 result = 0;
    UINTN i;

    if (work == NULL || value == NULL || width > 32U ||
        work->bit_cursor > work->payload_size * 8ULL ||
        width > work->payload_size * 8ULL - work->bit_cursor) {
        if (work != NULL) {
            work->invalid = 1;
        }
        return 0;
    }
    for (i = 0; i < width; i++) {
        UINT32 bit = work->bit_cursor++;

        result = (result << 1) |
                 ((work->payload[bit >> 3] >> (7U - (bit & 7U))) & 1U);
    }
    *value = result;
    return 1;
}

static BOOLEAN fw_dec_make_codebook(FW_DEC_CODEBOOK *book, const UINT8 *lengths,
                                    UINTN symbol_count)
{
    UINT32 next_code = 0;
    UINTN ordered_count = 0;
    UINTN width;
    UINTN symbol;

    if (book == NULL || lengths == NULL || symbol_count == 0 ||
        symbol_count > FW_DEC_LITERAL_COUNT) {
        return 0;
    }
    fw_set_mem(book, sizeof(*book), 0);
    for (symbol = 0; symbol < symbol_count; symbol++) {
        if (lengths[symbol] > FW_DEC_MAX_CODE_BITS) {
            return 0;
        }
        book->count[lengths[symbol]]++;
    }
    if (book->count[0] == symbol_count) {
        return 0;
    }
    book->count[0] = 0;

    for (width = 1; width <= FW_DEC_MAX_CODE_BITS; width++) {
        next_code = (next_code + book->count[width - 1U]) << 1;
        if (next_code + book->count[width] > (1U << width)) {
            return 0;
        }
        book->first_code[width] = next_code;
        book->first_symbol[width] = (UINT16)ordered_count;
        for (symbol = 0; symbol < symbol_count; symbol++) {
            if (lengths[symbol] == width) {
                book->ordered[ordered_count++] = (UINT16)symbol;
            }
        }
    }
    return next_code + book->count[FW_DEC_MAX_CODE_BITS] ==
           (1U << FW_DEC_MAX_CODE_BITS);
}

static VOID fw_dec_singleton(FW_DEC_CODEBOOK *book, UINT16 symbol)
{
    fw_set_mem(book, sizeof(*book), 0);
    book->sole_symbol = symbol;
    book->is_singleton = 1;
}

static BOOLEAN fw_dec_symbol(FW_DEC_WORKSPACE *work,
                             const FW_DEC_CODEBOOK *book, UINT16 *symbol)
{
    UINT32 code = 0;
    UINTN width;

    if (work == NULL || book == NULL || symbol == NULL) {
        return 0;
    }
    if (book->is_singleton) {
        *symbol = book->sole_symbol;
        return 1;
    }
    for (width = 1; width <= FW_DEC_MAX_CODE_BITS; width++) {
        UINT32 bit;
        UINT32 offset;

        if (!fw_dec_take_bits(work, 1, &bit)) {
            return 0;
        }
        code = (code << 1) | bit;
        if (code < book->first_code[width]) {
            continue;
        }
        offset = code - book->first_code[width];
        if (offset < book->count[width]) {
            *symbol = book->ordered[book->first_symbol[width] + offset];
            return 1;
        }
    }
    work->invalid = 1;
    return 0;
}

static BOOLEAN fw_dec_read_basic_lengths(FW_DEC_WORKSPACE *work,
                                         UINTN alphabet_size, UINTN count_width,
                                         UINTN zero_insert_position,
                                         FW_DEC_CODEBOOK *book)
{
    UINT32 encoded_count;
    UINTN cursor = 0;

    if (alphabet_size > sizeof(work->lengths) ||
        !fw_dec_take_bits(work, count_width, &encoded_count)) {
        return 0;
    }
    fw_set_mem(work->lengths, alphabet_size, 0);
    if (encoded_count == 0) {
        UINT32 repeated_symbol;

        if (!fw_dec_take_bits(work, count_width, &repeated_symbol) ||
            repeated_symbol >= alphabet_size) {
            work->invalid = 1;
            return 0;
        }
        fw_dec_singleton(book, (UINT16)repeated_symbol);
        return 1;
    }
    if (encoded_count > alphabet_size) {
        work->invalid = 1;
        return 0;
    }

    while (cursor < encoded_count) {
        UINT32 length;

        if (!fw_dec_take_bits(work, 3, &length)) {
            return 0;
        }
        if (length == 7) {
            UINT32 continuation;

            do {
                if (!fw_dec_take_bits(work, 1, &continuation)) {
                    return 0;
                }
                if (continuation != 0) {
                    length++;
                    if (length > FW_DEC_MAX_CODE_BITS) {
                        work->invalid = 1;
                        return 0;
                    }
                }
            } while (continuation != 0);
        }
        work->lengths[cursor++] = (UINT8)length;

        if (cursor == zero_insert_position) {
            UINT32 zero_count;

            if (!fw_dec_take_bits(work, 2, &zero_count) ||
                zero_count > encoded_count - cursor) {
                work->invalid = 1;
                return 0;
            }
            cursor += zero_count;
        }
    }
    return fw_dec_make_codebook(book, work->lengths, alphabet_size);
}

static BOOLEAN fw_dec_read_literal_book(FW_DEC_WORKSPACE *work)
{
    UINT32 encoded_count;
    UINTN cursor = 0;

    if (!fw_dec_take_bits(work, FW_DEC_LITERAL_COUNT_BITS, &encoded_count)) {
        return 0;
    }
    fw_set_mem(work->lengths, FW_DEC_LITERAL_COUNT, 0);
    if (encoded_count == 0) {
        UINT32 repeated_symbol;

        if (!fw_dec_take_bits(work, FW_DEC_LITERAL_COUNT_BITS,
                              &repeated_symbol) ||
            repeated_symbol >= FW_DEC_LITERAL_COUNT) {
            work->invalid = 1;
            return 0;
        }
        fw_dec_singleton(&work->literal, (UINT16)repeated_symbol);
        return 1;
    }
    if (encoded_count > FW_DEC_LITERAL_COUNT) {
        work->invalid = 1;
        return 0;
    }

    while (cursor < encoded_count) {
        UINT16 token;
        UINT32 run;

        if (!fw_dec_symbol(work, &work->auxiliary, &token) ||
            token >= FW_DEC_AUXILIARY_COUNT) {
            work->invalid = 1;
            return 0;
        }
        if (token >= 3) {
            work->lengths[cursor++] = (UINT8)(token - 2U);
            continue;
        }
        if (token == 0) {
            run = 1;
        } else if (token == 1) {
            if (!fw_dec_take_bits(work, 4, &run)) {
                return 0;
            }
            run += 3U;
        } else {
            if (!fw_dec_take_bits(work, FW_DEC_LITERAL_COUNT_BITS, &run)) {
                return 0;
            }
            run += 20U;
        }
        if (run > encoded_count - cursor) {
            work->invalid = 1;
            return 0;
        }
        cursor += run;
    }
    return fw_dec_make_codebook(&work->literal, work->lengths,
                                FW_DEC_LITERAL_COUNT);
}

static BOOLEAN fw_dec_start_block(FW_DEC_WORKSPACE *work)
{
    UINT32 symbols;

    if (!fw_dec_take_bits(work, 16, &symbols) || symbols == 0) {
        work->invalid = 1;
        return 0;
    }
    if (!fw_dec_read_basic_lengths(work, FW_DEC_AUXILIARY_COUNT,
                                   FW_DEC_AUXILIARY_COUNT_BITS, 3,
                                   &work->auxiliary) ||
        !fw_dec_read_literal_book(work) ||
        !fw_dec_read_basic_lengths(work, FW_DEC_POSITION_COUNT,
                                   FW_DEC_POSITION_COUNT_BITS, (UINTN)-1,
                                   &work->position)) {
        return 0;
    }
    work->block_symbols = (UINT16)symbols;
    return 1;
}

static BOOLEAN fw_dec_distance(FW_DEC_WORKSPACE *work, UINT32 *distance)
{
    UINT16 prefix;
    UINT32 suffix = 0;

    if (!fw_dec_symbol(work, &work->position, &prefix) ||
        prefix >= FW_DEC_POSITION_COUNT) {
        work->invalid = 1;
        return 0;
    }
    if (prefix > 1 && !fw_dec_take_bits(work, prefix - 1U, &suffix)) {
        return 0;
    }
    *distance = prefix <= 1 ? prefix : (1U << (prefix - 1U)) + suffix;
    return 1;
}

static BOOLEAN fw_dec_expand(FW_DEC_WORKSPACE *work)
{
    while (work->output_cursor < work->output_size) {
        UINT16 token;

        if (work->block_symbols == 0 && !fw_dec_start_block(work)) {
            return 0;
        }
        if (!fw_dec_symbol(work, &work->literal, &token)) {
            return 0;
        }
        work->block_symbols--;
        if (token < 256U) {
            work->output[work->output_cursor++] = (UINT8)token;
        } else {
            UINT32 length = token - FW_DEC_MATCH_BIAS;
            UINT32 distance;
            UINT32 source;

            if (!fw_dec_distance(work, &distance) ||
                distance >= work->output_cursor ||
                length > work->output_size - work->output_cursor) {
                work->invalid = 1;
                return 0;
            }
            source = work->output_cursor - distance - 1U;
            while (length-- != 0) {
                work->output[work->output_cursor++] = work->output[source++];
            }
        }
    }
    return !work->invalid;
}

static EFI_STATUS fw_decompress_get_info(FW_EFI_DECOMPRESS_PROTOCOL *This,
                                         VOID *Source, UINT32 SourceSize,
                                         UINT32 *DestinationSize,
                                         UINT32 *ScratchSize)
{
    const UINT8 *bytes = (const UINT8 *)Source;
    UINT32 payload_size;

    if (This == NULL || bytes == NULL || DestinationSize == NULL ||
        ScratchSize == NULL || SourceSize < 8U) {
        return EFI_INVALID_PARAMETER;
    }
    payload_size = fw_dec_u32(bytes);
    if (payload_size > SourceSize - 8U) {
        return EFI_INVALID_PARAMETER;
    }
    *DestinationSize = fw_dec_u32(bytes + 4U);
    *ScratchSize = sizeof(FW_DEC_WORKSPACE);
    return EFI_SUCCESS;
}

static EFI_STATUS fw_decompress_data(FW_EFI_DECOMPRESS_PROTOCOL *This,
                                     VOID *Source, UINT32 SourceSize,
                                     VOID *Destination, UINT32 DestinationSize,
                                     VOID *Scratch, UINT32 ScratchSize)
{
    const UINT8 *bytes = (const UINT8 *)Source;
    FW_DEC_WORKSPACE *work = (FW_DEC_WORKSPACE *)Scratch;
    UINT32 payload_size;
    UINT32 output_size;

    if (This == NULL || bytes == NULL || work == NULL || SourceSize < 8U ||
        ScratchSize < sizeof(*work)) {
        return EFI_INVALID_PARAMETER;
    }
    payload_size = fw_dec_u32(bytes);
    output_size = fw_dec_u32(bytes + 4U);
    if (payload_size > SourceSize - 8U || DestinationSize < output_size ||
        (output_size != 0 && (Destination == NULL || payload_size == 0))) {
        return EFI_INVALID_PARAMETER;
    }
    if (output_size == 0) {
        return EFI_SUCCESS;
    }

    fw_set_mem(work, sizeof(*work), 0);
    work->payload = bytes + 8U;
    work->payload_size = payload_size;
    work->output = (UINT8 *)Destination;
    work->output_size = output_size;
    return fw_dec_expand(work) ? EFI_SUCCESS : EFI_INVALID_PARAMETER;
}

FW_EFI_DECOMPRESS_PROTOCOL fw_decompress_protocol = {
    .GetInfo = fw_decompress_get_info,
    .Decompress = fw_decompress_data,
};

const UINT8 fw_decompress_protocol_guid[16] = {
    0xfe, 0x7c, 0x11, 0xd8, 0xa6, 0x94, 0xd4, 0x11,
    0x9a, 0x3a, 0x00, 0x90, 0x27, 0x3f, 0xc1, 0x4d,
};
