/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Bounded AML construction for the freestanding IA-64 firmware.
 */

#ifndef IA64_FIRMWARE_FW_ACPI_AML_H
#define IA64_FIRMWARE_FW_ACPI_AML_H

#include "fw-base.h"
#include "hw/ia64/ia64_platform_abi.h"

#define FW_ACPI_AML_MAX_PACKAGE_DEPTH 32U
#define FW_ACPI_ROOT_MEMORY_WINDOW_MAX 16U
#define FW_ACPI_UART_SSDT_AML_CAPACITY (IA64_PLATFORM_MAX_UARTS * 128U)

/* Holds 16 roots, 128 interrupt routes, and 16 supplemental memory windows. */
#define FW_ACPI_ZX6000_DSDT_AML_CAPACITY 12288U

/* ZX1 SBA CSR window advertised by the generated namespace. */
#define FW_ACPI_ZX1_SBA_CSR_BASE IA64_PLATFORM_ZX1_SBA_CSR_BASE
#define FW_ACPI_ZX1_SBA_CSR_SIZE IA64_PLATFORM_ZX1_SBA_CSR_SIZE

typedef struct FWAcpiAmlFrame {
    UINTN LengthOffset;
    UINTN DataOffset;
    UINT8 Kind;
} FWAcpiAmlFrame;

typedef struct FWAcpiAmlBuilder {
    UINT8 *Buffer;
    UINTN Capacity;
    UINTN Length;
    UINTN Depth;
    BOOLEAN Failed;
    FWAcpiAmlFrame Frame[FW_ACPI_AML_MAX_PACKAGE_DEPTH];
} FWAcpiAmlBuilder;

typedef struct FWAcpiRootMemoryWindow {
    UINTN RootIndex;
    UINT64 Base;
    UINT64 Size;
} FWAcpiRootMemoryWindow;

void fw_acpi_aml_builder_init(FWAcpiAmlBuilder *Builder, UINT8 *Buffer,
                              UINTN Capacity);
BOOLEAN fw_acpi_aml_builder_finish(FWAcpiAmlBuilder *Builder,
                                   UINTN *Length);
BOOLEAN fw_acpi_aml_builder_ok(const FWAcpiAmlBuilder *Builder);
UINTN fw_acpi_aml_builder_length(const FWAcpiAmlBuilder *Builder);

BOOLEAN fw_acpi_aml_scope_begin(FWAcpiAmlBuilder *Builder,
                                const CHAR8 *Name);
BOOLEAN fw_acpi_aml_device_begin(FWAcpiAmlBuilder *Builder,
                                 const CHAR8 *Name);
BOOLEAN fw_acpi_aml_package_begin(FWAcpiAmlBuilder *Builder,
                                  UINT8 ElementCount);
BOOLEAN fw_acpi_aml_package_end(FWAcpiAmlBuilder *Builder);

BOOLEAN fw_acpi_aml_name(FWAcpiAmlBuilder *Builder, const CHAR8 *Name);
BOOLEAN fw_acpi_aml_integer(FWAcpiAmlBuilder *Builder, UINT64 Value);
BOOLEAN fw_acpi_aml_eisa_id(FWAcpiAmlBuilder *Builder, const CHAR8 *Id);

BOOLEAN fw_acpi_aml_resource_template_begin(FWAcpiAmlBuilder *Builder);
BOOLEAN fw_acpi_aml_resource_template_end(FWAcpiAmlBuilder *Builder);
BOOLEAN fw_acpi_aml_memory32_fixed(FWAcpiAmlBuilder *Builder,
                                   BOOLEAN ReadWrite, UINT32 Base,
                                   UINT32 Size);
BOOLEAN fw_acpi_aml_io(FWAcpiAmlBuilder *Builder, BOOLEAN Decode16,
                       UINT16 Minimum, UINT16 Maximum, UINT8 Alignment,
                       UINT8 Length);
BOOLEAN fw_acpi_aml_word_bus_number(FWAcpiAmlBuilder *Builder,
                                    UINT16 Granularity, UINT16 Minimum,
                                    UINT16 Maximum, UINT16 Translation,
                                    UINT16 Length);
BOOLEAN fw_acpi_aml_dword_memory(FWAcpiAmlBuilder *Builder,
                                 UINT32 Granularity, UINT32 Minimum,
                                 UINT32 Maximum, UINT32 Translation,
                                 UINT32 Length);
BOOLEAN fw_acpi_aml_qword_memory(FWAcpiAmlBuilder *Builder,
                                 UINT64 Granularity, UINT64 Minimum,
                                 UINT64 Maximum, UINT64 Translation,
                                 UINT64 Length);
BOOLEAN fw_acpi_aml_qword_io(FWAcpiAmlBuilder *Builder,
                             UINT64 Granularity, UINT64 Minimum,
                             UINT64 Maximum, UINT64 Translation,
                             UINT64 Length);
BOOLEAN fw_acpi_aml_qword_io_to_memory(FWAcpiAmlBuilder *Builder,
                                       BOOLEAN SparseTranslation,
                                       UINT64 Granularity, UINT64 Minimum,
                                       UINT64 Maximum, UINT64 Translation,
                                       UINT64 Length);

BOOLEAN fw_acpi_ssdt_reparent_legacy_devices(UINT8 *Aml, UINTN Length,
                                             const CHAR8 Parent[4]);

/* Opt-in VPC routing for every slot of the conventional PCI root. */
BOOLEAN fw_acpi_build_vpc_dsdt(UINT8 *Buffer, UINTN Capacity,
                                UINTN IntxLines, UINTN *Length);

UINT32 fw_acpi_hp_root_uid(const IA64PlatformPciRoot *Root);

BOOLEAN fw_acpi_build_uart_ssdt(
    UINT8 *Buffer, UINTN Capacity, const IA64PlatformUart *Uarts,
    UINTN UartCount, UINTN RootCount, UINTN *Length);

/* Build the AML body shared by the HP zx1 and zx2 DSDTs. */
BOOLEAN fw_acpi_build_zx6000_dsdt(
    UINT8 *Buffer, UINTN Capacity,
    const IA64PlatformPciRoot *Roots, UINTN RootCount,
    const IA64PlatformPciRoute *Routes, UINTN RouteCount,
    const FWAcpiRootMemoryWindow *RootMemoryWindows,
    UINTN RootMemoryWindowCount,
    UINT64 LegacyIoBase,
    UINT64 AcpiPmBase, UINT64 AcpiPmSize,
    UINTN *Length);

#endif /* IA64_FIRMWARE_FW_ACPI_AML_H */
