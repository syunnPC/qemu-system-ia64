/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "jit.h"
#include <atomic>
#include <mutex>
#include <string>
#include <llvm-c/Transforms/PassBuilder.h>
#include <llvm/Config/llvm-config.h>
#include <llvm/ExecutionEngine/Orc/CompileUtils.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/MapperJITLinkMemoryManager.h>
#include <llvm/ExecutionEngine/Orc/ObjectLinkingLayer.h>
#include <llvm/Support/Process.h>
#include <llvm/Support/TargetSelect.h>
#ifdef _WIN32
#include <windows.h>
#endif

using namespace llvm;
using namespace llvm::orc;

namespace
{

class CacheMapper final : public InProcessMemoryMapper
{
    IA64LLVMCache &cache;

  public:
    CacheMapper(unsigned granularity, IA64LLVMCache &cache)
        : InProcessMemoryMapper(granularity), cache(cache)
    {
    }

    void reserve(size_t bytes, OnReservedFunction done) override
    {
        const uint64_t charge = alignTo(bytes, getPageSize());
        uint64_t used = __atomic_load_n(&cache.used, __ATOMIC_RELAXED);
        do {
            if (used > cache.limit || charge > cache.limit - used) {
                done(errorCodeToError(
                    std::make_error_code(std::errc::no_buffer_space)));
                return;
            }
        } while (!__atomic_compare_exchange_n(&cache.used, &used, used + charge,
                                              true, __ATOMIC_RELAXED,
                                              __ATOMIC_RELAXED));
        InProcessMemoryMapper::reserve(
            charge, [this, charge, done = std::move(done)](
                        Expected<ExecutorAddrRange> range) mutable {
                if (!range) {
                    __atomic_fetch_sub(&cache.used, charge, __ATOMIC_RELAXED);
                }
                done(std::move(range));
            });
    }
};

std::unique_ptr<LLJIT> jit;
std::once_flag init_once;
std::atomic<uint64_t> next_symbol { 0 };

void report_error(Error err)
{
    logAllUnhandledErrors(std::move(err), errs(), "IA-64 LLVM: ");
}

Expected<std::unique_ptr<jitlink::JITLinkMemoryManager>>
create_memory_manager(IA64LLVMCache &cache)
{
    auto page = sys::Process::getPageSize();
    if (!page) {
        return page.takeError();
    }
    unsigned granularity = *page;
#ifdef _WIN32
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    granularity = info.dwAllocationGranularity;
#endif
    auto storage = std::make_unique<CacheMapper>(granularity, cache);
    return std::make_unique<MapperJITLinkMemoryManager>(granularity,
                                                       std::move(storage));
}

} // namespace

bool ia64_llvm_compile_module(LLVMModuleRef ref, const IA64LLVMSymbol *symbols,
                              unsigned count, IA64LLVMCache *cache)
{
    Module *module = unwrap(ref);
    ThreadSafeModule tsm { std::unique_ptr<Module>(module),
                           std::unique_ptr<LLVMContext>(
                               &module->getContext()) };
    std::call_once(init_once, [cache]() {
        if (InitializeNativeTarget() || InitializeNativeTargetAsmPrinter()) {
            return;
        }
        LLJITBuilder builder;
        builder.setCompileFunctionCreator(
            [](JITTargetMachineBuilder tm)
                -> Expected<std::unique_ptr<IRCompileLayer::IRCompiler>> {
                return std::make_unique<ConcurrentIRCompiler>(std::move(tm));
            });
#if LLVM_VERSION_MAJOR >= 23
        builder.setMemoryManagerCreator([cache](ExecutionSession &) {
            return create_memory_manager(*cache);
        });
        builder.setObjectLinkingLayerCreator(
            [](ExecutionSession &es, jitlink::JITLinkMemoryManager &manager)
                -> Expected<std::unique_ptr<ObjectLayer>> {
                return std::make_unique<ObjectLinkingLayer>(es, manager);
            });
#else
        builder.setObjectLinkingLayerCreator(
            [cache](ExecutionSession &es)
                -> Expected<std::unique_ptr<ObjectLayer>> {
                auto manager = create_memory_manager(*cache);
                if (!manager) {
                    return manager.takeError();
                }
                return std::make_unique<ObjectLinkingLayer>(es,
                                                            std::move(*manager));
            });
#endif
        auto created = builder.create();
        if (!created) {
            report_error(created.takeError());
            return;
        }
        jit = std::move(*created);
        jit->getExecutionSession().setErrorReporter([](Error error) {
            handleAllErrors(std::move(error), [](const ErrorInfoBase &info) {
                if (info.convertToErrorCode() != std::errc::no_buffer_space) {
                    errs() << "IA-64 LLVM: " << info.message() << '\n';
                }
            });
        });
    });
    if (!jit) {
        return false;
    }
    std::string names[IA64_LLVM_MAX_BATCH];
    uint64_t base = next_symbol.fetch_add(count, std::memory_order_relaxed);
    for (unsigned i = 0; i < count; i++) {
        names[i] = "ia64_region_" + std::to_string(base + i);
        unwrap(symbols[i].function)->setName(names[i]);
    }
    module->setTargetTriple(jit->getTargetTriple());
    module->setDataLayout(jit->getDataLayout());
    auto opts = LLVMCreatePassBuilderOptions();
    LLVMErrorRef err = LLVMRunPasses(ref, "default<O2>,verify", nullptr, opts);
    LLVMDisposePassBuilderOptions(opts);
    if (err) {
        report_error(unwrap(err));
        return false;
    }
    auto tracker = jit->getMainJITDylib().createResourceTracker();
    if (auto e = jit->addIRModule(tracker, std::move(tsm))) {
        report_error(std::move(e));
        return false;
    }
    IA64LLVMRun entries[IA64_LLVM_MAX_BATCH];
    for (unsigned i = 0; i < count; i++) {
        auto addr = jit->lookup(names[i]);
        if (addr) {
            entries[i] = addr->toPtr<IA64LLVMRun>();
            continue;
        }
        /* The session reporter already handled materialization failures. */
        handleAllErrors(
            addr.takeError(), [](const FailedToMaterialize &) {},
            [](const ErrorInfoBase &info) {
                errs() << "IA-64 LLVM: " << info.message() << '\n';
            });
        Error removal = tracker->remove();
        if (removal) {
            report_error(std::move(removal));
        }
        return false;
    }
    for (unsigned i = 0; i < count; i++) {
        symbols[i].out->run = entries[i];
    }
    return true;
}
