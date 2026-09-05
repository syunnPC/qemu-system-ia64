/*
 * Intel 82466GX Integrated Hot-Plug Controller
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/core/hotplug.h"
#include "hw/core/qdev-properties.h"
#include "hw/ia64/intel_460gx_ihpc.h"
#include "hw/pci/pci.h"
#include "hw/pci/pci_bus.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/error-report.h"
#include "qemu/module.h"
#include "qemu/timer.h"

#define IHPC_VENDOR_ID                  0x8086
#define IHPC_DEVICE_ID                  0x123f
#define IHPC_HIDDEN_DEVICE_ID           0x123e
#define IHPC_REVISION                   0x01
#define IHPC_MMIO_SIZE                  0x100

#define IHPC_CFG_SLOT_ID                0x40
#define IHPC_CFG_MISC                   0x42
#define IHPC_CFG_FEATURES               0x44
#define IHPC_CFG_SWITCH_SERR            0x48
#define IHPC_CFG_POWER_FAULT_SERR       0x49
#define IHPC_CFG_ARB_SERR               0x4a
#define IHPC_CFG_INDEX                  0x50
#define IHPC_CFG_DATA                   0x54

#define IHPC_CFG_MISC_CHANGE_DID        BIT(15)
#define IHPC_CFG_MISC_INHIBIT           BIT(14)
#define IHPC_CFG_MISC_POWER_FAULT       BIT(13)
#define IHPC_CFG_MISC_LOCK_AUTO_DOWN    BIT(12)
#define IHPC_CFG_MISC_INDEX_ENABLE      BIT(7)
#define IHPC_CFG_MISC_RESERVED_ONE      BIT(1)
#define IHPC_CFG_MISC_BUSY              BIT(0)

#define IHPC_REG_SLOT_ENABLE            0x01
#define IHPC_REG_MISC                   0x02
#define IHPC_REG_LED                    0x04
#define IHPC_REG_INTERRUPT              0x08
#define IHPC_REG_INTERRUPT_MASK         0x0c
#define IHPC_REG_SERIAL_DATA            0x10
#define IHPC_REG_SERIAL_POINTER         0x11
#define IHPC_REG_GENERAL_OUTPUT         0x13
#define IHPC_REG_NON_INTERRUPT          0x14
#define IHPC_REG_SLOT_ID                0x28
#define IHPC_REG_SWITCH_REDIRECT        0x2c
#define IHPC_REG_SLOT_POWER             0x2d

#define IHPC_MISC_SERR_POWER_FAULT      BIT(14)
#define IHPC_MISC_INPUT_SCAN_COMPLETE   BIT(12)
#define IHPC_MISC_FREQ_66MHZ            BIT(11)
#define IHPC_MISC_POWER_FAULT_ENABLE    BIT(10)
#define IHPC_MISC_AUTO_POWER_DOWN_DIS   BIT(9)
#define IHPC_MISC_BUSY                  BIT(8)
#define IHPC_MISC_RESERVED_ONE          BIT(6)
#define IHPC_MISC_GENERAL_PENDING       BIT(3)
#define IHPC_MISC_SHIFT_PENDING         BIT(2)
#define IHPC_MISC_SHIFT_IRQ_ENABLE      BIT(1)
#define IHPC_MISC_SOGO                  BIT(0)
#define IHPC_MISC_VALID_MASK            (IHPC_MISC_SERR_POWER_FAULT | \
                                         IHPC_MISC_INPUT_SCAN_COMPLETE | \
                                         IHPC_MISC_FREQ_66MHZ | \
                                         IHPC_MISC_POWER_FAULT_ENABLE | \
                                         IHPC_MISC_AUTO_POWER_DOWN_DIS | \
                                         IHPC_MISC_BUSY | \
                                         IHPC_MISC_RESERVED_ONE | \
                                         IHPC_MISC_GENERAL_PENDING | \
                                         IHPC_MISC_SHIFT_PENDING | \
                                         IHPC_MISC_SHIFT_IRQ_ENABLE | \
                                         IHPC_MISC_SOGO)

#define IHPC_INPUT_SWITCH_SHIFT         0
#define IHPC_INPUT_FAULT_SHIFT          8
#define IHPC_INPUT_PRESENT1_SHIFT       16
#define IHPC_INPUT_PRESENT0_SHIFT       24
#define IHPC_INPUT_GROUP_MASK           0x3fU
#define IHPC_INPUT_VALID_MASK           UINT32_C(0x3f3f3f3f)

struct Intel82466GXIHPCState {
    PCIDevice parent_obj;

    MemoryRegion mmio;
    uint8_t first_slot;
    uint8_t slot_count;

    uint8_t slot_enable;
    uint8_t slot_power;
    uint8_t applied_enable;
    uint8_t applied_power;
    uint8_t present;
    uint8_t switch_closed;
    uint8_t power_fault;
    uint8_t m66_enable;
    uint8_t switch_redirect;
    uint8_t general_output;
    uint8_t serial_pointer;
    uint8_t slot_id;
    uint16_t misc;
    uint32_t led_control;
    uint32_t inputs;
    uint32_t interrupt_latch;
    uint32_t interrupt_pending;
    uint32_t interrupt_mask;
    bool config_write_once;
    bool irq_asserted;

    IA64ChipsetFaultNotify fault_notify;
    void *fault_opaque;
};

static uint8_t ihpc_slot_mask(const Intel82466GXIHPCState *s)
{
    return MAKE_64BIT_MASK(0, s->slot_count);
}

static void ihpc_update_irq(Intel82466GXIHPCState *s)
{
    bool level = (s->interrupt_pending & ~s->interrupt_mask) != 0 ||
                 (s->misc & IHPC_MISC_GENERAL_PENDING) != 0 ||
                 (s->misc & (IHPC_MISC_SHIFT_PENDING |
                             IHPC_MISC_SHIFT_IRQ_ENABLE)) ==
                 (IHPC_MISC_SHIFT_PENDING |
                  IHPC_MISC_SHIFT_IRQ_ENABLE);

    if (s->irq_asserted != level) {
        s->irq_asserted = level;
        pci_set_irq(PCI_DEVICE(s), level);
    }
}

static uint32_t ihpc_compose_inputs(const Intel82466GXIHPCState *s)
{
    uint32_t value = 0;
    uint8_t slots = ihpc_slot_mask(s);

    value |= (uint32_t)(~s->switch_closed & slots)
             << IHPC_INPUT_SWITCH_SHIFT;
    value |= (uint32_t)(~s->power_fault & slots)
             << IHPC_INPUT_FAULT_SHIFT;
    value |= (uint32_t)(~s->present & slots)
             << IHPC_INPUT_PRESENT1_SHIFT;
    value |= (uint32_t)(~s->present & slots)
             << IHPC_INPUT_PRESENT0_SHIFT;
    return value & IHPC_INPUT_VALID_MASK;
}

static void ihpc_latch_input_changes(Intel82466GXIHPCState *s,
                                     uint32_t old_inputs)
{
    uint32_t changed = (old_inputs ^ s->inputs) & IHPC_INPUT_VALID_MASK;
    uint32_t eligible = changed & ~s->interrupt_mask;
    uint32_t switch_bits = eligible & IHPC_INPUT_GROUP_MASK;
    uint32_t redirected = 0;
    unsigned int slot;

    for (slot = 0; slot < s->slot_count; slot++) {
        if ((switch_bits & BIT(slot)) &&
            (s->switch_redirect & BIT(slot))) {
            redirected |= BIT(slot);
        }
    }
    if (redirected) {
        PCI_DEVICE(s)->config[IHPC_CFG_SWITCH_SERR] |= redirected;
        if (pci_get_word(PCI_DEVICE(s)->config + PCI_COMMAND) &
            PCI_COMMAND_SERR) {
            pci_word_test_and_set_mask(
                PCI_DEVICE(s)->config + PCI_STATUS,
                PCI_STATUS_SIG_SYSTEM_ERROR);
        }
    }
    eligible &= ~redirected;
    s->interrupt_latch = (s->interrupt_latch & ~eligible) |
                         (s->inputs & eligible);
    s->interrupt_pending |= eligible;
    ihpc_update_irq(s);
}

static void ihpc_set_input_state(Intel82466GXIHPCState *s)
{
    uint32_t old_inputs = s->inputs;

    s->inputs = ihpc_compose_inputs(s);
    ihpc_latch_input_changes(s, old_inputs);
}

static uint32_t ihpc_interrupt_value(const Intel82466GXIHPCState *s)
{
    return ((s->interrupt_latch & s->interrupt_pending) |
            (s->inputs & ~s->interrupt_pending)) &
           IHPC_INPUT_VALID_MASK;
}

static PCIDevice *ihpc_device_in_slot(Intel82466GXIHPCState *s,
                                      unsigned int slot,
                                      unsigned int function)
{
    PCIBus *bus = pci_get_bus(PCI_DEVICE(s));

    return bus->devices[PCI_DEVFN(s->first_slot + slot, function)];
}

static void ihpc_set_slot_power(Intel82466GXIHPCState *s,
                                unsigned int slot, bool power)
{
    unsigned int function;

    for (function = 0; function < PCI_FUNC_MAX; function++) {
        PCIDevice *affected = ihpc_device_in_slot(s, slot, function);

        if (affected) {
            pci_set_power(affected, power);
        }
    }
}

static bool ihpc_slot_has_pending_unplug(Intel82466GXIHPCState *s,
                                         unsigned int slot)
{
    unsigned int function;

    for (function = 0; function < PCI_FUNC_MAX; function++) {
        PCIDevice *affected = ihpc_device_in_slot(s, slot, function);

        if (affected && affected->qdev.pending_deleted_event) {
            return true;
        }
    }
    return false;
}

static void ihpc_unplug_slot(Intel82466GXIHPCState *s, unsigned int slot)
{
    unsigned int function;

    for (function = 0; function < PCI_FUNC_MAX; function++) {
        PCIDevice *affected = ihpc_device_in_slot(s, slot, function);

        if (!affected || !affected->qdev.pending_deleted_event) {
            continue;
        }
        affected->qdev.pending_deleted_event = false;
        hotplug_handler_unplug(HOTPLUG_HANDLER(s), DEVICE(affected),
                               &error_abort);
        object_unparent(OBJECT(affected));
    }
    s->present &= ~BIT(slot);
    s->switch_closed &= ~BIT(slot);
    s->applied_enable &= ~BIT(slot);
    s->applied_power &= ~BIT(slot);
    ihpc_set_input_state(s);
}

static void ihpc_apply_outputs(Intel82466GXIHPCState *s)
{
    uint8_t slots = ihpc_slot_mask(s);
    uint8_t enable = s->slot_enable & slots;
    uint8_t power = (s->slot_power | enable) & slots;
    unsigned int slot;

    for (slot = 0; slot < s->slot_count; slot++) {
        bool new_power = power & BIT(slot);

        ihpc_set_slot_power(s, slot, new_power);
        if (!(enable & BIT(slot)) &&
            ihpc_slot_has_pending_unplug(s, slot)) {
            ihpc_unplug_slot(s, slot);
        }
    }
    s->applied_enable = enable;
    s->applied_power = power;
    s->misc &= ~(IHPC_MISC_SOGO | IHPC_MISC_BUSY);
    s->misc |= IHPC_MISC_SHIFT_PENDING;
    ihpc_update_irq(s);
}

static uint32_t ihpc_mmio_readl(Intel82466GXIHPCState *s,
                                hwaddr aligned)
{
    switch (aligned) {
    case 0x00:
        return (uint32_t)s->slot_enable << 8 | s->misc << 16;
    case IHPC_REG_LED:
        return s->led_control;
    case IHPC_REG_INTERRUPT:
        return ihpc_interrupt_value(s);
    case IHPC_REG_INTERRUPT_MASK:
        return s->interrupt_mask | ~IHPC_INPUT_VALID_MASK;
    case IHPC_REG_SERIAL_DATA:
        return (uint32_t)s->general_output << 24 |
               (uint32_t)s->serial_pointer << 8 |
               ((ihpc_interrupt_value(s) >>
                 ((s->serial_pointer & 3) * 8)) & 0xff);
    case IHPC_REG_NON_INTERRUPT:
        return s->m66_enable & ihpc_slot_mask(s);
    case IHPC_REG_SLOT_ID:
        return s->slot_id;
    case IHPC_REG_SWITCH_REDIRECT:
        return (uint32_t)s->slot_power << 8 | s->switch_redirect;
    default:
        return 0;
    }
}

static void ihpc_mmio_writel(Intel82466GXIHPCState *s, hwaddr aligned,
                             uint32_t value, uint32_t byte_mask)
{
    uint8_t slots = ihpc_slot_mask(s);

    switch (aligned) {
    case 0x00:
        if (byte_mask & 0x0000ff00) {
            s->slot_enable = (value >> 8) & slots;
            s->slot_power &= s->slot_enable;
        }
        if (byte_mask & 0xffff0000) {
            uint16_t write = value >> 16;
            uint16_t mask = byte_mask >> 16;
            uint16_t writable = IHPC_MISC_SERR_POWER_FAULT |
                                IHPC_MISC_INPUT_SCAN_COMPLETE |
                                IHPC_MISC_POWER_FAULT_ENABLE |
                                IHPC_MISC_AUTO_POWER_DOWN_DIS |
                                IHPC_MISC_SHIFT_IRQ_ENABLE |
                                IHPC_MISC_SOGO;

            if (PCI_DEVICE(s)->config[IHPC_CFG_MISC + 1] & BIT(4)) {
                writable &= ~IHPC_MISC_AUTO_POWER_DOWN_DIS;
            }
            if ((write & mask) & IHPC_MISC_SHIFT_PENDING) {
                s->misc &= ~IHPC_MISC_SHIFT_PENDING;
            }
            s->misc = (s->misc & ~(mask & writable)) |
                      (write & mask & writable) |
                      IHPC_MISC_RESERVED_ONE;
            if (s->misc & IHPC_MISC_SOGO) {
                s->misc |= IHPC_MISC_BUSY;
                ihpc_apply_outputs(s);
            }
            ihpc_update_irq(s);
        }
        break;
    case IHPC_REG_LED:
        s->led_control = (s->led_control & ~byte_mask) |
                         (value & byte_mask);
        s->led_control &= UINT32_C(0x3f3f3f3f);
        break;
    case IHPC_REG_INTERRUPT:
        s->interrupt_pending &= ~(value & byte_mask &
                                  IHPC_INPUT_VALID_MASK);
        ihpc_update_irq(s);
        break;
    case IHPC_REG_INTERRUPT_MASK:
        s->interrupt_mask = (s->interrupt_mask & ~byte_mask) |
                            (value & byte_mask);
        s->interrupt_mask |= ~IHPC_INPUT_VALID_MASK;
        ihpc_update_irq(s);
        break;
    case IHPC_REG_SERIAL_DATA:
        if (byte_mask & 0x0000ff00) {
            s->serial_pointer = (value >> 8) & 0xf;
        }
        if (byte_mask & 0xff000000) {
            s->general_output = (value >> 24) & slots;
        }
        break;
    case IHPC_REG_SLOT_ID:
        if (byte_mask & 0x000000ff) {
            s->slot_id = value;
        }
        break;
    case IHPC_REG_SWITCH_REDIRECT:
        if (byte_mask & 0x000000ff) {
            s->switch_redirect = value & slots;
        }
        if (byte_mask & 0x0000ff00) {
            uint8_t requested = (value >> 8) & slots;

            s->slot_power = (s->slot_power & s->slot_enable) |
                            (requested & ~s->slot_enable);
        }
        break;
    default:
        break;
    }
}

static uint64_t ihpc_mmio_read(void *opaque, hwaddr addr, unsigned int size)
{
    Intel82466GXIHPCState *s = opaque;
    uint64_t value = 0;
    unsigned int i;

    if (pci_get_word(PCI_DEVICE(s)->config + IHPC_CFG_MISC) &
        IHPC_CFG_MISC_INHIBIT) {
        return MAKE_64BIT_MASK(0, size * 8);
    }
    for (i = 0; i < size; i++) {
        hwaddr byte_addr = addr + i;
        uint32_t word = ihpc_mmio_readl(s, byte_addr & ~3);

        value |= (uint64_t)((word >> ((byte_addr & 3) * 8)) & 0xff)
                 << (i * 8);
    }
    return value;
}

static void ihpc_mmio_write(void *opaque, hwaddr addr, uint64_t value,
                            unsigned int size)
{
    Intel82466GXIHPCState *s = opaque;
    unsigned int i;

    if (pci_get_word(PCI_DEVICE(s)->config + IHPC_CFG_MISC) &
        IHPC_CFG_MISC_INHIBIT) {
        return;
    }
    for (i = 0; i < size; i++) {
        hwaddr byte_addr = addr + i;
        unsigned int shift = (byte_addr & 3) * 8;
        uint32_t byte = (value >> (i * 8)) & 0xff;

        ihpc_mmio_writel(s, byte_addr & ~3, byte << shift,
                         UINT32_C(0xff) << shift);
    }
}

static const MemoryRegionOps ihpc_mmio_ops = {
    .read = ihpc_mmio_read,
    .write = ihpc_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 4,
        .unaligned = true,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 4,
        .unaligned = true,
    },
};

static void ihpc_sync_config(Intel82466GXIHPCState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint16_t misc = pci_get_word(pdev->config + IHPC_CFG_MISC);
    uint32_t index = pci_get_long(pdev->config + IHPC_CFG_INDEX) & 0xfc;

    misc = (misc & ~IHPC_CFG_MISC_BUSY) |
           ((s->misc & IHPC_MISC_BUSY) ? IHPC_CFG_MISC_BUSY : 0) |
           IHPC_CFG_MISC_RESERVED_ONE;
    if (s->misc & IHPC_MISC_POWER_FAULT_ENABLE) {
        misc |= IHPC_CFG_MISC_POWER_FAULT;
    } else {
        misc &= ~IHPC_CFG_MISC_POWER_FAULT;
    }
    pci_set_word(pdev->config + IHPC_CFG_MISC, misc);
    pci_set_word(pdev->config + IHPC_CFG_SLOT_ID, s->slot_id);
    pci_set_long(pdev->config + IHPC_CFG_INDEX, index);
    if (misc & IHPC_CFG_MISC_INDEX_ENABLE) {
        pci_set_long(pdev->config + IHPC_CFG_DATA,
                     ihpc_mmio_readl(s, index));
    } else {
        pci_set_long(pdev->config + IHPC_CFG_DATA, 0);
    }
}

static uint32_t ihpc_read_config(PCIDevice *pdev, uint32_t address, int len)
{
    Intel82466GXIHPCState *s = INTEL_82466GX_IHPC(pdev);

    ihpc_sync_config(s);
    return pci_default_read_config(pdev, address, len);
}

static void ihpc_write_config(PCIDevice *pdev, uint32_t address,
                              uint32_t value, int len)
{
    Intel82466GXIHPCState *s = INTEL_82466GX_IHPC(pdev);
    uint16_t old_misc = pci_get_word(pdev->config + IHPC_CFG_MISC);
    uint8_t old_slot_id = pdev->config[IHPC_CFG_SLOT_ID];
    uint32_t old_index = pci_get_long(pdev->config + IHPC_CFG_INDEX);
    uint32_t old_data = pci_get_long(pdev->config + IHPC_CFG_DATA);
    uint8_t old_switch_serr = pdev->config[IHPC_CFG_SWITCH_SERR];
    uint8_t old_power_serr = pdev->config[IHPC_CFG_POWER_FAULT_SERR];

    pci_default_write_config(pdev, address, value, len);

    if (ranges_overlap(address, len, IHPC_CFG_SLOT_ID, 1)) {
        s->slot_id = pdev->config[IHPC_CFG_SLOT_ID];
    } else {
        pdev->config[IHPC_CFG_SLOT_ID] = old_slot_id;
    }

    if (ranges_overlap(address, len, IHPC_CFG_MISC, 2)) {
        uint16_t new_misc = pci_get_word(pdev->config + IHPC_CFG_MISC);
        uint16_t writable = IHPC_CFG_MISC_POWER_FAULT |
                            IHPC_CFG_MISC_INDEX_ENABLE;
        uint16_t once = IHPC_CFG_MISC_CHANGE_DID |
                        IHPC_CFG_MISC_INHIBIT |
                        IHPC_CFG_MISC_LOCK_AUTO_DOWN;

        if (!s->config_write_once) {
            writable |= once;
            if (new_misc & once) {
                s->config_write_once = true;
            }
        }
        new_misc = (old_misc & ~writable) | (new_misc & writable) |
                   IHPC_CFG_MISC_RESERVED_ONE;
        pci_set_word(pdev->config + IHPC_CFG_MISC, new_misc);
        if (new_misc & IHPC_CFG_MISC_CHANGE_DID) {
            pci_set_word(pdev->config + PCI_DEVICE_ID,
                         IHPC_HIDDEN_DEVICE_ID);
        }
        s->misc = (s->misc & ~IHPC_MISC_POWER_FAULT_ENABLE) |
                  ((new_misc & IHPC_CFG_MISC_POWER_FAULT) ?
                   IHPC_MISC_POWER_FAULT_ENABLE : 0);
    }
    if (ranges_overlap(address, len, IHPC_CFG_SWITCH_SERR, 1)) {
        pdev->config[IHPC_CFG_SWITCH_SERR] =
            old_switch_serr & ~pdev->config[IHPC_CFG_SWITCH_SERR];
    }
    if (ranges_overlap(address, len, IHPC_CFG_POWER_FAULT_SERR, 1)) {
        pdev->config[IHPC_CFG_POWER_FAULT_SERR] =
            old_power_serr & ~pdev->config[IHPC_CFG_POWER_FAULT_SERR];
    }
    if (ranges_overlap(address, len, IHPC_CFG_ARB_SERR, 1)) {
        pdev->config[IHPC_CFG_ARB_SERR] = 0;
    }
    if (ranges_overlap(address, len, IHPC_CFG_INDEX, 4)) {
        pci_set_long(pdev->config + IHPC_CFG_INDEX,
                     pci_get_long(pdev->config + IHPC_CFG_INDEX) & 0xfc);
    } else {
        pci_set_long(pdev->config + IHPC_CFG_INDEX, old_index);
    }
    if (ranges_overlap(address, len, IHPC_CFG_DATA, 4) &&
        (pci_get_word(pdev->config + IHPC_CFG_MISC) &
         IHPC_CFG_MISC_INDEX_ENABLE)) {
        uint32_t index = pci_get_long(pdev->config + IHPC_CFG_INDEX) & 0xfc;
        uint32_t new_data = pci_get_long(pdev->config + IHPC_CFG_DATA);

        ihpc_mmio_writel(s, index, new_data, UINT32_MAX);
    }
    pci_set_long(pdev->config + IHPC_CFG_DATA, old_data);
    ihpc_sync_config(s);
}

static bool ihpc_get_slot(Intel82466GXIHPCState *s, PCIDevice *pdev,
                          unsigned int *slot, Error **errp)
{
    unsigned int pci_slot = PCI_SLOT(pdev->devfn);

    if (pci_slot < s->first_slot ||
        pci_slot >= s->first_slot + s->slot_count) {
        error_setg(errp,
                   "Intel 82466GX IHPC controls PCI slots %u through %u",
                   s->first_slot, s->first_slot + s->slot_count - 1);
        return false;
    }
    *slot = pci_slot - s->first_slot;
    return true;
}

static void ihpc_device_plug(HotplugHandler *hotplug_dev, DeviceState *dev,
                             Error **errp)
{
    Intel82466GXIHPCState *s = INTEL_82466GX_IHPC(hotplug_dev);
    PCIDevice *pdev = PCI_DEVICE(dev);
    unsigned int slot;

    if (PCI_SLOT(pdev->devfn) < s->first_slot ||
        PCI_SLOT(pdev->devfn) >= s->first_slot + s->slot_count) {
        if (dev->hotplugged) {
            ihpc_get_slot(s, pdev, &slot, errp);
        }
        return;
    }
    slot = PCI_SLOT(pdev->devfn) - s->first_slot;
    s->present |= BIT(slot);
    s->switch_closed |= BIT(slot);
    if (dev->hotplugged) {
        s->slot_enable &= ~BIT(slot);
        s->slot_power &= ~BIT(slot);
        s->applied_enable &= ~BIT(slot);
        s->applied_power &= ~BIT(slot);
        ihpc_set_slot_power(s, slot, false);
        ihpc_set_input_state(s);
    } else {
        s->slot_enable |= BIT(slot);
        s->slot_power |= BIT(slot);
        s->applied_enable |= BIT(slot);
        s->applied_power |= BIT(slot);
        s->inputs = ihpc_compose_inputs(s);
    }
}

static void ihpc_device_unplug(HotplugHandler *hotplug_dev, DeviceState *dev,
                               Error **errp)
{
    (void)hotplug_dev;
    (void)errp;
    qdev_unrealize(dev);
}

static void ihpc_device_unplug_request(HotplugHandler *hotplug_dev,
                                       DeviceState *dev, Error **errp)
{
    Intel82466GXIHPCState *s = INTEL_82466GX_IHPC(hotplug_dev);
    PCIDevice *pdev = PCI_DEVICE(dev);
    int64_t expires;
    unsigned int function;
    unsigned int slot;

    if (!ihpc_get_slot(s, pdev, &slot, errp)) {
        return;
    }
    expires = qemu_clock_get_ms(QEMU_CLOCK_VIRTUAL) + 5000;
    for (function = 0; function < PCI_FUNC_MAX; function++) {
        PCIDevice *affected = ihpc_device_in_slot(s, slot, function);

        if (affected) {
            affected->qdev.pending_deleted_event = true;
            affected->qdev.pending_deleted_expires_ms = expires;
        }
    }
    s->switch_closed &= ~BIT(slot);
    ihpc_set_input_state(s);
    if (!(s->applied_enable & BIT(slot))) {
        ihpc_unplug_slot(s, slot);
    }
}

static bool ihpc_is_hotpluggable_bus(HotplugHandler *hotplug_dev,
                                     BusState *bus)
{
    return bus == BUS(pci_get_bus(PCI_DEVICE(hotplug_dev)));
}

static void ihpc_slot_present(void *opaque, int n, int level)
{
    Intel82466GXIHPCState *s = opaque;

    if (level) {
        s->present |= BIT(n);
    } else {
        s->present &= ~BIT(n);
    }
    ihpc_set_input_state(s);
}

static void ihpc_slot_switch(void *opaque, int n, int level)
{
    Intel82466GXIHPCState *s = opaque;

    if (level) {
        s->switch_closed |= BIT(n);
    } else {
        s->switch_closed &= ~BIT(n);
    }
    ihpc_set_input_state(s);
}

static void ihpc_power_fault(void *opaque, int n, int level)
{
    Intel82466GXIHPCState *s = opaque;
    PCIDevice *pdev = PCI_DEVICE(s);

    if (level) {
        s->power_fault |= BIT(n);
    } else {
        s->power_fault &= ~BIT(n);
    }
    ihpc_set_input_state(s);
    if (level &&
        (s->misc & IHPC_MISC_POWER_FAULT_ENABLE) &&
        (s->applied_power & BIT(n))) {
        pdev->config[IHPC_CFG_POWER_FAULT_SERR] |= BIT(n);
        if ((s->misc & IHPC_MISC_SERR_POWER_FAULT) &&
            (pci_get_word(pdev->config + PCI_COMMAND) & PCI_COMMAND_SERR)) {
            pci_word_test_and_set_mask(pdev->config + PCI_STATUS,
                                       PCI_STATUS_SIG_SYSTEM_ERROR);
        }
        if (s->fault_notify) {
            IA64ChipsetFault fault = {
                .source = IA64_CHIPSET_FAULT_460GX,
                .reason = IA64_CHIPSET_FAULT_POWER,
                .bus = pci_bus_num(pci_get_bus(pdev)),
                .severity = IA64_RAS_SEVERITY_FATAL,
                .requester = PCI_BUILD_BDF(pci_bus_num(pci_get_bus(pdev)),
                                           pdev->devfn),
                .status = BIT(n),
                .information = n,
            };

            s->fault_notify(s->fault_opaque, &fault);
        }
    }
}

static void ihpc_reset(DeviceState *dev)
{
    Intel82466GXIHPCState *s = INTEL_82466GX_IHPC(dev);
    PCIDevice *pdev = PCI_DEVICE(dev);
    uint8_t present = 0;
    unsigned int slot;

    for (slot = 0; slot < s->slot_count; slot++) {
        unsigned int function;

        for (function = 0; function < PCI_FUNC_MAX; function++) {
            if (ihpc_device_in_slot(s, slot, function)) {
                present |= BIT(slot);
                break;
            }
        }
    }
    s->slot_enable = present;
    s->slot_power = present;
    s->applied_enable = present;
    s->applied_power = present;
    s->present = present;
    s->switch_closed = present;
    s->power_fault = 0;
    s->m66_enable = 0;
    s->switch_redirect = 0;
    s->general_output = 0;
    s->serial_pointer = 0;
    s->slot_id = ((s->first_slot & 0xf) << 4) |
                 (s->slot_count & 0xf);
    s->misc = IHPC_MISC_RESERVED_ONE;
    s->led_control = (uint32_t)present | (uint32_t)present << 8;
    s->inputs = ihpc_compose_inputs(s);
    s->interrupt_latch = 0;
    s->interrupt_pending = 0;
    s->interrupt_mask = UINT32_MAX;
    s->config_write_once = false;
    s->irq_asserted = false;

    pci_set_word(pdev->config + PCI_COMMAND, 0);
    pci_set_word(pdev->config + PCI_STATUS, PCI_STATUS_DEVSEL_MEDIUM);
    pci_set_word(pdev->config + PCI_DEVICE_ID, IHPC_DEVICE_ID);
    pci_set_word(pdev->config + IHPC_CFG_MISC,
                 IHPC_CFG_MISC_RESERVED_ONE);
    pci_set_word(pdev->config + IHPC_CFG_FEATURES, 0);
    pdev->config[IHPC_CFG_SWITCH_SERR] = 0;
    pdev->config[IHPC_CFG_POWER_FAULT_SERR] = 0;
    pdev->config[IHPC_CFG_ARB_SERR] = 0;
    pci_set_long(pdev->config + IHPC_CFG_INDEX, 0);
    pci_set_long(pdev->config + IHPC_CFG_DATA, 0);
    ihpc_sync_config(s);
    for (slot = 0; slot < s->slot_count; slot++) {
        ihpc_set_slot_power(s, slot, present & BIT(slot));
    }
    pci_set_irq(pdev, 0);
}

static bool ihpc_validate_migrated_state(Intel82466GXIHPCState *s)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint8_t slots = ihpc_slot_mask(s);
    uint8_t actual = 0;
    uint16_t cfg_misc = pci_get_word(pdev->config + IHPC_CFG_MISC);
    uint16_t cfg_once = IHPC_CFG_MISC_CHANGE_DID |
                        IHPC_CFG_MISC_INHIBIT |
                        IHPC_CFG_MISC_LOCK_AUTO_DOWN;
    unsigned int slot;

    if ((s->slot_enable | s->slot_power | s->applied_enable |
         s->applied_power | s->present | s->switch_closed |
         s->power_fault | s->m66_enable | s->switch_redirect |
         s->general_output) & ~slots) {
        return false;
    }
    if ((s->applied_enable & ~s->applied_power) ||
        (s->misc & ~IHPC_MISC_VALID_MASK) ||
        !(s->misc & IHPC_MISC_RESERVED_ONE) ||
        (s->led_control & ~IHPC_INPUT_VALID_MASK) ||
        (s->inputs & ~IHPC_INPUT_VALID_MASK) ||
        (s->interrupt_latch & ~IHPC_INPUT_VALID_MASK) ||
        (s->interrupt_pending & ~IHPC_INPUT_VALID_MASK) ||
        (~s->interrupt_mask & ~IHPC_INPUT_VALID_MASK) ||
        s->serial_pointer > 0xf ||
        s->inputs != ihpc_compose_inputs(s) ||
        s->config_write_once != !!(cfg_misc & cfg_once) ||
        (!!(cfg_misc & IHPC_CFG_MISC_CHANGE_DID) !=
         (pci_get_word(pdev->config + PCI_DEVICE_ID) ==
          IHPC_HIDDEN_DEVICE_ID))) {
        return false;
    }
    for (slot = 0; slot < s->slot_count; slot++) {
        unsigned int function;

        for (function = 0; function < PCI_FUNC_MAX; function++) {
            if (ihpc_device_in_slot(s, slot, function)) {
                actual |= BIT(slot);
                break;
            }
        }
    }
    return !(actual & ~s->present);
}

static int ihpc_post_load(void *opaque, int version_id)
{
    Intel82466GXIHPCState *s = opaque;
    unsigned int slot;

    (void)version_id;
    if (!ihpc_validate_migrated_state(s)) {
        error_report("invalid Intel 82466GX IHPC migration state");
        return -EINVAL;
    }
    for (slot = 0; slot < s->slot_count; slot++) {
        ihpc_set_slot_power(s, slot, s->applied_power & BIT(slot));
    }
    s->irq_asserted = false;
    ihpc_sync_config(s);
    ihpc_update_irq(s);
    return 0;
}

static const VMStateDescription vmstate_intel_82466gx_ihpc = {
    .name = TYPE_INTEL_82466GX_IHPC,
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load = ihpc_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, Intel82466GXIHPCState),
        VMSTATE_UINT8_EQUAL(first_slot, Intel82466GXIHPCState),
        VMSTATE_UINT8_EQUAL(slot_count, Intel82466GXIHPCState),
        VMSTATE_UINT8(slot_enable, Intel82466GXIHPCState),
        VMSTATE_UINT8(slot_power, Intel82466GXIHPCState),
        VMSTATE_UINT8(applied_enable, Intel82466GXIHPCState),
        VMSTATE_UINT8(applied_power, Intel82466GXIHPCState),
        VMSTATE_UINT8(present, Intel82466GXIHPCState),
        VMSTATE_UINT8(switch_closed, Intel82466GXIHPCState),
        VMSTATE_UINT8(power_fault, Intel82466GXIHPCState),
        VMSTATE_UINT8(m66_enable, Intel82466GXIHPCState),
        VMSTATE_UINT8(switch_redirect, Intel82466GXIHPCState),
        VMSTATE_UINT8(general_output, Intel82466GXIHPCState),
        VMSTATE_UINT8(serial_pointer, Intel82466GXIHPCState),
        VMSTATE_UINT8(slot_id, Intel82466GXIHPCState),
        VMSTATE_UINT16(misc, Intel82466GXIHPCState),
        VMSTATE_UINT32(led_control, Intel82466GXIHPCState),
        VMSTATE_UINT32(inputs, Intel82466GXIHPCState),
        VMSTATE_UINT32(interrupt_latch, Intel82466GXIHPCState),
        VMSTATE_UINT32(interrupt_pending, Intel82466GXIHPCState),
        VMSTATE_UINT32(interrupt_mask, Intel82466GXIHPCState),
        VMSTATE_BOOL(config_write_once, Intel82466GXIHPCState),
        VMSTATE_END_OF_LIST()
    },
};

static void ihpc_realize(PCIDevice *pdev, Error **errp)
{
    Intel82466GXIHPCState *s = INTEL_82466GX_IHPC(pdev);

    if (!s->slot_count ||
        s->slot_count > INTEL_82466GX_IHPC_MAX_SLOTS ||
        s->first_slot + s->slot_count > PCI_SLOT_MAX + 1) {
        error_setg(errp,
                   "Intel 82466GX IHPC requires 1..6 slots in the PCI slot range");
        return;
    }
    if (PCI_SLOT(pdev->devfn) >= s->first_slot &&
        PCI_SLOT(pdev->devfn) < s->first_slot + s->slot_count) {
        error_setg(errp, "Intel 82466GX IHPC cannot control its own slot");
        return;
    }

    memset(pdev->wmask, 0, pci_config_size(pdev));
    memset(pdev->w1cmask, 0, pci_config_size(pdev));
    pci_set_word(pdev->wmask + PCI_COMMAND,
                 PCI_COMMAND_MEMORY | PCI_COMMAND_PARITY |
                 PCI_COMMAND_SERR);
    pci_set_word(pdev->w1cmask + PCI_STATUS,
                 PCI_STATUS_DETECTED_PARITY |
                 PCI_STATUS_SIG_SYSTEM_ERROR);
    pdev->wmask[PCI_CACHE_LINE_SIZE] = 0xff;
    pdev->wmask[PCI_LATENCY_TIMER] = 0xff;
    pdev->wmask[PCI_INTERRUPT_LINE] = 0xff;
    pdev->wmask[IHPC_CFG_SLOT_ID] = 0xff;
    pci_set_word(pdev->wmask + IHPC_CFG_MISC, UINT16_MAX);
    pdev->wmask[IHPC_CFG_SWITCH_SERR] = 0x3f;
    pdev->wmask[IHPC_CFG_POWER_FAULT_SERR] = 0x3f;
    pci_set_long(pdev->wmask + IHPC_CFG_INDEX, 0xfc);
    pci_set_long(pdev->wmask + IHPC_CFG_DATA, UINT32_MAX);

    memory_region_init_io(&s->mmio, OBJECT(s), &ihpc_mmio_ops, s,
                          TYPE_INTEL_82466GX_IHPC, IHPC_MMIO_SIZE);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_MEMORY, &s->mmio);
    pci_config_set_interrupt_pin(pdev->config, 1);
    qdev_init_gpio_in_named(DEVICE(s), ihpc_slot_present,
                            INTEL_82466GX_IHPC_GPIO_SLOT_PRESENT,
                            INTEL_82466GX_IHPC_MAX_SLOTS);
    qdev_init_gpio_in_named(DEVICE(s), ihpc_slot_switch,
                            INTEL_82466GX_IHPC_GPIO_SLOT_SWITCH,
                            INTEL_82466GX_IHPC_MAX_SLOTS);
    qdev_init_gpio_in_named(DEVICE(s), ihpc_power_fault,
                            INTEL_82466GX_IHPC_GPIO_POWER_FAULT,
                            INTEL_82466GX_IHPC_MAX_SLOTS);
    qbus_set_hotplug_handler(BUS(pci_get_bus(pdev)), OBJECT(s));
}

static const Property ihpc_properties[] = {
    DEFINE_PROP_UINT8(INTEL_82466GX_IHPC_PROP_FIRST_SLOT,
                      Intel82466GXIHPCState, first_slot, 1),
    DEFINE_PROP_UINT8(INTEL_82466GX_IHPC_PROP_SLOT_COUNT,
                      Intel82466GXIHPCState, slot_count, 6),
};

static void ihpc_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *pc = PCI_DEVICE_CLASS(klass);
    HotplugHandlerClass *hc = HOTPLUG_HANDLER_CLASS(klass);

    (void)data;
    dc->desc = "Intel 82466GX Integrated Hot-Plug Controller";
    dc->vmsd = &vmstate_intel_82466gx_ihpc;
    device_class_set_legacy_reset(dc, ihpc_reset);
    device_class_set_props(dc, ihpc_properties);
    pc->realize = ihpc_realize;
    pc->config_read = ihpc_read_config;
    pc->config_write = ihpc_write_config;
    pc->vendor_id = IHPC_VENDOR_ID;
    pc->device_id = IHPC_DEVICE_ID;
    pc->revision = IHPC_REVISION;
    pc->class_id = PCI_CLASS_SYSTEM_PCI_HOTPLUG;
    pc->subsystem_vendor_id = IHPC_VENDOR_ID;
    pc->subsystem_id = IHPC_DEVICE_ID;
    hc->plug = ihpc_device_plug;
    hc->unplug_request = ihpc_device_unplug_request;
    hc->unplug = ihpc_device_unplug;
    hc->is_hotpluggable_bus = ihpc_is_hotpluggable_bus;
}

static const TypeInfo ihpc_type_info = {
    .name = TYPE_INTEL_82466GX_IHPC,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(Intel82466GXIHPCState),
    .class_init = ihpc_class_init,
    .interfaces = (const InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { TYPE_HOTPLUG_HANDLER },
        { }
    },
};

void intel_82466gx_ihpc_set_fault_notify(
    Intel82466GXIHPCState *s, IA64ChipsetFaultNotify notify, void *opaque)
{
    s->fault_notify = notify;
    s->fault_opaque = opaque;
}

static void ihpc_register_types(void)
{
    type_register_static(&ihpc_type_info);
}
type_init(ihpc_register_types)
