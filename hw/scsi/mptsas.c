/*
 * QEMU LSI SAS1068 Host Bus Adapter emulation
 * Based on the QEMU Megaraid emulator
 *
 * Copyright (c) 2009-2012 Hannes Reinecke, SUSE Labs
 * Copyright (c) 2012 Verizon, Inc.
 * Copyright (c) 2016 Red Hat, Inc.
 *
 * Authors: Don Slutz, Paolo Bonzini
 *
 * Technical references are listed in
 * docs/devel/device-emulation-provenance.rst.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, see <http://www.gnu.org/licenses/>.
 */

#include "qemu/osdep.h"
#include "hw/pci/pci.h"
#include "hw/core/qdev-properties.h"
#include "system/dma.h"
#include "hw/pci/msi.h"
#include "qemu/iov.h"
#include "qemu/main-loop.h"
#include "qemu/module.h"
#include "hw/scsi/scsi.h"
#include "scsi/constants.h"
#include "trace.h"
#include "qapi/error.h"
#include "mptsas.h"
#include "migration/qemu-file-types.h"
#include "migration/vmstate.h"
#include "mpi.h"

#define NAA_LOCALLY_ASSIGNED_ID 0x3ULL
#define IEEE_COMPANY_LOCALLY_ASSIGNED 0x525400

#define MPTSAS1068_PRODUCT_ID                  \
    (MPI_FW_HEADER_PID_FAMILY_1068_SAS |       \
     MPI_FW_HEADER_PID_PROD_INITIATOR_SCSI |   \
     MPI_FW_HEADER_PID_TYPE_SAS)

#define MPTSPI1030_PRODUCT_ID                  \
    (MPI_FW_HEADER_PID_FAMILY_1030C0_SCSI |    \
     MPI_FW_HEADER_PID_PROD_INITIATOR_SCSI |   \
     MPI_FW_HEADER_PID_TYPE_SCSI)

#define MPT_FW_VERSION 0x01329200
#define MPT_MSG_VERSION 0x0105
#define MPT_NVDATA_FORMAT_VERSION 0x2d
#define MPT_NVDATA_VERSION (MPT_NVDATA_FORMAT_VERSION << 8)
#define MPT_NVDATA_HEADER_SIGNATURE 0x4e69636b
#define MPT_NVDATA_PRODUCT_SIGNATURE 0x4672617a
#define MPT_NVDATA_STATE_VALID 0xf8

typedef struct QEMU_PACKED MPTNvdataHeader {
    uint32_t signature;
    uint8_t state;
    uint8_t checksum;
    uint16_t total_bytes;
    uint16_t nvdata_version;
    uint16_t mpi_version;
    uint8_t header_words;
    uint8_t directory_entry_words;
    uint8_t persistent_header_words;
    uint8_t product_id_words;
    uint32_t directory_entries;
    uint32_t persistent_entries;
    uint32_t seeprom_fw_vars_offset;
    uint32_t seeprom_buffer_offset;
    uint32_t reserved;
} MPTNvdataHeader;

typedef struct QEMU_PACKED MPTNvdataProductId {
    uint32_t signature;
    char vendor[8];
    char product[16];
    char revision[4];
    uint32_t reserved[8];
} MPTNvdataProductId;

typedef struct QEMU_PACKED MPTNvdataDirectoryEntry {
    uint32_t page_flags;
    uint32_t page_location;
} MPTNvdataDirectoryEntry;

typedef struct QEMU_PACKED MPTNvdataPersistentHeader {
    uint8_t state;
    uint8_t checksum;
    uint16_t next_dword_offset;
} MPTNvdataPersistentHeader;

typedef struct QEMU_PACKED MPTNvdataImage {
    MPTNvdataHeader header;
    MPTNvdataProductId product_id;
} MPTNvdataImage;

#define MPT_NVDATA_SIZE sizeof(MPTNvdataImage)
#define MPT_FW_IMAGE_SIZE (MPI_FW_HEADER_SIZE + MPI_EXT_IMAGE_HEADER_SIZE + \
                          MPT_NVDATA_SIZE)
#define MPT_FW_IMAGE_MAX_SIZE 0x400000

/* Slot indicators exclude power switching and physical bypass controls. */
#define MPTSAS_ENCLOSURE_INDICATORS 0x008213ff

static size_t mptsas_fw_image_size(MPTSASState *s)
{
    return s->fw_image_size ? s->fw_image_size :
           mptsas_is_spi(s) ? MPI_FW_HEADER_SIZE : MPT_FW_IMAGE_SIZE;
}

struct MPTSASRequest {
    MPIMsgSCSIIORequest scsi_io;
    SCSIRequest *sreq;
    QEMUSGList qsg;
    MPTSASState *dev;

    QTAILQ_ENTRY(MPTSASRequest) next;
};

static void mptsas_update_interrupt(MPTSASState *s)
{
    PCIDevice *pci = (PCIDevice *) s;
    uint32_t state = s->intr_status & ~(s->intr_mask | MPI_HIS_IOP_DOORBELL_STATUS);

    if (!s->reply_irq_ready) {
        state &= ~MPI_HIS_REPLY_MESSAGE_INTERRUPT;
    }
    if (msi_enabled(pci)) {
        if (state & ~s->irq_state) {
            trace_mptsas_irq_msi(s);
            msi_notify(pci, 0);
        }
    }

    s->irq_state = state;
    trace_mptsas_irq_intx(s, state && !msi_enabled(pci));
    pci_set_irq(pci, state && !msi_enabled(pci));
}

static void mptsas_coalescing_expired(void *opaque)
{
    MPTSASState *s = opaque;

    timer_del(s->coalescing_timer);
    s->coalescing_count = 0;
    s->reply_irq_ready = true;
    mptsas_update_interrupt(s);
}

void mptsas_coalescing_changed(MPTSASState *s)
{
    if (!s->coalescing_count) {
        return;
    }
    if (!(s->ioc1_flags & MPI_IOCPAGE1_REPLY_COALESCING) ||
        (!s->ioc1_coalescing_timeout && !s->ioc1_coalescing_depth) ||
        (s->ioc1_coalescing_depth &&
         s->coalescing_count >= s->ioc1_coalescing_depth)) {
        mptsas_coalescing_expired(s);
    } else if (s->ioc1_coalescing_timeout &&
               !timer_pending(s->coalescing_timer)) {
        timer_mod(s->coalescing_timer,
                  qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL) +
                  (uint64_t)s->ioc1_coalescing_timeout * 1000);
    }
}

static void mptsas_reply_interrupt(MPTSASState *s)
{
    if (!s->reply_irq_ready) {
        s->coalescing_count++;
        mptsas_coalescing_changed(s);
    }
    mptsas_update_interrupt(s);
}

static void mptsas_set_fault(MPTSASState *s, uint32_t code)
{
    if ((s->state & MPI_IOC_STATE_FAULT) == 0) {
        s->state = MPI_IOC_STATE_FAULT | code;
    }
}

#define MPTSAS_FIFO_INVALID(s, name)                     \
    ((s)->name##_head >= ARRAY_SIZE((s)->name) ||        \
     (s)->name##_tail >= ARRAY_SIZE((s)->name))

#define MPTSAS_FIFO_EMPTY(s, name)                       \
    ((s)->name##_head == (s)->name##_tail)

#define MPTSAS_FIFO_FULL(s, name)                        \
    ((s)->name##_head == ((s)->name##_tail + 1) % ARRAY_SIZE((s)->name))

#define MPTSAS_FIFO_GET(s, name) ({                      \
    uint32_t _val = (s)->name[(s)->name##_head++];       \
    (s)->name##_head %= ARRAY_SIZE((s)->name);           \
    _val;                                                \
})

#define MPTSAS_FIFO_PUT(s, name, val) do {       \
    (s)->name[(s)->name##_tail++] = (val);       \
    (s)->name##_tail %= ARRAY_SIZE((s)->name);   \
} while(0)

static void mptsas_post_reply(MPTSASState *s, MPIDefaultReply *reply)
{
    PCIDevice *pci = (PCIDevice *) s;
    uint32_t addr_lo;

    if (MPTSAS_FIFO_EMPTY(s, reply_free) || MPTSAS_FIFO_FULL(s, reply_post)) {
        mptsas_set_fault(s, MPI_IOCSTATUS_INSUFFICIENT_RESOURCES);
        return;
    }

    addr_lo = MPTSAS_FIFO_GET(s, reply_free);

    pci_dma_write(pci, addr_lo | s->host_mfa_high_addr, reply,
                  MIN(s->reply_frame_size, 4 * reply->MsgLength));

    MPTSAS_FIFO_PUT(s, reply_post, MPI_ADDRESS_REPLY_A_BIT | (addr_lo >> 1));

    s->intr_status |= MPI_HIS_REPLY_MESSAGE_INTERRUPT;
    if (s->doorbell_state == DOORBELL_WRITE) {
        s->doorbell_state = DOORBELL_NONE;
        s->intr_status |= MPI_HIS_DOORBELL_INTERRUPT;
    }
    mptsas_reply_interrupt(s);
}

void mptsas_reply(MPTSASState *s, MPIDefaultReply *reply)
{
    if (s->doorbell_state == DOORBELL_WRITE) {
        /* The reply is sent out in 16 bit chunks, while the size
         * in the reply is in 32 bit units.
         */
        s->doorbell_state = DOORBELL_READ;
        s->doorbell_reply_idx = 0;
        s->doorbell_reply_size = reply->MsgLength * 2;
        memcpy(s->doorbell_reply, reply, s->doorbell_reply_size * 2);
        s->intr_status |= MPI_HIS_DOORBELL_INTERRUPT;
        mptsas_update_interrupt(s);
    } else {
        mptsas_post_reply(s, reply);
    }
}

static void mptsas_turbo_reply(MPTSASState *s, uint32_t msgctx)
{
    if (MPTSAS_FIFO_FULL(s, reply_post)) {
        mptsas_set_fault(s, MPI_IOCSTATUS_INSUFFICIENT_RESOURCES);
        return;
    }

    /* The reply is just the message context ID (bit 31 = clear). */
    MPTSAS_FIFO_PUT(s, reply_post, msgctx);

    s->intr_status |= MPI_HIS_REPLY_MESSAGE_INTERRUPT;
    mptsas_reply_interrupt(s);
}

#define MPTSAS_MAX_REQUEST_SIZE 52
#define MPTSAS_REQUEST_FRAME_SIZE 128

static const int mpi_request_sizes[] = {
    [MPI_FUNCTION_SCSI_IO_REQUEST]    = sizeof(MPIMsgSCSIIORequest),
    [MPI_FUNCTION_SCSI_TASK_MGMT]     = sizeof(MPIMsgSCSITaskMgmt),
    [MPI_FUNCTION_IOC_INIT]           = sizeof(MPIMsgIOCInit),
    [MPI_FUNCTION_IOC_FACTS]          = sizeof(MPIMsgIOCFacts),
    [MPI_FUNCTION_CONFIG]             = sizeof(MPIMsgConfig),
    [MPI_FUNCTION_PORT_FACTS]         = sizeof(MPIMsgPortFacts),
    [MPI_FUNCTION_PORT_ENABLE]        = sizeof(MPIMsgPortEnable),
    [MPI_FUNCTION_EVENT_NOTIFICATION] = sizeof(MPIMsgEventNotify),
    [MPI_FUNCTION_FW_UPLOAD]         = sizeof(MPIMsgFWUpload),
    [MPI_FUNCTION_FW_DOWNLOAD]       = sizeof(MPIMsgFWDownload),
    [MPI_FUNCTION_TOOLBOX]           = sizeof(MPIMsgToolboxClean),
    [MPI_FUNCTION_SAS_IO_UNIT_CONTROL] = sizeof(MPIMsgSASIOUnitControl),
    [MPI_FUNCTION_SCSI_ENCLOSURE_PROCESSOR] = sizeof(MPIMsgSEP),
};

static dma_addr_t mptsas_ld_sg_base(MPTSASState *s, uint32_t flags_and_length,
                                    dma_addr_t *sgaddr)
{
    const MemTxAttrs attrs = MEMTXATTRS_UNSPECIFIED;
    PCIDevice *pci = (PCIDevice *) s;
    dma_addr_t addr;

    if (flags_and_length & MPI_SGE_FLAGS_64_BIT_ADDRESSING) {
        uint64_t addr64;

        ldq_le_pci_dma(pci, *sgaddr + 4, &addr64, attrs);
        addr = addr64;
        *sgaddr += 12;
    } else {
        uint32_t addr32;

        ldl_le_pci_dma(pci, *sgaddr + 4, &addr32, attrs);
        addr = addr32;
        *sgaddr += 8;
    }
    return addr;
}

static int mptsas_build_sgl(MPTSASState *s, MPTSASRequest *req, hwaddr req_addr)
{
    PCIDevice *pci = (PCIDevice *) s;
    hwaddr next_chain_addr;
    uint32_t left;
    hwaddr sgaddr;
    uint32_t chain_offset;

    chain_offset = req->scsi_io.ChainOffset;
    next_chain_addr = req_addr + chain_offset * sizeof(uint32_t);
    sgaddr = req_addr + sizeof(MPIMsgSCSIIORequest);
    pci_dma_sglist_init(&req->qsg, pci, 4);
    left = req->scsi_io.DataLength;

    /* Commands without a data phase do not carry a scatter/gather list. */
    if (!left) {
        return 0;
    }

    for(;;) {
        dma_addr_t addr, len;
        uint32_t flags_and_length;

        ldl_le_pci_dma(pci, sgaddr, &flags_and_length, MEMTXATTRS_UNSPECIFIED);
        len = flags_and_length & MPI_SGE_LENGTH_MASK;
        if ((flags_and_length & MPI_SGE_FLAGS_ELEMENT_TYPE_MASK)
            != MPI_SGE_FLAGS_SIMPLE_ELEMENT ||
            (!len &&
             !(flags_and_length & MPI_SGE_FLAGS_END_OF_LIST) &&
             !(flags_and_length & MPI_SGE_FLAGS_END_OF_BUFFER))) {
            return MPI_IOCSTATUS_INVALID_SGL;
        }

        len = MIN(len, left);
        if (!len) {
            /* We reached the desired transfer length, ignore extra
             * elements of the s/g list.
             */
            break;
        }

        addr = mptsas_ld_sg_base(s, flags_and_length, &sgaddr);
        qemu_sglist_add(&req->qsg, addr, len);
        left -= len;

        if (flags_and_length & MPI_SGE_FLAGS_END_OF_LIST) {
            break;
        }

        if (flags_and_length & MPI_SGE_FLAGS_LAST_ELEMENT) {
            if (!chain_offset) {
                break;
            }

            ldl_le_pci_dma(pci, next_chain_addr, &flags_and_length,
                           MEMTXATTRS_UNSPECIFIED);
            if ((flags_and_length & MPI_SGE_FLAGS_ELEMENT_TYPE_MASK)
                != MPI_SGE_FLAGS_CHAIN_ELEMENT) {
                return MPI_IOCSTATUS_INVALID_SGL;
            }

            sgaddr = mptsas_ld_sg_base(s, flags_and_length, &next_chain_addr);
            chain_offset =
                (flags_and_length & MPI_SGE_CHAIN_OFFSET_MASK) >> MPI_SGE_CHAIN_OFFSET_SHIFT;
            next_chain_addr = sgaddr + chain_offset * sizeof(uint32_t);
        }
    }
    return 0;
}

static void mptsas_free_request(MPTSASRequest *req)
{
    if (req->sreq != NULL) {
        req->sreq->hba_private = NULL;
        scsi_req_unref(req->sreq);
        req->sreq = NULL;
    }
    qemu_sglist_destroy(&req->qsg);
    g_free(req);
}

static int mptsas_scsi_lun(const uint8_t lun[8])
{
    /* Only bus-zero peripheral and flat-space addresses are represented. */
    if ((lun[0] != 0 && (lun[0] & 0xc0) != 0x40) ||
        lun[2] || lun[3] || lun[4] || lun[5] || lun[6] || lun[7]) {
        /* An unrepresented address must not alias an attached logical unit. */
        return -1;
    }

    return lduw_be_p(lun) & 0x3fff;
}

static int mptsas_scsi_device_find(MPTSASState *s, int bus, int target,
                                   int lun, SCSIDevice **sdev)
{
    if (bus != 0) {
        return MPI_IOCSTATUS_SCSI_INVALID_BUS;
    }

    if (target >= s->max_devices) {
        return MPI_IOCSTATUS_SCSI_INVALID_TARGETID;
    }

    *sdev = scsi_device_find(&s->bus, bus, target, lun);
    if (!*sdev) {
        return MPI_IOCSTATUS_SCSI_DEVICE_NOT_THERE;
    }

    return 0;
}

static int mptsas_process_scsi_io_request(MPTSASState *s,
                                          MPIMsgSCSIIORequest *scsi_io,
                                          hwaddr addr)
{
    MPTSASRequest *req;
    MPIMsgSCSIIOReply reply;
    SCSIDevice *sdev;
    int status, lun;

    mptsas_fix_scsi_io_endianness(scsi_io);
    lun = mptsas_scsi_lun(scsi_io->LUN);

    trace_mptsas_process_scsi_io_request(s, scsi_io->Bus, scsi_io->TargetID,
                                         lun, scsi_io->DataLength,
                                         scsi_io->CDB[0], scsi_io->Control,
                                         ldq_be_p(scsi_io->LUN));

    status = mptsas_scsi_device_find(s, scsi_io->Bus, scsi_io->TargetID,
                                     lun, &sdev);
    if (status) {
        goto bad;
    }

    req = g_new0(MPTSASRequest, 1);
    req->scsi_io = *scsi_io;
    req->dev = s;

    status = mptsas_build_sgl(s, req, addr);
    if (status) {
        goto free_bad;
    }

    if (req->qsg.size < scsi_io->DataLength) {
        trace_mptsas_sgl_overflow(s, scsi_io->MsgContext, scsi_io->DataLength,
                                  req->qsg.size);
        status = MPI_IOCSTATUS_INVALID_SGL;
        goto free_bad;
    }

    req->sreq = scsi_req_new(sdev, scsi_io->MsgContext, lun, scsi_io->CDB,
                             scsi_io->CDBLength, req);

    if (req->sreq->cmd.xfer > scsi_io->DataLength) {
        goto overrun;
    }
    /* Transfer direction is irrelevant when there is no data phase. */
    if (req->sreq->cmd.xfer) {
        switch (scsi_io->Control & MPI_SCSIIO_CONTROL_DATADIRECTION_MASK) {
        case MPI_SCSIIO_CONTROL_NODATATRANSFER:
            if (req->sreq->cmd.mode != SCSI_XFER_NONE) {
                goto overrun;
            }
            break;

        case MPI_SCSIIO_CONTROL_WRITE:
            if (req->sreq->cmd.mode != SCSI_XFER_TO_DEV) {
                goto overrun;
            }
            break;

        case MPI_SCSIIO_CONTROL_READ:
            if (req->sreq->cmd.mode != SCSI_XFER_FROM_DEV) {
                goto overrun;
            }
            break;
        }
    } else {
        req->sreq->residual = scsi_io->DataLength;
    }

    if (scsi_req_enqueue(req->sreq)) {
        scsi_req_continue(req->sreq);
    }
    return 0;

overrun:
    trace_mptsas_scsi_overflow(s, scsi_io->MsgContext, req->sreq->cmd.xfer,
                               scsi_io->DataLength);
    status = MPI_IOCSTATUS_SCSI_DATA_OVERRUN;
free_bad:
    mptsas_free_request(req);
bad:
    trace_mptsas_scsi_request_error(s, scsi_io->MsgContext, status);
    memset(&reply, 0, sizeof(reply));
    reply.TargetID          = scsi_io->TargetID;
    reply.Bus               = scsi_io->Bus;
    reply.MsgLength         = sizeof(reply) / 4;
    reply.Function          = scsi_io->Function;
    reply.CDBLength         = scsi_io->CDBLength;
    reply.SenseBufferLength = scsi_io->SenseBufferLength;
    reply.MsgContext        = scsi_io->MsgContext;
    reply.SCSIState         = MPI_SCSI_STATE_NO_SCSI_STATUS;
    reply.IOCStatus         = status;

    mptsas_fix_scsi_io_reply_endianness(&reply);
    mptsas_reply(s, (MPIDefaultReply *)&reply);

    return 0;
}

typedef struct {
    Notifier                notifier;
    MPTSASState             *s;
    MPIMsgSCSITaskMgmtReply *reply;
} MPTSASCancelNotifier;

static void mptsas_cancel_notify(Notifier *notifier, void *data)
{
    MPTSASCancelNotifier *n = container_of(notifier,
                                           MPTSASCancelNotifier,
                                           notifier);

    /* Abusing IOCLogInfo to store the expected number of requests... */
    if (++n->reply->TerminationCount == n->reply->IOCLogInfo) {
        n->reply->IOCLogInfo = 0;
        mptsas_fix_scsi_task_mgmt_reply_endianness(n->reply);
        mptsas_post_reply(n->s, (MPIDefaultReply *)n->reply);
        g_free(n->reply);
    }
    g_free(n);
}

static void mptsas_process_scsi_task_mgmt(MPTSASState *s, MPIMsgSCSITaskMgmt *req)
{
    MPIMsgSCSITaskMgmtReply reply;
    MPIMsgSCSITaskMgmtReply *reply_async;
    int status, count, lun;
    SCSIDevice *sdev;
    SCSIRequest *r, *next;
    BusChild *kid;

    mptsas_fix_scsi_task_mgmt_endianness(req);
    lun = mptsas_scsi_lun(req->LUN);

    QEMU_BUILD_BUG_ON(MPTSAS_MAX_REQUEST_SIZE < sizeof(*req));
    QEMU_BUILD_BUG_ON(sizeof(s->doorbell_msg) < sizeof(*req));
    QEMU_BUILD_BUG_ON(sizeof(s->doorbell_reply) < sizeof(reply));

    memset(&reply, 0, sizeof(reply));
    reply.TargetID   = req->TargetID;
    reply.Bus        = req->Bus;
    reply.MsgLength  = sizeof(reply) / 4;
    reply.Function   = req->Function;
    reply.TaskType   = req->TaskType;
    reply.MsgContext = req->MsgContext;

    switch (req->TaskType) {
    case MPI_SCSITASKMGMT_TASKTYPE_ABORT_TASK:
    case MPI_SCSITASKMGMT_TASKTYPE_QUERY_TASK:
        status = mptsas_scsi_device_find(s, req->Bus, req->TargetID,
                                         lun, &sdev);
        if (status) {
            reply.IOCStatus = status;
            goto out;
        }
        if (sdev->lun != lun) {
            reply.ResponseCode = MPI_SCSITASKMGMT_RSP_TM_INVALID_LUN;
            goto out;
        }

        QTAILQ_FOREACH_SAFE(r, &sdev->requests, next, next) {
            MPTSASRequest *cmd_req = r->hba_private;
            if (cmd_req && cmd_req->scsi_io.MsgContext == req->TaskMsgContext) {
                break;
            }
        }
        if (r) {
            /*
             * Assert that the request has not been completed yet, we
             * check for it in the loop above.
             */
            assert(r->hba_private);
            if (req->TaskType == MPI_SCSITASKMGMT_TASKTYPE_QUERY_TASK) {
                /* "If the specified command is present in the task set, then
                 * return a service response set to FUNCTION SUCCEEDED".
                 */
                reply.ResponseCode = MPI_SCSITASKMGMT_RSP_TM_SUCCEEDED;
            } else {
                MPTSASCancelNotifier *notifier;

                reply_async = g_memdup(&reply, sizeof(MPIMsgSCSITaskMgmtReply));
                reply_async->IOCLogInfo = INT_MAX;

                count = 1;
                notifier = g_new(MPTSASCancelNotifier, 1);
                notifier->s = s;
                notifier->reply = reply_async;
                notifier->notifier.notify = mptsas_cancel_notify;
                scsi_req_cancel_async(r, &notifier->notifier);
                goto reply_maybe_async;
            }
        }
        break;

    case MPI_SCSITASKMGMT_TASKTYPE_ABRT_TASK_SET:
    case MPI_SCSITASKMGMT_TASKTYPE_CLEAR_TASK_SET:
        status = mptsas_scsi_device_find(s, req->Bus, req->TargetID,
                                         lun, &sdev);
        if (status) {
            reply.IOCStatus = status;
            goto out;
        }
        if (sdev->lun != lun) {
            reply.ResponseCode = MPI_SCSITASKMGMT_RSP_TM_INVALID_LUN;
            goto out;
        }

        reply_async = g_memdup(&reply, sizeof(MPIMsgSCSITaskMgmtReply));
        reply_async->IOCLogInfo = INT_MAX;

        count = 0;
        QTAILQ_FOREACH_SAFE(r, &sdev->requests, next, next) {
            if (r->hba_private) {
                MPTSASCancelNotifier *notifier;

                count++;
                notifier = g_new(MPTSASCancelNotifier, 1);
                notifier->s = s;
                notifier->reply = reply_async;
                notifier->notifier.notify = mptsas_cancel_notify;
                scsi_req_cancel_async(r, &notifier->notifier);
            }
        }

reply_maybe_async:
        if (reply_async->TerminationCount < count) {
            reply_async->IOCLogInfo = count;
            return;
        }
        g_free(reply_async);
        reply.TerminationCount = count;
        break;

    case MPI_SCSITASKMGMT_TASKTYPE_LOGICAL_UNIT_RESET:
        status = mptsas_scsi_device_find(s, req->Bus, req->TargetID,
                                         lun, &sdev);
        if (status) {
            reply.IOCStatus = status;
            goto out;
        }
        if (sdev->lun != lun) {
            reply.ResponseCode = MPI_SCSITASKMGMT_RSP_TM_INVALID_LUN;
            goto out;
        }
        device_cold_reset(&sdev->qdev);
        break;

    case MPI_SCSITASKMGMT_TASKTYPE_TARGET_RESET:
        if (req->Bus != 0) {
            reply.IOCStatus = MPI_IOCSTATUS_SCSI_INVALID_BUS;
            goto out;
        }
        if (req->TargetID >= s->max_devices) {
            reply.IOCStatus = MPI_IOCSTATUS_SCSI_INVALID_TARGETID;
            goto out;
        }

        QTAILQ_FOREACH(kid, &s->bus.qbus.children, sibling) {
            sdev = SCSI_DEVICE(kid->child);
            if (sdev->channel == 0 && sdev->id == req->TargetID) {
                device_cold_reset(kid->child);
            }
        }
        break;

    case MPI_SCSITASKMGMT_TASKTYPE_RESET_BUS:
        bus_cold_reset(BUS(&s->bus));
        break;

    default:
        reply.ResponseCode = MPI_SCSITASKMGMT_RSP_TM_NOT_SUPPORTED;
        break;
    }

out:
    mptsas_fix_scsi_task_mgmt_reply_endianness(&reply);
    mptsas_post_reply(s, (MPIDefaultReply *)&reply);
}

static void mptsas_process_ioc_init(MPTSASState *s, MPIMsgIOCInit *req)
{
    MPIMsgIOCInitReply reply;
    unsigned int requested_devices;

    mptsas_fix_ioc_init_endianness(req);

    QEMU_BUILD_BUG_ON(MPTSAS_MAX_REQUEST_SIZE < sizeof(*req));
    QEMU_BUILD_BUG_ON(sizeof(s->doorbell_msg) < sizeof(*req));
    QEMU_BUILD_BUG_ON(sizeof(s->doorbell_reply) < sizeof(reply));

    requested_devices = req->MaxDevices ? req->MaxDevices : 256;
    s->who_init               = req->WhoInit;
    s->reply_frame_size       = req->ReplyFrameSize;
    s->max_buses              = MIN(req->MaxBuses, 1);
    s->max_devices            = MIN(requested_devices,
                                    mptsas_max_devices(s));
    s->host_mfa_high_addr     = (hwaddr)req->HostMfaHighAddr << 32;
    s->sense_buffer_high_addr = (hwaddr)req->SenseBufferHighAddr << 32;

    if (s->state == MPI_IOC_STATE_READY) {
        s->state = MPI_IOC_STATE_OPERATIONAL;
    }

    memset(&reply, 0, sizeof(reply));
    reply.WhoInit    = req->WhoInit;
    reply.MsgLength  = sizeof(reply) / 4;
    reply.Function   = req->Function;
    reply.MaxDevices = req->MaxDevices;
    reply.MaxBuses   = req->MaxBuses;
    reply.MsgContext = req->MsgContext;

    mptsas_fix_ioc_init_reply_endianness(&reply);
    mptsas_reply(s, (MPIDefaultReply *)&reply);
}

static void mptsas_process_ioc_facts(MPTSASState *s,
                                     MPIMsgIOCFacts *req)
{
    MPIMsgIOCFactsReply reply;
    uint32_t version = s->fw_image ? ldl_le_p(s->fw_image + 0x24) :
                                    MPT_FW_VERSION;

    mptsas_fix_ioc_facts_endianness(req);

    QEMU_BUILD_BUG_ON(MPTSAS_MAX_REQUEST_SIZE < sizeof(*req));
    QEMU_BUILD_BUG_ON(sizeof(s->doorbell_msg) < sizeof(*req));
    QEMU_BUILD_BUG_ON(sizeof(s->doorbell_reply) < sizeof(reply));

    memset(&reply, 0, sizeof(reply));
    reply.MsgVersion                 = MPT_MSG_VERSION;
    reply.MsgLength                  = sizeof(reply) / 4;
    reply.Function                   = req->Function;
    reply.MsgContext                 = req->MsgContext;
    reply.MaxChainDepth              = MPTSAS_MAXIMUM_CHAIN_DEPTH;
    reply.WhoInit                    = s->who_init;
    reply.BlockSize                  = MPTSAS_MAX_REQUEST_SIZE / sizeof(uint32_t);
    reply.ReplyQueueDepth            = ARRAY_SIZE(s->reply_post) - 1;
    QEMU_BUILD_BUG_ON(ARRAY_SIZE(s->reply_post) != ARRAY_SIZE(s->reply_free));

    /* RequestFrameSize is expressed in 32-bit words. */
    reply.RequestFrameSize           = MPTSAS_REQUEST_FRAME_SIZE / 4;
    reply.ProductID                  = mptsas_is_spi(s) ?
                                       MPTSPI1030_PRODUCT_ID :
                                       MPTSAS1068_PRODUCT_ID;
    reply.CurrentHostMfaHighAddr     = s->host_mfa_high_addr >> 32;
    reply.GlobalCredits              = ARRAY_SIZE(s->request_post) - 1;
    reply.NumberOfPorts              = mptsas_num_ports(s);
    reply.CurrentSenseBufferHighAddr = s->sense_buffer_high_addr >> 32;
    reply.CurReplyFrameSize          = s->reply_frame_size;
    reply.MaxDevices                 = s->max_devices;
    reply.MaxBuses                   = s->max_buses;
    reply.FWImageSize                = mptsas_fw_image_size(s);
    reply.FWVersionDev               = version & 0xff;
    reply.FWVersionUnit              = (version >> 8) & 0xff;
    reply.FWVersionMinor             = (version >> 16) & 0xff;
    reply.FWVersionMajor             = version >> 24;

    mptsas_fix_ioc_facts_reply_endianness(&reply);
    mptsas_reply(s, (MPIDefaultReply *)&reply);
}

static void mptsas_process_port_facts(MPTSASState *s,
                                     MPIMsgPortFacts *req)
{
    MPIMsgPortFactsReply reply;

    mptsas_fix_port_facts_endianness(req);

    QEMU_BUILD_BUG_ON(MPTSAS_MAX_REQUEST_SIZE < sizeof(*req));
    QEMU_BUILD_BUG_ON(sizeof(s->doorbell_msg) < sizeof(*req));
    QEMU_BUILD_BUG_ON(sizeof(s->doorbell_reply) < sizeof(reply));

    memset(&reply, 0, sizeof(reply));
    reply.MsgLength  = sizeof(reply) / 4;
    reply.Function   = req->Function;
    reply.PortNumber = req->PortNumber;
    reply.MsgContext = req->MsgContext;

    if (req->PortNumber < mptsas_num_ports(s)) {
        reply.PortType = mptsas_is_spi(s) ? MPI_PORTFACTS_PORTTYPE_SCSI :
                                           MPI_PORTFACTS_PORTTYPE_SAS;
        reply.MaxDevices = mptsas_max_devices(s);
        reply.PortSCSIID = mptsas_is_spi(s) ?
            s->spi_port_configuration &
            MPI_SCSIPORTPAGE1_CFG_PORT_SCSI_ID_MASK : MPTSAS_NUM_PORTS;
        reply.ProtocolFlags = MPI_PORTFACTS_PROTOCOL_INITIATOR;
        if (!mptsas_is_spi(s)) {
            reply.ProtocolFlags |= MPI_PORTFACTS_PROTOCOL_LOGBUSADDR;
        }
    } else {
        reply.IOCStatus = MPI_IOCSTATUS_INVALID_FIELD;
    }

    mptsas_fix_port_facts_reply_endianness(&reply);
    mptsas_reply(s, (MPIDefaultReply *)&reply);
}

static void mptsas_process_port_enable(MPTSASState *s,
                                       MPIMsgPortEnable *req)
{
    MPIMsgPortEnableReply reply;

    mptsas_fix_port_enable_endianness(req);

    QEMU_BUILD_BUG_ON(MPTSAS_MAX_REQUEST_SIZE < sizeof(*req));
    QEMU_BUILD_BUG_ON(sizeof(s->doorbell_msg) < sizeof(*req));
    QEMU_BUILD_BUG_ON(sizeof(s->doorbell_reply) < sizeof(reply));

    memset(&reply, 0, sizeof(reply));
    reply.MsgLength  = sizeof(reply) / 4;
    reply.PortNumber = req->PortNumber;
    reply.Function   = req->Function;
    reply.MsgContext = req->MsgContext;

    if (req->PortNumber >= mptsas_num_ports(s)) {
        reply.IOCStatus = MPI_IOCSTATUS_INVALID_FIELD;
    }

    mptsas_fix_port_enable_reply_endianness(&reply);
    mptsas_reply(s, (MPIDefaultReply *)&reply);
}

static void mptsas_process_event_notification(MPTSASState *s,
                                              MPIMsgEventNotify *req)
{
    MPIMsgEventNotifyReply reply;

    mptsas_fix_event_notification_endianness(req);

    QEMU_BUILD_BUG_ON(MPTSAS_MAX_REQUEST_SIZE < sizeof(*req));
    QEMU_BUILD_BUG_ON(sizeof(s->doorbell_msg) < sizeof(*req));
    QEMU_BUILD_BUG_ON(sizeof(s->doorbell_reply) < sizeof(reply));

    /* Don't even bother storing whether event notification is enabled,
     * since it is not accessible.
     */

    memset(&reply, 0, sizeof(reply));
    reply.EventDataLength = sizeof(reply.Data) / 4;
    reply.MsgLength       = sizeof(reply) / 4;
    reply.Function        = req->Function;

    /* This is set because events are sent through the reply FIFOs.  */
    reply.MsgFlags        = MPI_MSGFLAGS_CONTINUATION_REPLY;

    reply.MsgContext      = req->MsgContext;
    reply.Event           = MPI_EVENT_EVENT_CHANGE;
    reply.Data[0]         = !!req->Switch;

    mptsas_fix_event_notification_reply_endianness(&reply);
    mptsas_reply(s, (MPIDefaultReply *)&reply);
}

static void mptsas_fw_image(MPTSASState *s, uint8_t *image)
{
    uint32_t checksum = 0;
    unsigned i;

    /* The IOC runs directly in the model; its image contains only metadata. */
    memset(image, 0, MPT_FW_IMAGE_SIZE);
    stl_le_p(image + 0x04, MPI_FW_HEADER_SIGNATURE_0);
    stl_le_p(image + 0x08, MPI_FW_HEADER_SIGNATURE_1);
    stl_le_p(image + 0x0c, MPI_FW_HEADER_SIGNATURE_2);
    stw_le_p(image + 0x20, PCI_VENDOR_ID_LSI_LOGIC);
    stw_le_p(image + 0x22, mptsas_is_spi(s) ? MPTSPI1030_PRODUCT_ID :
                                            MPTSAS1068_PRODUCT_ID);
    stl_le_p(image + 0x24, MPT_FW_VERSION);
    stl_le_p(image + 0x2c, MPI_FW_HEADER_SIZE);
    stl_le_p(image + 0x40, MPI_FW_HEADER_WHAT_SIGNATURE);
    memcpy(image + 0x44, "Fusion-MPT emulation", 20);
    stl_le_p(image + 0x64, MPI_FW_HEADER_WHAT_SIGNATURE);
    memcpy(image + 0x68, "QEMU", 5);
    if (!mptsas_is_spi(s)) {
        uint8_t *ext = image + MPI_FW_HEADER_SIZE;
        const MPTNvdataImage nvdata = {
            .header = {
                .signature = cpu_to_le32(MPT_NVDATA_HEADER_SIGNATURE),
                .state = MPT_NVDATA_STATE_VALID,
                .total_bytes = cpu_to_le16(sizeof(nvdata)),
                .nvdata_version = cpu_to_le16(MPT_NVDATA_VERSION),
                .mpi_version = cpu_to_le16(MPT_MSG_VERSION),
                .header_words = sizeof(MPTNvdataHeader) / 4,
                .directory_entry_words = sizeof(MPTNvdataDirectoryEntry) / 4,
                .persistent_header_words =
                    sizeof(MPTNvdataPersistentHeader) / 4,
                .product_id_words = sizeof(MPTNvdataProductId) / 4,
            },
            .product_id = {
                .signature = cpu_to_le32(MPT_NVDATA_PRODUCT_SIGNATURE),
                .vendor = "QEMU",
                .product = "QEMU MPT Fusion",
                .revision = "2.5",
            },
        };

        stl_le_p(image + 0x10, MPT_NVDATA_VERSION);
        stl_le_p(image + 0x30, MPI_FW_HEADER_SIZE);
        ext[0] = MPI_EXT_IMAGE_TYPE_NVDATA;
        stl_le_p(ext + 8, MPI_EXT_IMAGE_HEADER_SIZE + MPT_NVDATA_SIZE);

        memcpy(ext + MPI_EXT_IMAGE_HEADER_SIZE, &nvdata, sizeof(nvdata));
        for (i = 0; i < MPI_EXT_IMAGE_HEADER_SIZE + MPT_NVDATA_SIZE; i += 4) {
            checksum += ldl_le_p(ext + i);
        }
        stl_le_p(ext + 4, -checksum);
        checksum = 0;
    }
    for (i = 0; i < MPI_FW_HEADER_SIZE; i += 4) {
        checksum += ldl_le_p(image + i);
    }
    stl_le_p(image + 0x1c, -checksum);
}

static void mptsas_process_fw_upload(MPTSASState *s, MPIMsgFWUpload *req)
{
    MPIMsgFWUploadReply reply = {
        .ImageType = req->ImageType,
        .MsgLength = sizeof(reply) / 4,
        .Function = req->Function,
        .MsgContext = req->MsgContext,
    };
    uint8_t metadata[MPT_FW_IMAGE_SIZE];
    const uint8_t *image = s->fw_image;
    size_t image_size = mptsas_fw_image_size(s);
    uint32_t flags, offset = 0, length = 0;
    uint32_t transfer_length;
    dma_addr_t address = 0;
    uint16_t status = MPI_IOCSTATUS_SUCCESS;

    QEMU_BUILD_BUG_ON(MPTSAS_MAX_REQUEST_SIZE < sizeof(*req));
    QEMU_BUILD_BUG_ON(sizeof(s->doorbell_reply) < sizeof(reply));

    if (req->ImageType != MPI_FW_UPLOAD_ITYPE_FW_IOC_MEM &&
        req->ImageType != MPI_FW_UPLOAD_ITYPE_FW_FLASH) {
        status = MPI_IOCSTATUS_INVALID_FIELD;
        goto done;
    }
    if (s->doorbell_state == DOORBELL_WRITE &&
        s->doorbell_cnt * 4 < offsetof(MPIMsgFWUpload, SGL) + 8) {
        status = MPI_IOCSTATUS_INVALID_SGL;
        goto done;
    }
    offset = le32_to_cpu(req->TC.ImageOffset);
    length = le32_to_cpu(req->TC.ImageSize);
    flags = le32_to_cpu(req->SGL.FlagsLength);
    if (req->ChainOffset || req->TC.ContextSize ||
        req->TC.DetailsLength != 12 || req->TC.Flags ||
        (flags & (MPI_SGE_FLAGS_ELEMENT_TYPE_MASK |
                  MPI_SGE_FLAGS_LOCAL_ADDRESS | MPI_SGE_FLAGS_DIRECTION)) !=
            MPI_SGE_FLAGS_SIMPLE_ELEMENT ||
        ((flags & MPI_SGE_FLAGS_64_BIT_ADDRESSING) &&
         s->doorbell_state == DOORBELL_WRITE &&
         s->doorbell_cnt * 4 < sizeof(*req))) {
        status = MPI_IOCSTATUS_INVALID_SGL;
        goto done;
    }
    if (offset > image_size) {
        status = MPI_IOCSTATUS_INVALID_FIELD;
        goto done;
    }
    transfer_length = MIN(length, image_size - offset);
    if ((flags & MPI_SGE_LENGTH_MASK) < transfer_length) {
        status = MPI_IOCSTATUS_INVALID_SGL;
        goto done;
    }
    address = flags & MPI_SGE_FLAGS_64_BIT_ADDRESSING ?
              le64_to_cpu(req->SGL.u.Address64) :
              le32_to_cpu(req->SGL.u.Address32);
    if (!image) {
        mptsas_fw_image(s, metadata);
        image = metadata;
    }
    if (pci_dma_write(PCI_DEVICE(s), address, image + offset,
                      transfer_length) != MEMTX_OK) {
        status = MPI_IOCSTATUS_INTERNAL_ERROR;
        goto done;
    }
    reply.ActualImageSize = cpu_to_le32(image_size);
done:
    trace_mptsas_process_fw_upload(s, req->ImageType, offset, length,
                                  address, status);
    reply.IOCStatus = cpu_to_le16(status);
    mptsas_reply(s, (MPIDefaultReply *)&reply);
}

static bool mptsas_fw_image_valid(const uint8_t *image, uint32_t size)
{
    uint32_t offset = 0, length, next;

    if (size < MPI_FW_HEADER_SIZE || size > MPT_FW_IMAGE_MAX_SIZE ||
        (size & 3) ||
        (uint32_t)ldl_le_p(image + 4) != MPI_FW_HEADER_SIGNATURE_0 ||
        (uint32_t)ldl_le_p(image + 8) != MPI_FW_HEADER_SIGNATURE_1 ||
        (uint32_t)ldl_le_p(image + 12) != MPI_FW_HEADER_SIGNATURE_2 ||
        lduw_le_p(image + 0x20) != PCI_VENDOR_ID_LSI_LOGIC) {
        return false;
    }
    length = ldl_le_p(image + 0x2c);
    next = ldl_le_p(image + 0x30);
    for (;;) {
        uint32_t checksum = 0;
        uint32_t i;

        if (length < (offset ? MPI_EXT_IMAGE_HEADER_SIZE :
                               MPI_FW_HEADER_SIZE) ||
            (length & 3) || length > size - offset) {
            return false;
        }
        for (i = 0; i < length; i += 4) {
            checksum += ldl_le_p(image + offset + i);
        }
        if (checksum) {
            return false;
        }
        if (!next) {
            return true;
        }
        if ((next & 3) || next < offset + length ||
            next > size - MPI_EXT_IMAGE_HEADER_SIZE) {
            return false;
        }
        offset = next;
        length = ldl_le_p(image + offset + 8);
        next = ldl_le_p(image + offset + 12);
    }
}

static void mptsas_process_fw_download(MPTSASState *s, MPIMsgFWDownload *req)
{
    MPIDefaultReply reply = {
        .Reserved = { req->ImageType, 0 },
        .MsgLength = sizeof(reply) / 4,
        .Function = req->Function,
        .MsgContext = req->MsgContext,
    };
    uint32_t length = le32_to_cpu(req->TC.ImageSize);
    uint32_t flags = le32_to_cpu(req->SGL.FlagsLength);
    uint16_t status = MPI_IOCSTATUS_SUCCESS;
    dma_addr_t address = 0;
    g_autofree uint8_t *image = NULL;

    if (req->ImageType != MPI_FW_DOWNLOAD_ITYPE_FW ||
        req->MsgFlags != MPI_FW_DOWNLOAD_LAST_SEGMENT || req->TC.ImageOffset ||
        length < MPI_FW_HEADER_SIZE || length > MPT_FW_IMAGE_MAX_SIZE) {
        status = MPI_IOCSTATUS_INVALID_FIELD;
        goto done;
    }
    if (req->ChainOffset || req->TC.ContextSize ||
        req->TC.DetailsLength != 12 || req->TC.Flags ||
        (flags & (MPI_SGE_FLAGS_ELEMENT_TYPE_MASK |
                  MPI_SGE_FLAGS_LOCAL_ADDRESS | MPI_SGE_FLAGS_DIRECTION)) !=
            (MPI_SGE_FLAGS_SIMPLE_ELEMENT | MPI_SGE_FLAGS_HOST_TO_IOC) ||
        (flags & MPI_SGE_LENGTH_MASK) < length ||
        (s->doorbell_state == DOORBELL_WRITE &&
         s->doorbell_cnt * 4 < offsetof(MPIMsgFWDownload, SGL) +
             (flags & MPI_SGE_FLAGS_64_BIT_ADDRESSING ? 12 : 8))) {
        status = MPI_IOCSTATUS_INVALID_SGL;
        goto done;
    }
    address = flags & MPI_SGE_FLAGS_64_BIT_ADDRESSING ?
              le64_to_cpu(req->SGL.u.Address64) :
              le32_to_cpu(req->SGL.u.Address32);
    image = g_malloc(length);
    if (pci_dma_read(PCI_DEVICE(s), address, image, length) != MEMTX_OK) {
        status = MPI_IOCSTATUS_INTERNAL_ERROR;
        goto done;
    }
    if (!mptsas_fw_image_valid(image, length)) {
        status = MPI_IOCSTATUS_INVALID_FIELD;
        goto done;
    }
    /* Retain flash contents for upload; the IOC runs directly in the model. */
    g_free(s->fw_image);
    s->fw_image = g_steal_pointer(&image);
    s->fw_image_size = length;
done:
    trace_mptsas_process_fw_download(s, req->ImageType, length, address,
                                    status);
    reply.IOCStatus = cpu_to_le16(status);
    mptsas_reply(s, &reply);
}

static void mptsas_process_toolbox(MPTSASState *s, MPIMsgToolboxClean *req)
{
    const uint32_t regions = MPI_TOOLBOX_CLEAN_NVSRAM |
        MPI_TOOLBOX_CLEAN_SEEPROM | MPI_TOOLBOX_CLEAN_BOOTLOADER |
        MPI_TOOLBOX_CLEAN_FW_BACKUP | MPI_TOOLBOX_CLEAN_OTHER_PERSIST_PAGES |
        MPI_TOOLBOX_CLEAN_BOOT_SERVICES |
        MPI_TOOLBOX_CLEAN_PERSIST_MANUFACT_PAGES;
    uint32_t flags = le32_to_cpu(req->Flags);
    uint16_t status = MPI_IOCSTATUS_SUCCESS;
    MPIDefaultReply reply = {
        .Reserved = { req->Tool, 0 },
        .MsgLength = sizeof(reply) / 4,
        .Function = req->Function,
        .MsgContext = req->MsgContext,
    };

    /* Clean stored configuration data and empty auxiliary regions. */
    if (req->Tool != MPI_TOOLBOX_CLEAN_TOOL || req->ChainOffset ||
        (s->doorbell_state == DOORBELL_WRITE &&
         s->doorbell_cnt * 4 < sizeof(*req)) || (flags & ~regions)) {
        status = MPI_IOCSTATUS_INVALID_FIELD;
    } else {
        const unsigned manufacturing =
            (1 << MPTSAS_CONFIG_MANUFACTURING_1) |
            (1 << MPTSAS_CONFIG_MANUFACTURING_4);
        unsigned i;
        unsigned clear = 0;

        if (flags & MPI_TOOLBOX_CLEAN_PERSIST_MANUFACT_PAGES) {
            clear |= manufacturing;
        } else if (flags & MPI_TOOLBOX_CLEAN_SEEPROM) {
            clear |= 1 << MPTSAS_CONFIG_MANUFACTURING_1;
        }
        if (flags & MPI_TOOLBOX_CLEAN_OTHER_PERSIST_PAGES) {
            clear |= MPTSAS_CONFIG_PAGE_MASK & ~manufacturing;
        }
        for (i = 0; i < ARRAY_SIZE(s->config_nvram); i++) {
            if (clear & (1 << i)) {
                memset(s->config_nvram[i], 0, sizeof(s->config_nvram[i]));
                s->config_nvram_written &= ~(1 << i);
            }
        }
    }
    reply.IOCStatus = cpu_to_le16(status);
    trace_mptsas_process_toolbox(s, req->Tool, flags, status);
    mptsas_reply(s, &reply);
}

static void mptsas_process_sas_control(MPTSASState *s,
                                     MPIMsgSASIOUnitControl *req)
{
    MPIDefaultReply reply = {
        .Reserved = { req->Operation, 0 },
        .MsgLength = sizeof(reply) / 4,
        .Function = req->Function,
        .MsgContext = req->MsgContext,
    };
    uint16_t status = MPI_IOCSTATUS_SUCCESS;

    memcpy(reply.Reserved1, &req->DevHandle, sizeof(reply.Reserved1));
    if (mptsas_is_spi(s)) {
        status = MPI_IOCSTATUS_INVALID_FUNCTION;
    } else if (req->ChainOffset ||
               (s->doorbell_state == DOORBELL_WRITE &&
                s->doorbell_cnt * 4 < sizeof(*req))) {
        status = MPI_IOCSTATUS_INVALID_FIELD;
    } else if (req->Operation != MPI_SAS_OP_CLEAR_NOT_PRESENT &&
               req->Operation != MPI_SAS_OP_CLEAR_ALL_PERSISTENT) {
        status = MPI_IOCSTATUS_INVALID_FIELD;
    }
    /* The persistent mapping table is empty; target IDs follow the bus. */
    reply.IOCStatus = cpu_to_le16(status);
    trace_mptsas_process_sas_control(s, req->Operation, status);
    mptsas_reply(s, &reply);
}

static void mptsas_process_sep(MPTSASState *s, MPIMsgSEP *req)
{
    MPIMsgSEPReply reply = {
        .TargetID = req->TargetID,
        .Bus = req->Bus,
        .MsgLength = sizeof(reply) / 4,
        .Function = req->Function,
        .Action = req->Action,
        .MsgContext = req->MsgContext,
        .Slot = req->Slot,
        .EnclosureHandle = req->EnclosureHandle,
    };
    uint16_t status = MPI_IOCSTATUS_SUCCESS;
    uint32_t indicators = le32_to_cpu(req->SlotStatus);
    unsigned first = mptsas_first_slot(s);
    unsigned slot;

    if (mptsas_is_spi(s)) {
        status = MPI_IOCSTATUS_INVALID_FUNCTION;
        goto done;
    }
    if (req->ChainOffset || req->Flags > MPI_SEP_ENCLOSURE_SLOT_ADDRESS ||
        req->Action > MPI_SEP_ACTION_READ_STATUS ||
        (s->doorbell_state == DOORBELL_WRITE &&
         s->doorbell_cnt * 4 < sizeof(*req))) {
        status = MPI_IOCSTATUS_INVALID_FIELD;
        goto done;
    }
    if (req->Flags == MPI_SEP_ENCLOSURE_SLOT_ADDRESS) {
        slot = le16_to_cpu(req->Slot) - first;
        if (le16_to_cpu(req->EnclosureHandle) != MPTSAS_ENCLOSURE_HANDLE ||
            slot >= MPTSAS_NUM_PORTS) {
            status = MPI_IOCSTATUS_INVALID_FIELD;
            goto done;
        }
    } else {
        slot = req->TargetID;
        if (req->Bus || slot >= MPTSAS_NUM_PORTS ||
            !scsi_device_find(&s->bus, 0, slot, 0)) {
            status = req->Bus ? MPI_IOCSTATUS_SCSI_INVALID_BUS :
                               MPI_IOCSTATUS_SCSI_INVALID_TARGETID;
            goto done;
        }
    }
    if (req->Action == MPI_SEP_ACTION_WRITE_STATUS) {
        if (indicators & ~MPTSAS_ENCLOSURE_INDICATORS) {
            status = MPI_IOCSTATUS_INVALID_FIELD;
            goto done;
        }
        s->enclosure_status[slot] = indicators;
    }
    reply.Slot = cpu_to_le16(slot + first);
    reply.EnclosureHandle = cpu_to_le16(MPTSAS_ENCLOSURE_HANDLE);
    reply.SlotStatus = cpu_to_le32(s->enclosure_status[slot]);
done:
    reply.IOCStatus = cpu_to_le16(status);
    mptsas_reply(s, (MPIDefaultReply *)&reply);
}

static void mptsas_process_message(MPTSASState *s, MPIRequestHeader *req)
{
    MPIDefaultReply reply;

    trace_mptsas_process_message(s, req->Function, req->MsgContext);
    switch (req->Function) {
    case MPI_FUNCTION_SCSI_TASK_MGMT:
        mptsas_process_scsi_task_mgmt(s, (MPIMsgSCSITaskMgmt *)req);
        break;

    case MPI_FUNCTION_IOC_INIT:
        mptsas_process_ioc_init(s, (MPIMsgIOCInit *)req);
        break;

    case MPI_FUNCTION_IOC_FACTS:
        mptsas_process_ioc_facts(s, (MPIMsgIOCFacts *)req);
        break;

    case MPI_FUNCTION_PORT_FACTS:
        mptsas_process_port_facts(s, (MPIMsgPortFacts *)req);
        break;

    case MPI_FUNCTION_PORT_ENABLE:
        mptsas_process_port_enable(s, (MPIMsgPortEnable *)req);
        break;

    case MPI_FUNCTION_EVENT_NOTIFICATION:
        mptsas_process_event_notification(s, (MPIMsgEventNotify *)req);
        break;

    case MPI_FUNCTION_CONFIG:
        mptsas_process_config(s, (MPIMsgConfig *)req);
        break;

    case MPI_FUNCTION_FW_UPLOAD:
        mptsas_process_fw_upload(s, (MPIMsgFWUpload *)req);
        break;

    case MPI_FUNCTION_FW_DOWNLOAD:
        mptsas_process_fw_download(s, (MPIMsgFWDownload *)req);
        break;

    case MPI_FUNCTION_TOOLBOX:
        mptsas_process_toolbox(s, (MPIMsgToolboxClean *)req);
        break;

    case MPI_FUNCTION_SAS_IO_UNIT_CONTROL:
        mptsas_process_sas_control(s, (MPIMsgSASIOUnitControl *)req);
        break;

    case MPI_FUNCTION_SCSI_ENCLOSURE_PROCESSOR:
        mptsas_process_sep(s, (MPIMsgSEP *)req);
        break;

    default:
        trace_mptsas_unhandled_cmd(s, le32_to_cpu(req->MsgContext),
                                  req->Function);
        /* An unsupported message fails without faulting the controller. */
        memset(&reply, 0, sizeof(reply));
        reply.MsgLength = sizeof(reply) / 4;
        reply.Function = req->Function;
        reply.MsgContext = req->MsgContext;
        reply.IOCStatus = cpu_to_le16(MPI_IOCSTATUS_INVALID_FUNCTION);
        mptsas_reply(s, &reply);
        break;
    }
}

static void mptsas_fetch_request(MPTSASState *s)
{
    PCIDevice *pci = (PCIDevice *) s;
    char req[MPTSAS_MAX_REQUEST_SIZE];
    MPIRequestHeader *hdr = (MPIRequestHeader *)req;
    hwaddr addr;
    int size;

    /* Read the message header from the guest first. */
    addr = s->host_mfa_high_addr | MPTSAS_FIFO_GET(s, request_post);
    pci_dma_read(pci, addr, req, sizeof(*hdr));

    if (hdr->Function < ARRAY_SIZE(mpi_request_sizes) &&
        mpi_request_sizes[hdr->Function]) {
        /* Read the rest of the request based on the type.  Do not
         * reread everything, as that could cause a TOC/TOU mismatch
         * and leak data from the QEMU stack.
         */
        size = mpi_request_sizes[hdr->Function];
        assert(size <= MPTSAS_MAX_REQUEST_SIZE);
        pci_dma_read(pci, addr + sizeof(*hdr), &req[sizeof(*hdr)],
                     size - sizeof(*hdr));
    }

    if (hdr->Function == MPI_FUNCTION_SCSI_IO_REQUEST) {
        /* SCSI I/O requests are separate from mptsas_process_message
         * because they cannot be sent through the doorbell yet.
         */
        mptsas_process_scsi_io_request(s, (MPIMsgSCSIIORequest *)req, addr);
    } else {
        mptsas_process_message(s, (MPIRequestHeader *)req);
    }
}

static void mptsas_fetch_requests(void *opaque)
{
    MPTSASState *s = opaque;

    if (s->state != MPI_IOC_STATE_OPERATIONAL) {
        mptsas_set_fault(s, MPI_IOCSTATUS_INVALID_STATE);
        return;
    }
    while (!MPTSAS_FIFO_EMPTY(s, request_post)) {
        mptsas_fetch_request(s);
    }
}

static void mptsas_soft_reset(MPTSASState *s)
{
    uint32_t save_mask;

    trace_mptsas_reset(s);

    timer_del(s->coalescing_timer);
    s->coalescing_count = 0;
    s->reply_irq_ready = false;
    /* Temporarily disable interrupts */
    save_mask = s->intr_mask;
    s->intr_mask = MPI_HIM_DIM | MPI_HIM_RIM;
    mptsas_update_interrupt(s);

    bus_cold_reset(BUS(&s->bus));
    s->intr_status = 0;
    s->intr_mask = save_mask;

    s->reply_free_tail = 0;
    s->reply_free_head = 0;
    s->reply_post_tail = 0;
    s->reply_post_head = 0;
    s->request_post_tail = 0;
    s->request_post_head = 0;
    memset(s->enclosure_status, 0, sizeof(s->enclosure_status));
    qemu_bh_cancel(s->request_bh);

    /* Discard an unfinished handshake along with the message FIFOs. */
    s->doorbell_state = DOORBELL_NONE;
    s->doorbell_idx = 0;
    s->doorbell_cnt = 0;
    s->doorbell_reply_idx = 0;
    s->doorbell_reply_size = 0;

    s->state = MPI_IOC_STATE_READY;
}

static uint32_t mptsas_doorbell_read(MPTSASState *s)
{
    uint32_t ret;

    ret = (s->who_init << MPI_DOORBELL_WHO_INIT_SHIFT) & MPI_DOORBELL_WHO_INIT_MASK;
    ret |= s->state;
    switch (s->doorbell_state) {
    case DOORBELL_NONE:
        break;

    case DOORBELL_WRITE:
        ret |= MPI_DOORBELL_ACTIVE;
        break;

    case DOORBELL_READ:
        /* Get rid of the IOC fault code.  */
        ret &= ~MPI_DOORBELL_DATA_MASK;

        assert(s->intr_status & MPI_HIS_DOORBELL_INTERRUPT);
        assert(s->doorbell_reply_idx <= s->doorbell_reply_size);

        ret |= MPI_DOORBELL_ACTIVE;
        if (s->doorbell_reply_idx < s->doorbell_reply_size) {
            ret |= le16_to_cpu(s->doorbell_reply[s->doorbell_reply_idx++]);
        }
        break;

    default:
        abort();
    }

    return ret;
}

static void mptsas_doorbell_write(MPTSASState *s, uint32_t val)
{
    if (s->doorbell_state == DOORBELL_WRITE) {
        if (s->doorbell_idx < s->doorbell_cnt) {
            s->doorbell_msg[s->doorbell_idx++] = cpu_to_le32(val);
            if (s->doorbell_idx == s->doorbell_cnt) {
                mptsas_process_message(s, (MPIRequestHeader *)s->doorbell_msg);
            }
        }
        return;
    }

    switch ((val & MPI_DOORBELL_FUNCTION_MASK) >> MPI_DOORBELL_FUNCTION_SHIFT) {
    case MPI_FUNCTION_IOC_MESSAGE_UNIT_RESET:
    case MPI_FUNCTION_IO_UNIT_RESET:
        /* Both requests use the model's existing soft-reset path. */
        mptsas_soft_reset(s);
        break;
    case MPI_FUNCTION_HANDSHAKE:
        s->doorbell_state = DOORBELL_WRITE;
        s->doorbell_idx = 0;
        s->doorbell_cnt = (val & MPI_DOORBELL_ADD_DWORDS_MASK)
            >> MPI_DOORBELL_ADD_DWORDS_SHIFT;
        s->intr_status |= MPI_HIS_DOORBELL_INTERRUPT;
        mptsas_update_interrupt(s);
        break;
    default:
        trace_mptsas_unhandled_doorbell_cmd(s, val);
        break;
    }
}

static void mptsas_write_sequence_write(MPTSASState *s, uint32_t val)
{
    /* If the diagnostic register is enabled, any write to this register
     * will disable it.  Otherwise, the guest has to do a magic five-write
     * sequence.
     */
    if (s->diagnostic & MPI_DIAG_DRWE) {
        goto disable;
    }

    switch (s->diagnostic_idx) {
    case 0:
        if ((val & MPI_WRSEQ_KEY_VALUE_MASK) != MPI_WRSEQ_1ST_KEY_VALUE) {
            goto disable;
        }
        break;
    case 1:
        if ((val & MPI_WRSEQ_KEY_VALUE_MASK) != MPI_WRSEQ_2ND_KEY_VALUE) {
            goto disable;
        }
        break;
    case 2:
        if ((val & MPI_WRSEQ_KEY_VALUE_MASK) != MPI_WRSEQ_3RD_KEY_VALUE) {
            goto disable;
        }
        break;
    case 3:
        if ((val & MPI_WRSEQ_KEY_VALUE_MASK) != MPI_WRSEQ_4TH_KEY_VALUE) {
            goto disable;
        }
        break;
    case 4:
        if ((val & MPI_WRSEQ_KEY_VALUE_MASK) != MPI_WRSEQ_5TH_KEY_VALUE) {
            goto disable;
        }
        /* Prepare Spaceball One for departure, and change the
         * combination on my luggage!
         */
        s->diagnostic |= MPI_DIAG_DRWE;
        break;
    }
    s->diagnostic_idx++;
    return;

disable:
    s->diagnostic &= ~MPI_DIAG_DRWE;
    s->diagnostic_idx = 0;
}

static int mptsas_hard_reset(MPTSASState *s)
{
    mptsas_soft_reset(s);

    memcpy(s->config_current, s->config_nvram,
           sizeof(s->config_current));
    s->config_current_written = s->config_nvram_written;

    s->who_init = MPI_WHOINIT_NO_ONE;
    s->doorbell_state = DOORBELL_NONE;
    memset(s->doorbell_msg, 0, sizeof(s->doorbell_msg));
    s->doorbell_idx = 0;
    s->doorbell_cnt = 0;
    memset(s->doorbell_reply, 0, sizeof(s->doorbell_reply));
    s->doorbell_reply_idx = 0;
    s->doorbell_reply_size = 0;

    s->diagnostic_idx = 0;
    s->diagnostic = 0;
    s->intr_mask = MPI_HIM_DIM | MPI_HIM_RIM;

    memset(s->request_post, 0, sizeof(s->request_post));
    memset(s->reply_post, 0, sizeof(s->reply_post));
    memset(s->reply_free, 0, sizeof(s->reply_free));

    s->host_mfa_high_addr = 0;
    s->sense_buffer_high_addr = 0;
    s->reply_frame_size = 0;
    s->max_devices = mptsas_max_devices(s);
    s->max_buses = 1;
    s->ioc1_flags = 0;
    s->ioc1_coalescing_timeout = 0;
    s->ioc1_coalescing_depth = 0;
    s->spi_port_configuration = MPTSPI_DEFAULT_PORT_CONFIGURATION;
    s->spi_port_on_bus_timer = 0;
    memset(s->spi_requested_params, 0, sizeof(s->spi_requested_params));
    memset(s->spi_configuration, 0, sizeof(s->spi_configuration));

    mptsas_update_interrupt(s);

    return 0;
}

static void mptsas_interrupt_status_write(MPTSASState *s)
{
    switch (s->doorbell_state) {
    case DOORBELL_NONE:
    case DOORBELL_WRITE:
        s->intr_status &= ~MPI_HIS_DOORBELL_INTERRUPT;
        break;

    case DOORBELL_READ:
        assert(s->intr_status & MPI_HIS_DOORBELL_INTERRUPT);
        /* Acknowledge this word before raising the next doorbell interrupt. */
        s->intr_status &= ~MPI_HIS_DOORBELL_INTERRUPT;
        mptsas_update_interrupt(s);
        if (s->doorbell_reply_idx == s->doorbell_reply_size) {
            s->doorbell_state = DOORBELL_NONE;
        }
        s->intr_status |= MPI_HIS_DOORBELL_INTERRUPT;
        break;

    default:
        abort();
    }
    mptsas_update_interrupt(s);
}

static uint32_t mptsas_reply_post_read(MPTSASState *s)
{
    uint32_t ret;

    if (!MPTSAS_FIFO_EMPTY(s, reply_post)) {
        ret = MPTSAS_FIFO_GET(s, reply_post);
    } else {
        ret = -1;
        s->intr_status &= ~MPI_HIS_REPLY_MESSAGE_INTERRUPT;
        timer_del(s->coalescing_timer);
        s->coalescing_count = 0;
        s->reply_irq_ready = false;
        mptsas_update_interrupt(s);
    }

    return ret;
}

static uint64_t mptsas_mmio_read(void *opaque, hwaddr addr,
                                  unsigned size)
{
    MPTSASState *s = opaque;
    uint32_t ret = 0;

    switch (addr & ~3) {
    case MPI_DOORBELL_OFFSET:
        ret = mptsas_doorbell_read(s);
        break;

    case MPI_DIAGNOSTIC_OFFSET:
        ret = s->diagnostic;
        break;

    case MPI_HOST_INTERRUPT_STATUS_OFFSET:
        ret = s->intr_status;
        break;

    case MPI_HOST_INTERRUPT_MASK_OFFSET:
        ret = s->intr_mask;
        break;

    case MPI_REPLY_POST_FIFO_OFFSET:
        ret = mptsas_reply_post_read(s);
        break;

    default:
        trace_mptsas_mmio_unhandled_read(s, addr);
        break;
    }
    trace_mptsas_mmio_read(s, addr, ret);
    return ret;
}

static void mptsas_mmio_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned size)
{
    MPTSASState *s = opaque;

    trace_mptsas_mmio_write(s, addr, val);
    switch (addr) {
    case MPI_DOORBELL_OFFSET:
        mptsas_doorbell_write(s, val);
        break;

    case MPI_WRITE_SEQUENCE_OFFSET:
        mptsas_write_sequence_write(s, val);
        break;

    case MPI_DIAGNOSTIC_OFFSET:
        if (val & MPI_DIAG_RESET_ADAPTER) {
            mptsas_hard_reset(s);
        }
        break;

    case MPI_HOST_INTERRUPT_STATUS_OFFSET:
        mptsas_interrupt_status_write(s);
        break;

    case MPI_HOST_INTERRUPT_MASK_OFFSET:
        s->intr_mask = val & (MPI_HIM_RIM | MPI_HIM_DIM);
        mptsas_update_interrupt(s);
        break;

    case MPI_REQUEST_POST_FIFO_OFFSET:
        if (MPTSAS_FIFO_FULL(s, request_post)) {
            mptsas_set_fault(s, MPI_IOCSTATUS_INSUFFICIENT_RESOURCES);
        } else {
            MPTSAS_FIFO_PUT(s, request_post, val & ~0x03);
            qemu_bh_schedule(s->request_bh);
        }
        break;

    case MPI_REPLY_FREE_FIFO_OFFSET:
        if (MPTSAS_FIFO_FULL(s, reply_free)) {
            mptsas_set_fault(s, MPI_IOCSTATUS_INSUFFICIENT_RESOURCES);
        } else {
            MPTSAS_FIFO_PUT(s, reply_free, val);
        }
        break;

    default:
        trace_mptsas_mmio_unhandled_write(s, addr, val);
        break;
    }
}

static const MemoryRegionOps mptsas_mmio_ops = {
    .read = mptsas_mmio_read,
    .write = mptsas_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    }
};

static const MemoryRegionOps mptsas_port_ops = {
    .read = mptsas_mmio_read,
    .write = mptsas_mmio_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    }
};

static uint64_t mptsas_diag_read(void *opaque, hwaddr addr,
                                   unsigned size)
{
    MPTSASState *s = opaque;
    trace_mptsas_diag_read(s, addr, 0);
    return 0;
}

static void mptsas_diag_write(void *opaque, hwaddr addr,
                               uint64_t val, unsigned size)
{
    MPTSASState *s = opaque;
    trace_mptsas_diag_write(s, addr, val);
}

static const MemoryRegionOps mptsas_diag_ops = {
    .read = mptsas_diag_read,
    .write = mptsas_diag_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .impl = {
        .min_access_size = 4,
        .max_access_size = 4,
    }
};

static uint64_t mptsas_pci_rom_read(void *opaque, hwaddr addr,
                                    unsigned int size)
{
    return UINT64_MAX;
}

static void mptsas_pci_rom_write(void *opaque, hwaddr addr, uint64_t value,
                                 unsigned int size)
{
}

static const MemoryRegionOps mptsas_pci_rom_ops = {
    .read = mptsas_pci_rom_read,
    .write = mptsas_pci_rom_write,
    .endianness = DEVICE_LITTLE_ENDIAN,
    .valid = {
        .min_access_size = 1,
        .max_access_size = 8,
        .unaligned = true,
    },
    .impl = {
        .min_access_size = 1,
        .max_access_size = 8,
        .unaligned = true,
    },
};

static QEMUSGList *mptsas_get_sg_list(SCSIRequest *sreq)
{
    MPTSASRequest *req = sreq->hba_private;

    return &req->qsg;
}

static void mptsas_command_complete(SCSIRequest *sreq,
        size_t resid)
{
    MPTSASRequest *req = sreq->hba_private;
    MPTSASState *s = req->dev;
    uint8_t sense_buf[SCSI_SENSE_BUF_SIZE];
    uint8_t sense_len;

    hwaddr sense_buffer_addr = req->dev->sense_buffer_high_addr |
            req->scsi_io.SenseBufferLowAddr;

    trace_mptsas_command_complete(s, req->scsi_io.MsgContext,
                                  sreq->status, resid);

    sense_len = scsi_req_get_sense(sreq, sense_buf, SCSI_SENSE_BUF_SIZE);
    if (sense_len > 0) {
        pci_dma_write(PCI_DEVICE(s), sense_buffer_addr, sense_buf,
                      MIN(req->scsi_io.SenseBufferLength, sense_len));
    }

    if (sreq->status != GOOD || resid ||
        req->dev->doorbell_state == DOORBELL_WRITE) {
        MPIMsgSCSIIOReply reply;

        memset(&reply, 0, sizeof(reply));
        reply.TargetID          = req->scsi_io.TargetID;
        reply.Bus               = req->scsi_io.Bus;
        reply.MsgLength         = sizeof(reply) / 4;
        reply.Function          = req->scsi_io.Function;
        reply.CDBLength         = req->scsi_io.CDBLength;
        reply.SenseBufferLength = req->scsi_io.SenseBufferLength;
        reply.MsgFlags          = req->scsi_io.MsgFlags;
        reply.MsgContext        = req->scsi_io.MsgContext;
        reply.SCSIStatus        = sreq->status;
        if (sreq->status == GOOD) {
            reply.TransferCount = req->scsi_io.DataLength - resid;
            if (resid) {
                reply.IOCStatus     = MPI_IOCSTATUS_SCSI_DATA_UNDERRUN;
            }
        } else {
            reply.SCSIState     = MPI_SCSI_STATE_AUTOSENSE_VALID;
            reply.SenseCount    = sense_len;
            reply.IOCStatus     = MPI_IOCSTATUS_SUCCESS;
        }

        mptsas_fix_scsi_io_reply_endianness(&reply);
        mptsas_post_reply(req->dev, (MPIDefaultReply *)&reply);
    } else {
        mptsas_turbo_reply(req->dev, req->scsi_io.MsgContext);
    }

    mptsas_free_request(req);
}

static void mptsas_request_cancelled(SCSIRequest *sreq)
{
    MPTSASRequest *req = sreq->hba_private;
    MPIMsgSCSIIOReply reply;

    memset(&reply, 0, sizeof(reply));
    reply.TargetID          = req->scsi_io.TargetID;
    reply.Bus               = req->scsi_io.Bus;
    reply.MsgLength         = sizeof(reply) / 4;
    reply.Function          = req->scsi_io.Function;
    reply.CDBLength         = req->scsi_io.CDBLength;
    reply.SenseBufferLength = req->scsi_io.SenseBufferLength;
    reply.MsgFlags          = req->scsi_io.MsgFlags;
    reply.MsgContext        = req->scsi_io.MsgContext;
    reply.SCSIState         = MPI_SCSI_STATE_NO_SCSI_STATUS;
    reply.IOCStatus         = MPI_IOCSTATUS_SCSI_TASK_TERMINATED;

    mptsas_fix_scsi_io_reply_endianness(&reply);
    mptsas_post_reply(req->dev, (MPIDefaultReply *)&reply);
    mptsas_free_request(req);
}

static void mptsas_save_request(QEMUFile *f, SCSIRequest *sreq)
{
    MPTSASRequest *req = sreq->hba_private;
    int i;

    qemu_put_buffer(f, (unsigned char *)&req->scsi_io, sizeof(req->scsi_io));
    qemu_put_be32(f, req->qsg.nsg);
    for (i = 0; i < req->qsg.nsg; i++) {
        qemu_put_be64(f, req->qsg.sg[i].base);
        qemu_put_be64(f, req->qsg.sg[i].len);
    }
}

static void *mptsas_load_request(QEMUFile *f, SCSIRequest *sreq)
{
    SCSIBus *bus = sreq->bus;
    MPTSASState *s = container_of(bus, MPTSASState, bus);
    PCIDevice *pci = PCI_DEVICE(s);
    MPTSASRequest *req;
    int i, n;

    req = g_new(MPTSASRequest, 1);
    qemu_get_buffer(f, (unsigned char *)&req->scsi_io, sizeof(req->scsi_io));

    n = qemu_get_be32(f);
    /* TODO: add a way for SCSIBusInfo's load_request to fail,
     * and fail migration instead of asserting here.
     * This is just one thing (there are probably more) that must be
     * fixed before we can allow NDEBUG compilation.
     */
    assert(n >= 0);

    pci_dma_sglist_init(&req->qsg, pci, n);
    for (i = 0; i < n; i++) {
        uint64_t base = qemu_get_be64(f);
        uint64_t len = qemu_get_be64(f);
        qemu_sglist_add(&req->qsg, base, len);
    }

    scsi_req_ref(sreq);
    req->sreq = sreq;
    req->dev = s;

    return req;
}

static const struct SCSIBusInfo mptsas_scsi_info = {
    .tcq = true,
    .max_target = MPTSAS_NUM_PORTS - 1,
    .max_lun = 1,

    .get_sg_list = mptsas_get_sg_list,
    .complete = mptsas_command_complete,
    .cancel = mptsas_request_cancelled,
    .save_request = mptsas_save_request,
    .load_request = mptsas_load_request,
};

static const struct SCSIBusInfo mptspi_scsi_info = {
    .tcq = true,
    .max_target = MPTSPI_MAX_TARGETS - 1,
    .max_lun = 255,

    .get_sg_list = mptsas_get_sg_list,
    .complete = mptsas_command_complete,
    .cancel = mptsas_request_cancelled,
    .save_request = mptsas_save_request,
    .load_request = mptsas_load_request,
};

static void mptsas_scsi_realize(PCIDevice *dev, Error **errp)
{
    MPTSASState *s = MPT_SAS(dev);
    Error *err = NULL;
    uint8_t memory_bar_type = PCI_BASE_ADDRESS_SPACE_MEMORY |
                              PCI_BASE_ADDRESS_MEM_TYPE_32;
    unsigned int diag_bar = 2;
    int ret;

    if (s->pci_rom_size && !is_power_of_2(s->pci_rom_size)) {
        error_setg(errp, "x-pci-rom-size must be zero or a power of two");
        return;
    }
    if (s->pci_rom_size && dev->romfile && dev->romfile[0]) {
        error_setg(errp, "x-pci-rom-size cannot be used with romfile");
        return;
    }

    dev->config[PCI_LATENCY_TIMER] = 0;
    dev->config[PCI_INTERRUPT_PIN] = 0x01;

    if (s->msi != ON_OFF_AUTO_OFF) {
        ret = msi_init(dev, 0, 1, true, false, &err);
        /* Any error other than -ENOTSUP(board's MSI support is broken)
         * is a programming error */
        assert(!ret || ret == -ENOTSUP);
        if (ret && s->msi == ON_OFF_AUTO_ON) {
            /* Can't satisfy user's explicit msi=on request, fail */
            error_append_hint(&err, "You have to use msi=auto (default) or "
                    "msi=off with this machine type.\n");
            error_propagate(errp, err);
            return;
        }
        assert(!err || s->msi == ON_OFF_AUTO_AUTO);
        /* With msi=auto, we fall back to MSI off silently */
        error_free(err);

        /* Only used for migration.  */
        s->msi_in_use = (ret == 0);
    }

    memory_region_init_io(&s->mmio_io, OBJECT(s), &mptsas_mmio_ops, s,
                          "mptsas-mmio", 0x4000);
    memory_region_init_io(&s->port_io, OBJECT(s), &mptsas_port_ops, s,
                          "mptsas-io", 256);
    memory_region_init_io(&s->diag_io, OBJECT(s), &mptsas_diag_ops, s,
                          "mptsas-diag", 0x10000);

    if (s->pci_64bit_bars) {
        memory_bar_type = PCI_BASE_ADDRESS_SPACE_MEMORY |
                          PCI_BASE_ADDRESS_MEM_TYPE_64;
        diag_bar = 3;
    }

    pci_register_bar(dev, 0, PCI_BASE_ADDRESS_SPACE_IO, &s->port_io);
    pci_register_bar(dev, 1, memory_bar_type, &s->mmio_io);
    pci_register_bar(dev, diag_bar, memory_bar_type, &s->diag_io);

    /* The optional expansion ROM aperture contains no option ROM image. */
    if (s->pci_rom_size) {
        memory_region_init_io(&s->pci_rom, OBJECT(s), &mptsas_pci_rom_ops, s,
                              "mptsas-pci-rom", s->pci_rom_size);
        pci_register_bar(dev, PCI_ROM_SLOT, 0, &s->pci_rom);
    }

    if (!mptsas_is_spi(s) && !s->sas_addr) {
        s->sas_addr = ((NAA_LOCALLY_ASSIGNED_ID << 24) |
                       IEEE_COMPANY_LOCALLY_ASSIGNED) << 36;
        s->sas_addr |= (pci_dev_bus_num(dev) << 16);
        s->sas_addr |= (PCI_SLOT(dev->devfn) << 8);
        s->sas_addr |= PCI_FUNC(dev->devfn) * (2 * MPTSAS_NUM_PORTS);
    }
    s->max_devices = mptsas_max_devices(s);

    s->coalescing_timer = timer_new_ns(QEMU_CLOCK_VIRTUAL,
                                      mptsas_coalescing_expired, s);
    s->request_bh = qemu_bh_new_guarded(mptsas_fetch_requests, s,
                                        &DEVICE(dev)->mem_reentrancy_guard);

    scsi_bus_init(&s->bus, sizeof(s->bus), &dev->qdev,
                  mptsas_is_spi(s) ? &mptspi_scsi_info : &mptsas_scsi_info);
}

static void mptsas_scsi_uninit(PCIDevice *dev)
{
    MPTSASState *s = MPT_SAS(dev);

    g_clear_pointer(&s->fw_image, g_free);

    timer_free(s->coalescing_timer);
    qemu_bh_delete(s->request_bh);
    msi_uninit(dev);
}

static void mptsas_reset(DeviceState *dev)
{
    MPTSASState *s = MPT_SAS(dev);

    mptsas_hard_reset(s);
}

static bool mptsas_load_legacy_reply_fifo(uint32_t *fifo, uint16_t *head,
                                        uint16_t *tail)
{
    uint32_t pending[MPTSAS_REPLY_QUEUE_DEPTH_V0];
    unsigned count = 0;

    if (*head > MPTSAS_REPLY_QUEUE_DEPTH_V0 ||
        *tail > MPTSAS_REPLY_QUEUE_DEPTH_V0) {
        return false;
    }
    /* Preserve FIFO order when the old ring wraps at a smaller index. */
    while (*head != *tail) {
        pending[count++] = fifo[*head];
        *head = (*head + 1) % (MPTSAS_REPLY_QUEUE_DEPTH_V0 + 1);
    }
    memcpy(fifo, pending, count * sizeof(*fifo));
    *head = 0;
    *tail = count;
    return true;
}

static int mptsas_post_load(void *opaque, int version_id)
{
    MPTSASState *s = opaque;
    const uint32_t spi_port_configuration_mask =
        MPI_SCSIPORTPAGE1_CFG_PORT_SCSI_ID_MASK |
        MPI_SCSIPORTPAGE1_CFG_PORT_RESPONSE_ID_MASK;
    uint8_t expected_variant;

    expected_variant = object_dynamic_cast(OBJECT(s), TYPE_LSI53C1030) ?
                       MPT_FUSION_VARIANT_LSI53C1030 :
                       MPT_FUSION_VARIANT_SAS1068;

    if (s->variant == UINT8_MAX) {
        if (expected_variant != MPT_FUSION_VARIANT_SAS1068) {
            return -EINVAL;
        }
        s->variant = MPT_FUSION_VARIANT_SAS1068;
    } else if (s->variant != expected_variant) {
        return -EINVAL;
    }

    if (version_id == 0 &&
        (!mptsas_load_legacy_reply_fifo(s->reply_post,
                                        &s->reply_post_head,
                                        &s->reply_post_tail) ||
         !mptsas_load_legacy_reply_fifo(s->reply_free,
                                        &s->reply_free_head,
                                        &s->reply_free_tail))) {
        return -EINVAL;
    }

    if (s->doorbell_idx < 0 || s->doorbell_cnt < 0 ||
        s->doorbell_reply_idx < 0 || s->doorbell_reply_size < 0 ||
        s->doorbell_idx > s->doorbell_cnt ||
        s->doorbell_cnt > ARRAY_SIZE(s->doorbell_msg) ||
        s->doorbell_reply_idx > s->doorbell_reply_size ||
        s->doorbell_reply_size > ARRAY_SIZE(s->doorbell_reply) ||
        MPTSAS_FIFO_INVALID(s, request_post) ||
        MPTSAS_FIFO_INVALID(s, reply_post) ||
        MPTSAS_FIFO_INVALID(s, reply_free) ||
        s->diagnostic_idx > 5 ||
        s->doorbell_state > DOORBELL_READ) {
        return -EINVAL;
    }

    if ((s->config_nvram_written & ~MPTSAS_CONFIG_PAGE_MASK) ||
        (s->config_current_written & ~MPTSAS_CONFIG_PAGE_MASK) ||
        (s->fw_image_size &&
         !mptsas_fw_image_valid(s->fw_image, s->fw_image_size)) ||
        (s->ioc1_flags & ~(uint32_t)MPI_IOCPAGE1_REPLY_COALESCING)) {
        return -EINVAL;
    }

    if (mptsas_is_spi(s)) {
        unsigned int port_id =
            s->spi_port_configuration &
            MPI_SCSIPORTPAGE1_CFG_PORT_SCSI_ID_MASK;

        if (!s->max_devices || s->max_devices > MPTSPI_MAX_TARGETS ||
            s->max_buses > 1 ||
            (s->spi_port_configuration & ~spi_port_configuration_mask) ||
            port_id >= MPTSPI_MAX_TARGETS) {
            return -EINVAL;
        }
    } else if (!s->max_devices || s->max_devices > MPTSAS_NUM_PORTS ||
               s->max_buses > 1) {
        return -EINVAL;
    }

    mptsas_update_interrupt(s);
    return 0;
}

static int mptsas_pre_load(void *opaque)
{
    MPTSASState *s = opaque;

    memset(s->enclosure_status, 0, sizeof(s->enclosure_status));
    g_clear_pointer(&s->fw_image, g_free);
    s->fw_image_size = 0;
    memset(s->config_nvram, 0, sizeof(s->config_nvram));
    s->config_nvram_written = 0;
    memset(s->config_current, 0, sizeof(s->config_current));
    s->config_current_written = 0;
    s->variant = UINT8_MAX;
    timer_del(s->coalescing_timer);
    s->coalescing_count = 0;
    s->reply_irq_ready = true; /* Legacy streams interrupt immediately. */
    s->irq_state = 0;
    s->ioc1_flags = 0;
    s->ioc1_coalescing_timeout = 0;
    s->ioc1_coalescing_depth = 0;
    s->spi_port_configuration = MPTSPI_DEFAULT_PORT_CONFIGURATION;
    s->spi_port_on_bus_timer = 0;
    return 0;
}

static bool mptspi_vmstate_needed(void *opaque)
{
    MPTSASState *s = opaque;

    return mptsas_is_spi(s);
}

static bool mptsas_ioc1_vmstate_needed(void *opaque)
{
    MPTSASState *s = opaque;

    return s->ioc1_flags || s->ioc1_coalescing_timeout ||
           s->ioc1_coalescing_depth;
}

static const VMStateDescription vmstate_mptsas_ioc1 = {
    .name = "mptsas/ioc-page-1",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = mptsas_ioc1_vmstate_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(ioc1_flags, MPTSASState),
        VMSTATE_UINT32(ioc1_coalescing_timeout, MPTSASState),
        VMSTATE_UINT8(ioc1_coalescing_depth, MPTSASState),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_mptspi_variant = {
    .name = "mptsas/spi-variant",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = mptspi_vmstate_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8(variant, MPTSASState),
        VMSTATE_UINT32_ARRAY(spi_requested_params, MPTSASState,
                             MPTSPI_MAX_TARGETS),
        VMSTATE_UINT32_ARRAY(spi_configuration, MPTSASState,
                             MPTSPI_MAX_TARGETS),
        VMSTATE_UINT32(spi_port_configuration, MPTSASState),
        VMSTATE_UINT32(spi_port_on_bus_timer, MPTSASState),
        VMSTATE_END_OF_LIST()
    },
};

static bool mptsas_fw_vmstate_needed(void *opaque)
{
    MPTSASState *s = opaque;

    return s->fw_image_size != 0;
}

static const VMStateDescription vmstate_mptsas_fw = {
    .name = "mptsas/firmware-image",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = mptsas_fw_vmstate_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32(fw_image_size, MPTSASState),
        VMSTATE_VBUFFER_ALLOC_UINT32(fw_image, MPTSASState, 1, NULL,
                                    fw_image_size),
        VMSTATE_END_OF_LIST()
    },
};

static bool mptsas_nvram_vmstate_needed(void *opaque)
{
    MPTSASState *s = opaque;

    return s->config_nvram_written || s->config_current_written;
}

static const VMStateDescription vmstate_mptsas_nvram = {
    .name = "mptsas/config-pages",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = mptsas_nvram_vmstate_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT8(config_nvram_written, MPTSASState),
        VMSTATE_UINT8_2DARRAY(config_nvram, MPTSASState,
                            MPTSAS_CONFIG_PAGE_COUNT,
                            MPTSAS_CONFIG_PAGE_DATA_SIZE),
        VMSTATE_UINT8(config_current_written, MPTSASState),
        VMSTATE_UINT8_2DARRAY(config_current, MPTSASState,
                            MPTSAS_CONFIG_PAGE_COUNT,
                            MPTSAS_CONFIG_PAGE_DATA_SIZE),
        VMSTATE_END_OF_LIST()
    },
};

static bool mptsas_enclosure_vmstate_needed(void *opaque)
{
    MPTSASState *s = opaque;
    unsigned i;

    for (i = 0; i < MPTSAS_NUM_PORTS; i++) {
        if (s->enclosure_status[i]) {
            return true;
        }
    }
    return false;
}

static const VMStateDescription vmstate_mptsas_enclosure = {
    .name = "mptsas/enclosure",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = mptsas_enclosure_vmstate_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_UINT32_ARRAY(enclosure_status, MPTSASState, MPTSAS_NUM_PORTS),
        VMSTATE_END_OF_LIST()
    },
};

static bool mptsas_coalescing_needed(void *opaque)
{
    MPTSASState *s = opaque;

    /* Preserve the idle state so the next reply starts a new batch. */
    return s->coalescing_count != 0 || !s->reply_irq_ready;
}

static const VMStateDescription vmstate_mptsas_coalescing = {
    .name = "mptsas/coalescing",
    .version_id = 1,
    .minimum_version_id = 1,
    .needed = mptsas_coalescing_needed,
    .fields = (const VMStateField[]) {
        VMSTATE_TIMER_PTR(coalescing_timer, MPTSASState),
        VMSTATE_UINT32(coalescing_count, MPTSASState),
        VMSTATE_BOOL(reply_irq_ready, MPTSASState),
        VMSTATE_END_OF_LIST()
    },
};

static const VMStateDescription vmstate_mptsas = {
    .name = "mptsas",
    .version_id = 1,
    .minimum_version_id = 0,
    .pre_load = mptsas_pre_load,
    .post_load = mptsas_post_load,
    .fields = (const VMStateField[]) {
        VMSTATE_PCI_DEVICE(dev, MPTSASState),
        VMSTATE_BOOL(msi_in_use, MPTSASState),
        VMSTATE_UINT32(state, MPTSASState),
        VMSTATE_UINT8(who_init, MPTSASState),
        VMSTATE_UINT8(doorbell_state, MPTSASState),
        VMSTATE_UINT32_ARRAY(doorbell_msg, MPTSASState, 256),
        VMSTATE_INT32(doorbell_idx, MPTSASState),
        VMSTATE_INT32(doorbell_cnt, MPTSASState),

        VMSTATE_UINT16_ARRAY(doorbell_reply, MPTSASState, 256),
        VMSTATE_INT32(doorbell_reply_idx, MPTSASState),
        VMSTATE_INT32(doorbell_reply_size, MPTSASState),

        VMSTATE_UINT32(diagnostic, MPTSASState),
        VMSTATE_UINT8(diagnostic_idx, MPTSASState),

        VMSTATE_UINT32(intr_status, MPTSASState),
        VMSTATE_UINT32(intr_mask, MPTSASState),

        VMSTATE_UINT32_ARRAY(request_post, MPTSASState,
                             MPTSAS_REQUEST_QUEUE_DEPTH + 1),
        VMSTATE_UINT16(request_post_head, MPTSASState),
        VMSTATE_UINT16(request_post_tail, MPTSASState),

        VMSTATE_UINT32_SUB_ARRAY(reply_post, MPTSASState, 0,
                                 MPTSAS_REPLY_QUEUE_DEPTH_V0 + 1),
        VMSTATE_UINT16(reply_post_head, MPTSASState),
        VMSTATE_UINT16(reply_post_tail, MPTSASState),

        VMSTATE_UINT32_SUB_ARRAY(reply_free, MPTSASState, 0,
                                 MPTSAS_REPLY_QUEUE_DEPTH_V0 + 1),
        VMSTATE_UINT16(reply_free_head, MPTSASState),
        VMSTATE_UINT16(reply_free_tail, MPTSASState),

        VMSTATE_UINT16(max_buses, MPTSASState),
        VMSTATE_UINT16(max_devices, MPTSASState),
        VMSTATE_UINT16(reply_frame_size, MPTSASState),
        VMSTATE_UINT64(host_mfa_high_addr, MPTSASState),
        VMSTATE_UINT64(sense_buffer_high_addr, MPTSASState),
        VMSTATE_SUB_ARRAY(reply_post, MPTSASState,
                          MPTSAS_REPLY_QUEUE_DEPTH_V0 + 1,
                          MPTSAS_REPLY_QUEUE_DEPTH -
                          MPTSAS_REPLY_QUEUE_DEPTH_V0,
                          1, vmstate_info_uint32, uint32_t),
        VMSTATE_SUB_ARRAY(reply_free, MPTSASState,
                          MPTSAS_REPLY_QUEUE_DEPTH_V0 + 1,
                          MPTSAS_REPLY_QUEUE_DEPTH -
                          MPTSAS_REPLY_QUEUE_DEPTH_V0,
                          1, vmstate_info_uint32, uint32_t),
        VMSTATE_END_OF_LIST()
    },
    .subsections = (const VMStateDescription * const []) {
        &vmstate_mptsas_coalescing,
        &vmstate_mptsas_enclosure,
        &vmstate_mptsas_fw,
        &vmstate_mptsas_nvram,
        &vmstate_mptsas_ioc1,
        &vmstate_mptspi_variant,
        NULL
    },
};

static const Property mptsas_properties[] = {
    DEFINE_PROP_UINT64("sas_address", MPTSASState, sas_addr, 0),
    DEFINE_PROP_ON_OFF_AUTO("msi", MPTSASState, msi, ON_OFF_AUTO_AUTO),
    DEFINE_PROP_BOOL("x-pci-64bit-bars", MPTSASState, pci_64bit_bars, false),
    DEFINE_PROP_UINT32("x-pci-rom-size", MPTSASState, pci_rom_size, 0),
};

static void mpt_fusion_class_init(ObjectClass *oc, const void *data)
{
    DeviceClass *dc = DEVICE_CLASS(oc);
    PCIDeviceClass *pc = PCI_DEVICE_CLASS(oc);

    pc->realize = mptsas_scsi_realize;
    pc->exit = mptsas_scsi_uninit;
    pc->romfile = 0;
    pc->vendor_id = PCI_VENDOR_ID_LSI_LOGIC;
    pc->subsystem_vendor_id = PCI_VENDOR_ID_LSI_LOGIC;
    pc->class_id = PCI_CLASS_STORAGE_SCSI;
    device_class_set_props(dc, mptsas_properties);
    device_class_set_legacy_reset(dc, mptsas_reset);
    dc->vmsd = &vmstate_mptsas;
    set_bit(DEVICE_CATEGORY_STORAGE, dc->categories);
}

static const TypeInfo mpt_fusion_info = {
    .name = TYPE_MPT_FUSION,
    .parent = TYPE_PCI_DEVICE,
    .instance_size = sizeof(MPTSASState),
    .abstract = true,
    .class_init = mpt_fusion_class_init,
    .interfaces = (const InterfaceInfo[]) {
        { INTERFACE_CONVENTIONAL_PCI_DEVICE },
        { },
    },
};

static void mptsas_register_types(void)
{
    type_register_static(&mpt_fusion_info);
}

type_init(mptsas_register_types)
