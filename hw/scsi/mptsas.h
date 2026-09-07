#ifndef MPTSAS_H
#define MPTSAS_H

#include "mpi.h"
#include "hw/pci/pci_device.h"
#include "hw/scsi/scsi.h"

#define MPTSAS_NUM_PORTS 8
#define MPTSAS_ENCLOSURE_HANDLE (2 * MPTSAS_NUM_PORTS + 1)
#define MPTSPI_NUM_PORTS 1
#define MPTSPI_MAX_TARGETS 16
#define MPTSPI_HOST_ID 7
#define MPTSPI_DEFAULT_PORT_CONFIGURATION \
    (MPTSPI_HOST_ID | \
     (1U << (MPTSPI_HOST_ID + \
             MPI_SCSIPORTPAGE1_CFG_SHIFT_PORT_RESPONSE_ID)))
#define MPTSAS_MAX_FRAMES 2048     /* Firmware limit at 65535 */

#define MPTSAS_REQUEST_QUEUE_DEPTH 128
#define MPTSAS_REPLY_QUEUE_DEPTH_V0 128
#define MPTSAS_REPLY_QUEUE_DEPTH   256

#define MPTSAS_MAXIMUM_CHAIN_DEPTH 0x22

enum {
    MPTSAS_CONFIG_MANUFACTURING_1,
    MPTSAS_CONFIG_MANUFACTURING_4,
    MPTSAS_CONFIG_IO_UNIT_1,
    MPTSAS_CONFIG_SAS_IO_UNIT_1,
    MPTSAS_CONFIG_SAS_IO_UNIT_2,
    MPTSAS_CONFIG_PAGE_COUNT,
};

#define MPTSAS_CONFIG_PAGE_DATA_SIZE 256
#define MPTSAS_CONFIG_PAGE_MASK ((1U << MPTSAS_CONFIG_PAGE_COUNT) - 1)

typedef struct MPTSASRequest MPTSASRequest;

#define TYPE_MPT_FUSION "mpt-fusion"
#define TYPE_MPTSAS1068 "mptsas1068"
#define TYPE_LSI53C1030 "lsi53c1030"
typedef struct MPTSASState MPTSASState;
DECLARE_INSTANCE_CHECKER(MPTSASState, MPT_FUSION,
                         TYPE_MPT_FUSION)

#define MPT_SAS(obj) MPT_FUSION(obj)

typedef enum MPTFusionVariant {
    MPT_FUSION_VARIANT_SAS1068,
    MPT_FUSION_VARIANT_LSI53C1030,
} MPTFusionVariant;

enum {
    DOORBELL_NONE,
    DOORBELL_WRITE,
    DOORBELL_READ
};

struct MPTSASState {
    PCIDevice dev;
    MemoryRegion mmio_io;
    MemoryRegion port_io;
    MemoryRegion diag_io;
    MemoryRegion pci_rom;
    QEMUBH *request_bh;

    /* properties */
    OnOffAuto msi;
    uint64_t sas_addr;
    bool pci_64bit_bars;
    uint32_t pci_rom_size;

    bool msi_in_use;
    uint8_t variant;

    /* Doorbell register */
    uint32_t state;
    uint8_t who_init;
    uint8_t doorbell_state;

    /* Buffer for requests that are sent through the doorbell register.  */
    uint32_t doorbell_msg[256];
    int doorbell_idx;
    int doorbell_cnt;

    uint16_t doorbell_reply[256];
    int doorbell_reply_idx;
    int doorbell_reply_size;

    /* Other registers */
    uint8_t diagnostic_idx;
    uint32_t diagnostic;
    uint32_t intr_mask;
    uint32_t intr_status;

    /* Request queues */
    uint32_t request_post[MPTSAS_REQUEST_QUEUE_DEPTH + 1];
    uint16_t request_post_head;
    uint16_t request_post_tail;

    uint32_t reply_post[MPTSAS_REPLY_QUEUE_DEPTH + 1];
    uint16_t reply_post_head;
    uint16_t reply_post_tail;

    uint32_t reply_free[MPTSAS_REPLY_QUEUE_DEPTH + 1];
    uint16_t reply_free_head;
    uint16_t reply_free_tail;

    /* IOC Facts */
    hwaddr host_mfa_high_addr;
    hwaddr sense_buffer_high_addr;
    uint16_t max_devices;
    uint16_t max_buses;
    uint16_t reply_frame_size;

    uint32_t fw_image_size;
    uint8_t *fw_image;
    uint8_t config_nvram[MPTSAS_CONFIG_PAGE_COUNT]
                        [MPTSAS_CONFIG_PAGE_DATA_SIZE];
    uint8_t config_nvram_written;
    uint8_t config_current[MPTSAS_CONFIG_PAGE_COUNT]
                          [MPTSAS_CONFIG_PAGE_DATA_SIZE];
    uint8_t config_current_written;
    uint32_t enclosure_status[MPTSAS_NUM_PORTS];

    /* IOC configuration page 1 current values. */
    uint32_t ioc1_flags;
    uint32_t ioc1_coalescing_timeout;
    uint8_t ioc1_coalescing_depth;

    uint32_t spi_port_configuration;
    uint32_t spi_port_on_bus_timer;
    uint32_t spi_requested_params[MPTSPI_MAX_TARGETS];
    uint32_t spi_configuration[MPTSPI_MAX_TARGETS];

    SCSIBus bus;
};

static inline SCSIBus *mpt_fusion_get_scsi_bus(PCIDevice *dev)
{
    return &MPT_FUSION(dev)->bus;
}

static inline bool mptsas_is_spi(const MPTSASState *s)
{
    return s->variant == MPT_FUSION_VARIANT_LSI53C1030;
}

static inline unsigned int mptsas_num_ports(const MPTSASState *s)
{
    return mptsas_is_spi(s) ? MPTSPI_NUM_PORTS : MPTSAS_NUM_PORTS;
}

static inline unsigned int mptsas_max_devices(const MPTSASState *s)
{
    return mptsas_is_spi(s) ? MPTSPI_MAX_TARGETS : MPTSAS_NUM_PORTS;
}

void mptsas_fix_scsi_io_endianness(MPIMsgSCSIIORequest *req);
void mptsas_fix_scsi_io_reply_endianness(MPIMsgSCSIIOReply *reply);
void mptsas_fix_scsi_task_mgmt_endianness(MPIMsgSCSITaskMgmt *req);
void mptsas_fix_scsi_task_mgmt_reply_endianness(MPIMsgSCSITaskMgmtReply *reply);
void mptsas_fix_ioc_init_endianness(MPIMsgIOCInit *req);
void mptsas_fix_ioc_init_reply_endianness(MPIMsgIOCInitReply *reply);
void mptsas_fix_ioc_facts_endianness(MPIMsgIOCFacts *req);
void mptsas_fix_ioc_facts_reply_endianness(MPIMsgIOCFactsReply *reply);
void mptsas_fix_config_endianness(MPIMsgConfig *req);
void mptsas_fix_config_reply_endianness(MPIMsgConfigReply *reply);
void mptsas_fix_port_facts_endianness(MPIMsgPortFacts *req);
void mptsas_fix_port_facts_reply_endianness(MPIMsgPortFactsReply *reply);
void mptsas_fix_port_enable_endianness(MPIMsgPortEnable *req);
void mptsas_fix_port_enable_reply_endianness(MPIMsgPortEnableReply *reply);
void mptsas_fix_event_notification_endianness(MPIMsgEventNotify *req);
void mptsas_fix_event_notification_reply_endianness(MPIMsgEventNotifyReply *reply);

void mptsas_reply(MPTSASState *s, MPIDefaultReply *reply);

void mptsas_process_config(MPTSASState *s, MPIMsgConfig *req);
unsigned mptsas_first_slot(const MPTSASState *s);

#endif /* MPTSAS_H */
