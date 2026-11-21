/* SPDX-License-Identifier: GPL-2.0-or-later */
#include "jit.h"
#include <llvm-c/Transforms/PassBuilder.h>
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

/* LLJIT materializes synchronously on QEMU's single compiler thread. */
class CacheMapper final : public InProcessMemoryMapper
{
  public:
    using InProcessMemoryMapper::InProcessMemoryMapper;

    uint64_t limit = 0;
    uint64_t used = 0;
    bool refused = false;

    void reserve(size_t bytes, OnReservedFunction done) override
    {
        if (used > limit || bytes > limit - used) {
            refused = true;
            done(errorCodeToError(
                std::make_error_code(std::errc::no_buffer_space)));
            return;
        }
        InProcessMemoryMapper::reserve(
            bytes, [this, done = std::move(done)](
                       Expected<ExecutorAddrRange> range) mutable {
                if (range) {
                    used += range->size();
                }
                done(std::move(range));
            });
    }
};

std::unique_ptr<LLJIT> jit;
CacheMapper *mapper;

void report_error(Error err)
{
    logAllUnhandledErrors(std::move(err), errs(), "IA-64 LLVM: ");
}

Expected<std::unique_ptr<ObjectLayer>> create_layer(ExecutionSession &session,
                                                    uint64_t limit)
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
    auto storage = std::make_unique<CacheMapper>(granularity);
    storage->limit = limit;
    mapper = storage.get();
    auto manager = std::make_unique<MapperJITLinkMemoryManager>(
        granularity, std::move(storage));
    return std::make_unique<ObjectLinkingLayer>(session, std::move(manager));
}

} // namespace

bool ia64_llvm_compile_module(LLVMModuleRef ref, const char *name,
                              IA64LLVMCode *out, IA64LLVMCache *cache)
{
    Module *module = unwrap(ref);
    ThreadSafeModule tsm { std::unique_ptr<Module>(module),
                           std::unique_ptr<LLVMContext>(
                               &module->getContext()) };
    if (!jit) {
        if (InitializeNativeTarget() || InitializeNativeTargetAsmPrinter()) {
            return false;
        }
        auto created = LLJITBuilder()
                           .setObjectLinkingLayerCreator(
                               [limit = cache->limit](ExecutionSession &es) {
                                   return create_layer(es, limit);
                               })
                           .create();
        if (!created) {
            mapper = nullptr;
            report_error(created.takeError());
            return false;
        }
        jit = std::move(*created);
        jit->getExecutionSession().setErrorReporter([](Error error) {
            handleAllErrors(std::move(error), [](const ErrorInfoBase &info) {
                if (!mapper->refused ||
                    info.convertToErrorCode() != std::errc::no_buffer_space) {
                    errs() << "IA-64 LLVM: " << info.message() << '\n';
                }
            });
        });
    }

    mapper->limit = cache->limit;
    mapper->refused = false;
    cache->used = mapper->used;
    if (cache->used >= cache->limit ||
        cache->limit - cache->used < mapper->getPageSize()) {
        return false;
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
        cache->used = mapper->used;
        report_error(std::move(e));
        return false;
    }
    auto addr = jit->lookup(name);
    cache->used = mapper->used;
    if (!addr) {
        if (mapper->refused) {
            consumeError(addr.takeError());
        } else {
            report_error(addr.takeError());
        }
        if (auto e = tracker->remove()) {
            report_error(std::move(e));
        }
        return false;
    }
    out->run = addr->toPtr<IA64LLVMRun>();
    return true;
}
