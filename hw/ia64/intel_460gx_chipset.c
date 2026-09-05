/*
 * Intel 460GX chipset configuration targets
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/core/qdev-properties.h"
#include "hw/ia64/intel_460gx_chipset.h"
#include "hw/ia64/intel_460gx_host.h"
#include "hw/pci/pci.h"
#include "migration/vmstate.h"
#include "qapi/error.h"
#include "qemu/cutils.h"
#include "qemu/module.h"
#include "system/qtest.h"

#define INTEL_460GX_CONFIG_SIZE 256
#define INTEL_460GX_VENDOR_ID   0x8086
#define INTEL_460GX_SAC_ID      0x84e0
#define INTEL_460GX_SDC_ID      0x84e1
#define INTEL_460GX_MAC_ID      0x84e3
#define INTEL_460GX_PXB_ID      0x84cb
#define INTEL_460GX_WXB_ID      0x84e6
#define INTEL_460GX_GXB_F1_ID   0x84ea
#define INTEL_460GX_GXB_F2_ID   0x84e2

#define INTEL_460GX_SAC_REVISION 0x03
#define INTEL_460GX_SDC_REVISION 0x03
#define INTEL_460GX_MAC_REVISION 0x03
#define INTEL_460GX_PXB_REVISION 0x05
#define INTEL_460GX_WXB_REVISION 0x07
#define INTEL_460GX_GXB_REVISION 0x02

#define INTEL_460GX_SAC_FERR     0x40
#define INTEL_460GX_SAC_NERR     0x44
#define INTEL_460GX_SAC_SA_FERR  0x60
#define INTEL_460GX_SAC_SECTID   0x80
#define INTEL_460GX_SAC_DEDTID   0x81

#define INTEL_460GX_SAC_MBE      BIT(31)
#define INTEL_460GX_SAC_MAE      BIT(30)
#define INTEL_460GX_SAC_SCME     BIT(24)
#define INTEL_460GX_SAC_SNE      BIT(23)
#define INTEL_460GX_SAC_SFE      BIT(22)

#define INTEL_460GX_SDC_FERR     0x80
#define INTEL_460GX_SDC_NERR     0x84

#define INTEL_460GX_MAC_FERR     0x98
#define INTEL_460GX_MAC_CMND_FERR 0x9c
#define INTEL_460GX_MAC_QUEUE_ERROR BIT(1)
#define INTEL_460GX_MAC_COMMAND_ERROR BIT(0)

static const uint8_t sac_defined_function_mask[] = {
    BIT(0) | BIT(1) | BIT(2),
    BIT(2) | BIT(3),
};

typedef struct Intel460GXRegisterBlock {
    uint8_t config[INTEL_460GX_CONFIG_SIZE];
    uint8_t reset[INTEL_460GX_CONFIG_SIZE];
    uint8_t wmask[INTEL_460GX_CONFIG_SIZE];
    uint8_t w1cmask[INTEL_460GX_CONFIG_SIZE];
    uint8_t sticky[INTEL_460GX_CONFIG_SIZE];
    uint16_t coupled_w1c_a;
    uint16_t coupled_w1c_b;
    uint8_t coupled_w1c_size;
    Intel460GXChipsetState *chipset;
    uint8_t decoded_role;
    uint8_t decoded_port;
} Intel460GXRegisterBlock;

enum {
    INTEL_460GX_DECODED_NONE,
    INTEL_460GX_DECODED_SAC,
    INTEL_460GX_DECODED_XXB,
};

struct Intel460GXChipsetState {
    DeviceState parent_obj;

    Intel460GXHostState *host;
    uint8_t expander_mask;
    Intel460GXRegisterBlock sac[2][8];
    Intel460GXRegisterBlock sdc;
    Intel460GXRegisterBlock memory_card[2][2];
    Intel460GXRegisterBlock downstream_sac[INTEL_460GX_DOWNSTREAM_PORTS];
    Intel460GXRegisterBlock expander[INTEL_460GX_DOWNSTREAM_PORTS];
    Intel460GXRegisterBlock gxb_function2;
    IA64ChipsetFaultNotify fault_notify;
    void *fault_opaque;
};

static uint32_t register_block_get_long(const uint8_t *config,
                                        unsigned int offset)
{
    return (uint32_t)config[offset] |
           (uint32_t)config[offset + 1] << 8 |
           (uint32_t)config[offset + 2] << 16 |
           (uint32_t)config[offset + 3] << 24;
}

static uint32_t register_block_read(void *opaque, uint16_t offset,
                                    unsigned size)
{
    Intel460GXRegisterBlock *block = opaque;
    uint32_t value = 0;
    unsigned i;

    if ((size != 1 && size != 2 && size != 4) ||
        offset > INTEL_460GX_CONFIG_SIZE - size) {
        return size == 4 ? UINT32_MAX : MAKE_64BIT_MASK(0, size * 8);
    }
    for (i = 0; i < size; i++) {
        value |= (uint32_t)block->config[offset + i] << (i * 8);
    }
    return value;
}

static void register_block_write_raw(Intel460GXRegisterBlock *block,
                                     uint16_t offset, uint32_t value,
                                     unsigned size)
{
    unsigned i;

    if ((size != 1 && size != 2 && size != 4) ||
        offset > INTEL_460GX_CONFIG_SIZE - size) {
        return;
    }
    for (i = 0; i < size; i++) {
        unsigned index = offset + i;
        uint8_t byte = value >> (i * 8);
        uint8_t old = block->config[index];
        uint8_t clear = byte & block->w1cmask[index];

        old = (old & ~block->wmask[index]) |
              (byte & block->wmask[index]);
        old &= ~clear;
        block->config[index] = old;
        if (index >= block->coupled_w1c_a &&
            index < block->coupled_w1c_a + block->coupled_w1c_size) {
            block->config[block->coupled_w1c_b +
                          index - block->coupled_w1c_a] &= ~clear;
        } else if (index >= block->coupled_w1c_b &&
                   index < block->coupled_w1c_b +
                           block->coupled_w1c_size) {
            block->config[block->coupled_w1c_a +
                          index - block->coupled_w1c_b] &= ~clear;
        }
    }
}

static void register_block_write(void *opaque, uint16_t offset,
                                 uint32_t value, unsigned size)
{
    Intel460GXRegisterBlock *block = opaque;
    Intel460GXDecodedStateUpdate update = { 0 };
    uint8_t old_config[INTEL_460GX_CONFIG_SIZE];
    Error *local_err = NULL;
    bool decoded = false;

    memcpy(old_config, block->config, sizeof(old_config));
    register_block_write_raw(block, offset, value, size);

    if (block->decoded_role == INTEL_460GX_DECODED_SAC) {
        if (ranges_overlap(offset, size, INTEL_460GX_SAC_CBN_OFFSET, 1)) {
            update.has_cbn = true;
            update.cbn = block->config[INTEL_460GX_SAC_CBN_OFFSET];
            decoded = true;
        }
        if (ranges_overlap(offset, size,
                           INTEL_460GX_SAC_DEVNPRES_OFFSET, 4)) {
            update.has_chipset_present = true;
            update.chipset_present =
                ~register_block_get_long(
                    block->config, INTEL_460GX_SAC_DEVNPRES_OFFSET) &
                intel_460gx_chipset_device_mask();
            decoded = true;
        }
    } else if (block->decoded_role == INTEL_460GX_DECODED_XXB &&
               ranges_overlap(offset, size,
                              INTEL_460GX_XXB_BUSNO_OFFSET, 2)) {
        update.route_mask = BIT(block->decoded_port);
        update.routes[block->decoded_port].first_bus =
            block->config[INTEL_460GX_XXB_BUSNO_OFFSET];
        update.routes[block->decoded_port].last_bus =
            block->config[INTEL_460GX_XXB_SUBNO_OFFSET];
        decoded = true;
    }

    if (decoded &&
        !intel_460gx_host_apply_decoded_update(block->chipset->host,
                                                &update, &local_err)) {
        memcpy(block->config, old_config, sizeof(block->config));
        error_free(local_err);
    }
}

static const Intel460GXConfigTargetOps register_block_ops = {
    .read = register_block_read,
    .write = register_block_write,
};

static void register_block_set_word(uint8_t *config, unsigned offset,
                                    uint16_t value)
{
    config[offset] = value;
    config[offset + 1] = value >> 8;
}

static void register_block_set_long(uint8_t *config, unsigned offset,
                                    uint32_t value)
{
    config[offset] = value;
    config[offset + 1] = value >> 8;
    config[offset + 2] = value >> 16;
    config[offset + 3] = value >> 24;
}

static void register_block_set_qword(uint8_t *config, unsigned int offset,
                                     uint64_t value)
{
    register_block_set_long(config, offset, value);
    register_block_set_long(config, offset + 4, value >> 32);
}

static bool register_block_first_error(Intel460GXRegisterBlock *block,
                                       unsigned int first_offset,
                                       unsigned int next_offset,
                                       uint32_t status)
{
    uint32_t first = register_block_get_long(block->config, first_offset);
    uint32_t next = register_block_get_long(block->config, next_offset);

    if (!first) {
        register_block_set_long(block->config, first_offset, status);
        return true;
    }
    register_block_set_long(block->config, next_offset, next | status);
    return false;
}

static void register_block_mark_sticky(Intel460GXRegisterBlock *block,
                                       unsigned int offset,
                                       unsigned int size)
{
    memset(block->sticky + offset, 0xff, size);
}

static void register_block_mark_writable(Intel460GXRegisterBlock *block,
                                         unsigned int offset,
                                         unsigned int size)
{
    memset(block->wmask + offset, 0xff, size);
}

static void register_block_mark_writable_low_bits(
    Intel460GXRegisterBlock *block, unsigned int offset, unsigned int bits)
{
    unsigned int bytes = bits / 8;

    memset(block->wmask + offset, 0xff, bytes);
    if (bits % 8) {
        block->wmask[offset + bytes] = MAKE_64BIT_MASK(0, bits % 8);
    }
}

static void register_block_init(Intel460GXRegisterBlock *block,
                                uint16_t device_id, uint16_t class_id,
                                uint8_t revision)
{
    memset(block, 0, sizeof(*block));
    register_block_set_word(block->reset, PCI_VENDOR_ID,
                            INTEL_460GX_VENDOR_ID);
    register_block_set_word(block->reset, PCI_DEVICE_ID, device_id);
    register_block_set_word(block->reset, PCI_STATUS,
                            PCI_STATUS_DEVSEL_MEDIUM);
    register_block_set_long(block->reset, PCI_REVISION_ID,
                            (uint32_t)class_id << 16 | revision);
    register_block_set_word(block->reset, PCI_SUBSYSTEM_VENDOR_ID,
                            INTEL_460GX_VENDOR_ID);
    register_block_set_word(block->reset, PCI_SUBSYSTEM_ID, device_id);
    memcpy(block->config, block->reset, sizeof(block->config));
}

static void intel_460gx_chipset_init_registers(
    Intel460GXChipsetState *s)
{
    unsigned i;
    unsigned function;

    for (i = 0; i < 2; i++) {
        for (function = 0; function < 8; function++) {
            register_block_init(&s->sac[i][function], INTEL_460GX_SAC_ID,
                                PCI_CLASS_BRIDGE_HOST,
                                INTEL_460GX_SAC_REVISION);
            s->sac[i][function].reset[PCI_HEADER_TYPE] =
                PCI_HEADER_TYPE_MULTI_FUNCTION;
        }
    }
    s->sac[0][0].chipset = s;
    s->sac[0][0].decoded_role = INTEL_460GX_DECODED_SAC;
    s->sac[0][0].wmask[INTEL_460GX_SAC_CBN_OFFSET] = UINT8_MAX;
    register_block_set_long(
        s->sac[0][0].wmask, INTEL_460GX_SAC_DEVNPRES_OFFSET,
        intel_460gx_chipset_device_mask());
    s->sac[0][0].reset[INTEL_460GX_SAC_CBN_OFFSET] =
        intel_460gx_host_get_cbn(s->host);
    register_block_set_long(
        s->sac[0][0].reset, INTEL_460GX_SAC_DEVNPRES_OFFSET,
        ~intel_460gx_chipset_present_mask(s->expander_mask) &
        intel_460gx_chipset_device_mask());
    s->sac[0][0].wmask[0x80] = BIT(7);
    s->sac[0][0].wmask[0x81] = BIT(7);
    s->sac[0][0].wmask[0x82] = BIT(7);
    s->sac[0][0].w1cmask[0x80] = BIT(6);
    s->sac[0][0].w1cmask[0x81] = BIT(6);
    s->sac[0][0].w1cmask[0x82] = BIT(6);
    memset(s->sac[0][0].sticky + 0x80, 0xff, 3);
    register_block_set_long(s->sac[0][1].w1cmask, 0x40,
                            UINT32_C(0xffff7fe1));
    register_block_set_long(s->sac[0][1].w1cmask, 0x44,
                            UINT32_C(0xffff7fe1));
    memset(s->sac[0][1].sticky + 0x40, 0xff, 8);
    register_block_mark_sticky(&s->sac[0][1], 0x60, 0x10);
    s->sac[0][1].wmask[0x80] = 0x3f;
    for (i = 0x90; i < 0xc0; i += 8) {
        register_block_mark_writable_low_bits(&s->sac[0][2], i, 40);
    }
    for (i = 0xd0; i < 0x100; i += 8) {
        register_block_mark_writable_low_bits(&s->sac[0][2], i, 41);
    }

    register_block_init(&s->sdc, INTEL_460GX_SDC_ID,
                        PCI_CLASS_BRIDGE_HOST,
                        INTEL_460GX_SDC_REVISION);
    memset(s->sdc.w1cmask + 0x80, 0xff, 8);
    s->sdc.coupled_w1c_a = 0x80;
    s->sdc.coupled_w1c_b = 0x84;
    s->sdc.coupled_w1c_size = 4;
    memset(s->sdc.wmask + 0xc8, 0xff, 4);
    register_block_mark_sticky(&s->sdc, 0x40, 0x4b);
    register_block_mark_sticky(&s->sdc, 0x8c, 3);
    register_block_mark_sticky(&s->sdc, 0xd0, 0x2b);
    register_block_set_long(s->sdc.wmask, 0x98, UINT32_C(0x0001ff7f));
    register_block_set_long(s->sdc.wmask, 0x9c, UINT32_C(0x0001ff7f));
    register_block_mark_writable_low_bits(&s->sdc, 0xa0, 40);
    register_block_mark_writable_low_bits(&s->sdc, 0xa8, 40);

    for (i = 0; i < 2; i++) {
        for (function = 0; function < 2; function++) {
            Intel460GXRegisterBlock *block =
                &s->memory_card[i][function];

            register_block_init(block, INTEL_460GX_MAC_ID,
                                PCI_CLASS_MEMORY_RAM,
                                INTEL_460GX_MAC_REVISION);
            block->reset[PCI_HEADER_TYPE] = PCI_HEADER_TYPE_MULTI_FUNCTION;
            register_block_mark_sticky(block, INTEL_460GX_MAC_FERR, 1);
            register_block_mark_sticky(block,
                                       INTEL_460GX_MAC_CMND_FERR, 3);
        }
    }

    for (i = 0; i < INTEL_460GX_DOWNSTREAM_PORTS; i++) {
        Intel460GXRegisterBlock *block = &s->expander[i];
        uint16_t device_id = INTEL_460GX_PXB_ID;
        uint8_t revision = INTEL_460GX_PXB_REVISION;

        register_block_init(&s->downstream_sac[i], INTEL_460GX_SAC_ID,
                            PCI_CLASS_BRIDGE_HOST,
                            INTEL_460GX_SAC_REVISION);
        s->downstream_sac[i].reset[PCI_HEADER_TYPE] =
            PCI_HEADER_TYPE_MULTI_FUNCTION;

        if (i == INTEL_460GX_WXB0_PORT || i == INTEL_460GX_WXB1_PORT) {
            device_id = INTEL_460GX_WXB_ID;
            revision = INTEL_460GX_WXB_REVISION;
        } else if (i == INTEL_460GX_GXB_PORT) {
            device_id = INTEL_460GX_GXB_F1_ID;
            revision = INTEL_460GX_GXB_REVISION;
        }
        register_block_init(block, device_id, PCI_CLASS_BRIDGE_HOST,
                            revision);
        block->chipset = s;
        block->decoded_role = INTEL_460GX_DECODED_XXB;
        block->decoded_port = i;
        block->wmask[INTEL_460GX_XXB_BUSNO_OFFSET] = UINT8_MAX;
        block->wmask[INTEL_460GX_XXB_SUBNO_OFFSET] = UINT8_MAX;
        if (i == INTEL_460GX_WXB0_PORT ||
            i == INTEL_460GX_WXB1_PORT) {
            block->w1cmask[0x44] = 0xeb;
            block->sticky[0x44] = 0xeb;
            register_block_set_word(block->reset, 0x45, 0x8040);
            block->wmask[0x46] = 0xbc;
            block->w1cmask[0x83] = 0xfb;
            block->w1cmask[0x87] = 0xfb;
            register_block_mark_sticky(block, 0x83, 1);
            register_block_mark_sticky(block, 0x87, 1);
            register_block_mark_sticky(block, 0xa5, 0x0f);
        } else if (i == INTEL_460GX_GXB_PORT) {
            block->w1cmask[0x44] = 0x7b;
            block->sticky[0x44] = 0x7b;
            block->wmask[0x46] = 0x7d;
            block->w1cmask[0x80] = 0x07;
            memset(block->w1cmask + 0x84, 0xff, 3);
            memset(block->w1cmask + 0x8c, 0xff, 3);
            register_block_mark_sticky(block, 0x80, 7);
            register_block_mark_sticky(block, 0x8c, 3);
            register_block_mark_sticky(block, 0xa0, 0x10);
        } else {
            block->w1cmask[0x44] = 0x7b;
            block->sticky[0x44] = 0x7b;
            block->wmask[0x46] = 0x7d;
        }
        register_block_mark_writable(block, 0xd8, 4);
        register_block_mark_writable(block, 0xe0, 4);
        block->wmask[0xdd] = 0xff;
        block->wmask[0xe5] = 0xff;
        register_block_mark_writable(block, 0xe8, 4);
    }
    s->expander[INTEL_460GX_GXB_PORT].reset[PCI_HEADER_TYPE] =
        PCI_HEADER_TYPE_MULTI_FUNCTION;
    register_block_init(&s->gxb_function2, INTEL_460GX_GXB_F2_ID,
                        PCI_CLASS_BRIDGE_OTHER,
                        INTEL_460GX_GXB_REVISION);
    s->gxb_function2.reset[PCI_HEADER_TYPE] =
        PCI_HEADER_TYPE_MULTI_FUNCTION;

    for (i = 0; i < 2; i++) {
        for (function = 0; function < 8; function++) {
            memcpy(s->sac[i][function].config,
                   s->sac[i][function].reset,
                   sizeof(s->sac[i][function].config));
        }
    }
    memcpy(s->sdc.config, s->sdc.reset, sizeof(s->sdc.config));
    for (i = 0; i < 2; i++) {
        for (function = 0; function < 2; function++) {
            memcpy(s->memory_card[i][function].config,
                   s->memory_card[i][function].reset,
                   sizeof(s->memory_card[i][function].config));
        }
    }
    for (i = 0; i < INTEL_460GX_DOWNSTREAM_PORTS; i++) {
        memcpy(s->downstream_sac[i].config,
               s->downstream_sac[i].reset,
               sizeof(s->downstream_sac[i].config));
        memcpy(s->expander[i].config, s->expander[i].reset,
               sizeof(s->expander[i].config));
    }
    memcpy(s->gxb_function2.config, s->gxb_function2.reset,
           sizeof(s->gxb_function2.config));
}

uint32_t intel_460gx_chipset_present_mask(uint8_t expander_mask)
{
    return INTEL_460GX_CHIPSET_FIXED_PRESENT_MASK |
           (uint32_t)expander_mask <<
           INTEL_460GX_CHIPSET_EXPANDER_DEVICE_BASE;
}

void intel_460gx_chipset_set_downstream_reset_range(
    Intel460GXChipsetState *s, unsigned int port,
    uint8_t first_bus, uint8_t last_bus)
{
    Intel460GXRegisterBlock *block;

    g_return_if_fail(port < INTEL_460GX_DOWNSTREAM_PORTS);
    g_return_if_fail(first_bus <= last_bus);
    block = &s->expander[port];
    block->reset[INTEL_460GX_XXB_BUSNO_OFFSET] = first_bus;
    block->reset[INTEL_460GX_XXB_SUBNO_OFFSET] = last_bus;
    block->config[INTEL_460GX_XXB_BUSNO_OFFSET] = first_bus;
    block->config[INTEL_460GX_XXB_SUBNO_OFFSET] = last_bus;
}

static void intel_460gx_record_sac_error(Intel460GXChipsetState *s,
                                         uint32_t status,
                                         uint64_t address)
{
    Intel460GXRegisterBlock *block = &s->sac[0][1];
    uint32_t first = register_block_get_long(block->config,
                                              INTEL_460GX_SAC_FERR);
    uint32_t next = register_block_get_long(block->config,
                                             INTEL_460GX_SAC_NERR);
    uint32_t old_first = first;
    bool record_address = false;

    if (status == INTEL_460GX_SAC_SCME) {
        if (!(first & INTEL_460GX_SAC_SCME)) {
            first |= INTEL_460GX_SAC_SCME;
            record_address = old_first == 0;
        } else if (!(first & INTEL_460GX_SAC_SNE)) {
            first |= INTEL_460GX_SAC_SNE;
        } else {
            next |= INTEL_460GX_SAC_SNE;
        }
    } else if (!(first & ~INTEL_460GX_SAC_SCME)) {
        first |= status;
        record_address = true;
    } else {
        next |= status;
    }

    if (first != old_first) {
        register_block_set_long(block->config, INTEL_460GX_SAC_FERR,
                                first);
    }
    if (record_address) {
        uint64_t encoded = (address >> 3) & MAKE_64BIT_MASK(0, 33);

        register_block_set_qword(block->config, INTEL_460GX_SAC_SA_FERR,
                                 encoded);
        register_block_set_qword(block->config,
                                 INTEL_460GX_SAC_SA_FERR + 8, encoded);
    }
    register_block_set_long(block->config, INTEL_460GX_SAC_NERR, next);
}

static void intel_460gx_record_itid(Intel460GXChipsetState *s,
                                    unsigned int offset, uint8_t itid)
{
    uint8_t old = s->sac[0][0].config[offset];

    if (!(old & BIT(7)) && !(old & BIT(6))) {
        s->sac[0][0].config[offset] = BIT(6) | (itid & 0x3f);
    }
}

static void intel_460gx_notify_fault(Intel460GXChipsetState *s,
                                     IA64ChipsetFaultReason reason,
                                     uint8_t severity, uint64_t address,
                                     uint64_t status, uint64_t information)
{
    IA64ChipsetFault fault;

    if (!s->fault_notify) {
        return;
    }
    fault = (IA64ChipsetFault) {
        .source = IA64_CHIPSET_FAULT_460GX,
        .reason = reason,
        .severity = severity,
        .address = address,
        .status = status,
        .information = information,
    };
    s->fault_notify(s->fault_opaque, &fault);
}

void intel_460gx_chipset_set_fault_notify(
    Intel460GXChipsetState *s, IA64ChipsetFaultNotify notify, void *opaque)
{
    s->fault_notify = notify;
    s->fault_opaque = opaque;
}

void intel_460gx_chipset_report_memory_error(
    Intel460GXChipsetState *s, unsigned int card, unsigned int mac,
    Intel460GXMemoryError error, uint64_t address, uint64_t data,
    uint8_t ecc, uint8_t chunk, uint8_t itid)
{
    Intel460GXRegisterBlock *sdc;
    Intel460GXRegisterBlock *mac_block;
    IA64ChipsetFaultReason reason;
    uint32_t sdc_status = 0;
    uint32_t sac_status;
    unsigned int log_offset = 0;
    uint8_t severity;
    bool first;

    g_return_if_fail(card < 2);
    g_return_if_fail(mac < 2);
    sdc = &s->sdc;
    mac_block = &s->memory_card[card][mac];

    switch (error) {
    case INTEL_460GX_MEMORY_ERROR_CORRECTED:
        sdc_status = card ? BIT(0) : BIT(2);
        sac_status = INTEL_460GX_SAC_SCME;
        log_offset = card ? 0x40 : 0x60;
        reason = IA64_CHIPSET_FAULT_MEMORY_CORRECTED;
        severity = IA64_RAS_SEVERITY_CORRECTED;
        intel_460gx_record_itid(s, INTEL_460GX_SAC_SECTID, itid);
        break;
    case INTEL_460GX_MEMORY_ERROR_UNCORRECTED:
        sdc_status = card ? BIT(1) : BIT(3);
        sac_status = INTEL_460GX_SAC_SNE;
        log_offset = card ? 0x50 : 0x70;
        reason = IA64_CHIPSET_FAULT_MEMORY_UNCORRECTED;
        severity = IA64_RAS_SEVERITY_RECOVERABLE;
        intel_460gx_record_itid(s, INTEL_460GX_SAC_DEDTID, itid);
        break;
    case INTEL_460GX_MEMORY_ERROR_COMMAND_PARITY:
        if (!mac_block->config[INTEL_460GX_MAC_FERR]) {
            mac_block->config[INTEL_460GX_MAC_FERR] =
                INTEL_460GX_MAC_COMMAND_ERROR;
            register_block_set_long(mac_block->config,
                                    INTEL_460GX_MAC_CMND_FERR,
                                    address & MAKE_64BIT_MASK(0, 22));
        }
        sac_status = card ? INTEL_460GX_SAC_MBE :
                            INTEL_460GX_SAC_MAE;
        reason = IA64_CHIPSET_FAULT_PARITY;
        severity = IA64_RAS_SEVERITY_RECOVERABLE;
        break;
    case INTEL_460GX_MEMORY_ERROR_QUEUE_OVERFLOW:
        if (!mac_block->config[INTEL_460GX_MAC_FERR]) {
            mac_block->config[INTEL_460GX_MAC_FERR] =
                INTEL_460GX_MAC_QUEUE_ERROR;
        }
        sac_status = card ? INTEL_460GX_SAC_MBE :
                            INTEL_460GX_SAC_MAE;
        reason = IA64_CHIPSET_FAULT_PROTOCOL;
        severity = IA64_RAS_SEVERITY_RECOVERABLE;
        break;
    default:
        g_assert_not_reached();
    }

    if (sdc_status) {
        first = register_block_first_error(sdc, INTEL_460GX_SDC_FERR,
                                           INTEL_460GX_SDC_NERR,
                                           sdc_status);
        if (first) {
            register_block_set_qword(sdc->config, log_offset, data);
            sdc->config[log_offset + 8] = ecc;
            register_block_set_word(sdc->config, log_offset + 9,
                                    (chunk & 0x7) << 6 | (itid & 0x3f));
        }
    }
    intel_460gx_record_sac_error(s, sac_status, address);
    intel_460gx_notify_fault(s, reason, severity, address,
                             (uint64_t)sac_status << 32 | sdc_status,
                             (uint64_t)card << 40 |
                             (uint64_t)mac << 32 |
                             (uint64_t)ecc << 16 |
                             (uint64_t)(chunk & 0x7) << 6 |
                             (itid & 0x3f));
}

static void intel_460gx_chipset_record_expander_error(
    Intel460GXChipsetState *s, unsigned int port, uint8_t status,
    uint64_t address, uint64_t data)
{
    Intel460GXRegisterBlock *block;
    uint8_t device_status;
    bool first = false;

    g_return_if_fail(port < INTEL_460GX_DOWNSTREAM_PORTS);
    g_return_if_fail(s->expander_mask & BIT(port));
    block = &s->expander[port];

    if (port == INTEL_460GX_WXB0_PORT ||
        port == INTEL_460GX_WXB1_PORT) {
        device_status = status & 0xfb;
        if (!(block->config[0x83] & 0x7b)) {
            block->config[0x83] |= device_status | BIT(7);
            block->config[0x44] |= BIT(3);
            first = true;
        } else {
            block->config[0x87] |= device_status;
            block->config[0x44] |= BIT(5);
        }
        if (first) {
            register_block_set_qword(block->config, 0xa5, address);
            block->config[0xad] = 0;
            register_block_set_long(block->config, 0xaf, data);
        }
    } else if (port == INTEL_460GX_GXB_PORT) {
        device_status = status;
        if (!block->config[0x84]) {
            block->config[0x84] = device_status;
            first = true;
        } else {
            block->config[0x8c] |= device_status;
        }
        block->config[0x80] |= BIT(2);
        if (first) {
            register_block_set_qword(block->config, 0xa0, address);
            register_block_set_qword(block->config, 0xa8, data);
        }
    } else {
        device_status = status & 0x7b;
        block->config[0x44] |= device_status;
    }

    intel_460gx_record_sac_error(s, BIT(29), address);
}

bool intel_460gx_chipset_report_expander_fault(
    Intel460GXChipsetState *s, unsigned int port,
    const IA64ChipsetFault *fault)
{
    IA64ChipsetFault routed;

    g_return_val_if_fail(fault != NULL, false);
    intel_460gx_chipset_record_expander_error(
        s, port, fault->status, fault->address, fault->information);
    if (!s->fault_notify) {
        return false;
    }
    routed = *fault;
    routed.source = IA64_CHIPSET_FAULT_460GX;
    return s->fault_notify(s->fault_opaque, &routed);
}

static bool register_target(Intel460GXChipsetState *s, unsigned device,
                            unsigned function,
                            Intel460GXRegisterBlock *block, Error **errp)
{
    return intel_460gx_host_register_chipset_target(
        s->host, device, function, &register_block_ops, block, errp);
}

static bool intel_460gx_qtest_command(CharFrontend *chr, gchar **words)
{
    Intel460GXChipsetState *chipset;
    uint64_t args[8];
    bool ambiguous;
    unsigned int i;
    int ret = 0;

    if (strcmp(words[0], "ia64-460gx-memory-error") != 0) {
        return false;
    }
    if (!words[1] || !words[2] || !words[3] || !words[4] || !words[5] ||
        !words[6] || !words[7] || !words[8] || words[9]) {
        qtest_sendf(chr,
                    "FAIL expected CARD MAC ERROR ADDR DATA ECC CHUNK ITID\n");
        return true;
    }
    for (i = 0; i < ARRAY_SIZE(args); i++) {
        ret |= qemu_strtou64(words[i + 1], NULL, 0, &args[i]);
    }
    if (ret || args[0] >= 2 || args[1] >= 2 ||
        args[2] > INTEL_460GX_MEMORY_ERROR_QUEUE_OVERFLOW ||
        args[5] > UINT8_MAX || args[6] > 7 || args[7] > 0x3f) {
        qtest_sendf(chr, "FAIL invalid 460GX memory error\n");
        return true;
    }
    chipset = INTEL_460GX_CHIPSET(object_resolve_path_type(
        "", TYPE_INTEL_460GX_CHIPSET, &ambiguous));
    if (!chipset || ambiguous) {
        qtest_sendf(chr, "FAIL requires one Intel 460GX chipset\n");
        return true;
    }
    intel_460gx_chipset_report_memory_error(
        chipset, args[0], args[1], args[2], args[3], args[4],
        args[5], args[6], args[7]);
    qtest_sendf(chr, "OK\n");
    return true;
}

static void intel_460gx_chipset_realize(DeviceState *dev, Error **errp)
{
    Intel460GXChipsetState *s = INTEL_460GX_CHIPSET(dev);
    uint32_t present;
    unsigned i;
    unsigned function;

    if (!s->host) {
        error_setg(errp, "%s requires the '%s' link",
                   TYPE_INTEL_460GX_CHIPSET,
                   INTEL_460GX_CHIPSET_PROP_HOST);
        return;
    }

    intel_460gx_chipset_init_registers(s);
    present = intel_460gx_chipset_present_mask(s->expander_mask);
    if (!intel_460gx_host_configure_chipset_present(s->host, present, errp)) {
        return;
    }

    if (!intel_460gx_host_register_bootstrap_sac(
            s->host, 0, &register_block_ops, &s->sac[0][0], errp)) {
        return;
    }
    for (i = 0; i < 2; i++) {
        for (function = 0; function < 8; function++) {
            if (!(sac_defined_function_mask[i] & BIT(function))) {
                continue;
            }
            if (!register_target(s, INTEL_460GX_CHIPSET_SAC_DEVICE + i,
                                 function, &s->sac[i][function], errp)) {
                return;
            }
        }
    }
    if (!register_target(s, INTEL_460GX_CHIPSET_SDC_DEVICE, 0,
                         &s->sdc, errp)) {
        return;
    }
    for (i = 0; i < 2; i++) {
        for (function = 0; function < 2; function++) {
            if (!register_target(
                    s, INTEL_460GX_CHIPSET_MEMORY_CARD_A_DEVICE + i,
                    function, &s->memory_card[i][function], errp)) {
                return;
            }
        }
    }
    for (i = 0; i < INTEL_460GX_DOWNSTREAM_PORTS; i++) {
        if (!register_target(s,
                             INTEL_460GX_CHIPSET_EXPANDER_DEVICE_BASE + i,
                             0, &s->downstream_sac[i], errp)) {
            return;
        }
    }
    for (i = 0; i < INTEL_460GX_DOWNSTREAM_PORTS; i++) {
        if ((s->expander_mask & BIT(i)) &&
            !register_target(s,
                             INTEL_460GX_CHIPSET_EXPANDER_DEVICE_BASE + i,
                             1, &s->expander[i], errp)) {
            return;
        }
    }
    if ((s->expander_mask & BIT(INTEL_460GX_GXB_PORT)) &&
        !register_target(s,
                         INTEL_460GX_CHIPSET_EXPANDER_DEVICE_BASE +
                         INTEL_460GX_GXB_PORT,
                         2, &s->gxb_function2, errp)) {
        return;
    }
    if (qtest_driver()) {
        qtest_add_command_cb(intel_460gx_qtest_command);
    }
}

static void register_block_reset(Intel460GXRegisterBlock *block)
{
    unsigned i;

    for (i = 0; i < INTEL_460GX_CONFIG_SIZE; i++) {
        block->config[i] = (block->config[i] & block->sticky[i]) |
                           (block->reset[i] & ~block->sticky[i]);
    }
}

static void intel_460gx_chipset_reset(DeviceState *dev)
{
    Intel460GXChipsetState *s = INTEL_460GX_CHIPSET(dev);
    unsigned i;
    unsigned function;

    for (i = 0; i < 2; i++) {
        for (function = 0; function < 8; function++) {
            register_block_reset(&s->sac[i][function]);
        }
    }
    register_block_reset(&s->sdc);
    for (i = 0; i < 2; i++) {
        for (function = 0; function < 2; function++) {
            register_block_reset(&s->memory_card[i][function]);
        }
    }
    for (i = 0; i < INTEL_460GX_DOWNSTREAM_PORTS; i++) {
        register_block_reset(&s->downstream_sac[i]);
        register_block_reset(&s->expander[i]);
    }
    register_block_reset(&s->gxb_function2);
}

static bool register_block_post_load(void *opaque, int version_id,
                                     Error **errp)
{
    Intel460GXRegisterBlock *block = opaque;
    unsigned i;

    (void)version_id;
    for (i = 0; i < INTEL_460GX_CONFIG_SIZE; i++) {
        uint8_t mutable = block->wmask[i] | block->w1cmask[i] |
                          block->sticky[i];

        if ((block->config[i] ^ block->reset[i]) & ~mutable) {
            error_setg(errp,
                       "460GX configuration target has immutable bits set");
            return false;
        }
    }
    return true;
}

static const VMStateDescription vmstate_intel_460gx_register_block = {
    .name = TYPE_INTEL_460GX_CHIPSET "/register-block",
    .version_id = 1,
    .minimum_version_id = 1,
    .post_load_errp = register_block_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8_ARRAY(config, Intel460GXRegisterBlock,
                            INTEL_460GX_CONFIG_SIZE),
        VMSTATE_END_OF_LIST()
    },
};

static bool intel_460gx_chipset_post_load(void *opaque, int version_id,
                                           Error **errp)
{
    Intel460GXChipsetState *s = opaque;
    Intel460GXDecodedStateUpdate update = { 0 };
    unsigned int port;

    if (version_id < 4) {
        return true;
    }
    update.has_cbn = true;
    update.cbn = s->sac[0][0].config[INTEL_460GX_SAC_CBN_OFFSET];
    update.has_chipset_present = true;
    update.chipset_present =
        ~register_block_get_long(s->sac[0][0].config,
                                 INTEL_460GX_SAC_DEVNPRES_OFFSET) &
        intel_460gx_chipset_device_mask();
    update.route_mask = s->expander_mask;
    for (port = 0; port < INTEL_460GX_DOWNSTREAM_PORTS; port++) {
        update.routes[port].first_bus =
            s->expander[port].config[INTEL_460GX_XXB_BUSNO_OFFSET];
        update.routes[port].last_bus =
            s->expander[port].config[INTEL_460GX_XXB_SUBNO_OFFSET];
    }
    return intel_460gx_host_apply_decoded_update(s->host, &update, errp);
}

static const VMStateDescription vmstate_intel_460gx_chipset = {
    .name = TYPE_INTEL_460GX_CHIPSET,
    .version_id = 4,
    .minimum_version_id = 2,
    .post_load_errp = intel_460gx_chipset_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8_EQUAL(expander_mask, Intel460GXChipsetState),
        VMSTATE_STRUCT_2DARRAY(sac, Intel460GXChipsetState, 2, 8, 1,
                               vmstate_intel_460gx_register_block,
                               Intel460GXRegisterBlock),
        VMSTATE_STRUCT(sdc, Intel460GXChipsetState, 1,
                       vmstate_intel_460gx_register_block,
                       Intel460GXRegisterBlock),
        VMSTATE_STRUCT_2DARRAY(memory_card, Intel460GXChipsetState,
                               2, 2, 3,
                               vmstate_intel_460gx_register_block,
                               Intel460GXRegisterBlock),
        VMSTATE_STRUCT_ARRAY(downstream_sac, Intel460GXChipsetState,
                             INTEL_460GX_DOWNSTREAM_PORTS, 1,
                             vmstate_intel_460gx_register_block,
                             Intel460GXRegisterBlock),
        VMSTATE_STRUCT_ARRAY(expander, Intel460GXChipsetState,
                             INTEL_460GX_DOWNSTREAM_PORTS, 1,
                             vmstate_intel_460gx_register_block,
                             Intel460GXRegisterBlock),
        VMSTATE_STRUCT(gxb_function2, Intel460GXChipsetState, 1,
                       vmstate_intel_460gx_register_block,
                       Intel460GXRegisterBlock),
        VMSTATE_END_OF_LIST()
    },
};

static const Property intel_460gx_chipset_properties[] = {
    DEFINE_PROP_LINK(INTEL_460GX_CHIPSET_PROP_HOST,
                     Intel460GXChipsetState, host,
                     TYPE_INTEL_460GX_HOST, Intel460GXHostState *),
    DEFINE_PROP_UINT8(INTEL_460GX_CHIPSET_PROP_EXPANDER_MASK,
                      Intel460GXChipsetState, expander_mask, 0),
};

static void intel_460gx_chipset_class_init(ObjectClass *klass,
                                            const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    dc->desc = "Intel 460GX chipset configuration targets";
    dc->realize = intel_460gx_chipset_realize;
    dc->user_creatable = false;
    dc->hotpluggable = false;
    dc->vmsd = &vmstate_intel_460gx_chipset;
    device_class_set_legacy_reset(dc, intel_460gx_chipset_reset);
    device_class_set_props(dc, intel_460gx_chipset_properties);
}

static const TypeInfo intel_460gx_chipset_type_info = {
    .name = TYPE_INTEL_460GX_CHIPSET,
    .parent = TYPE_DEVICE,
    .instance_size = sizeof(Intel460GXChipsetState),
    .class_init = intel_460gx_chipset_class_init,
};

static void intel_460gx_chipset_register_types(void)
{
    type_register_static(&intel_460gx_chipset_type_info);
}
type_init(intel_460gx_chipset_register_types)
