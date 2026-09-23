/* SPDX-License-Identifier: GPL-2.0-or-later */

#ifndef IA64_FUNCTIONAL_SERVICES_H
#define IA64_FUNCTIONAL_SERVICES_H

#include "../ia64-test.h"

#define START_IMAGE_CHILD_SIGNATURE 0x4941363453544152ULL

typedef struct {
    UINT64 Signature;
    EFI_HANDLE Controller;
    VOID *Interface;
    BOOLEAN UseExit;
    VOID *ReinstallProtocol;
    BOOLEAN *ConnectReady;
} TEST_START_IMAGE_CHILD_OPTIONS;

UINT64 test_sal_rse_call(UINT64 Entry, UINT64 Gp, UINT64 Rsc);

EFI_STATUS ia64_services_main(EFI_HANDLE image_handle,
                              EFI_SYSTEM_TABLE *system_table,
                              BOOLEAN tables_only);

#endif /* IA64_FUNCTIONAL_SERVICES_H */
