/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "qemu/osdep.h"
#include "qemu/bswap.h"
#include "elf.h"
#include "hw/ia64/ia64_loader.h"

static uint8_t *firmware;
static gsize firmware_size;

static void test_load(void)
{
    IA64FirmwareElf original, moved;
    unsigned i;

    g_assert(ia64_firmware_elf_load(firmware, firmware_size, 0,
                                    &original, &error_abort));
    g_assert(ia64_firmware_elf_load(firmware, firmware_size, 0x400000,
                                    &moved, &error_abort));
    g_assert_cmpuint(original.size, <, 0x200000);
    g_assert_cmphex(original.base, ==, 0x100000);
    g_assert_cmphex(moved.base, ==, 0x400000);
    g_assert_cmphex(moved.entry - original.entry, ==, 0x300000);
    g_assert_cmphex(moved.global_pointer - original.global_pointer, ==,
                    0x300000);
    g_assert_cmphex(moved.pal_entry - original.pal_entry, ==, 0x300000);
    for (i = 0; i < original.segments->len; i++) {
        IA64FirmwareSegment *s = &g_array_index(original.segments,
                                               IA64FirmwareSegment, i);
        g_assert_cmpuint(s->offset + s->size, <=, original.size);
    }
    ia64_firmware_elf_clear(&original);
    ia64_firmware_elf_clear(&moved);
}

static void reject(const uint8_t *data, size_t size, uint64_t base)
{
    IA64FirmwareElf out;
    Error *err = NULL;

    g_assert_false(ia64_firmware_elf_load(data, size, base, &out, &err));
    g_assert_nonnull(err);
    g_assert_null(out.data);
    g_assert_null(out.segments);
    error_free(err);
}

static unsigned section_index(const char *name)
{
    uint64_t sh = ldq_le_p(firmware + 40);
    unsigned count = lduw_le_p(firmware + 60);
    unsigned names = lduw_le_p(firmware + 62);
    const char *strings = (const char *)firmware +
        ldq_le_p(firmware + sh + names * sizeof(Elf64_Shdr) + 24);
    unsigned i;

    for (i = 0; i < count; i++) {
        const uint8_t *s = firmware + sh + i * sizeof(Elf64_Shdr);

        if (!strcmp(strings + ldl_le_p(s), name)) {
            return i;
        }
    }
    g_assert_not_reached();
}

static void test_got_relocations(void)
{
    uint64_t sh = ldq_le_p(firmware + 40);
    unsigned count = lduw_le_p(firmware + 60);
    unsigned got_index = section_index(".got");
    unsigned opd_index = section_index(".opd");
    unsigned sym_index = section_index(".symtab");
    const uint8_t *got = firmware + sh + got_index * sizeof(Elf64_Shdr);
    const uint8_t *symbols = firmware + sh + sym_index * sizeof(Elf64_Shdr);
    uint64_t got_size = ldq_le_p(got + 32);
    uint64_t rela_offset = ROUND_UP(firmware_size, 8);
    uint64_t rela_size = got_size / 8 * sizeof(Elf64_Rela);
    uint64_t new_sh = rela_offset + rela_size;
    size_t size = new_sh + (count + 1) * sizeof(Elf64_Shdr);
    g_autofree uint8_t *copy = g_malloc0(size);
    IA64FirmwareElf expected, actual;
    uint64_t symbol_value = 0;
    unsigned symbol = 0, first_rela = 0, i;
    uint8_t *s;

    for (i = 1; i < ldq_le_p(symbols + 32) / sizeof(Elf64_Sym); i++) {
        const uint8_t *sym = firmware + ldq_le_p(symbols + 24) +
            i * sizeof(Elf64_Sym);

        if (lduw_le_p(sym + 6) == opd_index &&
            ELF64_ST_TYPE(sym[4]) == STT_SECTION) {
            symbol = i;
            symbol_value = ldq_le_p(sym + 8);
            break;
        }
    }
    for (i = 1; i < count; i++) {
        const uint8_t *section = firmware + sh + i * sizeof(Elf64_Shdr);

        if (ldl_le_p(section + 4) == SHT_RELA) {
            first_rela = i;
            break;
        }
    }
    g_assert_cmpuint(symbol, >, 0);
    g_assert_cmpuint(first_rela, >, 0);
    memcpy(copy, firmware, firmware_size);
    memcpy(copy + new_sh, firmware + sh, count * sizeof(Elf64_Shdr));
    s = copy + new_sh + first_rela * sizeof(Elf64_Shdr);
    /* Retain the original records, and process explicit GOT fixups first. */
    memcpy(copy + new_sh + count * sizeof(Elf64_Shdr), s, sizeof(Elf64_Shdr));
    stl_le_p(s, 0);
    stq_le_p(s + 24, rela_offset);
    stq_le_p(s + 32, rela_size);
    stl_le_p(s + 40, sym_index);
    stl_le_p(s + 44, got_index);
    stq_le_p(copy + 40, new_sh);
    stw_le_p(copy + 60, count + 1);
    for (i = 0; i < got_size / 8; i++) {
        uint8_t *rel = copy + rela_offset + i * sizeof(Elf64_Rela);
        uint64_t value = ldq_le_p(firmware + ldq_le_p(got + 24) + i * 8);

        stq_le_p(rel, ldq_le_p(got + 16) + i * 8);
        stq_le_p(rel + 8, (uint64_t)symbol << 32 | R_IA64_DIR64LSB);
        stq_le_p(rel + 16, value - symbol_value);
    }
    g_assert_true(ia64_firmware_elf_load(firmware, firmware_size, 0x400000,
                                       &expected, &error_abort));
    g_assert_true(ia64_firmware_elf_load(copy, size, 0x400000,
                                       &actual, &error_abort));
    g_assert_cmpmem(actual.data, actual.size, expected.data, expected.size);
    ia64_firmware_elf_clear(&actual);
    ia64_firmware_elf_clear(&expected);
}

static void test_malformed(void)
{
    g_autofree uint8_t *copy = g_memdup2(firmware, firmware_size);
    uint64_t ph = ldq_le_p(copy + 32);
    uint64_t sh = ldq_le_p(copy + 40);
    unsigned shnum = lduw_le_p(copy + 60);
    unsigned i;

    reject(copy, 0, 0);
    reject(copy, sizeof(Elf64_Ehdr) - 1, 0);
    reject(copy, firmware_size - 1, 0);
    reject(copy, firmware_size, 0x401000);
    reject(copy, firmware_size, UINT64_MAX - 0xffff);
    stq_le_p(copy + ph + 32, UINT64_MAX);
    reject(copy, firmware_size, 0);
    memcpy(copy, firmware, firmware_size);
    stq_le_p(copy + sh + 24, UINT64_MAX);
    reject(copy, firmware_size, 0);
    memcpy(copy, firmware, firmware_size);
    for (i = 0; i < shnum; i++) {
        uint8_t *section = copy + sh + i * sizeof(Elf64_Shdr);
        uint64_t offset = ldq_le_p(section + 24);
        unsigned target = ldl_le_p(section + 44);

        if (ldl_le_p(section + 4) != SHT_RELA ||
            !(ldq_le_p(copy + sh + target * sizeof(Elf64_Shdr) + 8) &
              SHF_ALLOC)) {
            continue;
        }
        stl_le_p(copy + offset + 8, 0xff);
        reject(copy, firmware_size, 0);
        memcpy(copy, firmware, firmware_size);
        stq_le_p(copy + offset + 8, R_IA64_DIR64LSB);
        reject(copy, firmware_size, 0);
        return;
    }
    g_assert_not_reached();
}

static void test_relative_encoding(void)
{
    const unsigned types[] = {
        R_IA64_GPREL22, R_IA64_GPREL64I, R_IA64_LTOFF_FPTR22,
        R_IA64_PCREL21B, R_IA64_PCREL64LSB, R_IA64_SEGREL64LSB,
    };
    uint64_t sh = ldq_le_p(firmware + 40);
    unsigned shnum = lduw_le_p(firmware + 60);
    unsigned t;

    for (t = 0; t < G_N_ELEMENTS(types); t++) {
        g_autofree uint8_t *copy = g_memdup2(firmware, firmware_size);
        bool found = false;
        unsigned i;

        for (i = 0; i < shnum && !found; i++) {
            const uint8_t *section = copy + sh + i * sizeof(Elf64_Shdr);
            const uint8_t *target;
            uint64_t j;

            if (ldl_le_p(section + 4) != SHT_RELA) {
                continue;
            }
            target = copy + sh + ldl_le_p(section + 44) * sizeof(Elf64_Shdr);
            if (!(ldq_le_p(target + 8) & SHF_ALLOC)) {
                continue;
            }
            for (j = 0; j < ldq_le_p(section + 32); j += sizeof(Elf64_Rela)) {
                const uint8_t *rel = copy + ldq_le_p(section + 24) + j;
                uint64_t address, offset;
                bool instruction;

                if (ldl_le_p(rel + 8) != types[t]) {
                    continue;
                }
                instruction = types[t] != R_IA64_PCREL64LSB &&
                              types[t] != R_IA64_SEGREL64LSB;
                address = ldq_le_p(rel);
                if (instruction) {
                    address &= ~UINT64_C(15);
                }
                offset = ldq_le_p(target + 24) + address -
                         ldq_le_p(target + 16);
                memset(copy + offset, 0xff, instruction ? 16 : 8);
                reject(copy, firmware_size, 0x400000);
                found = true;
                break;
            }
        }
        g_assert_true(found);
    }
}

int main(int argc, char **argv)
{
    if (argc == 5 && !strcmp(argv[1], "--dump")) {
        IA64FirmwareElf out;
        g_assert(g_file_get_contents(argv[2], (char **)&firmware,
                                     &firmware_size, NULL));
        g_assert(ia64_firmware_elf_load(firmware, firmware_size,
                                       g_ascii_strtoull(argv[3], NULL, 0),
                                       &out, &error_abort));
        g_assert(g_file_set_contents(argv[4], (char *)out.data,
                                     out.size, NULL));
        ia64_firmware_elf_clear(&out);
        g_free(firmware);
        return 0;
    }
    g_assert_cmpint(argc, >=, 2);
    g_assert(g_file_get_contents(argv[1], (char **)&firmware,
                                 &firmware_size, NULL));
    argv[1] = argv[0];
    argc--;
    argv++;
    g_test_init(&argc, &argv, NULL);
    g_test_add_func("/ia64/firmware/elf/load", test_load);
    g_test_add_func("/ia64/firmware/elf/malformed", test_malformed);
    g_test_add_func("/ia64/firmware/elf/relative-encoding",
                    test_relative_encoding);
    g_test_add_func("/ia64/firmware/elf/got-relocations", test_got_relocations);
    int result = g_test_run();
    g_free(firmware);
    return result;
}
