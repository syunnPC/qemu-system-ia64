/*
 * SPDX-License-Identifier: GPL-2.0-or-later
 *
 * EFI 1.10 native Debug Support protocol for IA-64.
 */

#include "fw-debug-support.h"
#include "fw-services.h"

typedef struct {
    UINT64 Reserved;
    UINT64 R[31];
    UINT64 F[30][2];
    UINT64 Pr;
    UINT64 B[8];

    UINT64 ArRsc;
    UINT64 ArBsp;
    UINT64 ArBspstore;
    UINT64 ArRnat;
    UINT64 ArFcr;
    UINT64 ArEflag;
    UINT64 ArCsd;
    UINT64 ArSsd;
    UINT64 ArCflg;
    UINT64 ArFsr;
    UINT64 ArFir;
    UINT64 ArFdr;
    UINT64 ArCcv;
    UINT64 ArUnat;
    UINT64 ArFpsr;
    UINT64 ArPfs;
    UINT64 ArLc;
    UINT64 ArEc;

    UINT64 CrDcr;
    UINT64 CrItm;
    UINT64 CrIva;
    UINT64 CrPta;
    UINT64 CrIpsr;
    UINT64 CrIsr;
    UINT64 CrIip;
    UINT64 CrIfa;
    UINT64 CrItir;
    UINT64 CrIipa;
    UINT64 CrIfs;
    UINT64 CrIim;
    UINT64 CrIha;

    UINT64 Dbr[8];
    UINT64 Ibr[8];
    UINT64 IntNat;
} EFI_SYSTEM_CONTEXT_IPF;

typedef union {
    VOID *SystemContextEbc;
    VOID *SystemContextIa32;
    EFI_SYSTEM_CONTEXT_IPF *SystemContextIpf;
} EFI_SYSTEM_CONTEXT;

typedef VOID (*EFI_PERIODIC_CALLBACK)(EFI_SYSTEM_CONTEXT SystemContext);
typedef VOID (*EFI_EXCEPTION_CALLBACK)(EFI_EXCEPTION_TYPE ExceptionType,
                                       EFI_SYSTEM_CONTEXT SystemContext);

typedef struct _EFI_DEBUG_SUPPORT_PROTOCOL EFI_DEBUG_SUPPORT_PROTOCOL;

typedef EFI_STATUS (*EFI_GET_MAXIMUM_PROCESSOR_INDEX)(
    EFI_DEBUG_SUPPORT_PROTOCOL *This, UINTN *MaxProcessorIndex);
typedef EFI_STATUS (*EFI_REGISTER_PERIODIC_CALLBACK)(
    EFI_DEBUG_SUPPORT_PROTOCOL *This, UINTN ProcessorIndex,
    EFI_PERIODIC_CALLBACK PeriodicCallback);
typedef EFI_STATUS (*EFI_REGISTER_EXCEPTION_CALLBACK)(
    EFI_DEBUG_SUPPORT_PROTOCOL *This, UINTN ProcessorIndex,
    EFI_EXCEPTION_CALLBACK ExceptionCallback, EFI_EXCEPTION_TYPE ExceptionType);
typedef EFI_STATUS (*EFI_INVALIDATE_INSTRUCTION_CACHE)(
    EFI_DEBUG_SUPPORT_PROTOCOL *This, UINTN ProcessorIndex, VOID *Start,
    UINT64 Length);

struct _EFI_DEBUG_SUPPORT_PROTOCOL {
    UINT32 Isa;
    EFI_GET_MAXIMUM_PROCESSOR_INDEX GetMaximumProcessorIndex;
    EFI_REGISTER_PERIODIC_CALLBACK RegisterPeriodicCallback;
    EFI_REGISTER_EXCEPTION_CALLBACK RegisterExceptionCallback;
    EFI_INVALIDATE_INSTRUCTION_CACHE InvalidateInstructionCache;
};

#define DEBUG_SUPPORT_ISA_IPF                 0x0200U
#define DEBUG_SUPPORT_EXCEPTION_COUNT         48U
#define DEBUG_SUPPORT_EXTERNAL_INTERRUPT      12
#define DEBUG_SUPPORT_BREAKPOINT              11
#define DEBUG_SUPPORT_TIMER_VECTOR            0xefU
#define DEBUG_SUPPORT_TIMER_INTERVAL          \
    (10ULL * 1000ULL * FW_ITC_TICKS_PER_MICROSECOND)
#define DEBUG_SUPPORT_ITV_MASK                (1ULL << 16)
#define DEBUG_SUPPORT_IVT_STUB_MAX            64U
#define DEBUG_SUPPORT_PSR_RI_MASK             (3ULL << 41)
#define DEBUG_SUPPORT_PSR_RI_SHIFT            41U

typedef struct {
    BOOLEAN installed;
    UINTN length;
    UINT8 original[DEBUG_SUPPORT_IVT_STUB_MAX];
} DEBUG_SUPPORT_IVT_HOOK;

static const UINT8 mDebugSupportProtocolGuid[16] = {
    0x0c, 0x59, 0x55, 0x27, 0x3c, 0x6f, 0xfa, 0x42,
    0x9e, 0xa4, 0xa3, 0xba, 0x54, 0x3c, 0xda, 0x25,
};

static EFI_DEBUG_SUPPORT_PROTOCOL mDebugSupportProtocol;
static EFI_HANDLE mDebugSupportHandle;
static EFI_PERIODIC_CALLBACK mDebugPeriodicCallback;
static EFI_EXCEPTION_CALLBACK
    mDebugExceptionCallbacks[DEBUG_SUPPORT_EXCEPTION_COUNT];
static DEBUG_SUPPORT_IVT_HOOK mDebugIvtHooks[DEBUG_SUPPORT_EXCEPTION_COUNT];
static BOOLEAN mDebugDispatchActive[FW_MAX_CPUS];
static BOOLEAN mDebugTimerOwned;
static UINT64 mDebugSavedItv;
static UINT64 mDebugSavedItm;
static BOOLEAN mDebugCollectionOwned;
static BOOLEAN mDebugCollectionWasEnabled;

extern UINT8 fw_debug_ivt_stub_start[];
extern UINT8 fw_debug_ivt_stub_end[];

FW_STATIC_ASSERT(sizeof(EFI_SYSTEM_CONTEXT_IPF) == 1192,
                 debug_support_ipf_context_size);
FW_STATIC_ASSERT(__builtin_offsetof(EFI_SYSTEM_CONTEXT_IPF, R) == 8,
                 debug_support_ipf_r1_offset);
FW_STATIC_ASSERT(__builtin_offsetof(EFI_SYSTEM_CONTEXT_IPF, F) == 256,
                 debug_support_ipf_f2_offset);
FW_STATIC_ASSERT(__builtin_offsetof(EFI_SYSTEM_CONTEXT_IPF, CrDcr) == 952,
                 debug_support_ipf_cr_offset);
FW_STATIC_ASSERT(__builtin_offsetof(EFI_SYSTEM_CONTEXT_IPF, IntNat) == 1184,
                 debug_support_ipf_intnat_offset);

static BOOLEAN debug_support_exception_valid(EFI_EXCEPTION_TYPE ExceptionType)
{
    return (ExceptionType >= 0 && ExceptionType <= 12) ||
           (ExceptionType >= 20 && ExceptionType <= 27) ||
           (ExceptionType >= 29 && ExceptionType <= 36) ||
           (ExceptionType >= 45 && ExceptionType <= 47);
}

static UINTN debug_support_ivt_offset(EFI_EXCEPTION_TYPE ExceptionType)
{
    if (ExceptionType < 20) {
        return (UINTN)ExceptionType * 0x400U;
    }
    return 0x5000U + ((UINTN)ExceptionType - 20U) * 0x100U;
}

static UINT64 debug_support_interrupt_save(VOID)
{
    UINT64 psr = fw_read_psr();

    __asm__ volatile("rsm psr.i;;\n\tsrlz.d;;" ::: "memory");
    return psr;
}

static VOID debug_support_interrupt_restore(UINT64 SavedPsr)
{
    if ((SavedPsr & IA64_PSR_I) != 0) {
        __asm__ volatile("ssm psr.i;;\n\tsrlz.d;;" ::: "memory");
    }
}

static VOID debug_support_collection_acquire(VOID)
{
    if (mDebugCollectionOwned) {
        return;
    }
    mDebugCollectionWasEnabled = (fw_read_psr() & IA64_PSR_IC) != 0;
    if (!mDebugCollectionWasEnabled) {
        __asm__ volatile("ssm psr.ic;;\n\tsrlz.d;;" ::: "memory");
    }
    mDebugCollectionOwned = 1;
}

static VOID debug_support_collection_release(VOID)
{
    UINTN exception_type;

    if (!mDebugCollectionOwned) {
        return;
    }
    for (exception_type = 0; exception_type < DEBUG_SUPPORT_EXCEPTION_COUNT;
         exception_type++) {
        if (mDebugIvtHooks[exception_type].installed) {
            return;
        }
    }
    if (!mDebugCollectionWasEnabled) {
        __asm__ volatile("rsm psr.ic;;\n\tsrlz.d;;" ::: "memory");
    }
    mDebugCollectionOwned = 0;
}

static UINT64 debug_support_read_itv(VOID)
{
    UINT64 value;

    __asm__ volatile("mov %0 = cr.itv;;" : "=r"(value));
    return value;
}

static UINT64 debug_support_read_itm(VOID)
{
    UINT64 value;

    __asm__ volatile("mov %0 = cr.itm;;" : "=r"(value));
    return value;
}

static VOID debug_support_write_itv(UINT64 Value)
{
    __asm__ volatile("mov cr.itv = %0;;\n\tsrlz.d;;" : : "r"(Value) : "memory");
}

static VOID debug_support_write_itm(UINT64 Value)
{
    __asm__ volatile("mov cr.itm = %0;;\n\tsrlz.d;;" : : "r"(Value) : "memory");
}

static BOOLEAN debug_support_hook_install(EFI_EXCEPTION_TYPE ExceptionType)
{
    DEBUG_SUPPORT_IVT_HOOK *hook;
    UINT8 *vector;
    UINTN stub_length;
    UINTN slot_length;
    UINT64 saved_psr;

    if (!debug_support_exception_valid(ExceptionType)) {
        return 0;
    }
    hook = &mDebugIvtHooks[ExceptionType];
    if (hook->installed) {
        return 1;
    }
    stub_length = (UINTN)(fw_debug_ivt_stub_end - fw_debug_ivt_stub_start);
    slot_length = ExceptionType < 20 ? 0x400U : 0x100U;
    if (stub_length == 0 || stub_length > sizeof(hook->original) ||
        stub_length > slot_length) {
        return 0;
    }

    vector = (UINT8 *)(UINTN)(SAL_IVT_BASE +
                              debug_support_ivt_offset(ExceptionType));
    saved_psr = debug_support_interrupt_save();
    fw_copy_mem(hook->original, vector, stub_length);
    fw_copy_mem(vector, fw_debug_ivt_stub_start, stub_length);
    __asm__ volatile("mf;;" ::: "memory");
    fw_flush_instruction_cache(vector, stub_length);
    hook->length = stub_length;
    hook->installed = 1;
    debug_support_collection_acquire();
    debug_support_interrupt_restore(saved_psr);
    return 1;
}

static VOID debug_support_hook_remove(EFI_EXCEPTION_TYPE ExceptionType)
{
    DEBUG_SUPPORT_IVT_HOOK *hook;
    UINT8 *vector;
    UINT64 saved_psr;

    if (ExceptionType < 0 || ExceptionType >= DEBUG_SUPPORT_EXCEPTION_COUNT) {
        return;
    }
    hook = &mDebugIvtHooks[ExceptionType];
    if (!hook->installed) {
        return;
    }
    vector = (UINT8 *)(UINTN)(SAL_IVT_BASE +
                              debug_support_ivt_offset(ExceptionType));
    saved_psr = debug_support_interrupt_save();
    fw_copy_mem(vector, hook->original, hook->length);
    __asm__ volatile("mf;;" ::: "memory");
    fw_flush_instruction_cache(vector, hook->length);
    hook->installed = 0;
    hook->length = 0;
    debug_support_collection_release();
    debug_support_interrupt_restore(saved_psr);
}

static BOOLEAN debug_support_hook_needed(EFI_EXCEPTION_TYPE ExceptionType)
{
    return mDebugExceptionCallbacks[ExceptionType] != NULL ||
           (ExceptionType == DEBUG_SUPPORT_EXTERNAL_INTERRUPT &&
            mDebugPeriodicCallback != NULL);
}

static BOOLEAN debug_support_update_hook(EFI_EXCEPTION_TYPE ExceptionType)
{
    if (debug_support_hook_needed(ExceptionType)) {
        return debug_support_hook_install(ExceptionType);
    }
    debug_support_hook_remove(ExceptionType);
    return 1;
}

static VOID debug_support_timer_start(VOID)
{
    mDebugSavedItv = debug_support_read_itv();
    mDebugSavedItm = debug_support_read_itm();
    mDebugTimerOwned = 1;
    debug_support_write_itv(DEBUG_SUPPORT_ITV_MASK |
                            DEBUG_SUPPORT_TIMER_VECTOR);
    debug_support_write_itm(fw_read_itc() + DEBUG_SUPPORT_TIMER_INTERVAL);
    debug_support_write_itv(DEBUG_SUPPORT_TIMER_VECTOR);
}

static VOID debug_support_timer_stop(VOID)
{
    if (!mDebugTimerOwned) {
        return;
    }
    debug_support_write_itv(DEBUG_SUPPORT_ITV_MASK |
                            DEBUG_SUPPORT_TIMER_VECTOR);
    debug_support_write_itm(mDebugSavedItm);
    debug_support_write_itv(mDebugSavedItv);
    mDebugTimerOwned = 0;
}

static EFI_STATUS
debug_support_get_maximum_processor_index(EFI_DEBUG_SUPPORT_PROTOCOL *This,
                                          UINTN *MaxProcessorIndex)
{
    if (This != &mDebugSupportProtocol || MaxProcessorIndex == NULL) {
        return EFI_INVALID_PARAMETER;
    }
    /* The native timer and cache operations are implemented on the BSP. */
    *MaxProcessorIndex = 0;
    return EFI_SUCCESS;
}

static EFI_STATUS
debug_support_register_periodic_callback(EFI_DEBUG_SUPPORT_PROTOCOL *This,
                                         UINTN ProcessorIndex,
                                         EFI_PERIODIC_CALLBACK PeriodicCallback)
{
    UINT64 saved_psr;

    if (This != &mDebugSupportProtocol || ProcessorIndex != 0) {
        return EFI_INVALID_PARAMETER;
    }
    saved_psr = debug_support_interrupt_save();
    if (PeriodicCallback != NULL) {
        if (mDebugPeriodicCallback != NULL) {
            debug_support_interrupt_restore(saved_psr);
            return EFI_ALREADY_STARTED;
        }
        mDebugPeriodicCallback = PeriodicCallback;
        if (!debug_support_update_hook(DEBUG_SUPPORT_EXTERNAL_INTERRUPT)) {
            mDebugPeriodicCallback = NULL;
            debug_support_interrupt_restore(saved_psr);
            return EFI_OUT_OF_RESOURCES;
        }
        debug_support_timer_start();
    } else {
        mDebugPeriodicCallback = NULL;
        debug_support_timer_stop();
        (void)debug_support_update_hook(DEBUG_SUPPORT_EXTERNAL_INTERRUPT);
    }
    debug_support_interrupt_restore(saved_psr);
    return EFI_SUCCESS;
}

static EFI_STATUS debug_support_register_exception_callback(
    EFI_DEBUG_SUPPORT_PROTOCOL *This, UINTN ProcessorIndex,
    EFI_EXCEPTION_CALLBACK ExceptionCallback, EFI_EXCEPTION_TYPE ExceptionType)
{
    EFI_EXCEPTION_CALLBACK old_callback;
    UINT64 saved_psr;

    if (This != &mDebugSupportProtocol || ProcessorIndex != 0 ||
        !debug_support_exception_valid(ExceptionType)) {
        return EFI_INVALID_PARAMETER;
    }
    saved_psr = debug_support_interrupt_save();
    old_callback = mDebugExceptionCallbacks[ExceptionType];
    if (ExceptionCallback != NULL && old_callback != NULL) {
        debug_support_interrupt_restore(saved_psr);
        return EFI_ALREADY_STARTED;
    }
    mDebugExceptionCallbacks[ExceptionType] = ExceptionCallback;
    if (!debug_support_update_hook(ExceptionType)) {
        mDebugExceptionCallbacks[ExceptionType] = old_callback;
        debug_support_interrupt_restore(saved_psr);
        return EFI_OUT_OF_RESOURCES;
    }
    debug_support_interrupt_restore(saved_psr);
    return EFI_SUCCESS;
}

static EFI_STATUS
debug_support_invalidate_instruction_cache(EFI_DEBUG_SUPPORT_PROTOCOL *This,
                                           UINTN ProcessorIndex, VOID *Start,
                                           UINT64 Length)
{
    if (This != &mDebugSupportProtocol || ProcessorIndex != 0 ||
        (Start == NULL && Length != 0) ||
        Length > (UINT64)(~(UINTN)0) - (UINTN)Start) {
        return EFI_INVALID_PARAMETER;
    }
    fw_flush_instruction_cache(Start, (UINTN)Length);
    return EFI_SUCCESS;
}

VOID fw_debug_support_dispatch(EFI_EXCEPTION_TYPE ExceptionType,
                               EFI_SYSTEM_CONTEXT_IPF *Context,
                               UINTN ProcessorIndex)
{
    EFI_SYSTEM_CONTEXT system_context;

    if (Context == NULL || ProcessorIndex >= FW_MAX_CPUS ||
        !debug_support_exception_valid(ExceptionType)) {
        return;
    }
    system_context.SystemContextIpf = Context;
    if (mDebugDispatchActive[ProcessorIndex]) {
        if (ExceptionType == DEBUG_SUPPORT_EXTERNAL_INTERRUPT) {
            (void)fw_read_ivr();
            fw_write_eoi();
        }
        return;
    }
    mDebugDispatchActive[ProcessorIndex] = 1;

    if (ExceptionType == DEBUG_SUPPORT_EXTERNAL_INTERRUPT) {
        UINT64 vector = fw_read_ivr();
        BOOLEAN periodic = ProcessorIndex == 0 &&
                           vector == DEBUG_SUPPORT_TIMER_VECTOR &&
                           mDebugPeriodicCallback != NULL;

        if (periodic) {
            mDebugPeriodicCallback(system_context);
        }
        if (ProcessorIndex == 0 &&
            mDebugExceptionCallbacks[ExceptionType] != NULL) {
            mDebugExceptionCallbacks[ExceptionType](ExceptionType,
                                                    system_context);
        }
        if (periodic) {
            Context->CrItm = mDebugPeriodicCallback != NULL
                                 ? fw_read_itc() + DEBUG_SUPPORT_TIMER_INTERVAL
                                 : mDebugSavedItm;
        }
        fw_write_eoi();
    } else if (ProcessorIndex == 0 &&
               mDebugExceptionCallbacks[ExceptionType] != NULL) {
        mDebugExceptionCallbacks[ExceptionType](ExceptionType, system_context);
    }

    /*
     * A callback may unregister itself.  If that removed the final IVT hook,
     * collection_release() restored the live firmware PSR, but rfi will use
     * the saved interruption PSR supplied through this context.  Keep that
     * saved state consistent with the protocol's released ownership.
     */
    if (!mDebugCollectionOwned && !mDebugCollectionWasEnabled) {
        Context->CrIpsr &= ~IA64_PSR_IC;
    }
    mDebugDispatchActive[ProcessorIndex] = 0;
}

BOOLEAN fw_debug_support_install(VOID)
{
    EFI_HANDLE handle = NULL;

    fw_set_mem(&mDebugSupportProtocol, sizeof(mDebugSupportProtocol), 0);
    fw_set_mem(mDebugExceptionCallbacks, sizeof(mDebugExceptionCallbacks), 0);
    fw_set_mem(mDebugIvtHooks, sizeof(mDebugIvtHooks), 0);
    fw_set_mem(mDebugDispatchActive, sizeof(mDebugDispatchActive), 0);
    mDebugPeriodicCallback = NULL;
    mDebugTimerOwned = 0;
    mDebugCollectionOwned = 0;
    mDebugCollectionWasEnabled = 0;

    mDebugSupportProtocol.Isa = DEBUG_SUPPORT_ISA_IPF;
    mDebugSupportProtocol.GetMaximumProcessorIndex =
        debug_support_get_maximum_processor_index;
    mDebugSupportProtocol.RegisterPeriodicCallback =
        debug_support_register_periodic_callback;
    mDebugSupportProtocol.RegisterExceptionCallback =
        debug_support_register_exception_callback;
    mDebugSupportProtocol.InvalidateInstructionCache =
        debug_support_invalidate_instruction_cache;
    if (bs_install_protocol(&handle, (VOID *)mDebugSupportProtocolGuid, 0,
                            &mDebugSupportProtocol) != EFI_SUCCESS) {
        return 0;
    }
    mDebugSupportHandle = handle;
    return 1;
}

VOID fw_debug_support_exit_boot_services(VOID)
{
    UINTN exception_type;
    UINT64 saved_psr = debug_support_interrupt_save();

    mDebugPeriodicCallback = NULL;
    debug_support_timer_stop();
    for (exception_type = 0; exception_type < DEBUG_SUPPORT_EXCEPTION_COUNT;
         exception_type++) {
        mDebugExceptionCallbacks[exception_type] = NULL;
        debug_support_hook_remove((EFI_EXCEPTION_TYPE)exception_type);
    }
    debug_support_interrupt_restore(saved_psr);
}

#undef DEBUG_SUPPORT_ISA_IPF
#undef DEBUG_SUPPORT_EXCEPTION_COUNT
#undef DEBUG_SUPPORT_EXTERNAL_INTERRUPT
#undef DEBUG_SUPPORT_BREAKPOINT
#undef DEBUG_SUPPORT_TIMER_VECTOR
#undef DEBUG_SUPPORT_TIMER_INTERVAL
#undef DEBUG_SUPPORT_ITV_MASK
#undef DEBUG_SUPPORT_IVT_STUB_MAX
#undef DEBUG_SUPPORT_PSR_RI_MASK
#undef DEBUG_SUPPORT_PSR_RI_SHIFT
