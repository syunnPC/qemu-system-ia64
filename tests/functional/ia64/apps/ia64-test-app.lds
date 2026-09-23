/* SPDX-License-Identifier: GPL-2.0-or-later */
ENTRY(__efi_entry_descriptor)

SECTIONS
{
    . = DEFINED(__test_image_base) ? __test_image_base : 0x04000000;

    .text : { *(.text*) }
    . = ALIGN(16);
    .rodata : { *(.rodata*) }
    . = ALIGN(16);
    .opd : {
        __efi_entry_descriptor = .;
        *(.opd)
    }
    . = ALIGN(16);
    .data : { *(.data*) }
    . = ALIGN(16);
    .got : { *(.got*) }
    .IA_64.unwind_info : { *(.IA_64.unwind_info*) }
    .IA_64.unwind : { *(.IA_64.unwind*) }
    .bss : {
        *(.bss*)
        *(COMMON)
    }
}
