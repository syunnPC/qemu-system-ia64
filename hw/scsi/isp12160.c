/*
 * QLogic ISP12160 mailbox, queue, and SCSI model
 *
 * Models selected mailbox and queue registers, IOCBs, and a SCSI bus.  The
 * onboard RISC processor and firmware execution are not implemented; firmware
 * uploads are represented by their address range and additive checksum.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#include "qemu/osdep.h"

#include "hw/pci/pci.h"
#include "hw/pci/pci_device.h"
#include "hw/scsi/isp12160.h"
#include "hw/scsi/isp12160_iocb.h"
#include "hw/scsi/scsi.h"
#include "migration/qemu-file.h"
#include "migration/vmstate.h"
#include "qemu/bswap.h"
#include "qemu/error-report.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "qemu/timer.h"
#include "trace.h"

#define ISP12160_MAILBOX_BYTES          (ISP12160_MAILBOX_COUNT * 2)
#define ISP12160_RISC_WORDS             0x10000u
#define ISP12160_SCSI_MAX_OUTSTANDING     256U
#define ISP12160_SCSI_MAX_QUEUES          (32U * ISP12160_SCSI_MAX_LUNS)
#define ISP12160_SCSI_MAX_CHAIN_ENTRIES   UINT8_MAX
#define ISP12160_SCSI_MAX_SEGMENTS        \
    (ISP12160_IOCB_COMMAND_SEGMENTS + \
     ISP12160_IOCB_CONTINUE_SEGMENTS * \
     (ISP12160_SCSI_MAX_CHAIN_ENTRIES - 1U))
#define ISP12160_SCSI_BH_BUDGET           64U
#define ISP12160_SCSI_REQUEST_MAGIC        UINT32_C(0x49533252)
#define ISP12160_SCSI_REQUEST_VERSION      2U

#define ISP12160_HC_RESET_RELEASE_DISABLE \
    (ISP12160_HC_RESET_RISC | ISP12160_HC_RELEASE_RISC | \
     ISP12160_HC_DISABLE_BIOS)

typedef struct ISP12160QueueState {
    uint64_t base;
    uint16_t count;
    uint16_t producer;
    uint16_t consumer;
    bool valid;
    bool a64;
} ISP12160QueueState;

typedef struct ISP12160SCSIRequest {
    ISP12160State *controller;
    SCSIRequest *sreq;
    QTAILQ_ENTRY(ISP12160SCSIRequest) next;
    ISP12160IOCBCommand command;
    ISP12160IOCBSegment *segments;
    uint64_t transferred;
    uint16_t segment_index;
    uint32_t segment_offset;
    bool dma_failed;
    bool migration_invalid;
    bool timed_out;
    int64_t deadline;
} ISP12160SCSIRequest;

typedef QTAILQ_HEAD(, ISP12160SCSIRequest) ISP12160SCSIRequestList;

struct ISP12160State {
    PCIDevice parent_obj;

    MemoryRegion registers;
    MemoryRegion io_bar;
    MemoryRegion mmio_bar;
    QEMUBH *mailbox_bh;
    QEMUBH *queue_bh;
    QEMUTimer *request_timer;
    uint16_t initiator_id[2];
    uint16_t target_params[32];
    uint16_t target_sync[32];
    uint16_t target_ppr[32];
    uint16_t queue_depth[ISP12160_SCSI_MAX_QUEUES];
    uint16_t queue_throttle[ISP12160_SCSI_MAX_QUEUES];
    SCSIBus scsi_bus;
    ISP12160SCSIRequestList active_requests;

    uint16_t variant;
    uint16_t cfg1;
    uint16_t ictrl;
    uint16_t istatus;
    uint16_t semaphore;
    uint16_t host_command;
    uint16_t mailbox[ISP12160_MAILBOX_COUNT];
    uint16_t pending_mailbox[ISP12160_MAILBOX_COUNT];

    uint16_t token_address;
    uint16_t token_ram[ISP12160_QEMU_TOKEN_WORDS];
    uint16_t native_firmware_start;
    uint16_t native_firmware_checksum;
    uint32_t native_firmware_words;
    bool token_loaded;
    bool native_firmware_loaded;
    bool token_verified;
    bool risc_running;
    bool risc_paused;
    bool mailbox_pending;
    bool mailbox_staging;
    bool irq_ack_pending;
    bool response_irq_unobserved;

    /* The queue model configures rings without consuming IOCBs. */
    ISP12160QueueState request_queue;
    ISP12160QueueState response_queue;

    uint8_t pending_status[ISP12160_SCSI_MAX_OUTSTANDING]
                          [ISP12160_IOCB_ENTRY_BYTES];
    uint16_t pending_status_head;
    uint16_t pending_status_count;
    uint16_t active_request_count;
    bool dma_stalled;
    bool resetting;
};

static void isp12160_config_defaults(ISP12160State *s)
{
    s->initiator_id[0] = s->initiator_id[1] = 7;
    for (unsigned i = 0; i < 32; i++) {
        s->target_params[i] = 0xfc00;
        s->target_sync[i] = s->target_ppr[i] = 0;
    }
    for (unsigned i = 0; i < ARRAY_SIZE(s->queue_depth); i++) {
        s->queue_depth[i] = s->queue_throttle[i] =
            ISP12160_SCSI_MAX_OUTSTANDING;
    }
}

static bool isp12160_has_queues(const ISP12160State *s)
{
    return s->variant == ISP12160_VARIANT_QUEUE ||
           s->variant == ISP12160_VARIANT_SCSI;
}

static bool isp12160_has_scsi(const ISP12160State *s)
{
    return s->variant == ISP12160_VARIANT_SCSI;
}

static void isp12160_scsi_schedule_queue(ISP12160State *s);
static void isp12160_scsi_reset_transport(ISP12160State *s);
static bool isp12160_scsi_bytes_zero(const uint8_t *bytes, size_t length);

static bool isp12160_expected_token(ISP12160State *s,
                                    uint8_t token[ISP12160_QEMU_TOKEN_WORDS *
                                                  sizeof(uint16_t)])
{
    uint8_t activation[ISP12160_QEMU_ACTIVATION_BYTES];

    if (!isp12160_qemu_activation_generate(s->variant, activation,
                                        sizeof(activation))) {
        return false;
    }
    memcpy(token, activation + ISP12160_QEMU_ACTIVATION_HEADER_BYTES,
           ISP12160_QEMU_TOKEN_WORDS * sizeof(uint16_t));
    return true;
}

static bool isp12160_token_ram_valid(ISP12160State *s)
{
    uint8_t expected[ISP12160_QEMU_TOKEN_WORDS * sizeof(uint16_t)];
    unsigned int i;

    if (!isp12160_expected_token(s, expected)) {
        return false;
    }
    for (i = 0; i < ISP12160_QEMU_TOKEN_WORDS; i++) {
        if (s->token_ram[i] != lduw_le_p(expected + i * 2)) {
            return false;
        }
    }
    return true;
}

static void isp12160_update_irq(ISP12160State *s)
{
    const uint16_t enabled = ISP12160_ICTRL_ENABLE_INT |
                             ISP12160_ICTRL_ENABLE_RISC;
    bool level = (s->ictrl & enabled) == enabled &&
                 (s->istatus & ISP12160_ISTATUS_RISC_INT);

    pci_set_irq(PCI_DEVICE(s), level);
}

static void isp12160_reset_queues(ISP12160State *s)
{
    memset(&s->request_queue, 0, sizeof(s->request_queue));
    memset(&s->response_queue, 0, sizeof(s->response_queue));
    s->response_irq_unobserved = false;
}

static void isp12160_invalidate_token(ISP12160State *s)
{
    isp12160_scsi_reset_transport(s);
    s->token_address = 0;
    memset(s->token_ram, 0, sizeof(s->token_ram));
    s->native_firmware_start = 0;
    s->native_firmware_checksum = 0;
    s->native_firmware_words = 0;
    s->token_loaded = false;
    s->native_firmware_loaded = false;
    s->token_verified = false;
    s->risc_running = false;
    s->risc_paused = false;
    isp12160_reset_queues(s);
}

static void isp12160_reset_mailboxes(ISP12160State *s)
{
    memset(s->mailbox, 0, sizeof(s->mailbox));
    memset(s->pending_mailbox, 0, sizeof(s->pending_mailbox));

    /* Mailbox zero reports reset readiness; the remaining words are IDs. */
    s->mailbox[0] = ISP12160_MBS_FIRMWARE_ALIVE;
    s->mailbox[1] = ISP12160_PRODUCT_ID_1;
    s->mailbox[2] = ISP12160_PRODUCT_ID_2A;
    s->mailbox[3] = ISP12160_PRODUCT_ID_3;
    s->mailbox[4] = ISP12160_PRODUCT_ID_4;
}

static void isp12160_reset_risc(ISP12160State *s)
{
    if (s->mailbox_bh) {
        qemu_bh_cancel(s->mailbox_bh);
    }
    s->mailbox_pending = false;
    s->mailbox_staging = false;
    s->irq_ack_pending = false;
    isp12160_reset_mailboxes(s);
    s->semaphore = 0;
    s->istatus &= ~(ISP12160_ISTATUS_PCI_INT |
                    ISP12160_ISTATUS_RISC_INT);
    isp12160_invalidate_token(s);
    isp12160_update_irq(s);
}

static void isp12160_reset_state(ISP12160State *s)
{
    if (s->mailbox_bh) {
        qemu_bh_cancel(s->mailbox_bh);
    }

    isp12160_config_defaults(s);
    s->cfg1 = 0;
    s->ictrl = 0;
    s->istatus = 0;
    s->semaphore = 0;
    s->host_command = 0;
    s->mailbox_pending = false;
    s->mailbox_staging = false;
    s->irq_ack_pending = false;
    isp12160_reset_mailboxes(s);
    isp12160_invalidate_token(s);

    isp12160_update_irq(s);
}

static uint64_t isp12160_mailbox_dma_address(const uint16_t *mb, bool a64)
{
    uint64_t address = ((uint64_t)mb[2] << 16) | mb[3];

    if (a64) {
        address |= (uint64_t)mb[7] << 32;
        address |= (uint64_t)mb[6] << 48;
    }
    return address;
}

static uint16_t isp12160_load_qemu_token(ISP12160State *s,
                                         const uint16_t *mb, bool a64)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    uint8_t expected[ISP12160_QEMU_TOKEN_WORDS * sizeof(uint16_t)];
    uint8_t buffer[ISP12160_QEMU_TOKEN_WORDS * sizeof(uint16_t)];
    uint64_t dma_address;
    uint32_t risc_end;
    unsigned int i;

    /* A failed replacement must not leave an older token executable. */
    isp12160_invalidate_token(s);

    if (mb[4] != ISP12160_QEMU_TOKEN_WORDS) {
        return ISP12160_MBS_COMMAND_PARAM_ERR;
    }

    risc_end = (uint32_t)mb[1] + mb[4];
    if (risc_end > ISP12160_RISC_WORDS) {
        return ISP12160_MBS_COMMAND_PARAM_ERR;
    }

    dma_address = isp12160_mailbox_dma_address(mb, a64);
    if (dma_address > UINT64_MAX - (sizeof(buffer) - 1)) {
        return ISP12160_MBS_COMMAND_PARAM_ERR;
    }

    /* PCI bus mastering enables DMA. */
    if (!(pdev->config[PCI_COMMAND] & PCI_COMMAND_MASTER)) {
        return ISP12160_MBS_HOST_INTERFACE_ERR;
    }

    if (pci_dma_read(pdev, dma_address, buffer, sizeof(buffer)) != MEMTX_OK) {
        return ISP12160_MBS_HOST_INTERFACE_ERR;
    }

    if (!isp12160_expected_token(s, expected) ||
        memcmp(buffer, expected, sizeof(buffer))) {
        return ISP12160_MBS_TEST_FAILED;
    }

    s->token_address = mb[1];
    for (i = 0; i < ISP12160_QEMU_TOKEN_WORDS; i++) {
        s->token_ram[i] = lduw_le_p(buffer + i * sizeof(uint16_t));
    }
    s->token_loaded = true;
    return ISP12160_MBS_COMMAND_COMPLETE;
}

static uint16_t isp12160_load_native_firmware(ISP12160State *s,
                                               const uint16_t *mb, bool a64)
{
    PCIDevice *pdev = PCI_DEVICE(s);
    g_autofree uint8_t *buffer = NULL;
    uint64_t dma_address;
    uint32_t risc_end;
    uint32_t expected_address;
    size_t bytes;
    unsigned int i;

    if (!mb[4]) {
        return ISP12160_MBS_COMMAND_PARAM_ERR;
    }
    risc_end = (uint32_t)mb[1] + mb[4];
    if (risc_end > ISP12160_RISC_WORDS) {
        return ISP12160_MBS_COMMAND_PARAM_ERR;
    }
    expected_address = s->native_firmware_loaded ?
        (uint32_t)s->native_firmware_start + s->native_firmware_words :
        mb[1];
    if (mb[1] != expected_address) {
        isp12160_invalidate_token(s);
        return ISP12160_MBS_COMMAND_PARAM_ERR;
    }

    bytes = (size_t)mb[4] * sizeof(uint16_t);
    dma_address = isp12160_mailbox_dma_address(mb, a64);
    if (dma_address > UINT64_MAX - (bytes - 1U)) {
        isp12160_invalidate_token(s);
        return ISP12160_MBS_COMMAND_PARAM_ERR;
    }
    if (!(pdev->config[PCI_COMMAND] & PCI_COMMAND_MASTER)) {
        isp12160_invalidate_token(s);
        return ISP12160_MBS_HOST_INTERFACE_ERR;
    }

    buffer = g_malloc(bytes);
    if (pci_dma_read(pdev, dma_address, buffer, bytes) != MEMTX_OK) {
        isp12160_invalidate_token(s);
        return ISP12160_MBS_HOST_INTERFACE_ERR;
    }

    if (!s->native_firmware_loaded) {
        isp12160_invalidate_token(s);
        s->native_firmware_start = mb[1];
        s->native_firmware_loaded = true;
    }
    for (i = 0; i < mb[4]; i++) {
        s->native_firmware_checksum += lduw_le_p(buffer + i * 2U);
    }
    s->native_firmware_words += mb[4];
    return ISP12160_MBS_COMMAND_COMPLETE;
}

static uint16_t isp12160_verify_qemu_token(ISP12160State *s,
                                           const uint16_t *mb)
{
    isp12160_scsi_reset_transport(s);
    s->token_verified = false;
    s->risc_running = false;
    s->risc_paused = false;
    isp12160_reset_queues(s);

    if (s->native_firmware_loaded &&
        mb[1] == s->native_firmware_start &&
        !s->native_firmware_checksum) {
        s->token_verified = true;
        return ISP12160_MBS_COMMAND_COMPLETE;
    }

    if (!s->token_loaded || mb[1] != s->token_address) {
        return ISP12160_MBS_COMMAND_PARAM_ERR;
    }
    if (!isp12160_token_ram_valid(s)) {
        return ISP12160_MBS_TEST_FAILED;
    }

    s->token_verified = true;
    return ISP12160_MBS_COMMAND_COMPLETE;
}

static bool isp12160_queue_span_valid(uint64_t base, uint16_t count, bool a64)
{
    uint64_t bytes = (uint64_t)count * ISP12160_QUEUE_ENTRY_BYTES;

    return !(base & (ISP12160_QUEUE_BASE_ALIGNMENT - 1)) &&
           base <= UINT64_MAX - (bytes - 1) &&
           (a64 || base <= UINT32_MAX - (bytes - 1));
}

static uint16_t isp12160_init_queue(ISP12160State *s, const uint16_t *mb,
                                    bool request, bool a64)
{
    ISP12160QueueState *queue = request ? &s->request_queue :
                                         &s->response_queue;
    PCIDevice *pdev = PCI_DEVICE(s);
    uint16_t index = mb[request ? 4 : 5];
    uint16_t count = mb[1];
    uint64_t base = isp12160_mailbox_dma_address(mb, a64);

    if (!s->risc_running || s->risc_paused || queue->valid) {
        return ISP12160_MBS_COMMAND_ERR;
    }
    if (count < ISP12160_QUEUE_MIN_ENTRIES || index >= count ||
        !isp12160_queue_span_valid(base, count, a64)) {
        return ISP12160_MBS_COMMAND_PARAM_ERR;
    }
    if (!(pdev->config[PCI_COMMAND] & PCI_COMMAND_MASTER)) {
        return ISP12160_MBS_HOST_INTERFACE_ERR;
    }

    queue->base = base;
    queue->count = count;
    queue->producer = index;
    queue->consumer = index;
    queue->valid = true;
    queue->a64 = a64;

    /* Reads expose the device-owned side of each shared index. */
    s->mailbox[request ? 4 : 5] = index;
    return ISP12160_MBS_COMMAND_COMPLETE;
}

static uint16_t isp12160_execute_qemu_token(ISP12160State *s,
                                            const uint16_t *mb)
{
    if (s->native_firmware_loaded &&
        mb[1] == s->native_firmware_start) {
        s->token_verified = true;
        s->risc_running = true;
        s->risc_paused = false;
        return ISP12160_MBS_COMMAND_COMPLETE;
    }
    if (!s->token_loaded || !s->token_verified ||
        mb[1] != s->token_address) {
        return ISP12160_MBS_COMMAND_PARAM_ERR;
    }

    /* Execution activates a validated token without decoding its words. */
    s->risc_running = true;
    s->risc_paused = false;
    return ISP12160_MBS_COMMAND_COMPLETE;
}

static uint16_t isp12160_run_mailbox(ISP12160State *s,
                                     const uint16_t *mb)
{
    switch (mb[0]) {
    case ISP12160_MBC_NOP:
    case ISP12160_MBC_MAILBOX_TEST:
        return ISP12160_MBS_COMMAND_COMPLETE;

    case ISP12160_MBC_LOAD_RAM:
        return mb[4] <= ISP12160_QEMU_TOKEN_WORDS &&
               !s->native_firmware_loaded ?
               isp12160_load_qemu_token(s, mb, false) :
               isp12160_load_native_firmware(s, mb, false);

    case ISP12160_MBC_LOAD_RAM_A64_ROM:
        return mb[4] <= ISP12160_QEMU_TOKEN_WORDS &&
               !s->native_firmware_loaded ?
               isp12160_load_qemu_token(s, mb, true) :
               isp12160_load_native_firmware(s, mb, true);

    case ISP12160_MBC_VERIFY_CHECKSUM:
        return isp12160_verify_qemu_token(s, mb);

    case ISP12160_MBC_EXECUTE_FIRMWARE:
        return isp12160_execute_qemu_token(s, mb);

    case ISP12160_MBC_ABOUT_FIRMWARE:
        if (!s->risc_running || s->risc_paused) {
            return ISP12160_MBS_COMMAND_ERR;
        }
        memset(&s->mailbox[1], 0,
               (ISP12160_MAILBOX_COUNT - 1) * sizeof(uint16_t));
        if (s->native_firmware_loaded) {
            s->mailbox[1] = ISP12160_NATIVE_FIRMWARE_MAJOR;
            s->mailbox[2] = ISP12160_NATIVE_FIRMWARE_MINOR;
            s->mailbox[3] = ISP12160_NATIVE_FIRMWARE_PATCH;
        } else if (s->variant == ISP12160_VARIANT_SCSI) {
            s->mailbox[2] = 2;
        } else if (s->variant == ISP12160_VARIANT_QUEUE) {
            s->mailbox[2] = 1;
        } else {
            s->mailbox[3] = 1;
        }
        return ISP12160_MBS_COMMAND_COMPLETE;

    case ISP12160_MBC_INIT_REQUEST_QUEUE:
        return isp12160_has_queues(s) ?
               isp12160_init_queue(s, mb, true, false) :
               ISP12160_MBS_INVALID_COMMAND;

    case ISP12160_MBC_INIT_RESPONSE_QUEUE:
        return isp12160_has_queues(s) ?
               isp12160_init_queue(s, mb, false, false) :
               ISP12160_MBS_INVALID_COMMAND;

    case ISP12160_MBC_INIT_REQUEST_QUEUE_A64:
        return isp12160_has_queues(s) ?
               isp12160_init_queue(s, mb, true, true) :
               ISP12160_MBS_INVALID_COMMAND;

    case ISP12160_MBC_INIT_RESPONSE_QUEUE_A64:
        return isp12160_has_queues(s) ?
               isp12160_init_queue(s, mb, false, true) :
               ISP12160_MBS_INVALID_COMMAND;

    case ISP12160_MBC_BUS_RESET:
        if (!s->risc_running || s->risc_paused) {
            return ISP12160_MBS_COMMAND_ERR;
        }
        isp12160_scsi_reset_transport(s);
        return ISP12160_MBS_COMMAND_COMPLETE;

    case ISP12160_MBC_SET_INITIATOR_ID:
    case ISP12160_MBC_GET_INITIATOR_ID:
        if (!s->risc_running || s->risc_paused) {
            return ISP12160_MBS_COMMAND_ERR;
        }
        if (mb[1] & ~0x8fU) {
            return ISP12160_MBS_COMMAND_PARAM_ERR;
        }
        if (mb[0] == ISP12160_MBC_SET_INITIATOR_ID) {
            s->initiator_id[!!(mb[1] & 0x80)] = mb[1] & 15;
        } else {
            s->mailbox[1] = s->initiator_id[!!(mb[1] & 0x80)];
        }
        return ISP12160_MBS_COMMAND_COMPLETE;

    case ISP12160_MBC_SET_TARGET_PARAMETERS:
    case ISP12160_MBC_GET_TARGET_PARAMETERS:
    case ISP12160_MBC_SET_DEVICE_QUEUE:
    case ISP12160_MBC_GET_DEVICE_QUEUE: {
        unsigned target = ((mb[1] >> 8) & 15) | ((mb[1] >> 11) & 16);
        unsigned queue = target * ISP12160_SCSI_MAX_LUNS + (mb[1] & 31);
        bool device_queue = mb[0] == ISP12160_MBC_SET_DEVICE_QUEUE ||
                            mb[0] == ISP12160_MBC_GET_DEVICE_QUEUE;

        if (!s->risc_running || s->risc_paused) {
            return ISP12160_MBS_COMMAND_ERR;
        }
        if (mb[1] & ~(device_queue ? 0x8f1fU : 0x8f00U)) {
            return ISP12160_MBS_COMMAND_PARAM_ERR;
        }
        switch (mb[0]) {
        case ISP12160_MBC_SET_TARGET_PARAMETERS:
            s->target_params[target] = mb[2];
            s->target_sync[target] = mb[3];
            s->target_ppr[target] = mb[6];
            break;
        case ISP12160_MBC_GET_TARGET_PARAMETERS:
            s->mailbox[2] = s->target_params[target];
            s->mailbox[3] = s->target_sync[target];
            s->mailbox[6] = s->target_ppr[target];
            break;
        case ISP12160_MBC_SET_DEVICE_QUEUE:
            if (!mb[2] || !mb[3]) {
                return ISP12160_MBS_COMMAND_PARAM_ERR;
            }
            s->queue_depth[queue] = mb[2];
            s->queue_throttle[queue] = mb[3];
            break;
        case ISP12160_MBC_GET_DEVICE_QUEUE:
            s->mailbox[2] = s->queue_depth[queue];
            s->mailbox[3] = s->queue_throttle[queue];
            break;
        }
        isp12160_scsi_schedule_queue(s);
        return ISP12160_MBS_COMMAND_COMPLETE;
    }

    case ISP12160_MBC_SET_SELECTION_TIMEOUT:
    case ISP12160_MBC_SET_RETRY_COUNT:
    case ISP12160_MBC_SET_TAG_AGE_LIMIT:
    case ISP12160_MBC_SET_CLOCK_RATE:
    case ISP12160_MBC_SET_ACTIVE_NEGATION:
    case ISP12160_MBC_SET_ASYNC_DATA_SETUP:
    case ISP12160_MBC_SET_PCI_CONTROL:
    case ISP12160_MBC_SET_RESET_DELAY:
    case ISP12160_MBC_SET_SYSTEM_PARAMETER:
    case ISP12160_MBC_SET_FIRMWARE_FEATURES:
    case ISP12160_MBC_SET_DATA_OVERRUN_RECOVERY:
        return s->native_firmware_loaded && s->risc_running &&
               !s->risc_paused ? ISP12160_MBS_COMMAND_COMPLETE :
                                 ISP12160_MBS_COMMAND_ERR;

    case ISP12160_MBC_EXECUTE_IOCB:
        /* EXECUTE_IOCB is not part of the mailbox command set. */
        return ISP12160_MBS_INVALID_COMMAND;

    default:
        /* The mailbox model rejects commands outside its command set. */
        return ISP12160_MBS_INVALID_COMMAND;
    }
}

static void isp12160_mailbox_bh(void *opaque)
{
    ISP12160State *s = opaque;
    uint16_t status;

    if (!s->mailbox_pending) {
        return;
    }

    memcpy(s->mailbox, s->pending_mailbox, sizeof(s->mailbox));
    s->mailbox_pending = false;
    status = isp12160_run_mailbox(s, s->pending_mailbox);

    trace_isp12160_mailbox(s, s->pending_mailbox[0],
                           s->pending_mailbox[1],
                           s->pending_mailbox[2],
                           s->pending_mailbox[3],
                           s->pending_mailbox[4],
                           s->pending_mailbox[5],
                           s->pending_mailbox[6],
                           s->pending_mailbox[7], status);

    s->mailbox[0] = status;
    s->semaphore = ISP12160_SEMAPHORE_LOCK;
    s->istatus &= ~ISP12160_ISTATUS_PCI_INT;
    s->istatus |= ISP12160_ISTATUS_RISC_INT;
    isp12160_update_irq(s);
}

static uint16_t isp12160_ring_advance(uint16_t index, uint16_t count)
{
    return index + 1U == count ? 0 : index + 1U;
}

static uint16_t isp12160_ring_distance(uint16_t producer, uint16_t consumer,
                                       uint16_t count)
{
    return producer >= consumer ? producer - consumer :
                                  count - consumer + producer;
}

static uint16_t isp12160_response_free(const ISP12160State *s)
{
    const ISP12160QueueState *q = &s->response_queue;

    if (!q->valid) {
        return 0;
    }
    return q->consumer > q->producer ? q->consumer - q->producer - 1U :
           q->count - q->producer + q->consumer - 1U;
}

static bool isp12160_scsi_request_producer_valid(const ISP12160State *s,
                                               uint16_t producer)
{
    const ISP12160QueueState *q = &s->request_queue;
    uint16_t used = isp12160_ring_distance(q->producer, q->consumer,
                                           q->count);
    uint16_t added = isp12160_ring_distance(producer, q->producer,
                                            q->count);

    return added <= q->count - used - 1U;
}

static bool isp12160_scsi_response_consumer_valid(const ISP12160State *s,
                                                uint16_t consumer)
{
    const ISP12160QueueState *q = &s->response_queue;
    uint16_t posted = isp12160_ring_distance(q->producer, q->consumer,
                                             q->count);
    uint16_t consumed = isp12160_ring_distance(consumer, q->consumer,
                                               q->count);

    return consumed <= posted;
}

static uint64_t isp12160_queue_entry_address(const ISP12160QueueState *q,
                                             uint16_t index)
{
    return q->base + (uint64_t)index * ISP12160_QUEUE_ENTRY_BYTES;
}

static bool isp12160_scsi_transport_ready(const ISP12160State *s)
{
    return isp12160_has_scsi(s) && !s->resetting && s->risc_running &&
           !s->risc_paused && s->request_queue.valid &&
           s->response_queue.valid;
}

static bool isp12160_scsi_mailboxes_available(const ISP12160State *s)
{
    return !s->mailbox_pending && !s->mailbox_staging &&
           !(s->semaphore & ISP12160_SEMAPHORE_LOCK);
}

static void isp12160_scsi_schedule_queue(ISP12160State *s)
{
    if (isp12160_has_scsi(s) && s->queue_bh && !s->resetting) {
        qemu_bh_schedule(s->queue_bh);
    }
}

static bool isp12160_scsi_queue_status(ISP12160State *s,
                                     const ISP12160IOCBStatus *status)
{
    Error *local_err = NULL;
    uint16_t tail;

    if (s->pending_status_count >= ISP12160_SCSI_MAX_OUTSTANDING) {
        s->dma_stalled = true;
        return false;
    }
    trace_isp12160_iocb_status(s, status->handle,
                               status->completion_status,
                               status->state_flags,
                               status->residual_length);
    tail = (s->pending_status_head + s->pending_status_count) %
           ISP12160_SCSI_MAX_OUTSTANDING;
    if (!isp12160_iocb_build_status(s->pending_status[tail],
                                    ISP12160_IOCB_ENTRY_BYTES,
                                    status, &local_err)) {
        error_report_err(local_err);
        s->dma_stalled = true;
        return false;
    }
    s->pending_status_count++;
    isp12160_scsi_schedule_queue(s);
    return true;
}

static void isp12160_scsi_queue_simple_status(ISP12160State *s,
                                            uint32_t handle,
                                            uint16_t completion_status,
                                            uint16_t state_flags,
                                            uint32_t residual)
{
    ISP12160IOCBStatus status = {
        .handle = handle,
        .residual_length = residual,
        .completion_status = completion_status,
        .state_flags = state_flags,
    };

    isp12160_scsi_queue_status(s, &status);
}

static void isp12160_scsi_release_request(ISP12160SCSIRequest *request,
                                        const ISP12160IOCBStatus *status)
{
    ISP12160State *s = request->controller;
    SCSIRequest *sreq = request->sreq;

    QTAILQ_REMOVE(&s->active_requests, request, next);
    assert(s->active_request_count);
    s->active_request_count--;

    if (!s->resetting && status) {
        isp12160_scsi_queue_status(s, status);
    }

    sreq->hba_private = NULL;
    request->sreq = NULL;
    scsi_req_unref(sreq);
    g_free(request->segments);
    g_free(request);
    isp12160_scsi_schedule_queue(s);
}

static uint16_t isp12160_scsi_request_state(const ISP12160SCSIRequest *request,
                                          bool got_status)
{
    uint16_t flags = ISP12160_IOCB_SF_GOT_BUS |
                     ISP12160_IOCB_SF_GOT_TARGET |
                     ISP12160_IOCB_SF_SENT_CDB;

    if (got_status) {
        flags |= ISP12160_IOCB_SF_GOT_STATUS;
    }
    if (request->transferred) {
        flags |= ISP12160_IOCB_SF_TRANSFERRED_DATA;
    }
    if (request->transferred == request->command.transfer_length) {
        flags |= ISP12160_IOCB_SF_TRANSFER_COMPLETE;
    }
    return flags;
}

static void isp12160_scsi_command_complete(SCSIRequest *sreq, size_t residual)
{
    ISP12160SCSIRequest *request = sreq->hba_private;
    ISP12160State *s = request ? request->controller : NULL;

    if (!request) {
        return;
    }
    if (request->migration_invalid) {
        isp12160_scsi_release_request(request, NULL);
        return;
    }

    uint64_t remaining = request->command.transfer_length -
                         request->transferred;
    ISP12160IOCBStatus status = {
        .handle = request->command.handle,
        .residual_length = remaining,
        .scsi_status = sreq->status,
        .completion_status = remaining ?
            ISP12160_IOCB_CS_DATA_UNDERRUN : ISP12160_IOCB_CS_COMPLETE,
        .state_flags = isp12160_scsi_request_state(request, true),
    };
    int sense_length;

    (void)residual;
    sense_length = 0;
    if (request->timed_out) {
        status.completion_status = ISP12160_IOCB_CS_TIMEOUT;
        status.scsi_status = 0;
    } else if (!(request->command.control_flags &
                 ISP12160_IOCB_CONTROL_DISABLE_AUTOSENSE) &&
               (s->target_params[request->command.channel * 16 +
                                  request->command.target] & BIT(10))) {
        sense_length = scsi_req_get_sense(sreq, status.sense,
                                          sizeof(status.sense));
    }
    if (sense_length > 0) {
        status.sense_length = sense_length;
        status.state_flags |= ISP12160_IOCB_SF_GOT_SENSE;
    }
    isp12160_scsi_release_request(request, &status);
}

static uint16_t isp12160_scsi_host_completion(int host_status)
{
    switch (host_status) {
    case SCSI_HOST_NO_LUN:
    case SCSI_HOST_BUSY:
        return ISP12160_IOCB_CS_INCOMPLETE;
    case SCSI_HOST_TIME_OUT:
        return ISP12160_IOCB_CS_TIMEOUT;
    case SCSI_HOST_ABORTED:
        return ISP12160_IOCB_CS_ABORTED;
    case SCSI_HOST_RESET:
        return ISP12160_IOCB_CS_RESET;
    case SCSI_HOST_BAD_RESPONSE:
    case SCSI_HOST_ERROR:
    case SCSI_HOST_TRANSPORT_DISRUPTED:
    case SCSI_HOST_TARGET_FAILURE:
    case SCSI_HOST_RESERVATION_ERROR:
    case SCSI_HOST_ALLOCATION_FAILURE:
    case SCSI_HOST_MEDIUM_ERROR:
    default:
        return ISP12160_IOCB_CS_TRANSPORT_ERROR;
    }
}

static void isp12160_scsi_command_failed(SCSIRequest *sreq)
{
    ISP12160SCSIRequest *request = sreq->hba_private;

    if (!request) {
        return;
    }
    if (request->migration_invalid) {
        isp12160_scsi_release_request(request, NULL);
        return;
    }

    uint64_t remaining = request->command.transfer_length -
                         request->transferred;
    ISP12160IOCBStatus status = {
        .handle = request->command.handle,
        .residual_length = remaining,
        .completion_status = request->timed_out ? ISP12160_IOCB_CS_TIMEOUT :
            isp12160_scsi_host_completion(sreq->host_status),
        .state_flags = isp12160_scsi_request_state(request, false),
    };

    isp12160_scsi_release_request(request, &status);
}

static void isp12160_scsi_request_cancelled(SCSIRequest *sreq)
{
    ISP12160SCSIRequest *request = sreq->hba_private;

    if (!request) {
        return;
    }
    if (request->migration_invalid) {
        isp12160_scsi_release_request(request, NULL);
        return;
    }

    uint64_t remaining = request->command.transfer_length -
                         request->transferred;
    ISP12160IOCBStatus status = {
        .handle = request->command.handle,
        .residual_length = remaining,
        .completion_status = request->timed_out ? ISP12160_IOCB_CS_TIMEOUT :
                             request->dma_failed ?
            ISP12160_IOCB_CS_DMA_ERROR : ISP12160_IOCB_CS_ABORTED,
        .state_flags = isp12160_scsi_request_state(request, false),
    };

    isp12160_scsi_release_request(request,
                                request->controller->resetting ? NULL :
                                &status);
}

static MemTxResult isp12160_scsi_transfer_segments(ISP12160SCSIRequest *request,
                                                 uint8_t *buffer,
                                                 uint32_t length)
{
    ISP12160State *s = request->controller;
    PCIDevice *pdev = PCI_DEVICE(s);

    if (!(pdev->config[PCI_COMMAND] & PCI_COMMAND_MASTER) ||
        length > request->command.transfer_length - request->transferred) {
        return MEMTX_ERROR;
    }

    while (length) {
        ISP12160IOCBSegment *segment;
        uint32_t available;
        uint32_t chunk;
        uint64_t address;
        MemTxResult result;

        if (request->segment_index >= request->command.segment_count) {
            return MEMTX_ERROR;
        }
        segment = &request->segments[request->segment_index];
        available = segment->length - request->segment_offset;
        chunk = MIN(length, available);
        address = segment->address + request->segment_offset;
        if (request->command.direction ==
            ISP12160_IOCB_DIRECTION_FROM_DEVICE) {
            result = pci_dma_write(pdev, address, buffer, chunk);
        } else {
            result = pci_dma_read(pdev, address, buffer, chunk);
        }
        if (result != MEMTX_OK) {
            return result;
        }
        buffer += chunk;
        length -= chunk;
        request->transferred += chunk;
        request->segment_offset += chunk;
        if (request->segment_offset == segment->length) {
            request->segment_index++;
            request->segment_offset = 0;
        }
    }
    return MEMTX_OK;
}

static void isp12160_scsi_transfer_data(SCSIRequest *sreq, uint32_t length)
{
    ISP12160SCSIRequest *request = sreq->hba_private;
    DeviceState *dev;
    MemTxResult result;
    bool guard_was_engaged;
    uint8_t *buffer;

    if (!request) {
        scsi_req_cancel(sreq);
        return;
    }
    if (request->migration_invalid) {
        scsi_req_cancel(sreq);
        return;
    }
    buffer = scsi_req_get_buf(sreq);

    /*
     * An asynchronous SCSI completion runs outside the guarded queue BH.
     * Keep DMA from re-entering this controller through its own BAR: a
     * register write could otherwise reset and free @request before the DMA
     * helper returns.  RAM DMA is unaffected by the device IO guard.
     */
    dev = DEVICE(request->controller);
    guard_was_engaged = dev->mem_reentrancy_guard.engaged_in_io;
    dev->mem_reentrancy_guard.engaged_in_io = true;
    result = isp12160_scsi_transfer_segments(request, buffer, length);
    dev->mem_reentrancy_guard.engaged_in_io = guard_was_engaged;

    if (result != MEMTX_OK) {
        request->dma_failed = true;
        scsi_req_cancel(sreq);
        return;
    }
    scsi_req_continue(sreq);
}

static bool isp12160_scsi_flush_status(ISP12160State *s)
{
    ISP12160QueueState *q = &s->response_queue;
    PCIDevice *pdev = PCI_DEVICE(s);
    bool posted = false;

    if (!isp12160_scsi_transport_ready(s) ||
        !isp12160_scsi_mailboxes_available(s) ||
        s->irq_ack_pending ||
        !(pdev->config[PCI_COMMAND] & PCI_COMMAND_MASTER)) {
        return false;
    }

    s->dma_stalled = false;
    while (s->pending_status_count && isp12160_response_free(s)) {
        uint8_t *entry = s->pending_status[s->pending_status_head];

        if (pci_dma_write(pdev,
                          isp12160_queue_entry_address(q, q->producer),
                          entry, ISP12160_IOCB_ENTRY_BYTES) != MEMTX_OK) {
            s->dma_stalled = true;
            break;
        }
        memset(entry, 0, ISP12160_IOCB_ENTRY_BYTES);
        s->pending_status_head = (s->pending_status_head + 1U) %
                                 ISP12160_SCSI_MAX_OUTSTANDING;
        s->pending_status_count--;
        q->producer = isp12160_ring_advance(q->producer, q->count);
        posted = true;
    }
    if (posted) {
        s->response_irq_unobserved = true;
        s->istatus |= ISP12160_ISTATUS_RISC_INT;
        isp12160_update_irq(s);
    }
    return posted;
}

static void isp12160_scsi_advance_request(ISP12160State *s, uint8_t count)
{
    while (count--) {
        s->request_queue.consumer = isp12160_ring_advance(
            s->request_queue.consumer, s->request_queue.count);
    }
}

static bool isp12160_scsi_marker_blocked(ISP12160State *s,
                                         const uint8_t *entry)
{
    ISP12160SCSIRequest *request;
    uint8_t raw_target = entry[ISP12160_IOCB_MARKER_TARGET_OFFSET];
    uint8_t channel = raw_target >> 7;
    uint8_t target = raw_target & 0x7f;
    uint8_t lun = entry[ISP12160_IOCB_MARKER_LUN_OFFSET];
    uint8_t modifier = entry[ISP12160_IOCB_MARKER_MODIFIER_OFFSET];

    QTAILQ_FOREACH(request, &s->active_requests, next) {
        const ISP12160IOCBCommand *command = &request->command;

        if (command->channel != channel) {
            continue;
        }
        if (modifier == ISP12160_IOCB_MARKER_SYNC_ALL ||
            (modifier == ISP12160_IOCB_MARKER_SYNC_ID &&
             command->target == target) ||
            (modifier == ISP12160_IOCB_MARKER_SYNC_ID_LUN &&
             command->target == target && command->lun == lun)) {
            return true;
        }
    }
    return false;
}

static bool isp12160_scsi_direction_matches(const ISP12160IOCBCommand *command,
                                          const SCSIRequest *sreq)
{
    switch (command->direction) {
    case ISP12160_IOCB_DIRECTION_NONE:
        return sreq->cmd.mode == SCSI_XFER_NONE;
    case ISP12160_IOCB_DIRECTION_FROM_DEVICE:
        return sreq->cmd.mode == SCSI_XFER_FROM_DEV;
    case ISP12160_IOCB_DIRECTION_TO_DEVICE:
        return sreq->cmd.mode == SCSI_XFER_TO_DEV;
    default:
        return false;
    }
}

static bool isp12160_scsi_resolve_direction(ISP12160IOCBCommand *command,
                                            const SCSIRequest *sreq)
{
    if (command->direction != ISP12160_IOCB_DIRECTION_UNKNOWN) {
        return isp12160_scsi_direction_matches(command, sreq);
    }

    command->control_flags &= ~ISP12160_IOCB_CONTROL_DATA_UNKNOWN;
    switch (sreq->cmd.mode) {
    case SCSI_XFER_NONE:
        command->direction = ISP12160_IOCB_DIRECTION_NONE;
        break;
    case SCSI_XFER_FROM_DEV:
        command->direction = ISP12160_IOCB_DIRECTION_FROM_DEVICE;
        command->control_flags |= ISP12160_IOCB_CONTROL_DATA_FROM_DEVICE;
        break;
    case SCSI_XFER_TO_DEV:
        command->direction = ISP12160_IOCB_DIRECTION_TO_DEVICE;
        command->control_flags |= ISP12160_IOCB_CONTROL_DATA_TO_DEVICE;
        break;
    default:
        return false;
    }
    return true;
}

static bool isp12160_scsi_handle_active(const ISP12160State *s,
                                      uint32_t handle)
{
    ISP12160SCSIRequest *request;

    QTAILQ_FOREACH(request, &s->active_requests, next) {
        if (request->command.handle == handle) {
            return true;
        }
    }
    return false;
}

static void isp12160_scsi_submit(ISP12160State *s,
                               const ISP12160IOCBCommand *command,
                               ISP12160IOCBSegment *segments)
{
    SCSIDevice *device;
    ISP12160SCSIRequest *request;
    SCSIRequest *sreq;

    if (isp12160_scsi_handle_active(s, command->handle)) {
        isp12160_scsi_queue_simple_status(
            s, command->handle, ISP12160_IOCB_CS_TRANSPORT_ERROR, 0,
            command->transfer_length);
        g_free(segments);
        return;
    }

    device = scsi_device_find(&s->scsi_bus, command->channel,
                              command->target, command->lun);
    if (!device) {
        isp12160_scsi_queue_simple_status(
            s, command->handle, ISP12160_IOCB_CS_INCOMPLETE,
            ISP12160_IOCB_SF_GOT_BUS, command->transfer_length);
        g_free(segments);
        return;
    }

    request = g_new0(ISP12160SCSIRequest, 1);
    request->controller = s;
    request->command = *command;
    request->segments = segments;
    sreq = scsi_req_new(device, command->handle, command->lun,
                        request->command.cdb,
                        request->command.cdb_length, request);
    request->sreq = sreq;

    if (!isp12160_scsi_resolve_direction(&request->command, sreq) ||
        request->command.transfer_length != sreq->cmd.xfer) {
        sreq->hba_private = NULL;
        request->sreq = NULL;
        scsi_req_unref(sreq);
        isp12160_scsi_queue_simple_status(
            s, command->handle, ISP12160_IOCB_CS_DATA_OVERRUN,
            ISP12160_IOCB_SF_GOT_BUS |
            ISP12160_IOCB_SF_GOT_TARGET |
            ISP12160_IOCB_SF_SENT_CDB,
            command->transfer_length);
        g_free(request->segments);
        g_free(request);
        return;
    }

    QTAILQ_INSERT_TAIL(&s->active_requests, request, next);
    s->active_request_count++;
    request->deadline = command->timeout ?
        qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
        (uint64_t)command->timeout * NANOSECONDS_PER_SECOND : 0;
    scsi_req_enqueue_deferred(sreq);
}

static bool isp12160_scsi_process_one(ISP12160State *s)
{
    ISP12160QueueState *q = &s->request_queue;
    PCIDevice *pdev = PCI_DEVICE(s);
    g_autofree uint8_t *entries = NULL;
    ISP12160IOCBCommand command;
    ISP12160IOCBSegment *segments = NULL;
    Error *local_err = NULL;
    uint8_t first[ISP12160_IOCB_ENTRY_BYTES];
    uint16_t available;
    uint16_t segment_count;
    uint8_t entry_count;
    uint32_t handle;
    bool parsed;
    unsigned int i;

    available = isp12160_ring_distance(q->producer, q->consumer, q->count);
    if (!available) {
        return false;
    }
    if (!(pdev->config[PCI_COMMAND] & PCI_COMMAND_MASTER) ||
        pci_dma_read(pdev, isp12160_queue_entry_address(q, q->consumer),
                     first, sizeof(first)) != MEMTX_OK) {
        s->dma_stalled = true;
        return false;
    }

    trace_isp12160_iocb_request(s, q->consumer, q->producer, first[0],
                                first[1], ldl_le_p(first + 4));

    if (first[0] == ISP12160_IOCB_MARKER_TYPE && first[1] == 1) {
        if (isp12160_scsi_marker_blocked(s, first)) {
            return false;
        }
        isp12160_scsi_advance_request(s, 1);
        return true;
    }

    if (s->active_request_count + s->pending_status_count >=
        ISP12160_SCSI_MAX_OUTSTANDING ||
        s->active_request_count + s->pending_status_count >=
        isp12160_response_free(s)) {
        return false;
    }

    handle = ldl_le_p(first + 4);
    if ((first[0] != ISP12160_IOCB_COMMAND_TYPE &&
         first[0] != ISP12160_IOCB_COMMAND_A64_TYPE) || !first[1]) {
        isp12160_scsi_advance_request(s, 1);
        isp12160_scsi_queue_simple_status(
            s, handle, ISP12160_IOCB_CS_INVALID_ENTRY_TYPE, 0, 0);
        return true;
    }

    entry_count = first[1];
    if (entry_count >= q->count) {
        isp12160_scsi_advance_request(s, 1);
        isp12160_scsi_queue_simple_status(
            s, handle, ISP12160_IOCB_CS_INVALID_ENTRY_TYPE, 0, 0);
        return true;
    }
    if (entry_count > available) {
        return false;
    }
    segment_count = lduw_le_p(first + 18);
    if (segment_count > ISP12160_SCSI_MAX_SEGMENTS) {
        isp12160_scsi_advance_request(s, entry_count);
        isp12160_scsi_queue_simple_status(
            s, handle, ISP12160_IOCB_CS_INVALID_ENTRY_TYPE, 0, 0);
        return true;
    }

    entries = g_new(uint8_t,
                    (size_t)entry_count * ISP12160_IOCB_ENTRY_BYTES);
    memcpy(entries, first, sizeof(first));
    for (i = 1; i < entry_count; i++) {
        uint16_t index = (q->consumer + i) % q->count;

        if (pci_dma_read(pdev, isp12160_queue_entry_address(q, index),
                         entries + i * ISP12160_IOCB_ENTRY_BYTES,
                         ISP12160_IOCB_ENTRY_BYTES) != MEMTX_OK) {
            isp12160_scsi_advance_request(s, entry_count);
            isp12160_scsi_queue_simple_status(
                s, handle, ISP12160_IOCB_CS_DMA_ERROR, 0, 0);
            return true;
        }
    }

    if (segment_count) {
        segments = g_new0(ISP12160IOCBSegment, segment_count);
    }
    parsed = first[0] == ISP12160_IOCB_COMMAND_A64_TYPE ?
        isp12160_iocb_parse_a64(entries, entry_count, &command, segments,
                                segment_count, &local_err) :
        isp12160_iocb_parse_32(entries, entry_count, &command, segments,
                               segment_count, &local_err);
    if (!parsed || command.transfer_length > UINT32_MAX) {
        trace_isp12160_iocb_rejected(
            s, handle, parsed ? "transfer length exceeds 32 bits" :
            local_err ? error_get_pretty(local_err) : "invalid IOCB");
        error_free(local_err);
        g_free(segments);
        isp12160_scsi_advance_request(s, entry_count);
        isp12160_scsi_queue_simple_status(
            s, handle, ISP12160_IOCB_CS_INVALID_ENTRY_TYPE, 0, 0);
        return true;
    }

    trace_isp12160_iocb_command(s, command.handle, command.channel,
                                command.target, command.lun, command.cdb[0],
                                command.control_flags, command.segment_count,
                                command.transfer_length);
    isp12160_scsi_advance_request(s, entry_count);
    isp12160_scsi_submit(s, &command, segments);
    return true;
}

static bool isp12160_scsi_can_start(ISP12160State *s,
                                     ISP12160SCSIRequest *candidate)
{
    ISP12160SCSIRequest *p;
    unsigned target = candidate->command.channel * 16 +
                      candidate->command.target;
    unsigned queue = target * ISP12160_SCSI_MAX_LUNS + candidate->command.lun;
    unsigned depth = MIN(s->queue_depth[queue], s->queue_throttle[queue]);
    unsigned running = 0;
    bool before = true;
    bool head = candidate->command.control_flags &
                ISP12160_IOCB_CONTROL_HEAD_TAG;
    bool ordered = candidate->command.control_flags &
                   ISP12160_IOCB_CONTROL_ORDERED_TAG;

    if (!(s->target_params[target] & BIT(11))) {
        depth = 1;
    }
    QTAILQ_FOREACH(p, &s->active_requests, next) {
        if (p == candidate) {
            before = false;
            continue;
        }
        if (p->command.channel != candidate->command.channel ||
            p->command.target != candidate->command.target ||
            p->command.lun != candidate->command.lun) {
            continue;
        }
        if (!p->sreq->hba_deferred) {
            running++;
            if (p->command.control_flags & ISP12160_IOCB_CONTROL_ORDERED_TAG) {
                return false;
            }
        }
        if (before && (ordered || (!head &&
            (p->command.control_flags & ISP12160_IOCB_CONTROL_ORDERED_TAG)))) {
            return false;
        }
    }
    return running < depth;
}

static void isp12160_scsi_update_timer(ISP12160State *s)
{
    ISP12160SCSIRequest *p;
    int64_t deadline = INT64_MAX;

    QTAILQ_FOREACH(p, &s->active_requests, next) {
        if (p->deadline && !p->timed_out) {
            deadline = MIN(deadline, p->deadline);
        }
    }
    if (deadline == INT64_MAX) {
        timer_del(s->request_timer);
    } else {
        timer_mod(s->request_timer, deadline);
    }
}

static void isp12160_scsi_timeout(void *opaque)
{
    ISP12160State *s = opaque;
    ISP12160SCSIRequest *p;
    int64_t now = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

    /* Cancellation can release the BQL and remove any request. */
    for (;;) {
        QTAILQ_FOREACH(p, &s->active_requests, next) {
            if (p->deadline && p->deadline <= now && !p->timed_out) {
                break;
            }
        }
        if (!p) {
            break;
        }
        p->timed_out = true;
        scsi_req_cancel(p->sreq);
    }
    isp12160_scsi_update_timer(s);
}

static void isp12160_scsi_dispatch(ISP12160State *s)
{
    unsigned budget = ISP12160_SCSI_BH_BUDGET;

    while (budget) {
        ISP12160SCSIRequest *p, *next = NULL;

        budget--;
        QTAILQ_FOREACH(p, &s->active_requests, next) {
            if (!p->sreq->hba_deferred || !isp12160_scsi_can_start(s, p)) {
                continue;
            }
            if (!next) {
                next = p;
            }
            if (p->command.control_flags & ISP12160_IOCB_CONTROL_HEAD_TAG) {
                next = p;
                break;
            }
        }
        if (!next) {
            break;
        }
        scsi_req_start_deferred(next->sreq);
    }
    isp12160_scsi_update_timer(s);
    if (!budget) {
        isp12160_scsi_schedule_queue(s);
    }
}

static void isp12160_scsi_queue_bh(void *opaque)
{
    ISP12160State *s = opaque;
    unsigned int budget = ISP12160_SCSI_BH_BUDGET;

    if (!isp12160_scsi_transport_ready(s) ||
        !isp12160_scsi_mailboxes_available(s)) {
        return;
    }
    isp12160_scsi_flush_status(s);
    while (budget && isp12160_scsi_process_one(s)) {
        budget--;
        isp12160_scsi_flush_status(s);
    }
    isp12160_scsi_dispatch(s);
    isp12160_scsi_flush_status(s);
    if (!s->dma_stalled && budget == 0 &&
        isp12160_ring_distance(s->request_queue.producer,
                               s->request_queue.consumer,
                               s->request_queue.count)) {
        isp12160_scsi_schedule_queue(s);
    }
}

static void isp12160_scsi_reset_transport(ISP12160State *s)
{
    ISP12160SCSIRequest *request;

    if (!isp12160_has_scsi(s)) {
        return;
    }
    s->resetting = true;
    timer_del(s->request_timer);
    if (s->queue_bh) {
        qemu_bh_cancel(s->queue_bh);
    }
    /*
     * scsi_req_cancel() may wait with the BQL released.  Another request can
     * therefore complete and remove itself while cancellation is in
     * progress, so never retain a QTAILQ next pointer across the call.
     */
    while ((request = QTAILQ_FIRST(&s->active_requests)) != NULL) {
        uint16_t old_count = s->active_request_count;

        scsi_req_cancel(request->sreq);
        assert(s->active_request_count < old_count);
    }
    assert(QTAILQ_EMPTY(&s->active_requests));
    assert(!s->active_request_count);
    memset(s->pending_status, 0, sizeof(s->pending_status));
    s->pending_status_head = 0;
    s->pending_status_count = 0;
    s->dma_stalled = false;
    s->resetting = false;
}

static void isp12160_scsi_save_request(QEMUFile *f, SCSIRequest *sreq)
{
    ISP12160SCSIRequest *request = sreq->hba_private;
    unsigned int i;

    if (!request || request->migration_invalid) {
        qemu_file_set_error(f, -EINVAL);
        return;
    }

    qemu_put_be32(f, ISP12160_SCSI_REQUEST_MAGIC);
    qemu_put_be16(f, ISP12160_SCSI_REQUEST_VERSION);
    qemu_put_be32(f, request->command.handle);
    qemu_put_be16(f, request->command.timeout);
    qemu_put_be16(f, request->command.control_flags);
    qemu_put_be16(f, request->command.segment_count);
    qemu_put_byte(f, request->command.entry_count);
    qemu_put_byte(f, request->command.channel);
    qemu_put_byte(f, request->command.target);
    qemu_put_byte(f, request->command.lun);
    qemu_put_byte(f, request->command.cdb_length);
    qemu_put_byte(f, request->command.direction);
    qemu_put_buffer(f, request->command.cdb,
                    sizeof(request->command.cdb));
    qemu_put_be64(f, request->command.transfer_length);
    qemu_put_be64(f, request->transferred);
    qemu_put_be16(f, request->segment_index);
    qemu_put_be32(f, request->segment_offset);
    qemu_put_byte(f, request->dma_failed);
    qemu_put_be64(f, request->deadline);
    for (i = 0; i < request->command.segment_count; i++) {
        qemu_put_be64(f, request->segments[i].address);
        qemu_put_be32(f, request->segments[i].length);
    }
}

static bool isp12160_scsi_loaded_request_valid(
    const ISP12160SCSIRequest *request, const SCSIRequest *sreq)
{
    const ISP12160IOCBCommand *command = &request->command;
    uint64_t total = 0;
    uint64_t cursor = 0;
    uint16_t direction_bits;
    size_t expected_entries_32 = 1;
    size_t expected_entries_a64 = 1;
    unsigned int i;

    direction_bits = command->control_flags &
        (ISP12160_IOCB_CONTROL_DATA_FROM_DEVICE |
         ISP12160_IOCB_CONTROL_DATA_TO_DEVICE);
    if (command->segment_count > ISP12160_IOCB_COMMAND_SEGMENTS) {
        expected_entries_32 += DIV_ROUND_UP(
            command->segment_count - ISP12160_IOCB_COMMAND_SEGMENTS,
            ISP12160_IOCB_CONTINUE_SEGMENTS);
    }
    if (command->segment_count > ISP12160_IOCB_COMMAND_A64_SEGMENTS) {
        expected_entries_a64 += DIV_ROUND_UP(
            command->segment_count - ISP12160_IOCB_COMMAND_A64_SEGMENTS,
            ISP12160_IOCB_CONTINUE_A64_SEGMENTS);
    }
    if (!command->cdb_length ||
        command->cdb_length > ISP12160_IOCB_CDB_BYTES ||
        (command->entry_count != expected_entries_32 &&
         command->entry_count != expected_entries_a64) ||
        command->segment_count > ISP12160_SCSI_MAX_SEGMENTS ||
        command->channel > 1 || command->target >= 16 ||
        command->lun >= ISP12160_SCSI_MAX_LUNS ||
        command->direction > ISP12160_IOCB_DIRECTION_TO_DEVICE ||
        command->control_flags & ~ISP12160_IOCB_CONTROL_SUPPORTED ||
        (command->direction == ISP12160_IOCB_DIRECTION_NONE &&
         direction_bits) ||
        (command->direction == ISP12160_IOCB_DIRECTION_FROM_DEVICE &&
         direction_bits != ISP12160_IOCB_CONTROL_DATA_FROM_DEVICE) ||
        (command->direction == ISP12160_IOCB_DIRECTION_TO_DEVICE &&
         direction_bits != ISP12160_IOCB_CONTROL_DATA_TO_DEVICE) ||
        command->transfer_length > UINT32_MAX ||
        (command->segment_count == 0) !=
        (command->direction == ISP12160_IOCB_DIRECTION_NONE) ||
        request->segment_index > command->segment_count ||
        request->transferred > command->transfer_length ||
        request->dma_failed || sreq->tag != command->handle ||
        sreq->lun != command->lun ||
        sreq->dev->channel != command->channel ||
        sreq->dev->id != command->target ||
        sreq->cmd.len != command->cdb_length ||
        memcmp(sreq->cmd.buf, command->cdb, command->cdb_length) ||
        !isp12160_scsi_bytes_zero(
            command->cdb + command->cdb_length,
            ISP12160_IOCB_CDB_BYTES - command->cdb_length) ||
        sreq->cmd.xfer != command->transfer_length ||
        !isp12160_scsi_direction_matches(command, sreq)) {
        return false;
    }

    for (i = 0; i < command->segment_count; i++) {
        const ISP12160IOCBSegment *segment = &request->segments[i];

        if (!segment->length ||
            segment->address > UINT64_MAX - (segment->length - 1U) ||
            total > UINT64_MAX - segment->length) {
            return false;
        }
        if (i < request->segment_index) {
            cursor += segment->length;
        }
        total += segment->length;
    }
    if (request->segment_index < command->segment_count) {
        if (request->segment_offset >=
            request->segments[request->segment_index].length) {
            return false;
        }
        cursor += request->segment_offset;
    } else if (request->segment_offset) {
        return false;
    }
    return total == command->transfer_length &&
           cursor == request->transferred;
}

static void *isp12160_scsi_load_request(QEMUFile *f, SCSIRequest *sreq)
{
    SCSIBus *bus = sreq->bus;
    ISP12160State *s = container_of(bus, ISP12160State, scsi_bus);
    ISP12160SCSIRequest *request = g_new0(ISP12160SCSIRequest, 1);
    unsigned int i;
    uint16_t version;

    request->controller = s;
    if (qemu_get_be32(f) != ISP12160_SCSI_REQUEST_MAGIC) {
        goto invalid;
    }
    version = qemu_get_be16(f);
    if (version < 1 || version > ISP12160_SCSI_REQUEST_VERSION) {
        goto invalid;
    }
    request->command.handle = qemu_get_be32(f);
    request->command.timeout = qemu_get_be16(f);
    request->command.control_flags = qemu_get_be16(f);
    request->command.segment_count = qemu_get_be16(f);
    request->command.entry_count = qemu_get_ubyte(f);
    request->command.channel = qemu_get_ubyte(f);
    request->command.target = qemu_get_ubyte(f);
    request->command.lun = qemu_get_ubyte(f);
    request->command.cdb_length = qemu_get_ubyte(f);
    request->command.direction = qemu_get_ubyte(f);
    qemu_get_buffer(f, request->command.cdb,
                    sizeof(request->command.cdb));
    request->command.transfer_length = qemu_get_be64(f);
    request->transferred = qemu_get_be64(f);
    request->segment_index = qemu_get_be16(f);
    request->segment_offset = qemu_get_be32(f);
    request->dma_failed = qemu_get_ubyte(f);
    if (version >= 2) {
        request->deadline = qemu_get_be64(f);
        if (request->deadline < 0) {
            goto invalid;
        }
    }
    if (request->command.segment_count > ISP12160_SCSI_MAX_SEGMENTS) {
        goto invalid;
    }
    if (request->command.segment_count) {
        request->segments = g_new0(ISP12160IOCBSegment,
                                   request->command.segment_count);
    }
    for (i = 0; i < request->command.segment_count; i++) {
        request->segments[i].address = qemu_get_be64(f);
        request->segments[i].length = qemu_get_be32(f);
    }
    if (qemu_file_get_error(f) ||
        !isp12160_scsi_loaded_request_valid(request, sreq) ||
        s->active_request_count >= ISP12160_SCSI_MAX_OUTSTANDING ||
        !s->response_queue.valid ||
        s->pending_status_count + s->active_request_count + 1U >
        isp12160_response_free(s) ||
        isp12160_scsi_handle_active(s, request->command.handle)) {
        goto invalid;
    }

    request->sreq = scsi_req_ref(sreq);
    sreq->residual = request->command.transfer_length -
                     request->transferred;
    QTAILQ_INSERT_TAIL(&s->active_requests, request, next);
    s->active_request_count++;
    isp12160_scsi_update_timer(s);
    return request;

invalid:
    qemu_file_set_error(f, -EINVAL);
    g_free(request->segments);
    request->segments = NULL;

    /*
     * load_request cannot reject a request.  Retain an inert HBA object for
     * cancellation during migration teardown.
     */
    request->migration_invalid = true;
    request->sreq = scsi_req_ref(sreq);
    QTAILQ_INSERT_TAIL(&s->active_requests, request, next);
    s->active_request_count++;
    return request;
}

static const SCSIBusInfo isp12160_scsi_bus_info = {
    .tcq = true,
    .max_channel = 1,
    .max_target = 15,
    .max_lun = ISP12160_SCSI_MAX_LUNS - 1,
    .transfer_data = isp12160_scsi_transfer_data,
    .fail = isp12160_scsi_command_failed,
    .complete = isp12160_scsi_command_complete,
    .cancel = isp12160_scsi_request_cancelled,
    .save_request = isp12160_scsi_save_request,
    .load_request = isp12160_scsi_load_request,
};

static void isp12160_host_command_write(ISP12160State *s, uint16_t value)
{
    s->host_command = value;

    switch (value) {
    case ISP12160_HC_RESET_RISC:
    case ISP12160_HC_RESET_RELEASE_DISABLE:
        isp12160_reset_risc(s);
        break;

    case ISP12160_HC_PAUSE_RISC:
        if (s->risc_running) {
            s->risc_paused = true;
        }
        break;

    case ISP12160_HC_RELEASE_RISC:
        s->risc_paused = false;
        isp12160_scsi_schedule_queue(s);
        break;

    case ISP12160_HC_SET_HOST_INT:
        /*
         * A SCSI mailbox completion owns all eight mailbox words until the
         * guest releases the semaphore.  Mailbox-word writes are already
         * ignored during that interval; accepting SET_HOST_INT here would
         * relatch the stale completion as a new command and destroy it.
         */
        if (s->mailbox_pending ||
            (isp12160_has_scsi(s) &&
             (s->semaphore & ISP12160_SEMAPHORE_LOCK))) {
            break;
        }
        memcpy(s->pending_mailbox, s->mailbox,
               sizeof(s->pending_mailbox));
        s->mailbox[0] = ISP12160_MBS_BUSY;
        s->mailbox_pending = true;
        s->mailbox_staging = false;
        s->istatus |= ISP12160_ISTATUS_PCI_INT;
        qemu_bh_schedule(s->mailbox_bh);
        isp12160_update_irq(s);
        break;

    case ISP12160_HC_CLEAR_HOST_INT:
        s->istatus &= ~ISP12160_ISTATUS_PCI_INT;
        isp12160_update_irq(s);
        break;

    case ISP12160_HC_CLEAR_RISC_INT:
        s->istatus &= ~ISP12160_ISTATUS_RISC_INT;
        s->irq_ack_pending = false;
        if (s->response_irq_unobserved) {
            s->istatus |= ISP12160_ISTATUS_RISC_INT;
        }
        isp12160_update_irq(s);
        isp12160_scsi_schedule_queue(s);
        break;

    case ISP12160_HC_DISABLE_BIOS:
        /* There is no option ROM in mailbox; retain only the command latch. */
        break;

    default:
        /* Unsupported host commands are ignored. */
        break;
    }
}

static uint64_t isp12160_register_read(void *opaque, hwaddr address,
                                       unsigned int size)
{
    ISP12160State *s = opaque;

    (void)size;

    if (address >= ISP12160_REG_MAILBOX0 &&
        address < ISP12160_REG_MAILBOX0 + ISP12160_MAILBOX_BYTES) {
        unsigned int index = (address - ISP12160_REG_MAILBOX0) / 2;

        /* A pending command or latched completion owns all eight words. */
        if (s->mailbox_staging ||
            (s->semaphore & ISP12160_SEMAPHORE_LOCK)) {
            return s->mailbox[index];
        }
        if (!s->mailbox_staging && index == 4 &&
            s->request_queue.valid) {
            return s->request_queue.consumer;
        }
        if (!s->mailbox_staging && index == 5 &&
            s->response_queue.valid) {
            s->response_irq_unobserved = false;
            return s->response_queue.producer;
        }
        return s->mailbox[index];
    }

    switch (address) {
    case ISP12160_REG_CFG1:
        return s->cfg1;
    case ISP12160_REG_ICTRL:
        return s->ictrl;
    case ISP12160_REG_ISTATUS:
        return s->istatus;
    case ISP12160_REG_SEMAPHORE:
        return s->semaphore;
    case ISP12160_REG_HOST_COMMAND:
        return s->host_command;

    case ISP12160_REG_ID_LOW:
    case ISP12160_REG_ID_HIGH:
    case ISP12160_REG_CFG0:
    case ISP12160_REG_NVRAM:
    default:
        /*
         * ID, CFG0, NVRAM, and all remaining register blocks are not
         * implemented; reads return zero.
         */
        return 0;
    }
}

static void isp12160_register_write(void *opaque, hwaddr address,
                                    uint64_t data, unsigned int size)
{
    ISP12160State *s = opaque;
    uint16_t value = data;

    (void)size;

    if (address >= ISP12160_REG_MAILBOX0 &&
        address < ISP12160_REG_MAILBOX0 + ISP12160_MAILBOX_BYTES) {
        unsigned int index = (address - ISP12160_REG_MAILBOX0) / 2;

        if (isp12160_has_scsi(s) &&
            (s->mailbox_pending ||
             (s->semaphore & ISP12160_SEMAPHORE_LOCK))) {
            return;
        }
        if (index == 0 && isp12160_has_queues(s)) {
            s->mailbox_staging = true;
        }
        if (!s->mailbox_staging && index == 4 &&
            s->request_queue.valid) {
            bool accepted = value < s->request_queue.count &&
                (!isp12160_has_scsi(s) ||
                 isp12160_scsi_request_producer_valid(s, value));

            trace_isp12160_queue_index(s, 0, value, accepted,
                                       s->request_queue.producer,
                                       s->request_queue.consumer);
            if (accepted) {
                s->request_queue.producer = value;
                isp12160_scsi_schedule_queue(s);
            }
            return;
        }
        if (!s->mailbox_staging && index == 5 &&
            s->response_queue.valid) {
            bool accepted = value < s->response_queue.count &&
                (!isp12160_has_scsi(s) ||
                 isp12160_scsi_response_consumer_valid(s, value));

            trace_isp12160_queue_index(s, 1, value, accepted,
                                       s->response_queue.producer,
                                       s->response_queue.consumer);
            if (accepted) {
                s->response_queue.consumer = value;
                isp12160_scsi_schedule_queue(s);
            }
            return;
        }
        s->mailbox[index] = value;
        return;
    }

    switch (address) {
    case ISP12160_REG_CFG1:
        s->cfg1 = value;
        break;

    case ISP12160_REG_ICTRL:
        if (value & ISP12160_ICTRL_RESET) {
            /* Complete the mailbox soft-reset handshake immediately. */
            isp12160_reset_state(s);
            break;
        }
        s->ictrl = value & (ISP12160_ICTRL_ENABLE_INT |
                            ISP12160_ICTRL_ENABLE_RISC);
        isp12160_update_irq(s);
        break;

    case ISP12160_REG_SEMAPHORE:
        s->semaphore = value & ISP12160_SEMAPHORE_LOCK;
        if (!(s->semaphore & ISP12160_SEMAPHORE_LOCK)) {
            if (s->istatus & ISP12160_ISTATUS_RISC_INT) {
                s->irq_ack_pending = true;
            }
            isp12160_scsi_schedule_queue(s);
        }
        break;

    case ISP12160_REG_HOST_COMMAND:
        isp12160_host_command_write(s, value);
        break;

    default:
        /*
         * NVRAM, flash, GPIO, queue, and SCSI register blocks are not
         * implemented; writes are ignored.
         */
        break;
    }
}

static const MemoryRegionOps isp12160_register_ops = {
    .read = isp12160_register_read,
    .write = isp12160_register_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 2,
        .max_access_size = 2,
        .unaligned = false,
    },
    .impl = {
        .min_access_size = 2,
        .max_access_size = 2,
        .unaligned = false,
    },
};

static void isp12160_write_config(PCIDevice *pdev, uint32_t address,
                                  uint32_t value, int length)
{
    ISP12160State *s = ISP12160_MAILBOX(pdev);
    bool bus_master = pdev->config[PCI_COMMAND] & PCI_COMMAND_MASTER;

    pci_default_write_config(pdev, address, value, length);
    if (isp12160_has_scsi(s) && !bus_master &&
        (pdev->config[PCI_COMMAND] & PCI_COMMAND_MASTER)) {
        s->dma_stalled = false;
        isp12160_scsi_schedule_queue(s);
    }
}

static void isp12160_reset(DeviceState *dev)
{
    ISP12160State *s = ISP12160_MAILBOX(dev);

    isp12160_reset_state(s);
}

static bool isp12160_queue_state_valid(const ISP12160QueueState *queue)
{
    if (!queue->valid) {
        return !queue->base && !queue->count && !queue->producer &&
               !queue->consumer && !queue->a64;
    }

    return queue->count >= ISP12160_QUEUE_MIN_ENTRIES &&
           queue->producer < queue->count &&
           queue->consumer < queue->count &&
           isp12160_queue_span_valid(queue->base, queue->count, queue->a64);
}

static bool isp12160_scsi_completion_valid(uint16_t completion)
{
    switch (completion) {
    case ISP12160_IOCB_CS_COMPLETE:
    case ISP12160_IOCB_CS_INCOMPLETE:
    case ISP12160_IOCB_CS_DMA_ERROR:
    case ISP12160_IOCB_CS_TRANSPORT_ERROR:
    case ISP12160_IOCB_CS_RESET:
    case ISP12160_IOCB_CS_ABORTED:
    case ISP12160_IOCB_CS_TIMEOUT:
    case ISP12160_IOCB_CS_DATA_OVERRUN:
    case ISP12160_IOCB_CS_DATA_UNDERRUN:
    case ISP12160_IOCB_CS_INVALID_ENTRY_TYPE:
        return true;
    default:
        return false;
    }
}

static bool isp12160_scsi_bytes_zero(const uint8_t *bytes, size_t length)
{
    size_t i;

    for (i = 0; i < length; i++) {
        if (bytes[i]) {
            return false;
        }
    }
    return true;
}

static bool isp12160_scsi_status_entry_valid(const uint8_t *entry)
{
    uint16_t state_flags = lduw_le_p(entry + 12);
    uint16_t sense_length = lduw_le_p(entry + 18);
    uint16_t completion = lduw_le_p(entry + 10);
    uint32_t residual = ldl_le_p(entry + 20);

    return entry[0] == ISP12160_IOCB_STATUS_TYPE && entry[1] == 1 &&
           !entry[2] && !entry[3] && lduw_le_p(entry + 8) <= UINT8_MAX &&
           isp12160_scsi_completion_valid(completion) &&
           !(state_flags & ~ISP12160_IOCB_STATE_FLAGS_MASK) &&
           !lduw_le_p(entry + 14) && !lduw_le_p(entry + 16) &&
           sense_length <= ISP12160_IOCB_SENSE_BYTES &&
           isp12160_scsi_bytes_zero(entry + 24, 8) &&
           isp12160_scsi_bytes_zero(
               entry + 32 + sense_length,
               ISP12160_IOCB_SENSE_BYTES - sense_length) &&
           (!!sense_length ==
            !!(state_flags & ISP12160_IOCB_SF_GOT_SENSE)) &&
           (!sense_length ||
            (state_flags & ISP12160_IOCB_SF_GOT_STATUS)) &&
           (!lduw_le_p(entry + 8) ||
            (state_flags & ISP12160_IOCB_SF_GOT_STATUS)) &&
           (completion != ISP12160_IOCB_CS_COMPLETE || !residual) &&
           (!(state_flags & ISP12160_IOCB_SF_TRANSFER_COMPLETE) ||
            !residual);
}

static bool isp12160_scsi_pending_state_valid(const ISP12160State *s)
{
    bool occupied[ISP12160_SCSI_MAX_OUTSTANDING] = { false };
    unsigned int i;

    if (s->pending_status_head >= ISP12160_SCSI_MAX_OUTSTANDING ||
        s->pending_status_count > ISP12160_SCSI_MAX_OUTSTANDING ||
        s->active_request_count > ISP12160_SCSI_MAX_OUTSTANDING) {
        return false;
    }
    for (i = 0; i < s->pending_status_count; i++) {
        unsigned int index = (s->pending_status_head + i) %
                             ISP12160_SCSI_MAX_OUTSTANDING;

        occupied[index] = true;
        if (!isp12160_scsi_status_entry_valid(s->pending_status[index])) {
            return false;
        }
    }
    for (i = 0; i < ISP12160_SCSI_MAX_OUTSTANDING; i++) {
        if (!occupied[i] &&
            !isp12160_scsi_bytes_zero(s->pending_status[i],
                                    ISP12160_IOCB_ENTRY_BYTES)) {
            return false;
        }
    }
    if (s->pending_status_count && !s->response_queue.valid) {
        return false;
    }
    return !s->response_queue.valid ||
           s->pending_status_count <= isp12160_response_free(s);
}

static int isp12160_scsi_pre_save(void *opaque)
{
    ISP12160State *s = opaque;
    ISP12160SCSIRequest *request;

    QTAILQ_FOREACH(request, &s->active_requests, next) {
        if (request->migration_invalid || !request->sreq ||
            request->sreq->hba_private != request) {
            return -EINVAL;
        }
    }

    if (s->resetting || !isp12160_scsi_pending_state_valid(s) ||
        s->active_request_count + s->pending_status_count >
        ISP12160_SCSI_MAX_OUTSTANDING ||
        (s->response_queue.valid &&
         s->active_request_count + s->pending_status_count >
         isp12160_response_free(s))) {
        return -EINVAL;
    }
    return 0;
}

static int isp12160_post_load(void *opaque, int version_id)
{
    ISP12160State *s = opaque;
    bool firmware_loaded;
    unsigned int i;

    for (i = 0; i < ARRAY_SIZE(s->initiator_id); i++) {
        if (s->initiator_id[i] > 15) {
            return -EINVAL;
        }
    }
    for (i = 0; i < ARRAY_SIZE(s->queue_depth); i++) {
        if (!s->queue_depth[i] || !s->queue_throttle[i]) {
            return -EINVAL;
        }
    }

    if ((s->variant != ISP12160_VARIANT_MAILBOX &&
         s->variant != ISP12160_VARIANT_QUEUE &&
         s->variant != ISP12160_VARIANT_SCSI) ||
        s->ictrl & ~(ISP12160_ICTRL_ENABLE_INT |
                     ISP12160_ICTRL_ENABLE_RISC) ||
        s->istatus & ~(ISP12160_ISTATUS_PCI_INT |
                       ISP12160_ISTATUS_RISC_INT) ||
        s->semaphore & ~ISP12160_SEMAPHORE_LOCK ||
        (s->mailbox_pending && s->mailbox_staging) ||
        ((s->istatus & ISP12160_ISTATUS_PCI_INT) &&
         !s->mailbox_pending) ||
        (s->irq_ack_pending &&
         !(s->istatus & ISP12160_ISTATUS_RISC_INT)) ||
        (s->response_irq_unobserved &&
         (!isp12160_has_scsi(s) || !s->response_queue.valid ||
          !(s->istatus & ISP12160_ISTATUS_RISC_INT)))) {
        return -EINVAL;
    }

    firmware_loaded = s->token_loaded || s->native_firmware_loaded;
    if (s->token_loaded && s->native_firmware_loaded) {
        return -EINVAL;
    }
    if (s->token_loaded) {
        if ((uint32_t)s->token_address + ISP12160_QEMU_TOKEN_WORDS >
            ISP12160_RISC_WORDS ||
            !isp12160_token_ram_valid(s)) {
            return -EINVAL;
        }
    } else if (s->native_firmware_loaded) {
        if (!s->native_firmware_words ||
            (uint32_t)s->native_firmware_start +
                s->native_firmware_words > ISP12160_RISC_WORDS ||
            s->token_address) {
            return -EINVAL;
        }
        for (i = 0; i < ARRAY_SIZE(s->token_ram); i++) {
            if (s->token_ram[i]) {
                return -EINVAL;
            }
        }
    } else {
        if (s->token_address || s->native_firmware_start ||
            s->native_firmware_checksum || s->native_firmware_words ||
            s->token_verified || s->risc_running || s->risc_paused) {
            return -EINVAL;
        }
        for (i = 0; i < ARRAY_SIZE(s->token_ram); i++) {
            if (s->token_ram[i]) {
                return -EINVAL;
            }
        }
    }

    if (isp12160_has_scsi(s) && !isp12160_scsi_pending_state_valid(s)) {
        return -EINVAL;
    }

    if ((s->token_verified && !firmware_loaded) ||
        (s->risc_running && !s->token_verified) ||
        (s->risc_paused && !s->risc_running)) {
        return -EINVAL;
    }

    if (s->variant == ISP12160_VARIANT_MAILBOX) {
        if (!isp12160_queue_state_valid(&s->request_queue) ||
            !isp12160_queue_state_valid(&s->response_queue) ||
            s->request_queue.valid || s->response_queue.valid) {
            return -EINVAL;
        }
    } else {
        if (!isp12160_queue_state_valid(&s->request_queue) ||
            !isp12160_queue_state_valid(&s->response_queue) ||
            ((s->request_queue.valid || s->response_queue.valid) &&
             !s->risc_running)) {
            return -EINVAL;
        }
        if (!s->mailbox_staging &&
            !(s->semaphore & ISP12160_SEMAPHORE_LOCK)) {
            if (s->request_queue.valid) {
                s->mailbox[4] = s->request_queue.consumer;
            }
            if (s->response_queue.valid) {
                s->mailbox[5] = s->response_queue.producer;
            }
        }
    }

    if (s->mailbox_pending) {
        qemu_bh_schedule(s->mailbox_bh);
    }
    isp12160_scsi_schedule_queue(s);
    isp12160_update_irq(s);
    return 0;
}

static const VMStateDescription vmstate_isp12160_mailbox = {
    .name = TYPE_ISP12160_MAILBOX,
    .version_id = 4,
    .minimum_version_id = 4,
    .post_load = isp12160_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, ISP12160State),
        VMSTATE_UINT16_EQUAL(variant, ISP12160State),
        VMSTATE_UINT16(cfg1, ISP12160State),
        VMSTATE_UINT16(ictrl, ISP12160State),
        VMSTATE_UINT16(istatus, ISP12160State),
        VMSTATE_UINT16(semaphore, ISP12160State),
        VMSTATE_UINT16(host_command, ISP12160State),
        VMSTATE_UINT16_ARRAY(mailbox, ISP12160State,
                             ISP12160_MAILBOX_COUNT),
        VMSTATE_UINT16_ARRAY(pending_mailbox, ISP12160State,
                             ISP12160_MAILBOX_COUNT),
        VMSTATE_UINT16(token_address, ISP12160State),
        VMSTATE_UINT16_ARRAY(token_ram, ISP12160State,
                             ISP12160_QEMU_TOKEN_WORDS),
        VMSTATE_BOOL(token_loaded, ISP12160State),
        VMSTATE_BOOL(token_verified, ISP12160State),
        VMSTATE_BOOL(risc_running, ISP12160State),
        VMSTATE_BOOL(risc_paused, ISP12160State),
        VMSTATE_BOOL(mailbox_pending, ISP12160State),
        VMSTATE_UINT16(native_firmware_start, ISP12160State),
        VMSTATE_UINT16(native_firmware_checksum, ISP12160State),
        VMSTATE_UINT32(native_firmware_words, ISP12160State),
        VMSTATE_BOOL(native_firmware_loaded, ISP12160State),
        VMSTATE_BOOL(irq_ack_pending, ISP12160State),
        VMSTATE_UINT16_ARRAY(initiator_id, ISP12160State, 2),
        VMSTATE_UINT16_ARRAY(target_params, ISP12160State, 32),
        VMSTATE_UINT16_ARRAY(target_sync, ISP12160State, 32),
        VMSTATE_UINT16_ARRAY(target_ppr, ISP12160State, 32),
        VMSTATE_UINT16_ARRAY(queue_depth, ISP12160State,
                             ISP12160_SCSI_MAX_QUEUES),
        VMSTATE_UINT16_ARRAY(queue_throttle, ISP12160State,
                             ISP12160_SCSI_MAX_QUEUES),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_isp12160_queue = {
    .name = TYPE_ISP12160_QUEUE,
    .version_id = 4,
    .minimum_version_id = 4,
    .post_load = isp12160_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, ISP12160State),
        VMSTATE_UINT16_EQUAL(variant, ISP12160State),
        VMSTATE_UINT16(cfg1, ISP12160State),
        VMSTATE_UINT16(ictrl, ISP12160State),
        VMSTATE_UINT16(istatus, ISP12160State),
        VMSTATE_UINT16(semaphore, ISP12160State),
        VMSTATE_UINT16(host_command, ISP12160State),
        VMSTATE_UINT16_ARRAY(mailbox, ISP12160State,
                             ISP12160_MAILBOX_COUNT),
        VMSTATE_UINT16_ARRAY(pending_mailbox, ISP12160State,
                             ISP12160_MAILBOX_COUNT),
        VMSTATE_UINT16(token_address, ISP12160State),
        VMSTATE_UINT16_ARRAY(token_ram, ISP12160State,
                             ISP12160_QEMU_TOKEN_WORDS),
        VMSTATE_BOOL(token_loaded, ISP12160State),
        VMSTATE_BOOL(token_verified, ISP12160State),
        VMSTATE_BOOL(risc_running, ISP12160State),
        VMSTATE_BOOL(risc_paused, ISP12160State),
        VMSTATE_BOOL(mailbox_pending, ISP12160State),
        VMSTATE_UINT64(request_queue.base, ISP12160State),
        VMSTATE_UINT16(request_queue.count, ISP12160State),
        VMSTATE_UINT16(request_queue.producer, ISP12160State),
        VMSTATE_UINT16(request_queue.consumer, ISP12160State),
        VMSTATE_BOOL(request_queue.valid, ISP12160State),
        VMSTATE_BOOL(request_queue.a64, ISP12160State),
        VMSTATE_UINT64(response_queue.base, ISP12160State),
        VMSTATE_UINT16(response_queue.count, ISP12160State),
        VMSTATE_UINT16(response_queue.producer, ISP12160State),
        VMSTATE_UINT16(response_queue.consumer, ISP12160State),
        VMSTATE_BOOL(response_queue.valid, ISP12160State),
        VMSTATE_BOOL(response_queue.a64, ISP12160State),
        VMSTATE_BOOL(mailbox_staging, ISP12160State),
        VMSTATE_UINT16(native_firmware_start, ISP12160State),
        VMSTATE_UINT16(native_firmware_checksum, ISP12160State),
        VMSTATE_UINT32(native_firmware_words, ISP12160State),
        VMSTATE_BOOL(native_firmware_loaded, ISP12160State),
        VMSTATE_BOOL(irq_ack_pending, ISP12160State),
        VMSTATE_UINT16_ARRAY(initiator_id, ISP12160State, 2),
        VMSTATE_UINT16_ARRAY(target_params, ISP12160State, 32),
        VMSTATE_UINT16_ARRAY(target_sync, ISP12160State, 32),
        VMSTATE_UINT16_ARRAY(target_ppr, ISP12160State, 32),
        VMSTATE_UINT16_ARRAY(queue_depth, ISP12160State,
                             ISP12160_SCSI_MAX_QUEUES),
        VMSTATE_UINT16_ARRAY(queue_throttle, ISP12160State,
                             ISP12160_SCSI_MAX_QUEUES),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_isp12160_scsi = {
    .name = TYPE_ISP12160_SCSI,
    .version_id = 5,
    .minimum_version_id = 5,
    .pre_save = isp12160_scsi_pre_save,
    .post_load = isp12160_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_PCI_DEVICE(parent_obj, ISP12160State),
        VMSTATE_UINT16_EQUAL(variant, ISP12160State),
        VMSTATE_UINT16(cfg1, ISP12160State),
        VMSTATE_UINT16(ictrl, ISP12160State),
        VMSTATE_UINT16(istatus, ISP12160State),
        VMSTATE_UINT16(semaphore, ISP12160State),
        VMSTATE_UINT16(host_command, ISP12160State),
        VMSTATE_UINT16_ARRAY(mailbox, ISP12160State,
                             ISP12160_MAILBOX_COUNT),
        VMSTATE_UINT16_ARRAY(pending_mailbox, ISP12160State,
                             ISP12160_MAILBOX_COUNT),
        VMSTATE_UINT16(token_address, ISP12160State),
        VMSTATE_UINT16_ARRAY(token_ram, ISP12160State,
                             ISP12160_QEMU_TOKEN_WORDS),
        VMSTATE_BOOL(token_loaded, ISP12160State),
        VMSTATE_BOOL(token_verified, ISP12160State),
        VMSTATE_BOOL(risc_running, ISP12160State),
        VMSTATE_BOOL(risc_paused, ISP12160State),
        VMSTATE_BOOL(mailbox_pending, ISP12160State),
        VMSTATE_UINT64(request_queue.base, ISP12160State),
        VMSTATE_UINT16(request_queue.count, ISP12160State),
        VMSTATE_UINT16(request_queue.producer, ISP12160State),
        VMSTATE_UINT16(request_queue.consumer, ISP12160State),
        VMSTATE_BOOL(request_queue.valid, ISP12160State),
        VMSTATE_BOOL(request_queue.a64, ISP12160State),
        VMSTATE_UINT64(response_queue.base, ISP12160State),
        VMSTATE_UINT16(response_queue.count, ISP12160State),
        VMSTATE_UINT16(response_queue.producer, ISP12160State),
        VMSTATE_UINT16(response_queue.consumer, ISP12160State),
        VMSTATE_BOOL(response_queue.valid, ISP12160State),
        VMSTATE_BOOL(response_queue.a64, ISP12160State),
        VMSTATE_BOOL(mailbox_staging, ISP12160State),
        VMSTATE_UINT8_2DARRAY(pending_status, ISP12160State,
                              ISP12160_SCSI_MAX_OUTSTANDING,
                              ISP12160_IOCB_ENTRY_BYTES),
        VMSTATE_UINT16(pending_status_head, ISP12160State),
        VMSTATE_UINT16(pending_status_count, ISP12160State),
        VMSTATE_BOOL(dma_stalled, ISP12160State),
        VMSTATE_UINT16(native_firmware_start, ISP12160State),
        VMSTATE_UINT16(native_firmware_checksum, ISP12160State),
        VMSTATE_UINT32(native_firmware_words, ISP12160State),
        VMSTATE_BOOL(native_firmware_loaded, ISP12160State),
        VMSTATE_BOOL(irq_ack_pending, ISP12160State),
        VMSTATE_BOOL(response_irq_unobserved, ISP12160State),
        VMSTATE_UINT16_ARRAY(initiator_id, ISP12160State, 2),
        VMSTATE_UINT16_ARRAY(target_params, ISP12160State, 32),
        VMSTATE_UINT16_ARRAY(target_sync, ISP12160State, 32),
        VMSTATE_UINT16_ARRAY(target_ppr, ISP12160State, 32),
        VMSTATE_UINT16_ARRAY(queue_depth, ISP12160State,
                             ISP12160_SCSI_MAX_QUEUES),
        VMSTATE_UINT16_ARRAY(queue_throttle, ISP12160State,
                             ISP12160_SCSI_MAX_QUEUES),
        VMSTATE_END_OF_LIST()
    },
};

static void isp12160_realize(PCIDevice *pdev, Error **errp)
{
    ISP12160State *s = ISP12160_MAILBOX(pdev);

    (void)errp;

    pci_config_set_interrupt_pin(pdev->config, 1);

    pci_set_word(pdev->config + PCI_SUBSYSTEM_VENDOR_ID,
                 ISP12160_PCI_VENDOR_ID);
    pci_set_word(pdev->config + PCI_SUBSYSTEM_ID, 0x0007);

    memory_region_init_io(&s->registers, OBJECT(s), &isp12160_register_ops,
                          s, "isp12160-registers", ISP12160_MMIO_BAR_SIZE);
    memory_region_init_alias(&s->io_bar, OBJECT(s), "isp12160-io",
                             &s->registers, 0, ISP12160_REG_SIZE);
    memory_region_init_alias(&s->mmio_bar, OBJECT(s), "isp12160-mmio",
                             &s->registers, 0, ISP12160_MMIO_BAR_SIZE);
    pci_register_bar(pdev, 0, PCI_BASE_ADDRESS_SPACE_IO, &s->io_bar);
    pci_register_bar(pdev, 1,
                     PCI_BASE_ADDRESS_SPACE_MEMORY |
                     PCI_BASE_ADDRESS_MEM_TYPE_32,
                     &s->mmio_bar);

    s->mailbox_bh = aio_bh_new_guarded(qemu_get_aio_context(),
                                       isp12160_mailbox_bh, s,
                                       &DEVICE(pdev)->mem_reentrancy_guard);
    if (isp12160_has_scsi(s)) {
        s->request_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                         isp12160_scsi_timeout, s);
        s->queue_bh = aio_bh_new_guarded(
            qemu_get_aio_context(), isp12160_scsi_queue_bh, s,
            &DEVICE(pdev)->mem_reentrancy_guard);
        scsi_bus_init_named(&s->scsi_bus, sizeof(s->scsi_bus), DEVICE(pdev),
                            &isp12160_scsi_bus_info, "isp12160-scsi.0");
    }
    isp12160_reset_state(s);
}

static void isp12160_exit(PCIDevice *pdev)
{
    ISP12160State *s = ISP12160_MAILBOX(pdev);

    isp12160_scsi_reset_transport(s);
    pci_set_irq(pdev, false);
    if (s->queue_bh) {
        timer_free(s->request_timer);
        qemu_bh_delete(s->queue_bh);
        s->queue_bh = NULL;
    }
    qemu_bh_delete(s->mailbox_bh);
    s->mailbox_bh = NULL;
}

static void isp12160_mailbox_instance_init(Object *obj)
{
    ISP12160State *s = ISP12160_MAILBOX(obj);

    s->variant = ISP12160_VARIANT_MAILBOX;
    QTAILQ_INIT(&s->active_requests);
}

static void isp12160_queue_instance_init(Object *obj)
{
    ISP12160State *s = ISP12160_MAILBOX(obj);

    s->variant = ISP12160_VARIANT_QUEUE;
}

static void isp12160_scsi_instance_init(Object *obj)
{
    ISP12160State *s = ISP12160_MAILBOX(obj);

    s->variant = ISP12160_VARIANT_SCSI;
}

static void isp12160_mailbox_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);
    PCIDeviceClass *pc = PCI_DEVICE_CLASS(klass);

    (void)data;

    pc->realize = isp12160_realize;
    pc->exit = isp12160_exit;
    pc->config_write = isp12160_write_config;
    pc->vendor_id = ISP12160_PCI_VENDOR_ID;
    pc->device_id = ISP12160_PCI_DEVICE_ID;
    pc->revision = 0x06;
    pc->class_id = PCI_CLASS_STORAGE_SCSI;
    pc->subsystem_vendor_id = ISP12160_PCI_VENDOR_ID;
    pc->subsystem_id = 0x0007;
    pc->romfile = NULL;

    dc->desc = "QEMU ISP12160 mailbox core";
    dc->user_creatable = false;
    dc->hotpluggable = false;
    dc->vmsd = &vmstate_isp12160_mailbox;
    device_class_set_legacy_reset(dc, isp12160_reset);
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
}

static const TypeInfo isp12160_mailbox_info = {
    .name = TYPE_ISP12160_MAILBOX,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(ISP12160State),
    .instance_init = isp12160_mailbox_instance_init,
    .class_init = isp12160_mailbox_class_init,
    .interfaces = (const InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void isp12160_queue_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    (void)data;
    dc->desc = "QEMU ISP12160 queue-configuration core";
    dc->user_creatable = false;
    dc->hotpluggable = false;
    dc->vmsd = &vmstate_isp12160_queue;
}

static const TypeInfo isp12160_queue_info = {
    .name = TYPE_ISP12160_QUEUE,
    .parent = TYPE_ISP12160_MAILBOX,
    .instance_init = isp12160_queue_instance_init,
    .class_init = isp12160_queue_class_init,
};

static void isp12160_scsi_class_init(ObjectClass *klass, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(klass);

    (void)data;
    dc->desc = "QEMU ISP12160 SCSI controller";
    dc->user_creatable = false;
    dc->hotpluggable = false;
    dc->vmsd = &vmstate_isp12160_scsi;
}

static const TypeInfo isp12160_scsi_info = {
    .name = TYPE_ISP12160_SCSI,
    .parent = TYPE_ISP12160_QUEUE,
    .instance_init = isp12160_scsi_instance_init,
    .class_init = isp12160_scsi_class_init,
};

static void isp12160_register_types(void)
{
    type_register_static(&isp12160_mailbox_info);
    type_register_static(&isp12160_queue_info);
    type_register_static(&isp12160_scsi_info);
}

type_init(isp12160_register_types)
