/* SPDX-License-Identifier: GPL-2.0-or-later */
#ifndef IA64_FIRMWARE_LINKER_SYMBOLS_H
#define IA64_FIRMWARE_LINKER_SYMBOLS_H

extern UINT8 __firmware_start;
extern UINT8 __runtime_code_start;
extern UINT8 __runtime_data_start;
extern UINT8 _end;
extern UINT8 pal_proc_entry[];
extern UINT8 sal_proc_gp_anchor[];
extern UINT8 sal_proc_entry[];
extern UINT8 __runtime_end;
extern UINT8 __boot_start;
extern UINT8 __boot_end;
extern UINT32 __virtual_fixups_start[];
extern UINT32 __virtual_fixups_end[];

#endif
