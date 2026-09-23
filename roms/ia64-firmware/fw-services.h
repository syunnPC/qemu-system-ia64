/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Internal services shared by independently compiled firmware modules.
 */

#ifndef IA64_FIRMWARE_FW_SERVICES_H
#define IA64_FIRMWARE_FW_SERVICES_H

#include "fw-base.h"
#include "fw-uart-policy.h"
#include "hw/ia64/ia64_platform_abi.h"
#include "hw/ia64/ia64_vpc_abi.h"

#define IA64_PSR_IC (1ULL << 13)
#define IA64_PSR_I  (1ULL << 14)

#define SAL_IVT_BASE 0x0000000000010000ULL

#define FW_MAX_CPUS IA64_VPC_MAX_CPUS
#define EFI_PAGE_SIZE 0x1000U

#define UART_RBR      0x00U
#define UART_THR      0x00U
#define UART_IER      0x01U
#define UART_FCR      0x02U
#define UART_LCR      0x03U
#define UART_MCR      0x04U
#define UART_LSR      0x05U
#define UART_MSR      0x06U
#define UART_REGISTER_COUNT 8U
#define UART_LSR_DR   0x01U
#define UART_LSR_THRE 0x20U
#define UART_LCR_DLAB 0x80U
#define UART_MCR_DTR  0x01U
#define UART_MCR_RTS  0x02U
#define UART_MCR_LOOP 0x10U

#define FW_ITC_TICKS_PER_100NS (fw_itc_ticks_per_100ns())
#define FW_ITC_TICKS_PER_MICROSECOND (FW_ITC_TICKS_PER_100NS * 10ULL)
#define FW_ITC_TICKS_PER_SECOND (FW_ITC_TICKS_PER_100NS * 10000000ULL)

#define PS2_DATA_PORT        0x60U
#define PS2_STATUS_PORT      0x64U
#define PS2_STATUS_OBF       0x01U
#define PS2_STATUS_IBF       0x02U
#define PS2_STATUS_MOUSE_OBF 0x20U

#define TPL_CALLBACK   8U
#define TPL_NOTIFY     16U
#define EVT_NOTIFY_WAIT 0x00000100U
#define EVT_TIMER       0x80000000U
#define EVT_NOTIFY_SIGNAL 0x00000200U

#define TIMER_CANCEL   0U
#define TIMER_PERIODIC 1U
#define TIMER_RELATIVE 2U

void fw_copy_mem(VOID *destination, const VOID *source, UINTN length);
UINT64 fw_read_itc(void);
UINT64 fw_itc_ticks_per_100ns(void);
BOOLEAN fw_platform_descriptor_init(UINT64 descriptor_gpa,
                                    UINT64 descriptor_size,
                                    UINT64 platform_id);
BOOLEAN fw_vpc_devices_enabled(void);
BOOLEAN fw_i2000_profile_enabled(void);
const FW_UART_POLICY *fw_i2000_uart_policy(void);
UINT64 fw_platform_legacy_io_base(void);
UINT64 fw_platform_legacy_io_size(void);
UINT64 fw_platform_legacy_io_port(UINTN port);
UINT64 fw_platform_local_sapic_base(void);
UINT64 fw_platform_local_sapic_size(void);
UINT64 fw_platform_console_base(void);
UINT32 fw_platform_console_stride(void);
UINT64 fw_platform_console_clock_hz(void);
UINT64 fw_platform_console_default_baud(void);
UINT64 fw_platform_nvram_base(void);
UINT64 fw_platform_nvram_size(void);
UINT64 fw_platform_rtc_base(void);
UINT64 fw_platform_rtc_size(void);
volatile UINT8 *fw_uart_reg(UINTN offset);
UINT64 fw_read_psr(void);
UINT64 fw_read_ivr(void);
void fw_write_eoi(void);
void fw_flush_instruction_cache(VOID *start, UINTN bytes);
UINT64 fw_handoff_debug_port_base(void);
BOOLEAN fw_handoff_i8042_enabled(void);
VOID *fw_system_table(VOID);

volatile UINT8 *ps2_reg(UINTN address);
UINT8 ps2_read_status(void);
BOOLEAN ps2_write_command(UINT8 command);
BOOLEAN ps2_write_data(UINT8 data);
BOOLEAN ps2_keyboard_raw_push(UINT8 data);

EFI_STATUS bs_install_protocol(EFI_HANDLE *handle, void *protocol,
                               UINTN interface_type, VOID *interface);
EFI_STATUS bs_uninstall_protocol(EFI_HANDLE handle, void *protocol,
                                 VOID *interface);
EFI_STATUS bs_locate_protocol(void *protocol, VOID *registration,
                              VOID **interface);
EFI_STATUS bs_create_event(UINT32 type, UINTN notify_tpl,
                           EFI_EVENT_NOTIFY notify_function,
                           VOID *notify_context, EFI_EVENT *event);
EFI_STATUS bs_signal_event(EFI_EVENT event);
EFI_STATUS bs_close_event(EFI_EVENT event);
EFI_STATUS bs_set_timer(EFI_EVENT event, UINTN type, UINT64 trigger_time);
EFI_TPL bs_raise_tpl(EFI_TPL new_tpl);
VOID bs_restore_tpl(EFI_TPL old_tpl);
EFI_STATUS bs_stall(UINTN microseconds);
EFI_STATUS bs_allocate_pool(EFI_MEMORY_TYPE pool_type, UINTN size,
                            VOID **buffer);
EFI_STATUS bs_free_pool(VOID *buffer);

BOOLEAN fw_graphics_present(VOID);
UINT64 fw_graphics_bar_length(VOID);
UINT64 fw_graphics_framebuffer_base(VOID);
UINT64 fw_graphics_framebuffer_size(VOID);
UINT32 fw_graphics_pixels_per_scan_line(VOID);
EFI_STATUS fw_graphics_reset_current_mode(BOOLEAN redraw_text);
EFI_STATUS fw_graphics_set_uga_mode(UINT32 horizontal, UINT32 vertical,
                                    UINT32 color_depth, UINT32 refresh_rate);
EFI_HANDLE fw_graphics_handle(VOID);
BOOLEAN fw_protocol_interface_installed(EFI_HANDLE handle, VOID *protocol,
                                        VOID **interface);

extern UINT64 sal_runtime_area_top;
VOID *fw_boot_dma_buffer(UINTN Size, UINTN Alignment);
extern const UINT8 mDevicePathProtocolGuid[16];

#endif /* IA64_FIRMWARE_FW_SERVICES_H */
