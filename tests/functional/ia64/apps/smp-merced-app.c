/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "ia64-test.h"

#define TEST_SAL_SET_VECTORS        0x01000000ULL
#define TEST_SAL_VECTOR_BOOT_RENDEZ 2U
#define TEST_SAL_SUCCESS            0ULL
#define TEST_LOCAL_SAPIC_BASE       0x00000000fee00000ULL
#define TEST_AP_WAKE_VECTOR         0xffU
#define TEST_MAX_PROCESSOR_COUNT    64U
#define TEST_RENDEZVOUS_ROUNDS      2U
#define TEST_HANDLER_CHECKSUM_BYTES 16U
#define TEST_WAIT_TICKS             5000000000ULL
#define TEST_RETURN_TICKS           10000000ULL
#define TEST_RENDEZVOUS_PAGE        0x0000000006000000ULL
#define TEST_ALAT_ITERATIONS         4096U
#define TEST_ALAT_DATA_INDEX         TEST_MAX_PROCESSOR_COUNT
#define TEST_ALAT_READY_INDEX       (TEST_ALAT_DATA_INDEX + 1U)
#define TEST_ALAT_GO_INDEX          (TEST_ALAT_DATA_INDEX + 2U)
#define TEST_ALAT_SENTINEL           0xfeedfacecafebeefULL

typedef struct {
    UINT64 Status;
    UINT64 Value0;
    UINT64 Value1;
    UINT64 Value2;
} TEST_SAL_RETURN;

typedef TEST_SAL_RETURN (*TEST_SAL_PROC)(UINT64, UINT64, UINT64, UINT64,
                                        UINT64, UINT64, UINT64, UINT64);

static UINT8 sal_guid[16] = IA64_GUID_SAL;
static UINT8 acpi20_guid[16] = IA64_GUID_ACPI20;
static UINTN mProcessorCount;

static volatile UINT64 *rendezvous_counts(VOID)
{
    return (volatile UINT64 *)(UINTN)TEST_RENDEZVOUS_PAGE;
}

static VOID *find_config_table(EFI_SYSTEM_TABLE *SystemTable,
                               const UINT8 *Guid)
{
    UINTN i;

    for (i = 0; i < SystemTable->NumberOfTableEntries; i++) {
        if (ia64_bytes_equal(SystemTable->ConfigurationTable[i].VendorGuid,
                             Guid, 16)) {
            return (VOID *)(UINTN)
                SystemTable->ConfigurationTable[i].VendorTable;
        }
    }
    return NULL;
}

static UINT16 get_u16(const VOID *Address)
{
    const UINT8 *p = (const UINT8 *)Address;

    return (UINT16)p[0] | ((UINT16)p[1] << 8);
}

static UINT32 get_u32(const VOID *Address)
{
    const UINT8 *p = (const UINT8 *)Address;

    return (UINT32)p[0] | ((UINT32)p[1] << 8) |
           ((UINT32)p[2] << 16) | ((UINT32)p[3] << 24);
}

static UINT64 get_u64(const VOID *Address)
{
    const UINT8 *p = (const UINT8 *)Address;

    return (UINT64)get_u32(p) | ((UINT64)get_u32(p + 4) << 32);
}

static UINTN acpi_processor_count(EFI_SYSTEM_TABLE *SystemTable)
{
    UINT8 *rsdp = (UINT8 *)find_config_table(SystemTable, acpi20_guid);
    UINT8 *xsdt;
    UINT32 xsdt_length;
    UINTN entries;
    UINTN i;

    if (rsdp == NULL || !ia64_bytes_equal(rsdp, "RSD PTR ", 8) ||
        get_u32(rsdp + 20U) < 36U) {
        return 0;
    }
    xsdt = (UINT8 *)(UINTN)get_u64(rsdp + 24U);
    if (xsdt == NULL || get_u32(xsdt) != 0x54445358U) {
        return 0;
    }
    xsdt_length = get_u32(xsdt + 4U);
    if (xsdt_length < 36U || (xsdt_length - 36U) % 8U != 0) {
        return 0;
    }
    entries = (xsdt_length - 36U) / 8U;
    for (i = 0; i < entries; i++) {
        UINT8 *madt = (UINT8 *)(UINTN)get_u64(xsdt + 36U + i * 8U);
        UINT32 madt_length;
        UINTN offset;
        UINT64 present = 0;
        UINTN enabled = 0;

        if (madt == NULL || get_u32(madt) != 0x43495041U) {
            continue;
        }
        madt_length = get_u32(madt + 4U);
        if (madt_length < 44U) {
            return 0;
        }
        for (offset = 44U; offset + 2U <= madt_length; ) {
            UINTN length = madt[offset + 1U];

            if (length < 2U || length > madt_length - offset) {
                return 0;
            }
            if (madt[offset] == 7U && length >= 12U &&
                (get_u32(madt + offset + 8U) & 1U) != 0) {
                UINTN id = madt[offset + 3U];

                if (id >= TEST_MAX_PROCESSOR_COUNT ||
                    (present & (1ULL << id)) != 0) {
                    return 0;
                }
                present |= 1ULL << id;
                enabled++;
            }
            offset += length;
        }
        if (offset != madt_length || enabled == 0 ||
            enabled > TEST_MAX_PROCESSOR_COUNT ||
            present != (enabled == TEST_MAX_PROCESSOR_COUNT ?
                        ~(UINT64)0 : (1ULL << enabled) - 1U)) {
            return 0;
        }
        return enabled;
    }
    return 0;
}

static UINTN sal_descriptor_size(UINT8 Type)
{
    switch (Type) {
    case 0:
        return 48U;
    case 1:
        return 32U;
    case 2:
        return 16U;
    case 3:
        return 32U;
    case 4:
    case 5:
        return 16U;
    default:
        return 0;
    }
}

static BOOLEAN find_sal_descriptors(UINT8 *Sal, UINT64 *Procedure,
                                    UINT64 *Gp, UINT64 *WakeVector)
{
    UINT32 length;
    UINT16 entries;
    UINTN offset = 96U;
    UINTN i;
    BOOLEAN entrypoint = 0;
    BOOLEAN wake = 0;

    if (Sal == NULL || get_u32(Sal) != 0x5f545353U) {
        return 0;
    }
    length = get_u32(Sal + 4U);
    entries = get_u16(Sal + 10U);
    if (length < 96U || length > 4096U ||
        ia64_checksum8(Sal, length) != 0) {
        return 0;
    }
    for (i = 0; i < entries; i++) {
        UINTN size;

        if (offset >= length) {
            return 0;
        }
        size = sal_descriptor_size(Sal[offset]);
        if (size == 0 || size > length - offset) {
            return 0;
        }
        if (Sal[offset] == 0) {
            *Procedure = get_u64(Sal + offset + 16U);
            *Gp = get_u64(Sal + offset + 24U);
            entrypoint = *Procedure != 0;
        } else if (Sal[offset] == 5 && Sal[offset + 1U] == 0) {
            *WakeVector = get_u64(Sal + offset + 8U);
            wake = *WakeVector >= 0x10U && *WakeVector <= 0xffU;
        }
        offset += size;
    }
    return offset == length && entrypoint && wake;
}

static UINT64 read_lid(VOID)
{
    UINT64 lid;

    __asm__ volatile ("mov %0 = cr.lid;;" : "=r"(lid) : : "memory");
    return lid;
}

static UINT64 read_itc(VOID)
{
    UINT64 itc;

    __asm__ volatile ("mov %0 = ar.itc;;" : "=r"(itc) : : "memory");
    return itc;
}

/* Volatile keeps shared bare-metal state observable across CPUs. */
static UINT64 alat_local_probe(volatile UINT64 *Data)
{
    UINT64 value;

    __asm__ volatile ("ld8.a %0=[%1];;\n\t"
                      "mov %0=%2;;\n\t"
                      "ld8.c.nc %0=[%1];;"
                      : "=&r"(value)
                      : "r"(Data), "r"(TEST_ALAT_SENTINEL)
                      : "memory");
    return value;
}

static BOOLEAN alat_remote_store_probe(VOID)
{
    /* Volatile keeps the rendezvous handshake loads live. */
    volatile UINT64 *page = rendezvous_counts();
    UINT64 iteration;

    for (iteration = 1; iteration <= TEST_ALAT_ITERATIONS; iteration++) {
        UINT64 result;
        UINT64 observed;

        while (page[TEST_ALAT_READY_INDEX] != iteration) {
            __asm__ volatile ("hint @pause" : : : "memory");
        }
        __asm__ volatile (
            "ld8.a %0=[%3];;\n\t"
            "mov %0=%5;;\n\t"
            "st8.rel [%2]=%4;;\n"
            "1:\n\t"
            "ld8.acq %1=[%3];;\n\t"
            "cmp.eq p6,p7=%1,%4;;\n\t"
            "(p7) br.cond.spnt 1b;;\n\t"
            "ld8.c.nc %0=[%3];;"
            : "=&r"(result), "=&r"(observed)
            : "r"(&page[TEST_ALAT_GO_INDEX]),
              "r"(&page[TEST_ALAT_DATA_INDEX]), "r"(iteration),
              "r"(TEST_ALAT_SENTINEL)
            : "p6", "p7", "memory");
        if (result != iteration) {
            return 0;
        }
    }
    return 1;
}

static VOID ap_rendezvous(VOID)
{
    UINTN id = (read_lid() >> 24) & 0xffU;
    UINT64 masked = 1ULL << 16;

    /* The GP=0 SAL handler is position-independent and uses no app globals. */
    if (id > 0 && id < TEST_MAX_PROCESSOR_COUNT) {
        volatile UINT64 *counts =
            (volatile UINT64 *)(UINTN)TEST_RENDEZVOUS_PAGE;

        if (id == 1 && counts[id] == 0) {
            UINT64 iteration;

            for (iteration = 1; iteration <= TEST_ALAT_ITERATIONS;
                 iteration++) {
                counts[TEST_ALAT_READY_INDEX] = iteration;
                __asm__ volatile ("mf;;" : : : "memory");
                while (counts[TEST_ALAT_GO_INDEX] != iteration) {
                    __asm__ volatile ("hint @pause" : : : "memory");
                }
                counts[TEST_ALAT_DATA_INDEX] = iteration;
                __asm__ volatile ("mf;;" : : : "memory");
            }
        }

        counts[id]++;
        __asm__ volatile ("mf;;" : : : "memory");
        /*
         * SAL defines TPR as scratch on return.  Leave the wake vector
         * masked until firmware regains control and clears it.
         */
        __asm__ volatile ("mov cr.tpr = %0;;\n\tsrlz.d;;"
                          : : "r"(masked) : "memory");
    }
}

static VOID send_wake_ipi(UINTN Id, UINT64 Vector)
{
    volatile UINT64 *ipi = (volatile UINT64 *)(UINTN)
        (TEST_LOCAL_SAPIC_BASE + (Id << 12));

    __asm__ volatile ("mf;;" : : : "memory");
    *ipi = Vector;
}

static BOOLEAN wait_for_round(UINT64 Round)
{
    volatile UINT64 *counts = rendezvous_counts();
    UINT64 deadline = read_itc() + TEST_WAIT_TICKS;

    for (;;) {
        UINTN id;
        BOOLEAN complete = 1;

        __asm__ volatile ("mf;;" : : : "memory");
        for (id = 1; id < mProcessorCount; id++) {
            if (counts[id] < Round) {
                complete = 0;
            }
        }
        if (complete) {
            return 1;
        }
        if ((INTN)(read_itc() - deadline) >= 0) {
            return 0;
        }
        __asm__ volatile ("hint @pause" : : : "memory");
    }
}

static VOID wait_for_rendezvous_return(VOID)
{
    UINT64 deadline = read_itc() + TEST_RETURN_TICKS;

    while ((INTN)(read_itc() - deadline) < 0) {
        __asm__ volatile ("hint @pause" : : : "memory");
    }
}

EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    IA64_TEST_CONTEXT context = {
        .SystemTable = SystemTable,
        .Suite = "smp-merced",
        .Passed = 0,
        .Failed = 0,
        .DirectUart = 0,
    };
    UINT8 *sal = (UINT8 *)find_config_table(SystemTable, sal_guid);
    UINT64 sal_descriptor[2] __attribute__((aligned(16)));
    UINT64 *handler_descriptor = (UINT64 *)(UINTN)ap_rendezvous;
    UINT64 wake_vector = 0;
    TEST_SAL_RETURN result = { ~(UINT64)0, 0, 0, 0 };
    TEST_SAL_PROC sal_proc;
    EFI_PHYSICAL_ADDRESS rendezvous_page = TEST_RENDEZVOUS_PAGE;
    EFI_STATUS page_status;
    UINTN id;
    BOOLEAN descriptors;
    BOOLEAN first_round = 0;
    BOOLEAN second_round = 0;
    BOOLEAN full_alat = 0;
    BOOLEAN alat_remote_store = 0;

    (void)ImageHandle;
    mProcessorCount = acpi_processor_count(SystemTable);
    page_status = SystemTable->BootServices->AllocatePages(
        AllocateAddress, EfiLoaderData, 1, &rendezvous_page);
    if (page_status == EFI_SUCCESS) {
        UINTN offset;

        for (offset = 0; offset < EFI_PAGE_SIZE; offset += sizeof(UINT64)) {
            *(volatile UINT64 *)(UINTN)(rendezvous_page + offset) = 0;
        }
        full_alat = alat_local_probe(
            &rendezvous_counts()[TEST_ALAT_DATA_INDEX]) ==
            TEST_ALAT_SENTINEL;
    }
    descriptors = find_sal_descriptors(sal, &sal_descriptor[0],
                                       &sal_descriptor[1], &wake_vector);
    ia64_test_check(&context, "sal-ap-wake",
                    mProcessorCount != 0 &&
                        page_status == EFI_SUCCESS && descriptors &&
                        wake_vector == TEST_AP_WAKE_VECTOR,
                    EFI_DEVICE_ERROR, "missing-rendezvous-descriptor");

    if (descriptors) {
        sal_proc = (TEST_SAL_PROC)(UINTN)sal_descriptor;
        result = sal_proc(TEST_SAL_SET_VECTORS,
                          TEST_SAL_VECTOR_BOOT_RENDEZ,
                          handler_descriptor[0], 0,
                          TEST_HANDLER_CHECKSUM_BYTES, 0, 0, 0);
    }
    if (result.Status == TEST_SAL_SUCCESS) {
        for (id = 1; id < mProcessorCount; id++) {
            send_wake_ipi(id, wake_vector);
        }
        if (mProcessorCount > 1) {
            alat_remote_store = alat_remote_store_probe();
        }
        first_round = wait_for_round(1);
        if (first_round) {
            wait_for_rendezvous_return();
            for (id = 1; id < mProcessorCount; id++) {
                send_wake_ipi(id, wake_vector);
            }
            second_round = wait_for_round(TEST_RENDEZVOUS_ROUNDS);
        }
    }

    ia64_test_check(&context, "merced-rendezvous",
                    result.Status == TEST_SAL_SUCCESS && first_round,
                    EFI_TIMEOUT, "secondary-start-timeout");
    ia64_test_check(&context, "merced-rendezvous-return", second_round,
                    EFI_TIMEOUT, "secondary-return-timeout");
    if (full_alat) {
        ia64_test_check(&context, "full-alat-smp-store-ordering",
                        alat_remote_store, EFI_DEVICE_ERROR,
                        "stale-full-alat-entry");
    } else {
        ia64_test_check(&context, "zero-alat-check-reload",
                        alat_remote_store, EFI_DEVICE_ERROR,
                        "zero-alat-did-not-reload");
    }
    ia64_test_done(&context);
    return context.Failed == 0 ? EFI_SUCCESS : EFI_DEVICE_ERROR;
}

EFI_STATUS (*efi_entry_descriptor_reference)(EFI_HANDLE, EFI_SYSTEM_TABLE *)
    __attribute__((used)) = efi_main;
