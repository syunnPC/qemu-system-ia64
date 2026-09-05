/* SPDX-License-Identifier: GPL-2.0-or-later */

#include "ia64-test.h"

#define TEST_SAL_SET_VECTORS        0x01000000ULL
#define TEST_SAL_GET_STATE_INFO     0x01000001ULL
#define TEST_SAL_CLEAR_STATE_INFO   0x01000003ULL
#define TEST_SAL_MC_SET_PARAMS      0x01000005ULL
#define TEST_SAL_VECTOR_OS_MCA      0U
#define TEST_SAL_VECTOR_OS_INIT     1U
#define TEST_SAL_VECTOR_BOOT_RENDEZ 2U
#define TEST_SAL_STATE_MCA          0U
#define TEST_SAL_STATE_INIT         1U
#define TEST_SAL_MC_WAKEUP_PARAM    2U
#define TEST_SAL_MC_MEMORY_WAKEUP   2U
#define TEST_SAL_SUCCESS            0ULL
#define TEST_SAL_NO_INFORMATION     ((UINT64)-5)
#define TEST_SAL_INVALID_ADDRESS    ((UINT64)-4)
#define TEST_SAPIC_BASE             0x00000000fee00000ULL
#define TEST_SAPIC_DELIVERY_INIT    (5ULL << 8)
#define TEST_AP_WAKE_VECTOR         0xffULL
#define TEST_WAIT_TICKS             2000000000ULL
#define TEST_MCA_RETURN_CONTROL_PA  0x01800000ULL
#define TEST_MCA_RETURN_OBSERVED_PA 0x01800008ULL

typedef struct {
    UINT64 Status;
    UINT64 Value0;
    UINT64 Value1;
    UINT64 Value2;
} TEST_SAL_RETURN;

typedef TEST_SAL_RETURN (*TEST_SAL_PROC)(UINT64, UINT64, UINT64, UINT64,
                                        UINT64, UINT64, UINT64, UINT64);

static UINT8 sal_guid[16] = IA64_GUID_SAL;
static UINT64 sal_descriptor[2] __attribute__((aligned(16)));
static UINT8 init_record[2048] __attribute__((aligned(16)));
static UINT8 mca_record[2048] __attribute__((aligned(16)));
UINT8 ras_os_init_stack[16384] __attribute__((aligned(16), used));
UINT8 ras_os_mca_stack[16384] __attribute__((aligned(16), used));
static volatile UINT64 wakeup_value __attribute__((aligned(8)));
static volatile UINT64 init_primary_count;
static volatile UINT64 init_secondary_count;
static volatile UINT64 init_reason;
static volatile UINT64 ap_wake_count;
static volatile UINT64 mca_count;
static volatile UINT64 mca_reason;
static volatile UINT64 mca_get_status;
static volatile UINT64 mca_get_length;
static volatile UINT64 mca_bad_address_status;
static volatile UINT64 mca_clear_status;
static volatile UINT64 mca_after_clear_status;

extern VOID ras_os_init_entry(VOID);
extern VOID ras_os_mca_entry(VOID);
extern UINT64 ras_wait_preserved(volatile UINT64 *Counter, UINT64 SendInit);

__asm__(
    ".section .text.ras_os_init_entry, \"ax\"\n"
    ".align 16\n"
    ".global ras_os_init_entry\n"
    ".type ras_os_init_entry, @function\n"
    ".proc ras_os_init_entry\n"
    ".explicit\n"
    "ras_os_init_entry:\n"
    "  alloc r32 = ar.pfs, 0, 3, 5, 0\n"
    "  ;;\n"
    "  mov r33 = r1\n"
    "  mov r34 = r12\n"
    "  mov r5 = r10\n"
    "  movl r12 = ras_os_init_stack + 16384\n"
    "  mov r35 = r8\n"
    "  mov r36 = r9\n"
    "  mov r37 = r10\n"
    "  mov r38 = r11\n"
    "  ;;\n"
    "  br.call.sptk.many b0 = ras_os_init_handler\n"
    "  ;;\n"
    "  mov r9 = r5\n"
    "  mov r10 = r0\n"
    "  mov b6 = r34\n"
    "  ;;\n"
    "  br.sptk.many b6\n"
    "  ;;\n"
    ".endp ras_os_init_entry\n"
    ".size ras_os_init_entry, . - ras_os_init_entry\n"
    ".align 16\n"
    ".global ras_os_mca_entry\n"
    ".type ras_os_mca_entry, @function\n"
    ".proc ras_os_mca_entry\n"
    ".explicit\n"
    "ras_os_mca_entry:\n"
    "  alloc r32 = ar.pfs, 0, 3, 5, 0\n"
    "  ;;\n"
    "  mov r33 = r1\n"
    "  mov r34 = r12\n"
    "  mov r5 = r10\n"
    "  movl r12 = ras_os_mca_stack + 16384\n"
    "  mov r35 = r8\n"
    "  mov r36 = r9\n"
    "  mov r37 = r10\n"
    "  mov r38 = r11\n"
    "  ;;\n"
    "  br.call.sptk.many b0 = ras_os_mca_handler\n"
    "  ;;\n"
    "  mov r9 = r5\n"
    "  mov r10 = r0\n"
    "  mov b6 = r34\n"
    "  ;;\n"
    "  br.sptk.many b6\n"
    "  ;;\n"
    ".endp ras_os_mca_entry\n"
    ".size ras_os_mca_entry, . - ras_os_mca_entry\n"
    /*
     * Keep non-minimal state live across the entire handled interruption.
     * No C call is allowed between setting and checking these registers.
     * The host waits for the ready word before delivering MCA.
     */
    ".align 16\n"
    ".global ras_wait_preserved\n"
    ".type ras_wait_preserved, @function\n"
    ".proc ras_wait_preserved\n"
    ".explicit\n"
    "ras_wait_preserved:\n"
    "  alloc r34 = ar.pfs, 2, 8, 0, 0\n"
    "  ;;\n"
    "  mov r35 = b6\n"
    "  mov r36 = ar.ccv\n"
    "  mov r37 = ar.lc\n"
    "  mov r38 = ar.ec\n"
    "  mov r39 = ar.unat\n"
    "  mov r40 = ar.fpsr\n"
    "  mov r41 = ar.itc\n"
    "  movl r14 = 2000000000\n"
    "  ;;\n"
    "  add r41 = r41, r14\n"
    "  movl r17 = 0x123456789abcdef0\n"
    "  mov r18 = 7\n"
    "  mov r19 = 5\n"
    "  ;;\n"
    "  mov b6 = r17\n"
    "  mov ar.ccv = r17\n"
    "  mov ar.unat = r17\n"
    "  mov ar.lc = r18\n"
    "  mov ar.ec = r19\n"
    "  setf.sig f6 = r17\n"
    "  setf.sig f32 = r17\n"
    "  cmp.eq p6, p7 = r33, r0\n"
    "  ;;\n"
    "(p6) movl r14 = 0x01800010\n"
    "(p6) mov r15 = 1\n"
    "(p7) movl r14 = 0xfee00000\n"
    "(p7) mov r15 = 0x500\n"
    "  ;;\n"
    "  mf\n"
    "  ;;\n"
    "  st8 [r14] = r15\n"
    "  ;;\n"
    "1:\n"
    "  ld8 r14 = [r32]\n"
    "  mov r15 = ar.itc\n"
    "  ;;\n"
    "  cmp.ne p6, p7 = r14, r0\n"
    "  sub r15 = r15, r41\n"
    "  ;;\n"
    "(p6) br.cond.sptk.few 2f\n"
    "  cmp.lt p8, p9 = r15, r0\n"
    "  ;;\n"
    "(p8) br.cond.sptk.few 1b\n"
    "  ;;\n"
    "  mov r8 = r0\n"
    "  br.cond.sptk.few 3f\n"
    "  ;;\n"
    "2:\n"
    "  mov r8 = 1\n"
    "  mov r14 = ar.pfs\n"
    "  ;;\n"
    "  cmp.ne p6, p7 = r14, r34\n"
    "  ;;\n"
    "(p6) mov r8 = r0\n"
    "  .irp reg, b6, ar.ccv, ar.unat\n"
    "  mov r14 = \\reg\n"
    "  ;;\n"
    "  cmp.ne p6, p7 = r14, r17\n"
    "  ;;\n"
    "(p6) mov r8 = r0\n"
    "  .endr\n"
    "  mov r14 = ar.lc\n"
    "  ;;\n"
    "  cmp.ne p6, p7 = r14, r18\n"
    "  ;;\n"
    "(p6) mov r8 = r0\n"
    "  mov r14 = ar.ec\n"
    "  ;;\n"
    "  cmp.ne p6, p7 = r14, r19\n"
    "  ;;\n"
    "(p6) mov r8 = r0\n"
    "  mov r14 = ar.fpsr\n"
    "  ;;\n"
    "  cmp.ne p6, p7 = r14, r40\n"
    "  ;;\n"
    "(p6) mov r8 = r0\n"
    "  .irp reg, f6, f32\n"
    "  getf.sig r14 = \\reg\n"
    "  ;;\n"
    "  cmp.ne p6, p7 = r14, r17\n"
    "  ;;\n"
    "(p6) mov r8 = r0\n"
    "  .endr\n"
    "3:\n"
    "  mov b6 = r35\n"
    "  mov ar.ccv = r36\n"
    "  mov ar.lc = r37\n"
    "  mov ar.ec = r38\n"
    "  mov ar.unat = r39\n"
    "  mov ar.fpsr = r40\n"
    "  mov ar.pfs = r34\n"
    "  ;;\n"
    "  br.ret.sptk.many b0\n"
    "  ;;\n"
    ".endp ras_wait_preserved\n"
    ".size ras_wait_preserved, . - ras_wait_preserved\n"
    ".text\n");

UINT64 ras_os_init_handler(UINT64 PalProc, UINT64 SalProc,
                           UINT64 FirmwareGp, UINT64 Reason)
{
    (void)PalProc;
    (void)SalProc;
    (void)FirmwareGp;
    init_reason = Reason;
    if (Reason == 0) {
        init_primary_count++;
    } else {
        init_secondary_count++;
    }
    __asm__ volatile ("mf;;" : : : "memory");
    return 0;
}

UINT64 ras_os_mca_handler(UINT64 PalProc, UINT64 SalProc,
                          UINT64 FirmwareGp, UINT64 Reason)
{
    volatile UINT64 *return_control =
        (volatile UINT64 *)(UINTN)TEST_MCA_RETURN_CONTROL_PA;
    volatile UINT64 *return_observed =
        (volatile UINT64 *)(UINTN)TEST_MCA_RETURN_OBSERVED_PA;
    TEST_SAL_PROC sal_proc;
    TEST_SAL_RETURN result;

    (void)PalProc;
    sal_descriptor[0] = SalProc;
    sal_descriptor[1] = FirmwareGp;
    sal_proc = (TEST_SAL_PROC)(UINTN)sal_descriptor;
    mca_reason = Reason;
    result = sal_proc(TEST_SAL_GET_STATE_INFO, TEST_SAL_STATE_MCA, 0,
                      0x60000000ULL, 0, 0, 0, 0);
    mca_bad_address_status = result.Status;
    result = sal_proc(TEST_SAL_GET_STATE_INFO, TEST_SAL_STATE_MCA, 0,
                      (UINT64)(UINTN)mca_record, 0, 0, 0, 0);
    mca_get_status = result.Status;
    mca_get_length = result.Value0;
    result = sal_proc(TEST_SAL_CLEAR_STATE_INFO, TEST_SAL_STATE_MCA,
                      0, 0, 0, 0, 0, 0);
    mca_clear_status = result.Status;
    result = sal_proc(TEST_SAL_GET_STATE_INFO, TEST_SAL_STATE_MCA, 0,
                      (UINT64)(UINTN)mca_record, 0, 0, 0, 0);
    mca_after_clear_status = result.Status;
    mca_count++;
    __asm__ volatile ("mf;;" : : : "memory");
    *return_observed = *return_control;
    __asm__ volatile ("mf;;" : : : "memory");
    return *return_observed;
}

static VOID ap_wake_handler(VOID)
{
    ap_wake_count++;
    __asm__ volatile ("mf;;" : : : "memory");
}

static BOOLEAN bytes_equal(const UINT8 *Left, const UINT8 *Right, UINTN Size)
{
    UINTN i;

    for (i = 0; i < Size; i++) {
        if (Left[i] != Right[i]) {
            return 0;
        }
    }
    return 1;
}

static VOID *find_config_table(EFI_SYSTEM_TABLE *SystemTable,
                               const UINT8 *Guid)
{
    UINTN i;

    for (i = 0; i < SystemTable->NumberOfTableEntries; i++) {
        if (bytes_equal(SystemTable->ConfigurationTable[i].VendorGuid,
                        Guid, 16)) {
            return (VOID *)(UINTN)
                SystemTable->ConfigurationTable[i].VendorTable;
        }
    }
    return NULL;
}

static UINT16 get_u16(const VOID *Address)
{
    const UINT8 *p = Address;

    return (UINT16)p[0] | ((UINT16)p[1] << 8);
}

static UINT32 get_u32(const VOID *Address)
{
    const UINT8 *p = Address;

    return (UINT32)p[0] | ((UINT32)p[1] << 8) |
           ((UINT32)p[2] << 16) | ((UINT32)p[3] << 24);
}

static UINT64 get_u64(const VOID *Address)
{
    const UINT8 *p = Address;

    return (UINT64)get_u32(p) | ((UINT64)get_u32(p + 4) << 32);
}

static UINTN sal_descriptor_size(UINT8 Type)
{
    switch (Type) {
    case 0:
        return 48;
    case 1:
        return 32;
    case 2:
        return 16;
    case 3:
        return 32;
    case 4:
    case 5:
        return 16;
    default:
        return 0;
    }
}

static BOOLEAN find_sal_entry(UINT8 *Sal, UINT64 *Procedure, UINT64 *Gp)
{
    UINT32 length;
    UINT16 entries;
    UINTN offset = 96;
    UINTN i;

    if (Sal == NULL || get_u32(Sal) != 0x5f545353U) {
        return 0;
    }
    length = get_u32(Sal + 4);
    entries = get_u16(Sal + 10);
    if (length < 96 || length > 4096 || ia64_checksum8(Sal, length) != 0) {
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
            *Procedure = get_u64(Sal + offset + 16);
            *Gp = get_u64(Sal + offset + 24);
        }
        offset += size;
    }
    return offset == length && *Procedure != 0;
}

static UINT64 read_itc(VOID)
{
    UINT64 value;

    __asm__ volatile ("mov %0 = ar.itc;;" : "=r"(value));
    return value;
}

static BOOLEAN wait_nonzero(volatile UINT64 *Value)
{
    UINT64 deadline = read_itc() + TEST_WAIT_TICKS;

    while (*Value == 0) {
        if ((INTN)(read_itc() - deadline) >= 0) {
            return 0;
        }
        __asm__ volatile ("hint @pause;;" : : : "memory");
    }
    return 1;
}

EFI_STATUS efi_main(EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable)
{
    IA64_TEST_CONTEXT context = {
        .SystemTable = SystemTable,
        .Suite = "ras",
        .Passed = 0,
        .Failed = 0,
        .DirectUart = 0,
    };
    UINT8 *sal = find_config_table(SystemTable, sal_guid);
    UINT64 *init_handler = (UINT64 *)(UINTN)ras_os_init_entry;
    UINT64 *mca_handler = (UINT64 *)(UINTN)ras_os_mca_entry;
    UINT64 *ap_handler = (UINT64 *)(UINTN)ap_wake_handler;
    TEST_SAL_PROC sal_proc;
    TEST_SAL_RETURN result;
    BOOLEAN entry_found;
    BOOLEAN preserved;

    (void)ImageHandle;
    entry_found = find_sal_entry(sal, &sal_descriptor[0],
                                 &sal_descriptor[1]);
    sal_proc = (TEST_SAL_PROC)(UINTN)sal_descriptor;
    ia64_test_check(&context, "sal-entry", entry_found,
                    EFI_NOT_FOUND, "sal-descriptor");
    if (!entry_found) {
        ia64_test_done(&context);
        return EFI_NOT_FOUND;
    }

    result = sal_proc(TEST_SAL_SET_VECTORS, TEST_SAL_VECTOR_OS_INIT,
                      init_handler[0], init_handler[1], 64,
                      init_handler[0], init_handler[1], 64);
    ia64_test_check(&context, "os-init-register",
                    result.Status == TEST_SAL_SUCCESS,
                    result.Status, "sal-set-vectors");
    result = sal_proc(TEST_SAL_SET_VECTORS, TEST_SAL_VECTOR_OS_MCA,
                      mca_handler[0], mca_handler[1], 64, 0, 0, 0);
    ia64_test_check(&context, "os-mca-register",
                    result.Status == TEST_SAL_SUCCESS,
                    result.Status, "sal-set-vectors");
    result = sal_proc(TEST_SAL_SET_VECTORS, TEST_SAL_VECTOR_BOOT_RENDEZ,
                      ap_handler[0], ap_handler[1], 64, 0, 0, 0);
    ia64_test_check(&context, "ap-rendezvous-register",
                    result.Status == TEST_SAL_SUCCESS,
                    result.Status, "sal-set-vectors");
    *(volatile UINT64 *)(UINTN)(TEST_SAPIC_BASE + (1ULL << 12)) =
        TEST_AP_WAKE_VECTOR;
    ia64_test_check(&context, "ap-online", wait_nonzero(&ap_wake_count),
                    EFI_TIMEOUT, "boot-rendezvous");
    result = sal_proc(TEST_SAL_MC_SET_PARAMS, TEST_SAL_MC_WAKEUP_PARAM,
                      TEST_SAL_MC_MEMORY_WAKEUP,
                      (UINT64)(UINTN)&wakeup_value, 0, 0, 0, 0);
    ia64_test_check(&context, "mca-wakeup-register",
                    result.Status == TEST_SAL_SUCCESS,
                    result.Status, "sal-mc-set-params");

    preserved = ras_wait_preserved(&init_primary_count, 1);
    ia64_test_check(&context, "os-init-primary",
                    init_primary_count != 0 && init_reason == 0,
                    EFI_TIMEOUT, "primary-handler");
    ia64_test_check(&context, "os-init-state", preserved,
                    EFI_DEVICE_ERROR, "interrupted-registers");
    result = sal_proc(TEST_SAL_GET_STATE_INFO, TEST_SAL_STATE_INIT, 0,
                      (UINT64)(UINTN)init_record, 0, 0, 0, 0);
    ia64_test_check(&context, "init-record",
                    result.Status == TEST_SAL_SUCCESS && result.Value0 >= 64,
                    result.Status, "sal-get-state-info");
    result = sal_proc(TEST_SAL_CLEAR_STATE_INFO, TEST_SAL_STATE_INIT,
                      0, 0, 0, 0, 0, 0);
    ia64_test_check(&context, "init-clear",
                    result.Status == TEST_SAL_SUCCESS,
                    result.Status, "sal-clear-state-info");

    ia64_test_pass(&context, "mca-injection-ready");
    preserved = ras_wait_preserved(&mca_count, 0);
    ia64_test_check(&context, "os-mca-return",
                    mca_count != 0, EFI_TIMEOUT, "mca-handler");
    ia64_test_check(&context, "os-mca-state", preserved,
                    EFI_DEVICE_ERROR, "interrupted-registers");
    ia64_test_check(&context, "os-mca-gr11",
                    mca_reason == 2, EFI_DEVICE_ERROR, "rendezvous-result");
    ia64_test_check(&context, "os-init-secondary",
                    init_secondary_count != 0 && init_reason != 0,
                    EFI_DEVICE_ERROR, "secondary-handler");
    ia64_test_check(&context, "mca-record-lifecycle",
                    mca_bad_address_status == TEST_SAL_INVALID_ADDRESS &&
                    mca_get_status == TEST_SAL_SUCCESS &&
                    mca_get_length >= 64 &&
                    mca_clear_status == TEST_SAL_SUCCESS &&
                    mca_after_clear_status == TEST_SAL_NO_INFORMATION,
                    EFI_DEVICE_ERROR, "sal-state-info");
    ia64_test_done(&context);
    return context.Failed == 0 ? EFI_SUCCESS : EFI_DEVICE_ERROR;
}

EFI_STATUS (*efi_entry_descriptor_reference)(EFI_HANDLE, EFI_SYSTEM_TABLE *)
    __attribute__((used)) = efi_main;
