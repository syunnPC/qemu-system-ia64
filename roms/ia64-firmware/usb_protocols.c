/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * EFI 1.10 USB host-controller and device I/O protocols.
 */

#include "fw-usb.h"

typedef enum {
    EfiUsbHcStateHalt,
    EfiUsbHcStateOperational,
    EfiUsbHcStateSuspend,
    EfiUsbHcStateMaximum,
} EFI_USB_HC_STATE;

typedef enum {
    EfiUsbDataIn,
    EfiUsbDataOut,
    EfiUsbNoData,
} EFI_USB_DATA_DIRECTION;

typedef enum {
    EfiUsbPortEnable = 1,
    EfiUsbPortSuspend = 2,
    EfiUsbPortReset = 4,
    EfiUsbPortPower = 8,
    EfiUsbPortConnectChange = 16,
    EfiUsbPortEnableChange = 17,
    EfiUsbPortSuspendChange = 18,
    EfiUsbPortOverCurrentChange = 19,
    EfiUsbPortResetChange = 20,
} EFI_USB_PORT_FEATURE;

typedef struct {
    UINT8 RequestType;
    UINT8 Request;
    UINT16 Value;
    UINT16 Index;
    UINT16 Length;
} __attribute__((packed)) EFI_USB_DEVICE_REQUEST;

typedef struct {
    UINT16 PortStatus;
    UINT16 PortChangeStatus;
} EFI_USB_PORT_STATUS;

typedef EFI_STATUS (*EFI_ASYNC_USB_TRANSFER_CALLBACK)(
    VOID *Data, UINTN DataLength, VOID *Context, UINT32 Status);

typedef struct _EFI_USB_HC_PROTOCOL EFI_USB_HC_PROTOCOL;

struct _EFI_USB_HC_PROTOCOL {
    EFI_STATUS (*Reset)(EFI_USB_HC_PROTOCOL *This, UINT16 Attributes);
    EFI_STATUS (*GetState)(EFI_USB_HC_PROTOCOL *This,
                           EFI_USB_HC_STATE *State);
    EFI_STATUS (*SetState)(EFI_USB_HC_PROTOCOL *This,
                           EFI_USB_HC_STATE State);
    EFI_STATUS (*ControlTransfer)(EFI_USB_HC_PROTOCOL *This,
                                  UINT8 DeviceAddress,
                                  BOOLEAN IsSlowDevice,
                                  UINT8 MaximumPacketLength,
                                  EFI_USB_DEVICE_REQUEST *Request,
                                  EFI_USB_DATA_DIRECTION TransferDirection,
                                  VOID *Data, UINTN *DataLength,
                                  UINTN TimeOut, UINT32 *TransferResult);
    EFI_STATUS (*BulkTransfer)(EFI_USB_HC_PROTOCOL *This,
                               UINT8 DeviceAddress, UINT8 EndPointAddress,
                               UINT8 MaximumPacketLength, VOID *Data,
                               UINTN *DataLength, UINT8 *DataToggle,
                               UINTN TimeOut, UINT32 *TransferResult);
    EFI_STATUS (*AsyncInterruptTransfer)(
        EFI_USB_HC_PROTOCOL *This, UINT8 DeviceAddress,
        UINT8 EndPointAddress, BOOLEAN IsSlowDevice,
        UINT8 MaximumPacketLength, BOOLEAN IsNewTransfer,
        UINT8 *DataToggle, UINTN PollingInterval, UINTN DataLength,
        EFI_ASYNC_USB_TRANSFER_CALLBACK CallBackFunction, VOID *Context);
    EFI_STATUS (*SyncInterruptTransfer)(
        EFI_USB_HC_PROTOCOL *This, UINT8 DeviceAddress,
        UINT8 EndPointAddress, BOOLEAN IsSlowDevice,
        UINT8 MaximumPacketLength, VOID *Data, UINTN *DataLength,
        UINT8 *DataToggle, UINTN TimeOut, UINT32 *TransferResult);
    EFI_STATUS (*IsochronousTransfer)(
        EFI_USB_HC_PROTOCOL *This, UINT8 DeviceAddress,
        UINT8 EndPointAddress, UINT8 MaximumPacketLength,
        VOID *Data, UINTN DataLength, UINT32 *TransferResult);
    EFI_STATUS (*AsyncIsochronousTransfer)(
        EFI_USB_HC_PROTOCOL *This, UINT8 DeviceAddress,
        UINT8 EndPointAddress, UINT8 MaximumPacketLength,
        VOID *Data, UINTN DataLength,
        EFI_ASYNC_USB_TRANSFER_CALLBACK IsochronousCallBack,
        VOID *Context);
    EFI_STATUS (*GetRootHubPortNumber)(EFI_USB_HC_PROTOCOL *This,
                                       UINT8 *PortNumber);
    EFI_STATUS (*GetRootHubPortStatus)(EFI_USB_HC_PROTOCOL *This,
                                       UINT8 PortNumber,
                                       EFI_USB_PORT_STATUS *PortStatus);
    EFI_STATUS (*SetRootHubPortFeature)(EFI_USB_HC_PROTOCOL *This,
                                        UINT8 PortNumber,
                                        EFI_USB_PORT_FEATURE PortFeature);
    EFI_STATUS (*ClearRootHubPortFeature)(EFI_USB_HC_PROTOCOL *This,
                                          UINT8 PortNumber,
                                          EFI_USB_PORT_FEATURE PortFeature);
    UINT16 MajorRevision;
    UINT16 MinorRevision;
};

typedef struct {
    UINT8 Length;
    UINT8 DescriptorType;
    UINT16 BcdUSB;
    UINT8 DeviceClass;
    UINT8 DeviceSubClass;
    UINT8 DeviceProtocol;
    UINT8 MaxPacketSize0;
    UINT16 IdVendor;
    UINT16 IdProduct;
    UINT16 BcdDevice;
    UINT8 StrManufacturer;
    UINT8 StrProduct;
    UINT8 StrSerialNumber;
    UINT8 NumConfigurations;
} __attribute__((packed)) EFI_USB_DEVICE_DESCRIPTOR;

typedef struct {
    UINT8 Length;
    UINT8 DescriptorType;
    UINT16 TotalLength;
    UINT8 NumInterfaces;
    UINT8 ConfigurationValue;
    UINT8 Configuration;
    UINT8 Attributes;
    UINT8 MaxPower;
} __attribute__((packed)) EFI_USB_CONFIG_DESCRIPTOR;

typedef struct {
    UINT8 Length;
    UINT8 DescriptorType;
    UINT8 InterfaceNumber;
    UINT8 AlternateSetting;
    UINT8 NumEndpoints;
    UINT8 InterfaceClass;
    UINT8 InterfaceSubClass;
    UINT8 InterfaceProtocol;
    UINT8 Interface;
} __attribute__((packed)) EFI_USB_INTERFACE_DESCRIPTOR;

typedef struct {
    UINT8 Length;
    UINT8 DescriptorType;
    UINT8 EndpointAddress;
    UINT8 Attributes;
    UINT16 MaxPacketSize;
    UINT8 Interval;
} __attribute__((packed)) EFI_USB_ENDPOINT_DESCRIPTOR;

typedef struct _EFI_USB_IO_PROTOCOL EFI_USB_IO_PROTOCOL;

struct _EFI_USB_IO_PROTOCOL {
    EFI_STATUS (*UsbControlTransfer)(EFI_USB_IO_PROTOCOL *This,
                                     EFI_USB_DEVICE_REQUEST *Request,
                                     EFI_USB_DATA_DIRECTION Direction,
                                     UINT32 Timeout, VOID *Data,
                                     UINTN DataLength, UINT32 *Status);
    EFI_STATUS (*UsbBulkTransfer)(EFI_USB_IO_PROTOCOL *This,
                                  UINT8 DeviceEndpoint, VOID *Data,
                                  UINTN *DataLength, UINTN Timeout,
                                  UINT32 *Status);
    EFI_STATUS (*UsbAsyncInterruptTransfer)(
        EFI_USB_IO_PROTOCOL *This, UINT8 DeviceEndpoint,
        BOOLEAN IsNewTransfer, UINTN PollingInterval, UINTN DataLength,
        EFI_ASYNC_USB_TRANSFER_CALLBACK InterruptCallBack, VOID *Context);
    EFI_STATUS (*UsbSyncInterruptTransfer)(EFI_USB_IO_PROTOCOL *This,
                                           UINT8 DeviceEndpoint,
                                           VOID *Data, UINTN *DataLength,
                                           UINTN Timeout, UINT32 *Status);
    EFI_STATUS (*UsbIsochronousTransfer)(EFI_USB_IO_PROTOCOL *This,
                                         UINT8 DeviceEndpoint, VOID *Data,
                                         UINTN DataLength, UINT32 *Status);
    EFI_STATUS (*UsbAsyncIsochronousTransfer)(
        EFI_USB_IO_PROTOCOL *This, UINT8 DeviceEndpoint, VOID *Data,
        UINTN DataLength,
        EFI_ASYNC_USB_TRANSFER_CALLBACK IsochronousCallBack,
        VOID *Context);
    EFI_STATUS (*UsbGetDeviceDescriptor)(
        EFI_USB_IO_PROTOCOL *This,
        EFI_USB_DEVICE_DESCRIPTOR *DeviceDescriptor);
    EFI_STATUS (*UsbGetConfigDescriptor)(
        EFI_USB_IO_PROTOCOL *This,
        EFI_USB_CONFIG_DESCRIPTOR *ConfigurationDescriptor);
    EFI_STATUS (*UsbGetInterfaceDescriptor)(
        EFI_USB_IO_PROTOCOL *This,
        EFI_USB_INTERFACE_DESCRIPTOR *InterfaceDescriptor);
    EFI_STATUS (*UsbGetEndpointDescriptor)(
        EFI_USB_IO_PROTOCOL *This, UINT8 EndpointIndex,
        EFI_USB_ENDPOINT_DESCRIPTOR *EndpointDescriptor);
    EFI_STATUS (*UsbGetStringDescriptor)(EFI_USB_IO_PROTOCOL *This,
                                         UINT16 LangID, UINT8 StringID,
                                         CHAR16 **String);
    EFI_STATUS (*UsbGetSupportedLanguages)(EFI_USB_IO_PROTOCOL *This,
                                           UINT16 **LangIDTable,
                                           UINT16 *TableSize);
    EFI_STATUS (*UsbPortReset)(EFI_USB_IO_PROTOCOL *This);
};

#define EFI_USB_HC_RESET_GLOBAL          0x0001U
#define EFI_USB_HC_RESET_HOST_CONTROLLER 0x0002U

#define EFI_USB_NOERROR        0x0000U
#define EFI_USB_ERR_NOTEXECUTE 0x0001U
#define EFI_USB_ERR_STALL      0x0002U
#define EFI_USB_ERR_BUFFER     0x0004U
#define EFI_USB_ERR_BABBLE     0x0008U
#define EFI_USB_ERR_NAK        0x0010U
#define EFI_USB_ERR_CRC        0x0020U
#define EFI_USB_ERR_TIMEOUT    0x0040U
#define EFI_USB_ERR_BITSTUFF   0x0080U
#define EFI_USB_ERR_SYSTEM     0x0100U

#define USB_PORT_STAT_CONNECTION    0x0001U
#define USB_PORT_STAT_ENABLE        0x0002U
#define USB_PORT_STAT_SUSPEND       0x0004U
#define USB_PORT_STAT_OVERCURRENT   0x0008U
#define USB_PORT_STAT_RESET         0x0010U
#define USB_PORT_STAT_POWER         0x0100U
#define USB_PORT_STAT_LOW_SPEED     0x0200U
#define USB_PORT_STAT_C_CONNECTION  0x0001U
#define USB_PORT_STAT_C_ENABLE      0x0002U
#define USB_PORT_STAT_C_SUSPEND     0x0004U
#define USB_PORT_STAT_C_OVERCURRENT 0x0008U
#define USB_PORT_STAT_C_RESET       0x0010U

#define OHCI_REG_BULK_HEAD_ED    0x28U
#define OHCI_CTL_IE              (1U << 3)
#define OHCI_CTL_BLE             (1U << 5)
#define OHCI_CTL_HCFS_MASK       (3U << 6)
#define OHCI_USB_RESET           (0U << 6)
#define OHCI_USB_RESUME          (1U << 6)
#define OHCI_USB_SUSPEND         (3U << 6)
#define OHCI_STATUS_BLF          (1U << 2)
#define OHCI_ED_K                (1U << 14)
#define OHCI_ED_F                (1U << 15)
#define OHCI_TD_T_DATA0          (2U << 24)
#define OHCI_TD_T_DATA1          (3U << 24)
#define OHCI_ITD_CC_NOT_ACCESSED (0x0eU << 28)
#define OHCI_ITD_FC_SHIFT        24U
#define OHCI_ITD_PSW_CC_SHIFT    12U
#define OHCI_ITD_PSW_PAGE        (1U << 12)
#define OHCI_ITD_PSW_NOT_ACCESSED (0x0eU << 12)

#define FW_USB_MAX_TRANSFER      0xffffU
#define FW_USB_DATA_TD_MAX       16U
#define FW_USB_ASYNC_MAX         8U
#define FW_USB_ISO_ITD_MAX       ((FW_USB_MAX_TRANSFER + 7U) / 8U)
#define FW_USB_ENDPOINT_MAX      16U
#define FW_USB_LANGUAGE_MAX      16U
#define USB_DESC_DEVICE          1U
#define USB_DESC_CONFIGURATION   2U
#define USB_DESC_STRING          3U
#define USB_DESC_INTERFACE       4U
#define USB_DESC_ENDPOINT        5U
#define USB_REQ_GET_DESCRIPTOR   6U
#define USB_REQUEST_DEVICE_IN    0x80U
#define USB_ENDPOINT_DIRECTION_IN 0x80U
#define USB_ENDPOINT_NUMBER_MASK 0x0fU
#define USB_ENDPOINT_TYPE_MASK   0x03U
#define USB_ENDPOINT_ISOCHRONOUS 0x01U
#define USB_ENDPOINT_BULK        0x02U
#define USB_ENDPOINT_INTERRUPT   0x03U

typedef struct {
    FW_DEVICE_PATH_NODE Header;
    UINT8 ParentPortNumber;
    UINT8 InterfaceNumber;
} __attribute__((packed)) FW_USB_DEVICE_PATH_NODE;

#define FW_USB_DEVICE_PATH_MAX 128U

typedef struct {
    BOOLEAN in_use;
    UINT8 device_address;
    UINT8 endpoint_address;
    BOOLEAN low_speed;
    UINT8 maximum_packet;
    UINT8 data_toggle;
    UINTN data_length;
    EFI_ASYNC_USB_TRANSFER_CALLBACK callback;
    VOID *context;
    VOID *buffer;
    EFI_EVENT event;
} FW_USB_ASYNC_TRANSFER;

typedef struct {
    BOOLEAN in_use;
    UINT8 device_address;
    UINT8 endpoint_address;
    UINT8 maximum_packet;
    VOID *data;
    UINTN data_length;
    EFI_ASYNC_USB_TRANSFER_CALLBACK callback;
    VOID *context;
    EFI_EVENT event;
} FW_USB_ASYNC_ISO_TRANSFER;

typedef struct {
    UINT32 Flags;
    UINT32 BufferPage0;
    UINT32 Next;
    UINT32 BufferEnd;
    UINT16 Offset[8];
} __attribute__((packed)) FW_OHCI_ITD;

typedef struct {
    EFI_USB_IO_PROTOCOL protocol;
    EFI_HANDLE handle;
    UINT8 address;
    UINT8 port;
    BOOLEAN low_speed;
    UINT8 configuration_value;
    UINT8 interface_number;
    UINT8 endpoint_count;
    UINT8 endpoint_toggle[FW_USB_ENDPOINT_MAX];
    EFI_USB_DEVICE_DESCRIPTOR device;
    EFI_USB_CONFIG_DESCRIPTOR configuration;
    EFI_USB_INTERFACE_DESCRIPTOR interface;
    EFI_USB_ENDPOINT_DESCRIPTOR endpoint[FW_USB_ENDPOINT_MAX];
    UINT16 languages[FW_USB_LANGUAGE_MAX];
    UINT16 language_size;
    UINT8 device_path[FW_USB_DEVICE_PATH_MAX];
} FW_USB_IO_DEVICE;

static const UINT8 mUsbHcProtocolGuid[16] = {
    0x66, 0x92, 0x08, 0xf5, 0xa0, 0x1a, 0x53, 0x49,
    0x97, 0xd8, 0x56, 0x2f, 0x8a, 0x73, 0xb5, 0x19
};

static const UINT8 mUsbIoProtocolGuid[16] = {
    0xd6, 0x68, 0x2f, 0x2b, 0xd2, 0x0c, 0xcf, 0x44,
    0x8e, 0x8b, 0xbb, 0xa2, 0x0b, 0x1b, 0x5b, 0x75
};

static EFI_USB_HC_PROTOCOL mUsbHcProtocol;
static FW_USB_IO_DEVICE mUsbIoDevice;
static FW_USB_ASYNC_TRANSFER mUsbAsyncTransfers[FW_USB_ASYNC_MAX];
static FW_USB_ASYNC_ISO_TRANSFER mUsbAsyncIsoTransfers[FW_USB_ASYNC_MAX];
static BOOLEAN mUsbHcBusy;
static FW_OHCI_ED mUsbHcTransferEd __attribute__((aligned(16)));
static FW_OHCI_TD mUsbHcTransferTd[FW_USB_DATA_TD_MAX + 3U]
    __attribute__((aligned(16)));
static FW_OHCI_ITD *mUsbHcIsoTd;
static UINT8 mUsbHcSetupPacket[8] __attribute__((aligned(16)));
static UINT8 *mUsbHcTransferBuffer;

static BOOLEAN usb_hc_valid(EFI_USB_HC_PROTOCOL *This)
{
    return This == &mUsbHcProtocol && usb_ohci_controller_present();
}

static BOOLEAN usb_io_valid(EFI_USB_IO_PROTOCOL *This)
{
    return This == &mUsbIoDevice.protocol && mUsbIoDevice.handle != NULL &&
           mUsbKeyboardReady;
}

static UINT8 usb_hc_port_count(VOID)
{
    UINT32 count = usb_ohci_read(OHCI_REG_RH_DESCRIPTOR_A) & 0xffU;

    return count > 15U ? 0 : (UINT8)count;
}

static BOOLEAN usb_hc_packet_size_valid(UINT8 PacketSize,
                                        BOOLEAN LowSpeed)
{
    if (LowSpeed) {
        return PacketSize == 8U;
    }
    return PacketSize == 8U || PacketSize == 16U ||
           PacketSize == 32U || PacketSize == 64U;
}

static UINT32 usb_hc_condition_result(UINT32 Condition)
{
    switch (Condition) {
    case 0:
        return EFI_USB_NOERROR;
    case 1:
        return EFI_USB_ERR_CRC;
    case 2:
        return EFI_USB_ERR_BITSTUFF;
    case 4:
        return EFI_USB_ERR_STALL;
    case 5:
        return EFI_USB_ERR_TIMEOUT;
    case 8:
        return EFI_USB_ERR_BABBLE | EFI_USB_ERR_BUFFER;
    case 9:
    case 12:
    case 13:
        return EFI_USB_ERR_BUFFER;
    case 14:
    case 15:
        return EFI_USB_ERR_NOTEXECUTE;
    default:
        return EFI_USB_ERR_SYSTEM;
    }
}

static UINTN usb_hc_td_transferred(FW_OHCI_TD *Td, UINTN Length)
{
    UINT32 current = ((volatile FW_OHCI_TD *)Td)->CurrentBufferPointer;
    UINT32 end = ((volatile FW_OHCI_TD *)Td)->BufferEnd;
    UINTN remaining;

    if (Length == 0 || current == 0) {
        return Length;
    }
    if (current > end || (UINTN)(end - current) + 1U > Length) {
        return 0;
    }
    remaining = (UINTN)(end - current) + 1U;
    return Length - remaining;
}

static UINT64 usb_hc_timeout_ticks(UINTN TimeoutMilliseconds)
{
    UINT64 multiplier = 1000ULL * FW_ITC_TICKS_PER_MICROSECOND;

    if (TimeoutMilliseconds == 0) {
        return 0;
    }
    return TimeoutMilliseconds > ~0ULL / multiplier ? ~0ULL :
        (UINT64)TimeoutMilliseconds * multiplier;
}

static EFI_STATUS usb_hc_run_ed(FW_OHCI_ED *Ed, FW_OHCI_TD *Td,
                                UINTN TdCount, UINTN Timeout,
                                BOOLEAN BulkList, UINT32 *TransferResult)
{
    UINTN head_register = BulkList ? OHCI_REG_BULK_HEAD_ED :
                                     OHCI_REG_CONTROL_HEAD_ED;
    UINT32 list_enable = BulkList ? OHCI_CTL_BLE : OHCI_CTL_CLE;
    UINT32 list_filled = BulkList ? OHCI_STATUS_BLF : OHCI_STATUS_CLF;
    UINT32 saved_head;
    UINT32 control;
    UINT64 start;
    UINT64 timeout_ticks;
    EFI_STATUS status = EFI_SUCCESS;
    UINTN i;

    if ((usb_ohci_read(OHCI_REG_CONTROL) & OHCI_CTL_HCFS_MASK) !=
        OHCI_USB_OPERATIONAL) {
        *TransferResult = EFI_USB_ERR_SYSTEM;
        return EFI_DEVICE_ERROR;
    }
    saved_head = usb_ohci_read(head_register);
    control = usb_ohci_read(OHCI_REG_CONTROL);
    usb_ohci_write(OHCI_REG_HCCA, usb_ohci_phys(&mUsbOhciHcca));
    usb_ohci_write(OHCI_REG_CONTROL, control | list_enable);
    usb_dma_barrier();
    usb_ohci_write(head_register, usb_ohci_phys(Ed));
    usb_ohci_write(OHCI_REG_COMMAND_STATUS, list_filled);

    start = fw_read_itc();
    timeout_ticks = usb_hc_timeout_ticks(Timeout);
    for (;;) {
        UINT32 head = usb_ohci_ed_head(Ed);

        if ((head & OHCI_DPTR_MASK) == usb_ohci_ed_tail(Ed) ||
            (head & 1U) != 0) {
            break;
        }
        if (Timeout != 0 && fw_read_itc() - start >= timeout_ticks) {
            Ed->Flags |= OHCI_ED_K;
            usb_dma_barrier();
            *TransferResult = EFI_USB_ERR_TIMEOUT;
            status = EFI_TIMEOUT;
            break;
        }
    }
    usb_dma_barrier();
    usb_ohci_write(head_register, saved_head);
    if (status != EFI_SUCCESS) {
        return status;
    }

    *TransferResult = EFI_USB_NOERROR;
    for (i = 0; i < TdCount; i++) {
        UINT32 condition = usb_ohci_td_condition_code(&Td[i]);

        if (condition != OHCI_TD_CC_NOERROR) {
            *TransferResult |= usb_hc_condition_result(condition);
        }
    }
    if (*TransferResult == EFI_USB_NOERROR) {
        return EFI_SUCCESS;
    }
    if ((*TransferResult & EFI_USB_ERR_TIMEOUT) != 0) {
        return EFI_TIMEOUT;
    }
    return EFI_DEVICE_ERROR;
}

static EFI_STATUS usb_hc_data_transfer(UINT8 DeviceAddress,
                                       UINT8 EndPointAddress,
                                       BOOLEAN LowSpeed,
                                       UINT8 MaximumPacketLength,
                                       VOID *Data, UINTN *DataLength,
                                       UINT8 *DataToggle, UINTN Timeout,
                                       UINT32 *TransferResult)
{
    UINTN requested = *DataLength;
    UINTN td_count;
    UINTN offset;
    UINT32 direction;
    UINT32 flags;
    EFI_STATUS status;

    if (requested > FW_USB_MAX_TRANSFER) {
        return EFI_OUT_OF_RESOURCES;
    }
    if ((EndPointAddress & USB_ENDPOINT_DIRECTION_IN) == 0) {
        fw_copy_mem(mUsbHcTransferBuffer, Data, requested);
        direction = OHCI_TD_DIR_OUT;
    } else {
        fw_set_mem(mUsbHcTransferBuffer, requested, 0);
        direction = OHCI_TD_DIR_IN;
    }

    fw_set_mem(&mUsbHcTransferEd, sizeof(mUsbHcTransferEd), 0);
    fw_set_mem(mUsbHcTransferTd, sizeof(mUsbHcTransferTd), 0);
    td_count = (requested + EFI_PAGE_SIZE - 1U) / EFI_PAGE_SIZE;
    for (offset = 0; offset < td_count; offset++) {
        UINTN byte_offset = offset * EFI_PAGE_SIZE;
        UINTN length = requested - byte_offset;

        if (length > EFI_PAGE_SIZE) {
            length = EFI_PAGE_SIZE;
        }
        usb_ohci_init_td(&mUsbHcTransferTd[offset], direction,
                         mUsbHcTransferBuffer + byte_offset, length,
                         &mUsbHcTransferTd[offset + 1U],
                         direction == OHCI_TD_DIR_IN);
    }
    flags = DeviceAddress |
        ((UINT32)(EndPointAddress & USB_ENDPOINT_NUMBER_MASK) << 7) |
        (direction << OHCI_ED_D_SHIFT) |
        ((UINT32)MaximumPacketLength << OHCI_ED_MPS_SHIFT);
    if (LowSpeed) {
        flags |= OHCI_ED_S;
    }
    mUsbHcTransferEd.Flags = flags;
    mUsbHcTransferEd.Tail = usb_ohci_phys(&mUsbHcTransferTd[td_count]);
    mUsbHcTransferEd.Head = usb_ohci_phys(&mUsbHcTransferTd[0]) |
        (*DataToggle != 0 ? OHCI_ED_C : 0);
    mUsbHcTransferEd.Next = 0;
    usb_dma_barrier();

    status = usb_hc_run_ed(&mUsbHcTransferEd, mUsbHcTransferTd,
                           td_count, Timeout, 1, TransferResult);
    *DataLength = 0;
    for (offset = 0; offset < td_count; offset++) {
        UINTN length = requested - offset * EFI_PAGE_SIZE;
        UINTN transferred;

        if (length > EFI_PAGE_SIZE) {
            length = EFI_PAGE_SIZE;
        }
        transferred = usb_hc_td_transferred(&mUsbHcTransferTd[offset],
                                             length);
        *DataLength += transferred;
        if (transferred != length) {
            break;
        }
    }
    *DataToggle = (usb_ohci_ed_head(&mUsbHcTransferEd) & OHCI_ED_C) != 0;
    if ((EndPointAddress & USB_ENDPOINT_DIRECTION_IN) != 0 &&
        *DataLength != 0) {
        fw_copy_mem(Data, mUsbHcTransferBuffer, *DataLength);
    }
    return status;
}

static EFI_STATUS usb_hc_reset(EFI_USB_HC_PROTOCOL *This, UINT16 Attributes);

static EFI_STATUS usb_hc_get_state(EFI_USB_HC_PROTOCOL *This,
                                   EFI_USB_HC_STATE *State)
{
    UINT32 hcfs;

    if (!usb_hc_valid(This) || State == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    hcfs = usb_ohci_read(OHCI_REG_CONTROL) & OHCI_CTL_HCFS_MASK;
    if (hcfs == OHCI_USB_OPERATIONAL || hcfs == OHCI_USB_RESUME) {
        *State = EfiUsbHcStateOperational;
    } else if (hcfs == OHCI_USB_SUSPEND) {
        *State = EfiUsbHcStateSuspend;
    } else {
        *State = EfiUsbHcStateHalt;
    }
    return EFI_SUCCESS;
}

static EFI_STATUS usb_hc_set_state(EFI_USB_HC_PROTOCOL *This,
                                   EFI_USB_HC_STATE State)
{
    UINT32 control;
    UINT32 hcfs;

    if (!usb_hc_valid(This) || State >= EfiUsbHcStateMaximum) {
        return EFI_INVALID_PARAMETER;
    }
    control = usb_ohci_read(OHCI_REG_CONTROL);
    hcfs = State == EfiUsbHcStateOperational ? OHCI_USB_OPERATIONAL :
           State == EfiUsbHcStateSuspend ? OHCI_USB_SUSPEND :
                                           OHCI_USB_RESET;
    control = (control & ~OHCI_CTL_HCFS_MASK) | hcfs;
    usb_ohci_write(OHCI_REG_CONTROL, control);
    return (usb_ohci_read(OHCI_REG_CONTROL) & OHCI_CTL_HCFS_MASK) == hcfs ?
        EFI_SUCCESS : EFI_DEVICE_ERROR;
}

static EFI_STATUS usb_hc_control_transfer(
    EFI_USB_HC_PROTOCOL *This, UINT8 DeviceAddress,
    BOOLEAN IsSlowDevice, UINT8 MaximumPacketLength,
    EFI_USB_DEVICE_REQUEST *Request,
    EFI_USB_DATA_DIRECTION TransferDirection, VOID *Data,
    UINTN *DataLength, UINTN TimeOut, UINT32 *TransferResult)
{
    UINTN requested;
    UINTN data_tds;
    UINTN td_count;
    UINTN i;
    UINT32 flags;
    EFI_STATUS status;
    EFI_TPL old_tpl;

    if (!usb_hc_valid(This) || DeviceAddress > 127U || Request == NULL ||
        TransferResult == NULL || TransferDirection > EfiUsbNoData ||
        !usb_hc_packet_size_valid(MaximumPacketLength, IsSlowDevice)) {
        return EFI_INVALID_PARAMETER;
    }
    if (TransferDirection == EfiUsbNoData) {
        if (Data != NULL || Request->Length != 0 ||
            (DataLength != NULL && *DataLength != 0)) {
            return EFI_INVALID_PARAMETER;
        }
        requested = 0;
    } else {
        if (Data == NULL || DataLength == NULL) {
            return EFI_INVALID_PARAMETER;
        }
        requested = *DataLength;
        if (Request->Length != requested) {
            return EFI_INVALID_PARAMETER;
        }
    }
    if (requested > FW_USB_MAX_TRANSFER) {
        return EFI_OUT_OF_RESOURCES;
    }
    if (mUsbHcBusy) {
        return EFI_NOT_READY;
    }
    old_tpl = bs_raise_tpl(TPL_NOTIFY);
    mUsbHcBusy = 1;
    *TransferResult = EFI_USB_ERR_NOTEXECUTE;

    mUsbHcSetupPacket[0] = Request->RequestType;
    mUsbHcSetupPacket[1] = Request->Request;
    mUsbHcSetupPacket[2] = (UINT8)Request->Value;
    mUsbHcSetupPacket[3] = (UINT8)(Request->Value >> 8);
    mUsbHcSetupPacket[4] = (UINT8)Request->Index;
    mUsbHcSetupPacket[5] = (UINT8)(Request->Index >> 8);
    mUsbHcSetupPacket[6] = (UINT8)Request->Length;
    mUsbHcSetupPacket[7] = (UINT8)(Request->Length >> 8);
    if (TransferDirection == EfiUsbDataOut && requested != 0) {
        fw_copy_mem(mUsbHcTransferBuffer, Data, requested);
    } else if (requested != 0) {
        fw_set_mem(mUsbHcTransferBuffer, requested, 0);
    }

    fw_set_mem(&mUsbHcTransferEd, sizeof(mUsbHcTransferEd), 0);
    fw_set_mem(mUsbHcTransferTd, sizeof(mUsbHcTransferTd), 0);
    data_tds = (requested + EFI_PAGE_SIZE - 1U) / EFI_PAGE_SIZE;
    usb_ohci_init_td(&mUsbHcTransferTd[0], OHCI_TD_DIR_SETUP,
                     mUsbHcSetupPacket, sizeof(mUsbHcSetupPacket),
                     &mUsbHcTransferTd[1], 0);
    mUsbHcTransferTd[0].Flags |= OHCI_TD_T_DATA0;
    for (i = 0; i < data_tds; i++) {
        UINTN offset = i * EFI_PAGE_SIZE;
        UINTN length = requested - offset;
        UINT32 direction = TransferDirection == EfiUsbDataIn ?
            OHCI_TD_DIR_IN : OHCI_TD_DIR_OUT;

        if (length > EFI_PAGE_SIZE) {
            length = EFI_PAGE_SIZE;
        }
        usb_ohci_init_td(&mUsbHcTransferTd[1U + i], direction,
                         mUsbHcTransferBuffer + offset, length,
                         &mUsbHcTransferTd[2U + i],
                         direction == OHCI_TD_DIR_IN);
        if (i == 0) {
            mUsbHcTransferTd[1U + i].Flags |= OHCI_TD_T_DATA1;
        }
    }
    usb_ohci_init_td(&mUsbHcTransferTd[1U + data_tds],
                     TransferDirection == EfiUsbDataIn ?
                        OHCI_TD_DIR_OUT : OHCI_TD_DIR_IN,
                     NULL, 0, &mUsbHcTransferTd[2U + data_tds], 1);
    mUsbHcTransferTd[1U + data_tds].Flags |= OHCI_TD_T_DATA1;
    td_count = data_tds + 2U;

    flags = DeviceAddress |
        ((UINT32)MaximumPacketLength << OHCI_ED_MPS_SHIFT);
    if (IsSlowDevice) {
        flags |= OHCI_ED_S;
    }
    mUsbHcTransferEd.Flags = flags;
    mUsbHcTransferEd.Tail =
        usb_ohci_phys(&mUsbHcTransferTd[td_count]);
    mUsbHcTransferEd.Head = usb_ohci_phys(&mUsbHcTransferTd[0]);
    usb_dma_barrier();
    status = usb_hc_run_ed(&mUsbHcTransferEd, mUsbHcTransferTd,
                           td_count, TimeOut, 0, TransferResult);

    if (DataLength != NULL) {
        *DataLength = 0;
        for (i = 0; i < data_tds; i++) {
            UINTN length = requested - i * EFI_PAGE_SIZE;
            UINTN transferred;

            if (length > EFI_PAGE_SIZE) {
                length = EFI_PAGE_SIZE;
            }
            transferred = usb_hc_td_transferred(
                &mUsbHcTransferTd[1U + i], length);
            *DataLength += transferred;
            if (transferred != length) {
                break;
            }
        }
        if (TransferDirection == EfiUsbDataIn && *DataLength != 0) {
            fw_copy_mem(Data, mUsbHcTransferBuffer, *DataLength);
        }
    }
    mUsbHcBusy = 0;
    bs_restore_tpl(old_tpl);
    return status;
}

static EFI_STATUS usb_hc_bulk_transfer(
    EFI_USB_HC_PROTOCOL *This, UINT8 DeviceAddress,
    UINT8 EndPointAddress, UINT8 MaximumPacketLength, VOID *Data,
    UINTN *DataLength, UINT8 *DataToggle, UINTN TimeOut,
    UINT32 *TransferResult)
{
    UINTN requested;
    UINTN completed = 0;
    EFI_STATUS status = EFI_SUCCESS;
    EFI_TPL old_tpl;

    if (!usb_hc_valid(This) || DeviceAddress > 127U ||
        (EndPointAddress & USB_ENDPOINT_NUMBER_MASK) == 0 ||
        (EndPointAddress & 0x70U) != 0 || Data == NULL ||
        DataLength == NULL || *DataLength == 0 || DataToggle == NULL ||
        *DataToggle > 1U || TransferResult == NULL ||
        !usb_hc_packet_size_valid(MaximumPacketLength, 0)) {
        return EFI_INVALID_PARAMETER;
    }
    if (mUsbHcBusy) {
        return EFI_NOT_READY;
    }
    requested = *DataLength;
    *DataLength = 0;
    *TransferResult = EFI_USB_NOERROR;
    old_tpl = bs_raise_tpl(TPL_NOTIFY);
    mUsbHcBusy = 1;
    while (completed < requested) {
        UINTN chunk = requested - completed;
        UINTN transferred;
        UINT32 result;

        if (chunk > FW_USB_MAX_TRANSFER) {
            chunk = FW_USB_MAX_TRANSFER;
        }
        transferred = chunk;
        status = usb_hc_data_transfer(
            DeviceAddress, EndPointAddress, 0, MaximumPacketLength,
            (UINT8 *)Data + completed, &transferred, DataToggle,
            TimeOut, &result);
        *TransferResult |= result;
        completed += transferred;
        if (status != EFI_SUCCESS || transferred != chunk) {
            break;
        }
    }
    *DataLength = completed;
    mUsbHcBusy = 0;
    bs_restore_tpl(old_tpl);
    return status;
}

static EFI_STATUS usb_hc_sync_interrupt_transfer(
    EFI_USB_HC_PROTOCOL *This, UINT8 DeviceAddress,
    UINT8 EndPointAddress, BOOLEAN IsSlowDevice,
    UINT8 MaximumPacketLength, VOID *Data, UINTN *DataLength,
    UINT8 *DataToggle, UINTN TimeOut, UINT32 *TransferResult)
{
    EFI_STATUS status;
    EFI_TPL old_tpl;

    if (!usb_hc_valid(This) || DeviceAddress > 127U ||
        (EndPointAddress & USB_ENDPOINT_DIRECTION_IN) == 0 ||
        (EndPointAddress & USB_ENDPOINT_NUMBER_MASK) == 0 ||
        (EndPointAddress & 0x70U) != 0 || Data == NULL ||
        DataLength == NULL || *DataLength == 0 ||
        *DataLength > FW_USB_MAX_TRANSFER || DataToggle == NULL ||
        *DataToggle > 1U || TransferResult == NULL ||
        !usb_hc_packet_size_valid(MaximumPacketLength, IsSlowDevice)) {
        return EFI_INVALID_PARAMETER;
    }
    if (mUsbHcBusy) {
        return EFI_NOT_READY;
    }
    old_tpl = bs_raise_tpl(TPL_NOTIFY);
    mUsbHcBusy = 1;
    status = usb_hc_data_transfer(DeviceAddress, EndPointAddress,
                                  IsSlowDevice, MaximumPacketLength,
                                  Data, DataLength, DataToggle, TimeOut,
                                  TransferResult);
    mUsbHcBusy = 0;
    bs_restore_tpl(old_tpl);
    return status;
}

static VOID usb_hc_async_notify(EFI_EVENT Event, VOID *Context)
{
    FW_USB_ASYNC_TRANSFER *transfer = (FW_USB_ASYNC_TRANSFER *)Context;
    UINTN length;
    UINT32 result;
    EFI_STATUS status;
    EFI_ASYNC_USB_TRANSFER_CALLBACK callback;
    VOID *callback_context;
    VOID *data;

    (void)Event;
    if (transfer == NULL || !transfer->in_use || mUsbHcBusy) {
        return;
    }
    length = transfer->data_length;
    status = usb_hc_sync_interrupt_transfer(
        &mUsbHcProtocol, transfer->device_address,
        transfer->endpoint_address, transfer->low_speed,
        transfer->maximum_packet, transfer->buffer, &length,
        &transfer->data_toggle, 1, &result);
    if (status == EFI_TIMEOUT) {
        return;
    }
    callback = transfer->callback;
    callback_context = transfer->context;
    data = status == EFI_SUCCESS ? transfer->buffer : NULL;
    if (callback != NULL) {
        (void)callback(data, status == EFI_SUCCESS ? length : 0,
                       callback_context, result);
    }
}

static EFI_STATUS usb_hc_async_interrupt_transfer(
    EFI_USB_HC_PROTOCOL *This, UINT8 DeviceAddress,
    UINT8 EndPointAddress, BOOLEAN IsSlowDevice,
    UINT8 MaximumPacketLength, BOOLEAN IsNewTransfer,
    UINT8 *DataToggle, UINTN PollingInterval, UINTN DataLength,
    EFI_ASYNC_USB_TRANSFER_CALLBACK CallBackFunction, VOID *Context)
{
    UINTN i;

    if (!usb_hc_valid(This) || DeviceAddress > 127U ||
        (EndPointAddress & USB_ENDPOINT_DIRECTION_IN) == 0 ||
        (EndPointAddress & USB_ENDPOINT_NUMBER_MASK) == 0 ||
        (EndPointAddress & 0x70U) != 0 || DataToggle == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    if (!IsNewTransfer) {
        for (i = 0; i < FW_USB_ASYNC_MAX; i++) {
            FW_USB_ASYNC_TRANSFER *transfer = &mUsbAsyncTransfers[i];

            if (transfer->in_use &&
                transfer->device_address == DeviceAddress &&
                transfer->endpoint_address == EndPointAddress) {
                *DataToggle = transfer->data_toggle;
                (void)bs_set_timer(transfer->event, TIMER_CANCEL, 0);
                (void)bs_close_event(transfer->event);
                if (transfer->buffer != NULL) {
                    (void)bs_free_pool(transfer->buffer);
                }
                fw_set_mem(transfer, sizeof(*transfer), 0);
                return EFI_SUCCESS;
            }
        }
        return EFI_INVALID_PARAMETER;
    }
    if (*DataToggle > 1U || PollingInterval == 0 ||
        PollingInterval > 255U || DataLength == 0 ||
        DataLength > FW_USB_MAX_TRANSFER || CallBackFunction == NULL ||
        !usb_hc_packet_size_valid(MaximumPacketLength, IsSlowDevice)) {
        return EFI_INVALID_PARAMETER;
    }
    for (i = 0; i < FW_USB_ASYNC_MAX; i++) {
        if (mUsbAsyncTransfers[i].in_use &&
            mUsbAsyncTransfers[i].device_address == DeviceAddress &&
            mUsbAsyncTransfers[i].endpoint_address == EndPointAddress) {
            return EFI_INVALID_PARAMETER;
        }
    }
    for (i = 0; i < FW_USB_ASYNC_MAX; i++) {
        FW_USB_ASYNC_TRANSFER *transfer = &mUsbAsyncTransfers[i];
        EFI_STATUS status;

        if (transfer->in_use) {
            continue;
        }
        fw_set_mem(transfer, sizeof(*transfer), 0);
        status = bs_allocate_pool(EfiBootServicesData, DataLength,
                                  &transfer->buffer);
        if (status != EFI_SUCCESS) {
            return EFI_OUT_OF_RESOURCES;
        }
        transfer->in_use = 1;
        transfer->device_address = DeviceAddress;
        transfer->endpoint_address = EndPointAddress;
        transfer->low_speed = IsSlowDevice;
        transfer->maximum_packet = MaximumPacketLength;
        transfer->data_toggle = *DataToggle;
        transfer->data_length = DataLength;
        transfer->callback = CallBackFunction;
        transfer->context = Context;
        status = bs_create_event(EVT_TIMER | EVT_NOTIFY_SIGNAL,
                                 TPL_CALLBACK, usb_hc_async_notify,
                                 transfer, &transfer->event);
        if (status == EFI_SUCCESS) {
            status = bs_set_timer(transfer->event, TIMER_PERIODIC,
                                  (UINT64)PollingInterval * 10000ULL);
        }
        if (status != EFI_SUCCESS) {
            if (transfer->event != NULL) {
                (void)bs_close_event(transfer->event);
            }
            (void)bs_free_pool(transfer->buffer);
            fw_set_mem(transfer, sizeof(*transfer), 0);
            return EFI_OUT_OF_RESOURCES;
        }
        return EFI_SUCCESS;
    }
    return EFI_OUT_OF_RESOURCES;
}

static EFI_STATUS usb_hc_isochronous_transfer(
    EFI_USB_HC_PROTOCOL *This, UINT8 DeviceAddress,
    UINT8 EndPointAddress, UINT8 MaximumPacketLength,
    VOID *Data, UINTN DataLength, UINT32 *TransferResult)
{
    UINT32 saved_interrupt_table[32];
    UINT32 saved_control;
    UINT32 direction;
    UINTN packet_count;
    UINTN itd_count;
    UINTN packet = 0;
    UINTN offset = 0;
    UINTN i;
    UINT64 start;
    UINT64 timeout_ticks;
    EFI_STATUS status = EFI_SUCCESS;
    EFI_TPL old_tpl;

    if (!usb_hc_valid(This) || DeviceAddress > 127U ||
        (EndPointAddress & USB_ENDPOINT_NUMBER_MASK) == 0 ||
        (EndPointAddress & 0x70U) != 0 || MaximumPacketLength == 0 ||
        Data == NULL || DataLength == 0 ||
        TransferResult == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    if (DataLength > FW_USB_MAX_TRANSFER) {
        return EFI_OUT_OF_RESOURCES;
    }
    if (mUsbHcBusy) {
        return EFI_NOT_READY;
    }

    packet_count = (DataLength + MaximumPacketLength - 1U) /
                   MaximumPacketLength;
    itd_count = (packet_count + 7U) / 8U;
    if (itd_count == 0 || itd_count > FW_USB_ISO_ITD_MAX) {
        return EFI_OUT_OF_RESOURCES;
    }

    old_tpl = bs_raise_tpl(TPL_NOTIFY);
    mUsbHcBusy = 1;
    *TransferResult = EFI_USB_ERR_NOTEXECUTE;
    direction = (EndPointAddress & USB_ENDPOINT_DIRECTION_IN) != 0 ?
        OHCI_TD_DIR_IN : OHCI_TD_DIR_OUT;
    if (direction == OHCI_TD_DIR_OUT) {
        fw_copy_mem(mUsbHcTransferBuffer, Data, DataLength);
    } else {
        fw_set_mem(mUsbHcTransferBuffer, DataLength, 0);
    }

    fw_set_mem(&mUsbHcTransferEd, sizeof(mUsbHcTransferEd), 0);
    fw_set_mem(mUsbHcIsoTd,
               (itd_count + 1U) * sizeof(mUsbHcIsoTd[0]), 0);
    usb_dma_barrier();
    {
        UINT16 first_frame =
            (UINT16)(((volatile FW_OHCI_HCCA *)&mUsbOhciHcca)->FrameNumber +
                     2U);

        for (i = 0; i < itd_count; i++) {
            FW_OHCI_ITD *itd = &mUsbHcIsoTd[i];
            UINTN frames = packet_count - packet;
            UINTN group_offset = offset;
            UINTN group_length;
            UINT32 start_address;
            UINT32 end_address;
            UINT32 start_page;
            UINT32 end_page;
            UINTN frame;

            if (frames > 8U) {
                frames = 8U;
            }
            group_length = DataLength - group_offset;
            if (group_length > frames * MaximumPacketLength) {
                group_length = frames * MaximumPacketLength;
            }
            start_address = usb_ohci_phys(mUsbHcTransferBuffer +
                                           group_offset);
            end_address = start_address + (UINT32)group_length - 1U;
            start_page = start_address & ~0xfffU;
            end_page = end_address & ~0xfffU;
            if (end_page - start_page > EFI_PAGE_SIZE) {
                status = EFI_DEVICE_ERROR;
                *TransferResult = EFI_USB_ERR_SYSTEM;
                goto iso_done;
            }

            itd->Flags = OHCI_ITD_CC_NOT_ACCESSED |
                ((UINT32)(frames - 1U) << OHCI_ITD_FC_SHIFT) |
                (UINT16)(first_frame + packet);
            itd->BufferPage0 = start_address;
            itd->Next = usb_ohci_phys(&mUsbHcIsoTd[i + 1U]);
            itd->BufferEnd = end_address;
            for (frame = 0; frame < frames; frame++) {
                UINT32 packet_address = usb_ohci_phys(
                    mUsbHcTransferBuffer + offset);
                UINT32 page = packet_address & ~0xfffU;
                UINT16 page_select;
                UINTN length = DataLength - offset;

                if (length > MaximumPacketLength) {
                    length = MaximumPacketLength;
                }
                if (page == start_page) {
                    page_select = 0;
                } else if (page == end_page) {
                    page_select = OHCI_ITD_PSW_PAGE;
                } else {
                    status = EFI_DEVICE_ERROR;
                    *TransferResult = EFI_USB_ERR_SYSTEM;
                    goto iso_done;
                }
                itd->Offset[frame] = OHCI_ITD_PSW_NOT_ACCESSED |
                    page_select | (UINT16)(packet_address & 0xfffU);
                offset += length;
                packet++;
            }
        }
    }

    mUsbHcTransferEd.Flags = DeviceAddress |
        ((UINT32)(EndPointAddress & USB_ENDPOINT_NUMBER_MASK) << 7) |
        (direction << OHCI_ED_D_SHIFT) | OHCI_ED_F |
        ((UINT32)MaximumPacketLength << OHCI_ED_MPS_SHIFT);
    mUsbHcTransferEd.Tail = usb_ohci_phys(&mUsbHcIsoTd[itd_count]);
    mUsbHcTransferEd.Head = usb_ohci_phys(&mUsbHcIsoTd[0]);
    mUsbHcTransferEd.Next = 0;

    for (i = 0; i < FW_ARRAY_SIZE(mUsbOhciHcca.InterruptTable); i++) {
        saved_interrupt_table[i] = mUsbOhciHcca.InterruptTable[i];
        mUsbOhciHcca.InterruptTable[i] =
            usb_ohci_phys(&mUsbHcTransferEd);
    }
    saved_control = usb_ohci_read(OHCI_REG_CONTROL);
    usb_ohci_write(OHCI_REG_HCCA, usb_ohci_phys(&mUsbOhciHcca));
    usb_dma_barrier();
    usb_ohci_write(OHCI_REG_CONTROL,
                   saved_control | OHCI_CTL_PLE | OHCI_CTL_IE);

    start = fw_read_itc();
    timeout_ticks = usb_hc_timeout_ticks(packet_count + 1000U);
    while ((usb_ohci_ed_head(&mUsbHcTransferEd) & OHCI_DPTR_MASK) !=
           usb_ohci_ed_tail(&mUsbHcTransferEd)) {
        if (fw_read_itc() - start >= timeout_ticks) {
            mUsbHcTransferEd.Flags |= OHCI_ED_K;
            usb_dma_barrier();
            status = EFI_TIMEOUT;
            *TransferResult = EFI_USB_ERR_TIMEOUT;
            break;
        }
    }

    usb_ohci_write(OHCI_REG_CONTROL, saved_control);
    for (i = 0; i < FW_ARRAY_SIZE(mUsbOhciHcca.InterruptTable); i++) {
        mUsbOhciHcca.InterruptTable[i] = saved_interrupt_table[i];
    }
    usb_dma_barrier();

    if (status == EFI_SUCCESS) {
        *TransferResult = EFI_USB_NOERROR;
        packet = 0;
        for (i = 0; i < itd_count; i++) {
            UINTN frames = packet_count - packet;
            UINTN frame;

            if (frames > 8U) {
                frames = 8U;
            }
            for (frame = 0; frame < frames; frame++, packet++) {
                UINT32 condition =
                    mUsbHcIsoTd[i].Offset[frame] >>
                    OHCI_ITD_PSW_CC_SHIFT;

                if (condition != 0) {
                    *TransferResult |= usb_hc_condition_result(condition);
                }
            }
        }
        if (*TransferResult != EFI_USB_NOERROR) {
            status = EFI_DEVICE_ERROR;
        }
    }
    if (direction == OHCI_TD_DIR_IN && status == EFI_SUCCESS) {
        fw_copy_mem(Data, mUsbHcTransferBuffer, DataLength);
    }

iso_done:
    mUsbHcBusy = 0;
    bs_restore_tpl(old_tpl);
    return status;
}

static VOID usb_hc_async_iso_notify(EFI_EVENT Event, VOID *Context)
{
    FW_USB_ASYNC_ISO_TRANSFER *transfer =
        (FW_USB_ASYNC_ISO_TRANSFER *)Context;
    EFI_ASYNC_USB_TRANSFER_CALLBACK callback;
    VOID *callback_context;
    VOID *data;
    UINTN length;
    UINT32 result = EFI_USB_ERR_NOTEXECUTE;
    EFI_STATUS status;

    if (transfer == NULL || !transfer->in_use) {
        return;
    }
    if (mUsbHcBusy) {
        (void)bs_set_timer(Event, TIMER_RELATIVE, 10000ULL);
        return;
    }

    callback = transfer->callback;
    callback_context = transfer->context;
    data = transfer->data;
    length = transfer->data_length;
    status = usb_hc_isochronous_transfer(
        &mUsbHcProtocol, transfer->device_address,
        transfer->endpoint_address, transfer->maximum_packet,
        data, length, &result);
    (void)bs_set_timer(Event, TIMER_CANCEL, 0);
    (void)bs_close_event(Event);
    fw_set_mem(transfer, sizeof(*transfer), 0);
    if (callback != NULL) {
        (void)callback(status == EFI_SUCCESS ? data : NULL,
                       status == EFI_SUCCESS ? length : 0,
                       callback_context, result);
    }
}

static EFI_STATUS usb_hc_async_isochronous_transfer(
    EFI_USB_HC_PROTOCOL *This, UINT8 DeviceAddress,
    UINT8 EndPointAddress, UINT8 MaximumPacketLength,
    VOID *Data, UINTN DataLength,
    EFI_ASYNC_USB_TRANSFER_CALLBACK IsochronousCallBack, VOID *Context)
{
    UINTN i;

    if (!usb_hc_valid(This) || DeviceAddress > 127U ||
        (EndPointAddress & USB_ENDPOINT_NUMBER_MASK) == 0 ||
        (EndPointAddress & 0x70U) != 0 || MaximumPacketLength == 0 ||
        Data == NULL || DataLength == 0 ||
        IsochronousCallBack == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    if (DataLength > FW_USB_MAX_TRANSFER) {
        return EFI_OUT_OF_RESOURCES;
    }
    for (i = 0; i < FW_USB_ASYNC_MAX; i++) {
        FW_USB_ASYNC_ISO_TRANSFER *transfer = &mUsbAsyncIsoTransfers[i];

        if (transfer->in_use &&
            transfer->device_address == DeviceAddress &&
            transfer->endpoint_address == EndPointAddress) {
            return EFI_INVALID_PARAMETER;
        }
    }
    for (i = 0; i < FW_USB_ASYNC_MAX; i++) {
        FW_USB_ASYNC_ISO_TRANSFER *transfer = &mUsbAsyncIsoTransfers[i];
        EFI_STATUS status;

        if (transfer->in_use) {
            continue;
        }
        fw_set_mem(transfer, sizeof(*transfer), 0);
        transfer->in_use = 1;
        transfer->device_address = DeviceAddress;
        transfer->endpoint_address = EndPointAddress;
        transfer->maximum_packet = MaximumPacketLength;
        transfer->data = Data;
        transfer->data_length = DataLength;
        transfer->callback = IsochronousCallBack;
        transfer->context = Context;
        status = bs_create_event(EVT_TIMER | EVT_NOTIFY_SIGNAL,
                                 TPL_CALLBACK, usb_hc_async_iso_notify,
                                 transfer, &transfer->event);
        if (status == EFI_SUCCESS) {
            status = bs_set_timer(transfer->event, TIMER_RELATIVE,
                                  10000ULL);
        }
        if (status != EFI_SUCCESS) {
            if (transfer->event != NULL) {
                (void)bs_close_event(transfer->event);
            }
            fw_set_mem(transfer, sizeof(*transfer), 0);
            return EFI_OUT_OF_RESOURCES;
        }
        return EFI_SUCCESS;
    }
    return EFI_OUT_OF_RESOURCES;
}

static EFI_STATUS usb_hc_get_port_number(EFI_USB_HC_PROTOCOL *This,
                                         UINT8 *PortNumber)
{
    if (!usb_hc_valid(This) || PortNumber == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    *PortNumber = usb_hc_port_count();
    return *PortNumber != 0 ? EFI_SUCCESS : EFI_DEVICE_ERROR;
}

static EFI_STATUS usb_hc_get_port_status(EFI_USB_HC_PROTOCOL *This,
                                         UINT8 PortNumber,
                                         EFI_USB_PORT_STATUS *PortStatus)
{
    UINT32 raw;

    if (!usb_hc_valid(This) || PortStatus == NULL ||
        PortNumber >= usb_hc_port_count()) {
        return EFI_INVALID_PARAMETER;
    }
    raw = usb_ohci_read(OHCI_REG_RH_PORT_STATUS_BASE +
                        (UINTN)PortNumber * 4U);
    PortStatus->PortStatus = (UINT16)(raw & 0x031fU);
    PortStatus->PortChangeStatus = (UINT16)((raw >> 16) & 0x001fU);
    return EFI_SUCCESS;
}

static EFI_STATUS usb_hc_set_port_feature(EFI_USB_HC_PROTOCOL *This,
                                          UINT8 PortNumber,
                                          EFI_USB_PORT_FEATURE Feature)
{
    UINT32 value;

    if (!usb_hc_valid(This) || PortNumber >= usb_hc_port_count()) {
        return EFI_INVALID_PARAMETER;
    }
    switch (Feature) {
    case EfiUsbPortEnable:
        value = 1U << 1;
        break;
    case EfiUsbPortSuspend:
        value = 1U << 2;
        break;
    case EfiUsbPortReset:
        value = 1U << 4;
        break;
    case EfiUsbPortPower:
        value = 1U << 8;
        break;
    default:
        return EFI_INVALID_PARAMETER;
    }
    usb_ohci_write(OHCI_REG_RH_PORT_STATUS_BASE +
                   (UINTN)PortNumber * 4U, value);
    return EFI_SUCCESS;
}

static EFI_STATUS usb_hc_clear_port_feature(EFI_USB_HC_PROTOCOL *This,
                                            UINT8 PortNumber,
                                            EFI_USB_PORT_FEATURE Feature)
{
    UINT32 value;

    if (!usb_hc_valid(This) || PortNumber >= usb_hc_port_count()) {
        return EFI_INVALID_PARAMETER;
    }
    switch (Feature) {
    case EfiUsbPortEnable:
        value = 1U << 0;
        break;
    case EfiUsbPortSuspend:
        value = 1U << 3;
        break;
    case EfiUsbPortPower:
        value = 1U << 9;
        break;
    case EfiUsbPortConnectChange:
    case EfiUsbPortEnableChange:
    case EfiUsbPortSuspendChange:
    case EfiUsbPortOverCurrentChange:
    case EfiUsbPortResetChange:
        value = 1U << (UINT32)Feature;
        break;
    default:
        return EFI_INVALID_PARAMETER;
    }
    usb_ohci_write(OHCI_REG_RH_PORT_STATUS_BASE +
                   (UINTN)PortNumber * 4U, value);
    return EFI_SUCCESS;
}

static VOID usb_hc_cancel_async_all(VOID)
{
    UINTN i;

    for (i = 0; i < FW_USB_ASYNC_MAX; i++) {
        FW_USB_ASYNC_TRANSFER *transfer = &mUsbAsyncTransfers[i];

        if (!transfer->in_use) {
            continue;
        }
        (void)bs_set_timer(transfer->event, TIMER_CANCEL, 0);
        (void)bs_close_event(transfer->event);
        if (transfer->buffer != NULL) {
            (void)bs_free_pool(transfer->buffer);
        }
        fw_set_mem(transfer, sizeof(*transfer), 0);
    }
    for (i = 0; i < FW_USB_ASYNC_MAX; i++) {
        FW_USB_ASYNC_ISO_TRANSFER *transfer = &mUsbAsyncIsoTransfers[i];

        if (!transfer->in_use) {
            continue;
        }
        (void)bs_set_timer(transfer->event, TIMER_CANCEL, 0);
        (void)bs_close_event(transfer->event);
        fw_set_mem(transfer, sizeof(*transfer), 0);
    }
}

static EFI_STATUS usb_hc_reset(EFI_USB_HC_PROTOCOL *This, UINT16 Attributes)
{
    UINT8 port_count;
    UINT8 port;

    if (!usb_hc_valid(This) || Attributes == 0 ||
        (Attributes & ~(EFI_USB_HC_RESET_GLOBAL |
                        EFI_USB_HC_RESET_HOST_CONTROLLER)) != 0) {
        return EFI_INVALID_PARAMETER;
    }
    usb_hc_cancel_async_all();
    if (!usb_ohci_reset_controller()) {
        return EFI_DEVICE_ERROR;
    }
    fw_set_mem(&mUsbOhciHcca, sizeof(mUsbOhciHcca), 0);
    usb_ohci_write(OHCI_REG_HCCA, usb_ohci_phys(&mUsbOhciHcca));
    usb_ohci_write(OHCI_REG_PERIODIC_START, 0x2a2fU);
    usb_ohci_write(OHCI_REG_CONTROL, OHCI_USB_OPERATIONAL |
                   OHCI_CTL_CLE | OHCI_CTL_PLE);
    if ((Attributes & EFI_USB_HC_RESET_GLOBAL) != 0) {
        usb_ohci_write(OHCI_REG_RH_STATUS, OHCI_RHS_LPSC);
        port_count = usb_hc_port_count();
        for (port = 0; port < port_count; port++) {
            UINT32 status = usb_ohci_read(
                OHCI_REG_RH_PORT_STATUS_BASE + (UINTN)port * 4U);

            usb_ohci_write(OHCI_REG_RH_PORT_STATUS_BASE +
                           (UINTN)port * 4U, 1U << 8);
            if ((status & OHCI_PORT_CCS) != 0) {
                usb_ohci_write(OHCI_REG_RH_PORT_STATUS_BASE +
                               (UINTN)port * 4U, 1U << 4);
            }
        }
        (void)bs_stall(50000U);
    }
    mUsbKeyboardReady = 0;
    mUsbKeyboardTried = 0;
    return EFI_SUCCESS;
}

static EFI_USB_ENDPOINT_DESCRIPTOR *usb_io_endpoint(UINT8 Address)
{
    UINTN i;

    for (i = 0; i < mUsbIoDevice.endpoint_count; i++) {
        if (mUsbIoDevice.endpoint[i].EndpointAddress == Address) {
            return &mUsbIoDevice.endpoint[i];
        }
    }
    return NULL;
}

static EFI_STATUS usb_io_control_transfer(
    EFI_USB_IO_PROTOCOL *This, EFI_USB_DEVICE_REQUEST *Request,
    EFI_USB_DATA_DIRECTION Direction, UINT32 Timeout,
    VOID *Data, UINTN DataLength, UINT32 *Status)
{
    UINTN length = DataLength;

    if (!usb_io_valid(This)) {
        return EFI_INVALID_PARAMETER;
    }
    return mUsbHcProtocol.ControlTransfer(
        &mUsbHcProtocol, mUsbIoDevice.address, mUsbIoDevice.low_speed,
        mUsbIoDevice.device.MaxPacketSize0, Request, Direction,
        Data, Direction == EfiUsbNoData ? NULL : &length,
        Timeout, Status);
}

static EFI_STATUS usb_io_bulk_transfer(EFI_USB_IO_PROTOCOL *This,
                                       UINT8 DeviceEndpoint, VOID *Data,
                                       UINTN *DataLength, UINTN Timeout,
                                       UINT32 *Status)
{
    EFI_USB_ENDPOINT_DESCRIPTOR *endpoint;
    UINT8 index = DeviceEndpoint & USB_ENDPOINT_NUMBER_MASK;

    if (!usb_io_valid(This)) {
        return EFI_INVALID_PARAMETER;
    }
    endpoint = usb_io_endpoint(DeviceEndpoint);
    if (endpoint == NULL ||
        (endpoint->Attributes & USB_ENDPOINT_TYPE_MASK) != USB_ENDPOINT_BULK) {
        return EFI_INVALID_PARAMETER;
    }
    return mUsbHcProtocol.BulkTransfer(
        &mUsbHcProtocol, mUsbIoDevice.address, DeviceEndpoint,
        (UINT8)endpoint->MaxPacketSize, Data, DataLength,
        &mUsbIoDevice.endpoint_toggle[index], Timeout, Status);
}

static EFI_STATUS usb_io_async_interrupt_transfer(
    EFI_USB_IO_PROTOCOL *This, UINT8 DeviceEndpoint,
    BOOLEAN IsNewTransfer, UINTN PollingInterval, UINTN DataLength,
    EFI_ASYNC_USB_TRANSFER_CALLBACK Callback, VOID *Context)
{
    EFI_USB_ENDPOINT_DESCRIPTOR *endpoint;
    UINT8 index = DeviceEndpoint & USB_ENDPOINT_NUMBER_MASK;

    if (!usb_io_valid(This)) {
        return EFI_INVALID_PARAMETER;
    }
    endpoint = usb_io_endpoint(DeviceEndpoint);
    if (endpoint == NULL ||
        (endpoint->Attributes & USB_ENDPOINT_TYPE_MASK) !=
            USB_ENDPOINT_INTERRUPT) {
        return EFI_INVALID_PARAMETER;
    }
    return mUsbHcProtocol.AsyncInterruptTransfer(
        &mUsbHcProtocol, mUsbIoDevice.address, DeviceEndpoint,
        mUsbIoDevice.low_speed, (UINT8)endpoint->MaxPacketSize,
        IsNewTransfer, &mUsbIoDevice.endpoint_toggle[index],
        PollingInterval, DataLength, Callback, Context);
}

static EFI_STATUS usb_io_sync_interrupt_transfer(
    EFI_USB_IO_PROTOCOL *This, UINT8 DeviceEndpoint, VOID *Data,
    UINTN *DataLength, UINTN Timeout, UINT32 *Status)
{
    EFI_USB_ENDPOINT_DESCRIPTOR *endpoint;
    UINT8 index = DeviceEndpoint & USB_ENDPOINT_NUMBER_MASK;

    if (!usb_io_valid(This)) {
        return EFI_INVALID_PARAMETER;
    }
    endpoint = usb_io_endpoint(DeviceEndpoint);
    if (endpoint == NULL ||
        (endpoint->Attributes & USB_ENDPOINT_TYPE_MASK) !=
            USB_ENDPOINT_INTERRUPT) {
        return EFI_INVALID_PARAMETER;
    }
    return mUsbHcProtocol.SyncInterruptTransfer(
        &mUsbHcProtocol, mUsbIoDevice.address, DeviceEndpoint,
        mUsbIoDevice.low_speed, (UINT8)endpoint->MaxPacketSize,
        Data, DataLength, &mUsbIoDevice.endpoint_toggle[index],
        Timeout, Status);
}

static EFI_STATUS usb_io_isochronous_transfer(
    EFI_USB_IO_PROTOCOL *This, UINT8 DeviceEndpoint, VOID *Data,
    UINTN DataLength, UINT32 *Status)
{
    EFI_USB_ENDPOINT_DESCRIPTOR *endpoint;

    if (!usb_io_valid(This)) {
        return EFI_INVALID_PARAMETER;
    }
    endpoint = usb_io_endpoint(DeviceEndpoint);
    if (endpoint == NULL ||
        (endpoint->Attributes & USB_ENDPOINT_TYPE_MASK) !=
            USB_ENDPOINT_ISOCHRONOUS) {
        return EFI_INVALID_PARAMETER;
    }
    return mUsbHcProtocol.IsochronousTransfer(
        &mUsbHcProtocol, mUsbIoDevice.address, DeviceEndpoint,
        (UINT8)endpoint->MaxPacketSize, Data, DataLength, Status);
}

static EFI_STATUS usb_io_async_isochronous_transfer(
    EFI_USB_IO_PROTOCOL *This, UINT8 DeviceEndpoint, VOID *Data,
    UINTN DataLength, EFI_ASYNC_USB_TRANSFER_CALLBACK Callback,
    VOID *Context)
{
    EFI_USB_ENDPOINT_DESCRIPTOR *endpoint;

    if (!usb_io_valid(This)) {
        return EFI_INVALID_PARAMETER;
    }
    endpoint = usb_io_endpoint(DeviceEndpoint);
    if (endpoint == NULL ||
        (endpoint->Attributes & USB_ENDPOINT_TYPE_MASK) !=
            USB_ENDPOINT_ISOCHRONOUS) {
        return EFI_INVALID_PARAMETER;
    }
    return mUsbHcProtocol.AsyncIsochronousTransfer(
        &mUsbHcProtocol, mUsbIoDevice.address, DeviceEndpoint,
        (UINT8)endpoint->MaxPacketSize, Data, DataLength,
        Callback, Context);
}

static EFI_STATUS usb_io_get_device_descriptor(
    EFI_USB_IO_PROTOCOL *This, EFI_USB_DEVICE_DESCRIPTOR *Descriptor)
{
    if (!usb_io_valid(This) || Descriptor == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    *Descriptor = mUsbIoDevice.device;
    return EFI_SUCCESS;
}

static EFI_STATUS usb_io_get_config_descriptor(
    EFI_USB_IO_PROTOCOL *This, EFI_USB_CONFIG_DESCRIPTOR *Descriptor)
{
    if (!usb_io_valid(This) || Descriptor == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    *Descriptor = mUsbIoDevice.configuration;
    return EFI_SUCCESS;
}

static EFI_STATUS usb_io_get_interface_descriptor(
    EFI_USB_IO_PROTOCOL *This, EFI_USB_INTERFACE_DESCRIPTOR *Descriptor)
{
    if (!usb_io_valid(This) || Descriptor == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    *Descriptor = mUsbIoDevice.interface;
    return EFI_SUCCESS;
}

static EFI_STATUS usb_io_get_endpoint_descriptor(
    EFI_USB_IO_PROTOCOL *This, UINT8 EndpointIndex,
    EFI_USB_ENDPOINT_DESCRIPTOR *Descriptor)
{
    if (!usb_io_valid(This) || Descriptor == NULL || EndpointIndex > 15U) {
        return EFI_INVALID_PARAMETER;
    }
    if (EndpointIndex >= mUsbIoDevice.endpoint_count) {
        return EFI_NOT_FOUND;
    }
    *Descriptor = mUsbIoDevice.endpoint[EndpointIndex];
    return EFI_SUCCESS;
}

static EFI_STATUS usb_io_get_supported_languages(
    EFI_USB_IO_PROTOCOL *This, UINT16 **LanguageTable, UINT16 *TableSize)
{
    if (!usb_io_valid(This) || LanguageTable == NULL || TableSize == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    *LanguageTable = mUsbIoDevice.languages;
    *TableSize = mUsbIoDevice.language_size;
    return EFI_SUCCESS;
}

static EFI_STATUS usb_io_get_string_descriptor(EFI_USB_IO_PROTOCOL *This,
                                               UINT16 Language,
                                               UINT8 StringId,
                                               CHAR16 **String)
{
    EFI_USB_DEVICE_REQUEST request;
    UINT8 descriptor[255];
    UINTN length;
    UINTN characters;
    UINTN i;
    UINT32 result;
    CHAR16 *text;
    EFI_STATUS status;

    if (!usb_io_valid(This) || String == NULL || StringId == 0) {
        return EFI_INVALID_PARAMETER;
    }
    *String = NULL;
    request.RequestType = USB_REQUEST_DEVICE_IN;
    request.Request = USB_REQ_GET_DESCRIPTOR;
    request.Value = (UINT16)((USB_DESC_STRING << 8) | StringId);
    request.Index = Language;
    request.Length = sizeof(descriptor);
    length = sizeof(descriptor);
    status = mUsbHcProtocol.ControlTransfer(
        &mUsbHcProtocol, mUsbIoDevice.address, mUsbIoDevice.low_speed,
        mUsbIoDevice.device.MaxPacketSize0, &request, EfiUsbDataIn,
        descriptor, &length, 1000, &result);
    if (status != EFI_SUCCESS || length < 2 || descriptor[1] !=
        USB_DESC_STRING || descriptor[0] < 2 || descriptor[0] > length) {
        return EFI_NOT_FOUND;
    }
    characters = (descriptor[0] - 2U) / 2U;
    status = bs_allocate_pool(EfiBootServicesData,
                              (characters + 1U) * sizeof(CHAR16),
                              (VOID **)&text);
    if (status != EFI_SUCCESS) {
        return EFI_OUT_OF_RESOURCES;
    }
    for (i = 0; i < characters; i++) {
        text[i] = (CHAR16)descriptor[2U + i * 2U] |
                  ((CHAR16)descriptor[3U + i * 2U] << 8);
    }
    text[characters] = 0;
    *String = text;
    return EFI_SUCCESS;
}

static EFI_STATUS usb_io_port_reset(EFI_USB_IO_PROTOCOL *This)
{
    EFI_USB_DEVICE_REQUEST request;
    EFI_USB_PORT_STATUS port_status;
    UINT32 result;
    UINTN timeout;
    EFI_STATUS status;

    if (!usb_io_valid(This)) {
        return EFI_INVALID_PARAMETER;
    }
    status = mUsbHcProtocol.SetRootHubPortFeature(
        &mUsbHcProtocol, mUsbIoDevice.port, EfiUsbPortReset);
    if (status != EFI_SUCCESS) {
        return EFI_DEVICE_ERROR;
    }
    for (timeout = 0; timeout < 100U; timeout++) {
        (void)bs_stall(1000U);
        status = mUsbHcProtocol.GetRootHubPortStatus(
            &mUsbHcProtocol, mUsbIoDevice.port, &port_status);
        if (status == EFI_SUCCESS &&
            (port_status.PortStatus & USB_PORT_STAT_RESET) == 0 &&
            (port_status.PortStatus & USB_PORT_STAT_ENABLE) != 0) {
            break;
        }
    }
    if (timeout == 100U) {
        return EFI_DEVICE_ERROR;
    }
    (void)mUsbHcProtocol.ClearRootHubPortFeature(
        &mUsbHcProtocol, mUsbIoDevice.port, EfiUsbPortResetChange);

    request.RequestType = 0;
    request.Request = USB_REQ_SET_ADDRESS;
    request.Value = mUsbIoDevice.address;
    request.Index = 0;
    request.Length = 0;
    status = mUsbHcProtocol.ControlTransfer(
        &mUsbHcProtocol, 0, mUsbIoDevice.low_speed,
        mUsbIoDevice.device.MaxPacketSize0, &request, EfiUsbNoData,
        NULL, NULL, 1000, &result);
    if (status != EFI_SUCCESS) {
        return EFI_DEVICE_ERROR;
    }
    (void)bs_stall(2000U);
    request.Request = USB_REQ_SET_CONFIGURATION;
    request.Value = mUsbIoDevice.configuration_value;
    status = mUsbHcProtocol.ControlTransfer(
        &mUsbHcProtocol, mUsbIoDevice.address, mUsbIoDevice.low_speed,
        mUsbIoDevice.device.MaxPacketSize0, &request, EfiUsbNoData,
        NULL, NULL, 1000, &result);
    if (status != EFI_SUCCESS) {
        return EFI_DEVICE_ERROR;
    }
    request.RequestType = USB_TYPE_CLASS_INTERFACE_OUT;
    request.Request = USB_REQ_HID_SET_IDLE;
    request.Value = 0;
    request.Index = mUsbIoDevice.interface_number;
    status = mUsbHcProtocol.ControlTransfer(
        &mUsbHcProtocol, mUsbIoDevice.address, mUsbIoDevice.low_speed,
        mUsbIoDevice.device.MaxPacketSize0, &request, EfiUsbNoData,
        NULL, NULL, 1000, &result);
    if (status != EFI_SUCCESS) {
        return EFI_DEVICE_ERROR;
    }
    request.Request = USB_REQ_HID_SET_PROTOCOL;
    status = mUsbHcProtocol.ControlTransfer(
        &mUsbHcProtocol, mUsbIoDevice.address, mUsbIoDevice.low_speed,
        mUsbIoDevice.device.MaxPacketSize0, &request, EfiUsbNoData,
        NULL, NULL, 1000, &result);
    if (status != EFI_SUCCESS) {
        return EFI_DEVICE_ERROR;
    }
    fw_set_mem(mUsbIoDevice.endpoint_toggle,
               sizeof(mUsbIoDevice.endpoint_toggle), 0);
    fw_set_mem(mUsbKeyboardPreviousReport,
               sizeof(mUsbKeyboardPreviousReport), 0);
    mUsbKeyboardReady = 1;
    usb_keyboard_submit_interrupt_td();
    return EFI_SUCCESS;
}

static BOOLEAN usb_io_fetch_descriptor(UINT16 Value, UINT16 Index,
                                       VOID *Buffer, UINTN *Length)
{
    EFI_USB_DEVICE_REQUEST request;
    UINT32 result;

    request.RequestType = USB_REQUEST_DEVICE_IN;
    request.Request = USB_REQ_GET_DESCRIPTOR;
    request.Value = Value;
    request.Index = Index;
    request.Length = (UINT16)*Length;
    return mUsbHcProtocol.ControlTransfer(
        &mUsbHcProtocol, OHCI_USB_KEYBOARD_ADDRESS,
        mUsbKeyboardLowSpeed, 8, &request, EfiUsbDataIn,
        Buffer, Length, 1000, &result) == EFI_SUCCESS;
}

static BOOLEAN usb_io_cache_descriptors(VOID)
{
    UINT8 language_descriptor[2U + FW_USB_LANGUAGE_MAX * 2U];
    UINT8 *const descriptor_buffer = mUsbHcTransferBuffer;
    EFI_USB_IO_PROTOCOL protocol = mUsbIoDevice.protocol;
    UINTN length;
    UINTN offset;
    BOOLEAN selected_interface = 0;

    fw_set_mem(&mUsbIoDevice, sizeof(mUsbIoDevice), 0);
    mUsbIoDevice.protocol = protocol;
    length = sizeof(mUsbIoDevice.device);
    if (!usb_io_fetch_descriptor((UINT16)(USB_DESC_DEVICE << 8), 0,
                                 &mUsbIoDevice.device, &length) ||
        length != sizeof(mUsbIoDevice.device) ||
        mUsbIoDevice.device.DescriptorType != USB_DESC_DEVICE ||
        !usb_hc_packet_size_valid(mUsbIoDevice.device.MaxPacketSize0,
                                  mUsbKeyboardLowSpeed)) {
        return 0;
    }
    length = sizeof(EFI_USB_CONFIG_DESCRIPTOR);
    if (!usb_io_fetch_descriptor((UINT16)(USB_DESC_CONFIGURATION << 8),
                                 0, descriptor_buffer, &length) ||
        length < sizeof(EFI_USB_CONFIG_DESCRIPTOR)) {
        return 0;
    }
    fw_copy_mem(&mUsbIoDevice.configuration, descriptor_buffer,
                sizeof(mUsbIoDevice.configuration));
    if (mUsbIoDevice.configuration.DescriptorType !=
            USB_DESC_CONFIGURATION ||
        mUsbIoDevice.configuration.TotalLength <
            sizeof(EFI_USB_CONFIG_DESCRIPTOR)) {
        return 0;
    }
    length = mUsbIoDevice.configuration.TotalLength;
    if (!usb_io_fetch_descriptor((UINT16)(USB_DESC_CONFIGURATION << 8),
                                 0, descriptor_buffer, &length) ||
        length < mUsbIoDevice.configuration.TotalLength) {
        return 0;
    }

    offset = 0;
    while (offset + 2U <= length) {
        UINT8 descriptor_length = descriptor_buffer[offset];
        UINT8 descriptor_type = descriptor_buffer[offset + 1U];

        if (descriptor_length < 2U || descriptor_length > length - offset) {
            return 0;
        }
        if (descriptor_type == USB_DESC_INTERFACE &&
            descriptor_length >= sizeof(EFI_USB_INTERFACE_DESCRIPTOR)) {
            EFI_USB_INTERFACE_DESCRIPTOR *interface =
                (EFI_USB_INTERFACE_DESCRIPTOR *)(VOID *)(
                    descriptor_buffer + offset);

            if (!selected_interface && interface->InterfaceClass == 3U &&
                interface->InterfaceSubClass == 1U &&
                interface->InterfaceProtocol == 1U) {
                mUsbIoDevice.interface = *interface;
                mUsbIoDevice.endpoint_count = 0;
                selected_interface = 1;
            } else if (selected_interface) {
                break;
            }
        } else if (selected_interface &&
                   descriptor_type == USB_DESC_ENDPOINT &&
                   descriptor_length >= sizeof(EFI_USB_ENDPOINT_DESCRIPTOR) &&
                   mUsbIoDevice.endpoint_count < FW_USB_ENDPOINT_MAX) {
            mUsbIoDevice.endpoint[mUsbIoDevice.endpoint_count++] =
                *(EFI_USB_ENDPOINT_DESCRIPTOR *)(VOID *)(
                    descriptor_buffer + offset);
        }
        offset += descriptor_length;
    }
    if (!selected_interface || mUsbIoDevice.endpoint_count == 0) {
        return 0;
    }

    mUsbIoDevice.address = OHCI_USB_KEYBOARD_ADDRESS;
    mUsbIoDevice.port = mUsbKeyboardPort;
    mUsbIoDevice.low_speed = mUsbKeyboardLowSpeed;
    mUsbIoDevice.configuration_value =
        mUsbIoDevice.configuration.ConfigurationValue;
    mUsbIoDevice.interface_number =
        mUsbIoDevice.interface.InterfaceNumber;

    length = sizeof(language_descriptor);
    if (usb_io_fetch_descriptor((UINT16)(USB_DESC_STRING << 8), 0,
                                language_descriptor, &length) &&
        length >= 2U && language_descriptor[1] == USB_DESC_STRING &&
        language_descriptor[0] >= 2U &&
        language_descriptor[0] <= length) {
        UINTN table_bytes = language_descriptor[0] - 2U;
        UINTN i;

        if (table_bytes > sizeof(mUsbIoDevice.languages)) {
            table_bytes = sizeof(mUsbIoDevice.languages);
        }
        table_bytes &= ~(UINTN)1U;
        for (i = 0; i < table_bytes / 2U; i++) {
            mUsbIoDevice.languages[i] =
                (UINT16)language_descriptor[2U + i * 2U] |
                ((UINT16)language_descriptor[3U + i * 2U] << 8);
        }
        mUsbIoDevice.language_size = (UINT16)table_bytes;
    }
    return 1;
}

static VOID usb_protocol_initialize_interfaces(VOID)
{
    mUsbHcProtocol.Reset = usb_hc_reset;
    mUsbHcProtocol.GetState = usb_hc_get_state;
    mUsbHcProtocol.SetState = usb_hc_set_state;
    mUsbHcProtocol.ControlTransfer = usb_hc_control_transfer;
    mUsbHcProtocol.BulkTransfer = usb_hc_bulk_transfer;
    mUsbHcProtocol.AsyncInterruptTransfer =
        usb_hc_async_interrupt_transfer;
    mUsbHcProtocol.SyncInterruptTransfer =
        usb_hc_sync_interrupt_transfer;
    mUsbHcProtocol.IsochronousTransfer = usb_hc_isochronous_transfer;
    mUsbHcProtocol.AsyncIsochronousTransfer =
        usb_hc_async_isochronous_transfer;
    mUsbHcProtocol.GetRootHubPortNumber = usb_hc_get_port_number;
    mUsbHcProtocol.GetRootHubPortStatus = usb_hc_get_port_status;
    mUsbHcProtocol.SetRootHubPortFeature = usb_hc_set_port_feature;
    mUsbHcProtocol.ClearRootHubPortFeature = usb_hc_clear_port_feature;
    mUsbHcProtocol.MajorRevision = 1;
    mUsbHcProtocol.MinorRevision = 1;

    mUsbIoDevice.protocol.UsbControlTransfer = usb_io_control_transfer;
    mUsbIoDevice.protocol.UsbBulkTransfer = usb_io_bulk_transfer;
    mUsbIoDevice.protocol.UsbAsyncInterruptTransfer =
        usb_io_async_interrupt_transfer;
    mUsbIoDevice.protocol.UsbSyncInterruptTransfer =
        usb_io_sync_interrupt_transfer;
    mUsbIoDevice.protocol.UsbIsochronousTransfer =
        usb_io_isochronous_transfer;
    mUsbIoDevice.protocol.UsbAsyncIsochronousTransfer =
        usb_io_async_isochronous_transfer;
    mUsbIoDevice.protocol.UsbGetDeviceDescriptor =
        usb_io_get_device_descriptor;
    mUsbIoDevice.protocol.UsbGetConfigDescriptor =
        usb_io_get_config_descriptor;
    mUsbIoDevice.protocol.UsbGetInterfaceDescriptor =
        usb_io_get_interface_descriptor;
    mUsbIoDevice.protocol.UsbGetEndpointDescriptor =
        usb_io_get_endpoint_descriptor;
    mUsbIoDevice.protocol.UsbGetStringDescriptor =
        usb_io_get_string_descriptor;
    mUsbIoDevice.protocol.UsbGetSupportedLanguages =
        usb_io_get_supported_languages;
    mUsbIoDevice.protocol.UsbPortReset = usb_io_port_reset;
}

static BOOLEAN usb_keyboard_device_path_init(UINT8 port, UINT8 interface)
{
    FW_USB_DEVICE_PATH_NODE usb_path;
    FW_DEVICE_PATH_NODE end = { 0x7f, 0xff, sizeof(end) };
    UINTN path_size;

    path_size = fw_usb_controller_device_path(
        mUsbIoDevice.device_path,
        sizeof(mUsbIoDevice.device_path) - sizeof(usb_path));
    if (path_size < sizeof(end)) {
        return 0;
    }
    usb_path.Header.Type = 0x03;
    usb_path.Header.SubType = 0x05;
    usb_path.Header.Length = sizeof(usb_path);
    usb_path.ParentPortNumber = port;
    usb_path.InterfaceNumber = interface;
    fw_copy_mem(mUsbIoDevice.device_path + path_size - sizeof(end),
                &usb_path, sizeof(usb_path));
    fw_copy_mem(mUsbIoDevice.device_path + path_size - sizeof(end) +
                sizeof(usb_path), &end, sizeof(end));
    return 1;
}

BOOLEAN fw_usb_protocols_install(VOID)
{
    if (mUsbHcIsoTd == NULL) {
        mUsbHcIsoTd = fw_boot_dma_buffer(
            (FW_USB_ISO_ITD_MAX + 1U) * sizeof(*mUsbHcIsoTd), 32);
    }
    if (mUsbHcTransferBuffer == NULL) {
        mUsbHcTransferBuffer = fw_boot_dma_buffer(FW_USB_MAX_TRANSFER + 1U,
                                                 4096);
    }
    if (!mUsbHcIsoTd || !mUsbHcTransferBuffer) {
        return 0;
    }

    EFI_HANDLE controller = fw_usb_controller_handle();
    EFI_STATUS status;

    if (controller == NULL || !usb_ohci_controller_present()) {
        if (!usb_keyboard_init()) {
            return 1;
        }
        if (!usb_keyboard_device_path_init(mUsbKeyboardPort, 0)) {
            return 0;
        }
        return bs_install_protocol(&mUsbIoDevice.handle,
                                    (VOID *)mDevicePathProtocolGuid, 0,
                                    mUsbIoDevice.device_path) == EFI_SUCCESS;
    }
    usb_protocol_initialize_interfaces();
    status = bs_install_protocol(&controller, (VOID *)mUsbHcProtocolGuid,
                                 0, &mUsbHcProtocol);
    if (status != EFI_SUCCESS) {
        return 0;
    }
    if (!usb_keyboard_init() || !usb_io_cache_descriptors()) {
        return 1;
    }
    if (!usb_keyboard_device_path_init(mUsbIoDevice.port,
                                       mUsbIoDevice.interface_number)) {
        (void)bs_uninstall_protocol(controller,
                                    (VOID *)mUsbHcProtocolGuid,
                                    &mUsbHcProtocol);
        return 0;
    }
    mUsbIoDevice.handle = NULL;
    status = bs_install_protocol(&mUsbIoDevice.handle,
                                 (VOID *)mUsbIoProtocolGuid, 0,
                                 &mUsbIoDevice.protocol);
    if (status == EFI_SUCCESS) {
        status = bs_install_protocol(&mUsbIoDevice.handle,
                                     (VOID *)mDevicePathProtocolGuid, 0,
                                     mUsbIoDevice.device_path);
    }
    if (status != EFI_SUCCESS) {
        if (mUsbIoDevice.handle != NULL) {
            (void)bs_uninstall_protocol(mUsbIoDevice.handle,
                                        (VOID *)mUsbIoProtocolGuid,
                                        &mUsbIoDevice.protocol);
        }
        mUsbIoDevice.handle = NULL;
        (void)bs_uninstall_protocol(controller,
                                    (VOID *)mUsbHcProtocolGuid,
                                    &mUsbHcProtocol);
        return 0;
    }
    return 1;
}

EFI_HANDLE fw_usb_keyboard_handle(VOID)
{
    return mUsbIoDevice.handle;
}
