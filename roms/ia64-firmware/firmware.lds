/* SPDX-License-Identifier: GPL-2.0-or-later */
/* IA-64 firmware reference layout; selectors come from ELF dependencies. */

ENTRY(_start)
PHDRS
{
    pal PT_LOAD FLAGS(7);
    runtime PT_LOAD FLAGS(7);
    boot PT_LOAD FLAGS(7);
}
SECTIONS
{
    . = 0x100000;
    __firmware_start = .;
    .text.boot : {
        KEEP(*(.text._start))
        KEEP(*(.text.pal_proc_entry))
        KEEP(*(.text.start_after_pal))
        KEEP(*(.text.start_secondary))
        KEEP(*(.text.fw_pal_halt_light))
    } :pal

    . = ALIGN(0x2000);
    __runtime_code_start = .;
    .text.runtime : {
        /* RETAIN_TEXT */
        *(.plt*)
    } :runtime

    .text.physical : ALIGN(16) {
        /* PHYSICAL_TEXT */
    } :runtime
    . = ALIGN(0x2000);
    __runtime_data_start = .;
    .rodata.runtime : {
        /* RETAIN_RODATA */
    } :runtime
    .rodata.physical : ALIGN(16) {
        /* PHYSICAL_RODATA */
    } :runtime
    .data.physical : ALIGN(16) {
        /* PHYSICAL_DATA */
    } :runtime
    .data.runtime : {
        /* RETAIN_DATA */
    } :runtime
    .opd : ALIGN(16) { *(.opd) } :runtime
    .got : ALIGN(16) {
        __gp = .;
        *(.got*)
    } :runtime
    .IA_64.unwind_info : ALIGN(16) { *(.IA_64.unwind_info*) } :runtime
    .IA_64.unwind : ALIGN(16) { *(.IA_64.unwind*) } :runtime
    .fw.virtual_fixups : ALIGN(8) {
        __virtual_fixups_start = .;
        LONG(1)
        LONG(0)
        . += 4088;
        __virtual_fixups_end = .;
    } :runtime
    .bss.physical (NOLOAD) : ALIGN(16) {
        /* PHYSICAL_BSS */
    } :runtime
    .bss.runtime (NOLOAD) : ALIGN(16) {
        /* RETAIN_BSS */
    } :runtime
    . = ALIGN(0x2000);
    __runtime_end = .;
    __boot_start = .;
    .text : { *(.text*) } :boot
    .rodata : ALIGN(16) { *(.rodata*) } :boot
    .data : ALIGN(16) { *(.data*) } :boot
    .bss (NOLOAD) : ALIGN(16) {
        _bss_start = .;
        *(SORT_BY_ALIGNMENT(.bss*))
        *(COMMON)
        . = ALIGN(8);
        _bss_end = .;
    } :boot
    __firmware_payload_end = .;
    __boot_end = .;
    _end = .;
    ASSERT(__firmware_payload_end - __firmware_start < 0x200000,
           "firmware fixed span must be less than 2 MiB")
}
