/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <inttypes.h>
#include <stdio.h>
#include <stddef.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/LLJIT.h>
#include <llvm-c/Target.h>
#include <llvm-c/Transforms/PassBuilder.h>
#include "region.h"
#include "lower.h"

typedef struct Lower {
    IA64LLVMCode *code;
    const IA64LLVMLayout *layout;
    LLVMContextRef ctx;
    LLVMModuleRef mod;
    LLVMBuilderRef b;
    LLVMTypeRef i1, i32, i64, ptr, probe_type, sig_type, store_type;
    LLVMValueRef fn, page, host, probe, read_sig, observed_store;
    bool stored;
    LLVMBasicBlockRef retry;
    LLVMValueRef gr[128], pr[64], fr[128];
    bool gr_written[128];
} Lower;

static LLVMOrcLLJITRef jit;
static uint64_t sequence;

static LLVMValueRef constant(Lower *l, uint64_t val)
{
    return LLVMConstInt(l->i64, val, 0);
}

static LLVMValueRef bytes_at(Lower *l, LLVMValueRef base, LLVMValueRef offset)
{
    return LLVMBuildGEP2(l->b, LLVMInt8TypeInContext(l->ctx), base, &offset, 1,
                         "");
}

static LLVMValueRef field_at(Lower *l, LLVMValueRef base, unsigned offset)
{
    return bytes_at(l, base, constant(l, offset));
}

static LLVMValueRef env_at(Lower *l, unsigned offset)
{
    return field_at(l, LLVMGetParam(l->fn, 0), offset);
}

static void require_gr(Lower *l, unsigned reg)
{
    if (reg >= 32 && reg - 31 > l->code->sof) {
        l->code->sof = reg - 31;
    }
}

static LLVMValueRef read_gr(Lower *l, unsigned reg)
{
    if (!reg) {
        return constant(l, 0);
    }
    if (!l->gr[reg]) {
        require_gr(l, reg);
        l->gr[reg] = LLVMBuildLoad2(l->b, l->i64,
                                    env_at(l, l->layout->gr + reg * 8), "");
    }
    return l->gr[reg];
}

static void write_gr(Lower *l, unsigned reg, LLVMValueRef value)
{
    require_gr(l, reg);
    l->gr[reg] = value;
    l->gr_written[reg] = true;
    if (reg >= 32) {
        unsigned bit = reg - 32;
        l->code->dirty[bit / 64] |= UINT64_C(1) << (bit % 64);
    }
}

static LLVMValueRef tlb_entry(Lower *l, LLVMValueRef addr)
{
    const IA64LLVMLayout *layout = l->layout;
    LLVMValueRef tlb = LLVMGetParam(l->fn, 3);
    LLVMBuilderRef b = l->b;
    LLVMValueRef index = LLVMBuildLShr(
        b, addr, constant(l, layout->page_bits - layout->entry_bits), "");
    LLVMValueRef tlb_mask =
        LLVMBuildLoad2(b, l->i64, field_at(l, tlb, layout->tlb_mask), "");
    LLVMValueRef table =
        LLVMBuildLoad2(b, l->ptr, field_at(l, tlb, layout->tlb_table), "");
    return bytes_at(l, table, LLVMBuildAnd(b, index, tlb_mask, ""));
}

static LLVMValueRef tlb_hit(Lower *l, LLVMValueRef entry, LLVMValueRef page,
                            unsigned tag_offset)
{
    LLVMValueRef tag =
        LLVMBuildLoad2(l->b, l->i64, field_at(l, entry, tag_offset), "");
    LLVMSetOrdering(tag, LLVMAtomicOrderingAcquire);
    return LLVMBuildICmp(l->b, LLVMIntEQ, tag, page, "");
}

static LLVMValueRef tlb_host(Lower *l, LLVMValueRef entry, LLVMValueRef addr)
{
    LLVMValueRef addend =
        LLVMBuildLoad2(l->b, l->i64, field_at(l, entry, l->layout->addend), "");
    return LLVMBuildIntToPtr(l->b, LLVMBuildAdd(l->b, addr, addend, ""), l->ptr,
                             "");
}

static LLVMValueRef read_ram(Lower *l, LLVMValueRef addr, unsigned size,
                             LLVMBasicBlockRef retry)
{
    LLVMBuilderRef b = l->b;
    uint64_t mask = (UINT64_C(1) << l->layout->page_bits) - 1;
    LLVMValueRef off = LLVMBuildAnd(b, addr, constant(l, mask), "");
    LLVMValueRef page = LLVMBuildAnd(b, addr, constant(l, ~mask), "");
    LLVMBasicBlockRef test =
        LLVMAppendBasicBlockInContext(l->ctx, l->fn, "page");
    LLVMBasicBlockRef fill =
        LLVMAppendBasicBlockInContext(l->ctx, l->fn, "fill");
    LLVMBasicBlockRef hit =
        LLVMAppendBasicBlockInContext(l->ctx, l->fn, "read");
    LLVMValueRef misaligned = LLVMBuildAnd(b, addr, constant(l, size - 1), "");
    LLVMBuildCondBr(b,
                    LLVMBuildICmp(b, LLVMIntEQ, misaligned, constant(l, 0), ""),
                    test, retry);
    LLVMPositionBuilderAtEnd(b, test);
    LLVMValueRef old = LLVMBuildLoad2(b, l->i64, l->page, "");
    LLVMBuildCondBr(b, LLVMBuildICmp(b, LLVMIntEQ, old, page, ""), hit, fill);
    LLVMPositionBuilderAtEnd(b, fill);
    LLVMValueRef entry = tlb_entry(l, addr);
    LLVMBasicBlockRef fast =
        LLVMAppendBasicBlockInContext(l->ctx, l->fn, "direct");
    LLVMBasicBlockRef slow =
        LLVMAppendBasicBlockInContext(l->ctx, l->fn, "probe");
    LLVMBasicBlockRef join =
        LLVMAppendBasicBlockInContext(l->ctx, l->fn, "mapped");
    LLVMBuildCondBr(b, tlb_hit(l, entry, page, l->layout->read_tag), fast,
                    slow);
    LLVMPositionBuilderAtEnd(b, fast);
    LLVMValueRef direct = tlb_host(l, entry, page);
    LLVMBuildBr(b, join);
    LLVMPositionBuilderAtEnd(b, slow);
    LLVMValueRef args[] = { LLVMGetParam(l->fn, 0), addr,
                            LLVMGetParam(l->fn, 1) };
    LLVMValueRef probed =
        LLVMBuildCall2(b, l->probe_type, l->probe, args, 3, "");
    LLVMBuildCondBr(b, LLVMBuildIsNull(b, probed, ""), retry, join);
    LLVMPositionBuilderAtEnd(b, join);
    LLVMValueRef ptr = LLVMBuildPhi(b, l->ptr, "");
    LLVMValueRef values[] = { direct, probed };
    LLVMBasicBlockRef blocks[] = { fast, slow };
    LLVMAddIncoming(ptr, values, blocks, 2);
    LLVMBuildStore(b, ptr, l->host);
    LLVMBuildStore(b, page, l->page);
    LLVMBuildBr(b, hit);
    LLVMPositionBuilderAtEnd(b, hit);
    LLVMValueRef base = LLVMBuildLoad2(b, l->ptr, l->host, "");
    LLVMTypeRef byte = LLVMInt8TypeInContext(l->ctx);
    LLVMValueRef h = LLVMBuildGEP2(b, byte, base, &off, 1, "");
    LLVMTypeRef t = LLVMIntTypeInContext(l->ctx, size * 8);
    LLVMValueRef v = LLVMBuildLoad2(b, t, h, "");
    LLVMSetAlignment(v, size);
    LLVMSetOrdering(v, LLVMAtomicOrderingAcquire);
    return size == 8 ? v : LLVMBuildZExt(b, v, l->i64, "");
}

static LLVMValueRef read_significand(Lower *l, unsigned f)
{
    LLVMBuilderRef b = l->b;
    LLVMValueRef tags = LLVMBuildLoad2(
        b, l->i64, env_at(l, l->layout->fr_sig + (f / 64) * 8), "");
    LLVMValueRef bit =
        LLVMBuildAnd(b, tags, constant(l, UINT64_C(1) << (f % 64)), "");
    LLVMBasicBlockRef fast =
        LLVMAppendBasicBlockInContext(l->ctx, l->fn, "sig");
    LLVMBasicBlockRef slow =
        LLVMAppendBasicBlockInContext(l->ctx, l->fn, "convert");
    LLVMBasicBlockRef join = LLVMAppendBasicBlockInContext(l->ctx, l->fn, "fr");
    LLVMBuildCondBr(b, LLVMBuildICmp(b, LLVMIntNE, bit, constant(l, 0), ""),
                    fast, slow);
    LLVMPositionBuilderAtEnd(b, fast);
    LLVMValueRef raw =
        LLVMBuildLoad2(b, l->i64, env_at(l, l->layout->fr + f * 8), "");
    LLVMBuildBr(b, join);
    LLVMPositionBuilderAtEnd(b, slow);
    LLVMValueRef args[] = { LLVMGetParam(l->fn, 0),
                            LLVMConstInt(l->i32, f, 0) };
    LLVMValueRef sig = LLVMBuildCall2(b, l->sig_type, l->read_sig, args, 2, "");
    LLVMBuildBr(b, join);
    LLVMPositionBuilderAtEnd(b, join);
    LLVMValueRef value = LLVMBuildPhi(b, l->i64, "");
    LLVMValueRef values[] = { raw, sig };
    LLVMBasicBlockRef blocks[] = { fast, slow };
    LLVMAddIncoming(value, values, blocks, 2);
    return value;
}

static void write_frame(Lower *l)
{
    for (unsigned i = 1; i < 128; i++) {
        if (l->gr_written[i]) {
            LLVMBuildStore(l->b, l->gr[i], env_at(l, l->layout->gr + i * 8));
        }
    }
    for (unsigned i = 1; i < 64; i++) {
        if (l->pr[i]) {
            LLVMValueRef v = LLVMBuildZExt(l->b, l->pr[i], l->i64, "");
            LLVMBuildStore(l->b, v, env_at(l, l->layout->pr + i * 8));
        }
    }
}

static LLVMBasicBlockRef memory_retry(Lower *l, unsigned index)
{
    if (!l->stored) {
        return l->retry;
    }
    /* A completed store cannot be replayed. Publish the preceding slots. */
    LLVMBasicBlockRef block = LLVMGetInsertBlock(l->b);
    LLVMBasicBlockRef exit =
        LLVMAppendBasicBlockInContext(l->ctx, l->fn, "resume");
    LLVMPositionBuilderAtEnd(l->b, exit);
    write_frame(l);
    for (unsigned i = 0; i < 2; i++) {
        uint64_t mask = l->code->dirty[i];
        if (mask) {
            LLVMValueRef ptr = env_at(l, l->layout->dirty + i * 8);
            LLVMValueRef old = LLVMBuildLoad2(l->b, l->i64, ptr, "");
            LLVMBuildStore(l->b, LLVMBuildOr(l->b, old, constant(l, mask), ""),
                           ptr);
        }
    }
    LLVMBuildRet(l->b, LLVMConstInt(l->i32, IA64_LLVM_RESUME + index, 0));
    LLVMPositionBuilderAtEnd(l->b, block);
    return exit;
}

static void write_ram(Lower *l, LLVMValueRef addr, LLVMValueRef value,
                      unsigned size, LLVMBasicBlockRef retry)
{
    LLVMBuilderRef b = l->b;
    uint64_t mask = (UINT64_C(1) << l->layout->page_bits) - 1;
    /* Recheck the write comparator at every store to honor dirty logging. */
    LLVMValueRef page = LLVMBuildAnd(b, addr, constant(l, ~mask), "");
    LLVMValueRef entry = tlb_entry(l, addr);
    LLVMValueRef match = tlb_hit(l, entry, page, l->layout->write_tag);
    LLVMValueRef off = LLVMBuildAnd(b, addr, constant(l, size - 1), "");
    LLVMValueRef aligned = LLVMBuildICmp(b, LLVMIntEQ, off, constant(l, 0), "");
    LLVMBasicBlockRef write =
        LLVMAppendBasicBlockInContext(l->ctx, l->fn, "write");
    LLVMBuildCondBr(b, LLVMBuildAnd(b, aligned, match, ""), write, retry);
    LLVMPositionBuilderAtEnd(b, write);
    LLVMValueRef host = tlb_host(l, entry, addr);
    if (l->observed_store) {
        LLVMValueRef args[] = { LLVMGetParam(l->fn, 0), host, addr, value,
                                LLVMConstInt(l->i32, size, 0) };
        LLVMBuildCall2(b, l->store_type, l->observed_store, args, 5, "");
    } else {
        LLVMTypeRef type = LLVMIntTypeInContext(l->ctx, size * 8);
        LLVMValueRef v = LLVMBuildTruncOrBitCast(b, value, type, "");
        LLVMValueRef st = LLVMBuildStore(b, v, host);
        LLVMSetAlignment(st, size);
        LLVMSetOrdering(st, LLVMAtomicOrderingRelease);
    }
    l->stored = true;
}

static bool lift_memory(Lower *l, const Ia64Instruction *in, unsigned index,
                        bool store, unsigned size)
{
    const IA64MemoryOperands *m = &in->operands.memory;
    bool update = in->imm_base_update || in->reg_base_update;
    if ((!store && !m->destination) || (store && in->reg_base_update) ||
        (update && (!m->base || (!store && m->base == m->destination)))) {
        return false;
    }
    LLVMValueRef addr = read_gr(l, m->base);
    LLVMValueRef increment = NULL;
    if (update) {
        increment = in->reg_base_update ? read_gr(l, m->source)
                                        : constant(l, m->immediate);
    }
    LLVMBasicBlockRef retry = memory_retry(l, index);
    if (store) {
        write_ram(l, addr, read_gr(l, m->source), size, retry);
    } else {
        write_gr(l, m->destination, read_ram(l, addr, size, retry));
    }
    if (update) {
        write_gr(l, m->base, LLVMBuildAdd(l->b, addr, increment, ""));
    }
    return true;
}

static bool lift_slot(Lower *l, const Ia64Instruction *in, unsigned index)
{
    if (!in->valid || in->encoding_class != IA64_ENCODING_DEFINED ||
        in->placement_illegal || in->reserved_field || in->qp ||
        in->mem_acquire || in->mem_release) {
        return false;
    }
    Ia64Opcode op = in->opcode;
    if (op == IA64_OP_NOP) {
        return true;
    }
    if (op >= IA64_OP_LD1 && op <= IA64_OP_LD8) {
        return lift_memory(l, in, index, false, 1u << (op - IA64_OP_LD1));
    }
    if (op >= IA64_OP_ST1 && op <= IA64_OP_ST8) {
        return lift_memory(l, in, index, true, 1u << (op - IA64_OP_ST1));
    }
    if (op == IA64_OP_GETF_SIG) {
        unsigned f = in->operands.floating.source1;
        unsigned dst = in->operands.floating.destination;
        if (!dst) {
            return false;
        }
        l->code->fr_inputs[f / 64] |= UINT64_C(1) << (f % 64);
        if (!l->fr[f]) {
            /* f0 is +0.0 and f1 is +1.0 in IA-64 register format. */
            l->fr[f] =
                f < 2 ? constant(l, (uint64_t)f << 63) : read_significand(l, f);
        }
        write_gr(l, dst, l->fr[f]);
        return true;
    }

    const IA64IntegerOperands *r = &in->operands.integer;
    LLVMValueRef lhs = read_gr(l, r->source1);
    LLVMValueRef rhs = read_gr(l, r->source2);
    LLVMValueRef value = ia64_llvm_lift_integer(l->b, in, lhs, rhs);
    if (!value) {
        return false;
    }
    if (LLVMTypeOf(value) == l->i1) {
        if (r->predicate1 == r->predicate2) {
            return false;
        }
        if (r->predicate1) {
            l->pr[r->predicate1] = value;
        }
        if (r->predicate2) {
            l->pr[r->predicate2] = LLVMBuildNot(l->b, value, "");
        }
    } else {
        if (!r->destination) {
            return false;
        }
        write_gr(l, r->destination, value);
    }
    l->code->mixes += op == IA64_OP_MIX4_L || op == IA64_OP_MIX4_R;
    return true;
}

static bool build_body(Lower *l, const uint64_t *words, unsigned bundles)
{
    for (unsigned i = 0; i < bundles; i++, words += 2) {
        uint64_t lo = words[0], hi = words[1];
        const IA64TemplateInfo *t =
            ia64_template_info(ia64_bundle_template_code(lo));
        if (!t->defined || t->units[1] == IA64_UNIT_L) {
            return false;
        }
        for (unsigned j = 0; j < IA64_BUNDLE_SLOTS; j++) {
            uint64_t raw = ia64_bundle_slot(lo, hi, j);
            Ia64Instruction in =
                ia64_decode_insn(t->units[j], raw, i * UINT64_C(16), j);
            if (!lift_slot(l, &in, i * IA64_BUNDLE_SLOTS + j)) {
                return false;
            }
        }
    }
    write_frame(l);
    LLVMBuildRet(l->b, LLVMConstInt(l->i32, IA64_LLVM_COMPLETE, 0));
    return true;
}

static bool succeeded(LLVMErrorRef err)
{
    if (!err) {
        return true;
    }
    char *s = LLVMGetErrorMessage(err);
    fprintf(stderr, "IA-64 LLVM: %s\n", s);
    LLVMDisposeErrorMessage(s);
    return false;
}

__attribute__((visibility("default"))) bool
ia64_llvm_compile_v4(unsigned abi, const uint64_t *words, unsigned bundles,
                     IA64LLVMCode *out, const IA64LLVMLayout *layout,
                     bool full_alat)
{
    const uint16_t byte_order = 1;
    if (abi != IA64_LLVM_ABI || !words || !out || !layout || !bundles ||
        bundles > IA64_LLVM_MAX_BUNDLES || sizeof(uintptr_t) != 8 ||
        *(const uint8_t *)&byte_order != 1) {
        return false;
    }
    *out = (IA64LLVMCode) { 0 };
    if (!jit &&
        (LLVMInitializeNativeTarget() || LLVMInitializeNativeAsmPrinter() ||
         !succeeded(LLVMOrcCreateLLJIT(&jit, NULL)))) {
        return false;
    }
    char name[64], *error = NULL;
    snprintf(name, sizeof(name), "ia64_region_%" PRIu64, ++sequence);
    LLVMContextRef ctx = LLVMContextCreate();
    LLVMOrcThreadSafeContextRef ts =
        LLVMOrcCreateNewThreadSafeContextFromLLVMContext(ctx);
    Lower l = { .code = out,
                .layout = layout,
                .ctx = ctx,
                .b = LLVMCreateBuilderInContext(ctx),
                .mod = LLVMModuleCreateWithNameInContext(name, ctx),
                .i1 = LLVMInt1TypeInContext(ctx),
                .i32 = LLVMInt32TypeInContext(ctx),
                .i64 = LLVMInt64TypeInContext(ctx),
                .ptr = LLVMPointerTypeInContext(ctx, 0) };
    LLVMTypeRef args[] = { l.ptr, l.i32, l.ptr, l.ptr };
    LLVMTypeRef probe_args[] = { l.ptr, l.i64, l.i32 };
    l.probe_type = LLVMFunctionType(l.ptr, probe_args, 3, 0);
    LLVMTypeRef sig_args[] = { l.ptr, l.i32 };
    l.sig_type = LLVMFunctionType(l.i64, sig_args, 2, 0);
    LLVMTypeRef store_args[] = { l.ptr, l.ptr, l.i64, l.i64, l.i32 };
    l.store_type =
        LLVMFunctionType(LLVMVoidTypeInContext(ctx), store_args, 5, 0);
    l.fn = LLVMAddFunction(l.mod, name, LLVMFunctionType(l.i32, args, 4, 0));
    LLVMBasicBlockRef start = LLVMAppendBasicBlockInContext(ctx, l.fn, "start");
    l.retry = LLVMAppendBasicBlockInContext(ctx, l.fn, "retry");
    LLVMPositionBuilderAtEnd(l.b, l.retry);
    LLVMBuildRet(l.b, LLVMConstInt(l.i32, IA64_LLVM_RETRY, 0));
    LLVMPositionBuilderAtEnd(l.b, start);
    LLVMValueRef access = LLVMGetParam(l.fn, 2);
    l.probe = LLVMBuildLoad2(l.b, l.ptr, access, "");
    l.read_sig = LLVMBuildLoad2(
        l.b, l.ptr, field_at(&l, access, offsetof(IA64LLVMAccess, read_sig)),
        "");
    if (full_alat) {
        l.observed_store = LLVMBuildLoad2(
            l.b, l.ptr,
            field_at(&l, access, offsetof(IA64LLVMAccess, observed_store)), "");
    }
    l.page = LLVMBuildAlloca(l.b, l.i64, "page_tag");
    l.host = LLVMBuildAlloca(l.b, l.ptr, "host_page");
    LLVMBuildStore(l.b, constant(&l, UINT64_MAX), l.page);
    LLVMBuildStore(l.b, LLVMConstNull(l.ptr), l.host);
    LLVMSetTarget(l.mod, LLVMOrcLLJITGetTripleString(jit));
    LLVMSetDataLayout(l.mod, LLVMOrcLLJITGetDataLayoutStr(jit));
    bool ok = build_body(&l, words, bundles) &&
              !LLVMVerifyModule(l.mod, LLVMReturnStatusAction, &error);
    LLVMDisposeMessage(error);
    LLVMDisposeBuilder(l.b);
    if (ok) {
        LLVMPassBuilderOptionsRef opts = LLVMCreatePassBuilderOptions();
        ok = succeeded(LLVMRunPasses(l.mod, "default<O2>,verify", NULL, opts));
        LLVMDisposePassBuilderOptions(opts);
    }
    if (ok) {
        LLVMOrcThreadSafeModuleRef m =
            LLVMOrcCreateNewThreadSafeModule(l.mod, ts);
        LLVMOrcExecutorAddress addr;
        ok = succeeded(LLVMOrcLLJITAddLLVMIRModule(
                 jit, LLVMOrcLLJITGetMainJITDylib(jit), m)) &&
             succeeded(LLVMOrcLLJITLookup(jit, &addr, name));
        if (ok) {
            out->run = (IA64LLVMRun)(uintptr_t)addr;
        }
    } else {
        LLVMDisposeModule(l.mod);
    }
    LLVMOrcDisposeThreadSafeContext(ts);
    return ok;
}
