/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * Internal USB and EFI USB protocol boundary.
 */

#ifndef IA64_FIRMWARE_FW_USB_H
#define IA64_FIRMWARE_FW_USB_H

#include "fw-device-path.h"
#include "fw-services.h"

#define OHCI_REG_REVISION             0x00U
#define OHCI_REG_CONTROL              0x04U
#define OHCI_REG_COMMAND_STATUS       0x08U
#define OHCI_REG_INTERRUPT_STATUS     0x0cU
#define OHCI_REG_INTERRUPT_DISABLE    0x14U
#define OHCI_REG_HCCA                 0x18U
#define OHCI_REG_CONTROL_HEAD_ED      0x20U
#define OHCI_REG_PERIODIC_START       0x40U
#define OHCI_REG_RH_DESCRIPTOR_A      0x48U
#define OHCI_REG_RH_STATUS            0x50U
#define OHCI_REG_RH_PORT_STATUS_BASE  0x54U

#define OHCI_CTL_PLE                  (1U << 2)
#define OHCI_CTL_CLE                  (1U << 4)
#define OHCI_USB_OPERATIONAL          0x80U
#define OHCI_STATUS_HCR               (1U << 0)
#define OHCI_STATUS_CLF               (1U << 1)
#define OHCI_RHS_LPSC                 (1U << 16)
#define OHCI_PORT_CCS                 (1U << 0)
#define OHCI_PORT_PES                 (1U << 1)
#define OHCI_PORT_PRS                 (1U << 4)
#define OHCI_PORT_PPS                 (1U << 8)
#define OHCI_PORT_LSDA                (1U << 9)
#define OHCI_PORT_WTC                 0x001f0000U
#define OHCI_TD_R                     (1U << 18)
#define OHCI_TD_DIR_SETUP             0U
#define OHCI_TD_DIR_OUT               1U
#define OHCI_TD_DIR_IN                2U
#define OHCI_TD_DP_SHIFT              19U
#define OHCI_TD_DI_SHIFT              21U
#define OHCI_TD_CC_SHIFT              28U
#define OHCI_TD_CC_NOERROR            0U
#define OHCI_TD_CC_NOT_ACCESSED       0x0fU
#define OHCI_ED_D_SHIFT               11U
#define OHCI_ED_S                     (1U << 13)
#define OHCI_ED_C                     2U
#define OHCI_ED_MPS_SHIFT             16U
#define OHCI_DPTR_MASK                0xfffffff0U
#define OHCI_USB_KEYBOARD_ADDRESS     1U
#define OHCI_USB_KEYBOARD_ENDPOINT    1U
#define OHCI_USB_KEYBOARD_REPORT_SIZE 8U

#define USB_REQ_SET_ADDRESS           0x05U
#define USB_REQ_SET_CONFIGURATION     0x09U
#define USB_REQ_HID_SET_IDLE          0x0aU
#define USB_REQ_HID_SET_PROTOCOL      0x0bU
#define USB_TYPE_CLASS_INTERFACE_OUT  0x21U

typedef struct {
    UINT32 InterruptTable[32];
    UINT16 FrameNumber;
    UINT16 Pad;
    UINT32 DoneHead;
    UINT8 Reserved[120];
} __attribute__((packed)) FW_OHCI_HCCA;

typedef struct {
    UINT32 Flags;
    UINT32 Tail;
    UINT32 Head;
    UINT32 Next;
} __attribute__((packed)) FW_OHCI_ED;

typedef struct {
    UINT32 Flags;
    UINT32 CurrentBufferPointer;
    UINT32 Next;
    UINT32 BufferEnd;
} __attribute__((packed)) FW_OHCI_TD;

extern BOOLEAN mUsbKeyboardTried;
extern BOOLEAN mUsbKeyboardReady;
extern BOOLEAN mUsbKeyboardLowSpeed;
extern UINT8 mUsbKeyboardPort;
extern FW_OHCI_HCCA mUsbOhciHcca;
extern UINT8 mUsbKeyboardPreviousReport[OHCI_USB_KEYBOARD_REPORT_SIZE];

UINT32 usb_ohci_read(UINTN offset);
void usb_ohci_write(UINTN offset, UINT32 value);
UINT32 usb_ohci_phys(const VOID *pointer);
void usb_dma_barrier(void);
UINT32 usb_ohci_ed_head(const FW_OHCI_ED *ed);
UINT32 usb_ohci_ed_tail(const FW_OHCI_ED *ed);
void usb_ohci_init_td(FW_OHCI_TD *td, UINT32 direction, VOID *buffer,
                      UINTN length, FW_OHCI_TD *next, BOOLEAN buffer_rounding);
UINT32 usb_ohci_td_condition_code(const FW_OHCI_TD *td);
BOOLEAN usb_ohci_controller_present(void);
BOOLEAN usb_ohci_reset_controller(void);
void usb_keyboard_submit_interrupt_td(void);
BOOLEAN usb_keyboard_init(void);

EFI_HANDLE fw_usb_controller_handle(VOID);
UINTN fw_usb_controller_device_path(UINT8 *buffer, UINTN capacity);
EFI_HANDLE fw_usb_keyboard_handle(VOID);

BOOLEAN fw_usb_protocols_install(VOID);

#endif /* IA64_FIRMWARE_FW_USB_H */
