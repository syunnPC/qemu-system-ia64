/*
 * Common IA-64 chipset fault notification.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef HW_IA64_RAS_H
#define HW_IA64_RAS_H

#include "hw/ia64/ia64_ras_abi.h"
#include "hw/core/sysbus.h"
#include "qapi/error.h"

#define TYPE_IA64_RAS_HUB "ia64-ras-hub"
OBJECT_DECLARE_SIMPLE_TYPE(IA64RasHubState, IA64_RAS_HUB)

typedef enum IA64ChipsetFaultSource {
    IA64_CHIPSET_FAULT_ZX1_MIO,
    IA64_CHIPSET_FAULT_ZX1_IOA,
    IA64_CHIPSET_FAULT_ZX2_MIO,
    IA64_CHIPSET_FAULT_ZX2_IOA,
    IA64_CHIPSET_FAULT_460GX,
    IA64_CHIPSET_FAULT_PCIE,
} IA64ChipsetFaultSource;

typedef enum IA64ChipsetFaultReason {
    IA64_CHIPSET_FAULT_CONFIG_ABORT,
    IA64_CHIPSET_FAULT_DECODE,
    IA64_CHIPSET_FAULT_IOMMU,
    IA64_CHIPSET_FAULT_PARITY,
    IA64_CHIPSET_FAULT_PROTOCOL,
    IA64_CHIPSET_FAULT_AER,
    IA64_CHIPSET_FAULT_MEMORY_CORRECTED,
    IA64_CHIPSET_FAULT_MEMORY_UNCORRECTED,
    IA64_CHIPSET_FAULT_POWER,
} IA64ChipsetFaultReason;

typedef enum IA64RasSeverity {
    IA64_RAS_SEVERITY_RECOVERABLE = IA64_RAS_SAL_STATUS_RECOVERABLE,
    IA64_RAS_SEVERITY_FATAL = IA64_RAS_SAL_STATUS_FATAL,
    IA64_RAS_SEVERITY_CORRECTED = IA64_RAS_SAL_STATUS_CORRECTED,
} IA64RasSeverity;

typedef struct IA64PCIeFaultInfo {
    bool valid;
    uint32_t port_type;
    uint32_t version;
    uint32_t command_status;
    uint8_t device_id[16];
    uint32_t bridge_control_status;
    uint8_t capability[60];
    uint8_t aer[96];
} IA64PCIeFaultInfo;

typedef struct IA64ChipsetFault {
    IA64ChipsetFaultSource source;
    IA64ChipsetFaultReason reason;
    uint16_t segment;
    uint8_t bus;
    uint8_t severity;
    uint32_t requester;
    uint64_t address;
    uint64_t status;
    uint64_t information;
    IA64PCIeFaultInfo pcie;
} IA64ChipsetFault;

typedef bool (*IA64ChipsetFaultNotify)(void *opaque,
                                       const IA64ChipsetFault *fault);

IA64RasHubState *ia64_ras_hub_create(Object *parent, const char *name,
                                     hwaddr base, Error **errp);
bool ia64_ras_hub_report_chipset_fault(void *opaque,
                                       const IA64ChipsetFault *fault);
bool ia64_ras_hub_report_processor_error(IA64RasHubState *hub,
                                         CPUState *cs,
                                         IA64RasSeverity severity,
                                         uint64_t status,
                                         uint64_t address,
                                         uint64_t information);

#endif /* HW_IA64_RAS_H */
