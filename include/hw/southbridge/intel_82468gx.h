/*
 * Intel 82468GX I/O and Firmware Bridge
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_SOUTHBRIDGE_INTEL_82468GX_H
#define HW_SOUTHBRIDGE_INTEL_82468GX_H

#include "hw/pci/pci.h"
#include "qom/object.h"

typedef struct IDEBus IDEBus;

#define TYPE_INTEL_82468GX_IFB "intel-82468gx-ifb"
OBJECT_DECLARE_SIMPLE_TYPE(Intel82468GXIFBState, INTEL_82468GX_IFB)

#define TYPE_INTEL_82468GX_IFB_IDE   "intel-82468gx-ifb-ide"
#define TYPE_INTEL_82468GX_IFB_USB   "intel-82468gx-ifb-usb-uhci"
#define TYPE_INTEL_82468GX_IFB_SMBUS "intel-82468gx-ifb-smbus"

#define INTEL_82468GX_IFB_FUNCTIONS 4
#define INTEL_82468GX_IFB_GPIO_LEGACY "legacy"
#define INTEL_82468GX_IFB_GPIO_ISA_IRQ "isa-irq"
#define INTEL_82468GX_IFB_GPIO_SCI    "sci"
#define INTEL_82468GX_IFB_GPIO_SMI    "smi"

#define INTEL_82468GX_IFB_VENDOR_ID       0x8086
#define INTEL_82468GX_IFB_LPC_DEVICE_ID   0x7600
#define INTEL_82468GX_IFB_IDE_DEVICE_ID   0x7601
#define INTEL_82468GX_IFB_USB_DEVICE_ID   0x7602
#define INTEL_82468GX_IFB_SMBUS_DEVICE_ID 0x7603

#define INTEL_82468GX_IFB_IDETIM_PRIMARY  0x40
#define INTEL_82468GX_IFB_IDETIM_SECONDARY 0x42
#define INTEL_82468GX_IFB_IDETIM_DECODE   BIT(15)

Intel82468GXIFBState *intel_82468gx_ifb_create(PCIBus *bus, int devfn,
                                               Error **errp);
PCIDevice *intel_82468gx_ifb_function(Intel82468GXIFBState *s,
                                      unsigned function);
ISABus *intel_82468gx_ifb_isa_bus(Intel82468GXIFBState *s);
IDEBus *intel_82468gx_ifb_ide_bus(Intel82468GXIFBState *s, unsigned channel);
I2CBus *intel_82468gx_ifb_smbus(Intel82468GXIFBState *s);
int intel_82468gx_ifb_pic_read_irq(Intel82468GXIFBState *s);
void intel_82468gx_ifb_configure_acpi(Intel82468GXIFBState *s,
                                      uint16_t io_base);

#endif
