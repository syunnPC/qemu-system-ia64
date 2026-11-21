/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <llvm-c/Analysis.h>
#include <llvm-c/Core.h>
#include <llvm-c/LLJIT.h>
#include <llvm-c/Target.h>
#include <llvm-c/Transforms/PassBuilder.h>
#include "engine/tier1/ir.h"
#include "engine/tier1/form.h"
#include "lower.h"

typedef struct Lower {
    const struct t1_ir *ir;
    LLVMContextRef ctx;
    LLVMModuleRef mod;
    LLVMBuilderRef b;
    LLVMTypeRef i1, i32, i64, ptr, probe_type, sig_type;
    LLVMValueRef fn, page, host, probe, read_sig;
    LLVMBasicBlockRef retry;
    LLVMValueRef *values;
} Lower;

static LLVMOrcLLJITRef jit;
static uint64_t sequence;

static LLVMValueRef constant(Lower *l, uint64_t val)
{
    return LLVMConstInt(l->i64, val, 0);
}
static LLVMValueRef arg(Lower *l, const struct t1_op *o, unsigned k)
{
    if (k >= o->nuse) {
        return NULL;
    }
    t1_value v = l->ir->use[o->use_first + k].value;
    return v == T1_VALUE_NONE ? NULL : l->values[t1_ir_resolve(l->ir, v)];
}
static LLVMValueRef home(Lower *l, unsigned el)
{
    unsigned pr = el > 256 && el < 320;
    if (!pr && (el == 0 || el >= 128)) {
        return NULL;
    }
    LLVMValueRef k = constant(l, pr ? el - 256 : el);
    return LLVMBuildGEP2(l->b, l->i64, LLVMGetParam(l->fn, pr), &k, 1, "");
}
static LLVMValueRef narrow(Lower *l, LLVMValueRef a, unsigned bits, bool sign)
{
    if (bits == 64) {
        return a;
    }
    LLVMTypeRef t = LLVMIntTypeInContext(l->ctx, bits);
    LLVMValueRef v = LLVMBuildTrunc(l->b, a, t, "");
    return sign ? LLVMBuildSExt(l->b, v, l->i64, "")
                : LLVMBuildZExt(l->b, v, l->i64, "");
}

static LLVMValueRef read_ram(Lower *l, LLVMValueRef addr, unsigned size)
{
    LLVMBuilderRef b = l->b;
    LLVMValueRef off = LLVMBuildAnd(b, addr, constant(l, 4095), "");
    LLVMValueRef page = LLVMBuildAnd(b, addr, constant(l, ~UINT64_C(4095)), "");
    LLVMBasicBlockRef test =
        LLVMAppendBasicBlockInContext(l->ctx, l->fn, "page");
    LLVMBasicBlockRef fill =
        LLVMAppendBasicBlockInContext(l->ctx, l->fn, "fill");
    LLVMBasicBlockRef hit =
        LLVMAppendBasicBlockInContext(l->ctx, l->fn, "read");
    LLVMValueRef misaligned = LLVMBuildAnd(b, addr, constant(l, size - 1), "");
    LLVMBuildCondBr(b,
                    LLVMBuildICmp(b, LLVMIntEQ, misaligned, constant(l, 0), ""),
                    test, l->retry);
    LLVMPositionBuilderAtEnd(b, test);
    LLVMValueRef old = LLVMBuildLoad2(b, l->i64, l->page, "");
    LLVMBuildCondBr(b, LLVMBuildICmp(b, LLVMIntEQ, old, page, ""), hit, fill);
    LLVMPositionBuilderAtEnd(b, fill);
    LLVMValueRef args[] = { LLVMGetParam(l->fn, 2), addr,
                            LLVMGetParam(l->fn, 3) };
    LLVMValueRef ptr = LLVMBuildCall2(b, l->probe_type, l->probe, args, 3, "");
    LLVMBuildStore(b, ptr, l->host);
    LLVMBuildStore(b, page, l->page);
    LLVMBuildCondBr(b, LLVMBuildIsNull(b, ptr, ""), l->retry, hit);
    LLVMPositionBuilderAtEnd(b, hit);
    LLVMValueRef base = LLVMBuildLoad2(b, l->ptr, l->host, "");
    LLVMTypeRef byte = LLVMInt8TypeInContext(l->ctx);
    LLVMValueRef h = LLVMBuildGEP2(b, byte, base, &off, 1, "");
    LLVMTypeRef t = LLVMIntTypeInContext(l->ctx, size * 8);
    LLVMValueRef v = LLVMBuildLoad2(b, t, h, "");
    LLVMSetAlignment(v, size);
    LLVMSetOrdering(v, LLVMAtomicOrderingMonotonic);
    return size == 8 ? v : LLVMBuildZExt(b, v, l->i64, "");
}

static LLVMValueRef helper_input(Lower *l, const struct t1_op *o, unsigned reg)
{
    if (reg == 0) {
        return constant(l, 0);
    }
    for (unsigned k = 1; k < o->nuse; k++) {
        if (l->ir->use[o->use_first + k].element == reg) {
            return arg(l, o, k);
        }
    }
    return NULL;
}
static LLVMValueRef mix(Lower *l, const struct t1_op *o)
{
    const struct t1_insn *ti = &l->ir->form->insn[o->imm];
    LLVMValueRef a = helper_input(l, o, ti->src1),
                 b = helper_input(l, o, ti->src2);
    if (!a || !b) {
        return NULL;
    }
    if (ti->insn.opcode == IA64_OP_MIX4_R) {
        a = LLVMBuildShl(l->b, a, constant(l, 32), "");
        b = LLVMBuildAnd(l->b, b, constant(l, UINT32_MAX), "");
    } else if (ti->insn.opcode == IA64_OP_MIX4_L) {
        a = LLVMBuildAnd(l->b, a, constant(l, ~UINT64_C(0xffffffff)), "");
        b = LLVMBuildLShr(l->b, b, constant(l, 32), "");
    } else {
        return NULL;
    }
    return LLVMBuildOr(l->b, a, b, "");
}

static bool commit(Lower *l, const struct t1_ir_point *p)
{
    const struct t1_form *f = l->ir->form;
    if (p->slot || p->ip != f->c.ip + f->ninsns / 3 * 16) {
        return false;
    }
    for (unsigned i = 0; i < p->frame_count; i++) {
        const struct t1_use *u = &l->ir->use[p->frame_first + i];
        if ((u->flags & T1_USE_HOME) || u->element == T1_ELEMENT_LSB ||
            (u->element > 128 && u->element < 256)) {
            continue;
        }
        if (u->value == T1_VALUE_NONE) {
            return false;
        }
        LLVMValueRef v = l->values[t1_ir_resolve(l->ir, u->value)];
        LLVMValueRef addr = home(l, u->element);
        if (!addr || !v) {
            return false;
        }
        if (LLVMTypeOf(v) == l->i1) {
            v = LLVMBuildZExt(l->b, v, l->i64, "");
        }
        LLVMBuildStore(l->b, v, addr);
    }
    LLVMBuildRet(l->b, LLVMConstInt(l->i32, 1, 0));
    return true;
}

static bool lower_op(Lower *l, const struct t1_op *o, LLVMValueRef *result)
{
    LLVMBuilderRef b = l->b;
    LLVMValueRef a = arg(l, o, 0), c = arg(l, o, 1), v = NULL;
    unsigned imm = o->imm;
    unsigned width = T1_FIELD_LEN(imm), pos = T1_FIELD_POS(imm);
    switch (o->code) {
    case T1_IR_CONST:
        v = LLVMConstInt(o->type == T1_I1 ? l->i1 : l->i64, o->imm, 0);
        break;
    case T1_IR_LIVEIN:
        if (imm == T1_ELEMENT_MEM || imm == T1_ELEMENT_LSB) {
            return true;
        }
        if (imm >= T1_ELEMENT_FR(2) && imm <= T1_ELEMENT_FR(127)) {
            LLVMValueRef args[] = { LLVMGetParam(l->fn, 2),
                                    LLVMConstInt(l->i32, imm - T1_ELEMENT_FR(0),
                                                 0) };
            v = LLVMBuildCall2(b, l->sig_type, l->read_sig, args, 2, "");
        } else if (imm >= T1_ELEMENT_FR_META(2) &&
                   imm <= T1_ELEMENT_FR_META(127)) {
            /* getf.sig consumes only the NaT bit, checked on entry. */
            v = constant(l, 0);
        } else if (imm > 128 && imm < 256) {
            v = LLVMConstInt(l->i1, 0, 0);
        } else {
            LLVMValueRef ptr = home(l, imm);
            if (!ptr) {
                return false;
            }
            v = LLVMBuildLoad2(b, l->i64, ptr, "");
            if (imm > 256) {
                v = LLVMBuildTrunc(b, v, l->i1, "");
            }
        }
        break;
    case T1_IR_ADD:
        v = LLVMBuildAdd(b, a, c, "");
        break;
    case T1_IR_SUB:
        v = LLVMBuildSub(b, a, c, "");
        break;
    case T1_IR_AND:
    case T1_IR_AND1:
        v = LLVMBuildAnd(b, a, c, "");
        break;
    case T1_IR_OR:
    case T1_IR_OR1:
        v = LLVMBuildOr(b, a, c, "");
        break;
    case T1_IR_XOR:
        v = LLVMBuildXor(b, a, c, "");
        break;
    case T1_IR_ANDCM:
        v = LLVMBuildAnd(b, a, LLVMBuildNot(b, c, ""), "");
        break;
    case T1_IR_NOT1:
        v = LLVMBuildNot(b, a, "");
        break;
    case T1_IR_SELECT:
        v = LLVMBuildSelect(b, a, c, arg(l, o, 2), "");
        break;
    case T1_IR_SHL:
    case T1_IR_SHRU:
    case T1_IR_SHR: {
        LLVMValueRef large =
            LLVMBuildICmp(b, LLVMIntUGT, c, constant(l, 63), "");
        LLVMValueRef n = LLVMBuildSelect(b, large, constant(l, 63), c, "");
        if (o->code == T1_IR_SHR) {
            v = LLVMBuildAShr(b, a, n, "");
        } else {
            v = o->code == T1_IR_SHL ? LLVMBuildShl(b, a, n, "")
                                     : LLVMBuildLShr(b, a, n, "");
            v = LLVMBuildSelect(b, large, constant(l, 0), v, "");
        }
        break;
    }
    case T1_IR_SHL_IMM:
        v = LLVMBuildShl(b, a, constant(l, imm & 63), "");
        break;
    case T1_IR_EXTR:
    case T1_IR_EXTRU:
    case T1_IR_DEPZ:
        if (pos >= 64 || !width || width > 64) {
            return false;
        }
        if (o->code == T1_IR_DEPZ) {
            v = LLVMBuildShl(b, narrow(l, a, width, false), constant(l, pos),
                             "");
        } else {
            v = narrow(l, LLVMBuildLShr(b, a, constant(l, pos), ""), width,
                       o->code == T1_IR_EXTR);
        }
        break;
    case T1_IR_SXT:
    case T1_IR_ZXT:
        if (!imm || imm > 64) {
            return false;
        }
        v = narrow(l, a, imm, o->code == T1_IR_SXT);
        break;
    case T1_IR_CMP: {
        static const LLVMIntPredicate pred[] = { LLVMIntEQ, LLVMIntSLT,
                                                 LLVMIntULT };
        if (T1_CMP_REL(imm) >= 3) {
            return false;
        }
        if (T1_CMP_WIDTH(imm) == 32) {
            a = LLVMBuildTrunc(b, a, l->i32, "");
            c = LLVMBuildTrunc(b, c, l->i32, "");
        } else if (T1_CMP_WIDTH(imm) != 64) {
            return false;
        }
        v = LLVMBuildICmp(b, pred[T1_CMP_REL(imm)], a, c, "");
        break;
    }
    case T1_IR_GETF_SIG:
        v = a;
        break;
    case T1_IR_FRNATTEST:
        v = LLVMConstInt(l->i1, 0, 0);
        break;
    case T1_IR_NATCHK:
        return true; /* Entry requires all GR NaTs clear. */
    case T1_IR_LOAD: {
        unsigned n = T1_ACCESS_BYTES(imm);
        if ((n != 1 && n != 2 && n != 4 && n != 8) || T1_ACCESS_ORDERED(imm)) {
            return false;
        }
        v = read_ram(l, a, n);
        break;
    }
    case T1_IR_GHELPER:
        v = mix(l, o);
        break;
    case T1_IR_HREAD:
        if (imm > 128 && imm < 256) {
            v = LLVMConstInt(l->i1, 0, 0);
        } else if (imm > 0 && imm < 128) {
            v = a;
        }
        break;
    case T1_IR_HGUARD:
        return true; /* Both selected MIX operations complete in registers. */
    default:
        return false;
    }
    *result = v;
    return v != NULL;
}

static bool build_body(Lower *l)
{
    const struct t1_ir *ir = l->ir;
    unsigned b = 0;
    for (unsigned steps = 0; steps < ir->nblocks; steps++) {
        const struct t1_block *blk = &ir->block[b];
        for (uint32_t i = blk->first; i != T1_NO_OP; i = ir->op[i].next) {
            const struct t1_op *o = &ir->op[i];
            if (o->code == T1_IR_EXIT) {
                return commit(l, &ir->point[o->point]);
            }
            if (o->code == T1_IR_JUMP) {
                if (blk->nsucc != 1 || blk->succ[0].nargs) {
                    return false;
                }
                b = blk->succ[0].target;
                break;
            }
            if (!lower_op(l, o, &l->values[T1_VALUE(i, 0)])) {
                return false;
            }
        }
    }
    return false;
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

bool ia64_llvm_lower(const struct t1_ir *ir, IA64LLVMCode *out)
{
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
    Lower l = { .ir = ir,
                .ctx = ctx,
                .b = LLVMCreateBuilderInContext(ctx),
                .mod = LLVMModuleCreateWithNameInContext(name, ctx),
                .i1 = LLVMInt1TypeInContext(ctx),
                .i32 = LLVMInt32TypeInContext(ctx),
                .i64 = LLVMInt64TypeInContext(ctx),
                .ptr = LLVMPointerTypeInContext(ctx, 0) };
    l.values = calloc(ir->nops * 4, sizeof(*l.values));
    LLVMTypeRef args[] = { l.ptr, l.ptr, l.ptr, l.i32, l.ptr };
    LLVMTypeRef probe_args[] = { l.ptr, l.i64, l.i32 };
    l.probe_type = LLVMFunctionType(l.ptr, probe_args, 3, 0);
    LLVMTypeRef sig_args[] = { l.ptr, l.i32 };
    l.sig_type = LLVMFunctionType(l.i64, sig_args, 2, 0);
    l.fn = LLVMAddFunction(l.mod, name, LLVMFunctionType(l.i32, args, 5, 0));
    LLVMBasicBlockRef start = LLVMAppendBasicBlockInContext(ctx, l.fn, "start");
    l.retry = LLVMAppendBasicBlockInContext(ctx, l.fn, "retry");
    LLVMPositionBuilderAtEnd(l.b, l.retry);
    LLVMBuildRet(l.b, LLVMConstInt(l.i32, 0, 0));
    LLVMPositionBuilderAtEnd(l.b, start);
    LLVMValueRef access = LLVMGetParam(l.fn, 4);
    LLVMValueRef one = constant(&l, 1);
    l.probe = LLVMBuildLoad2(l.b, l.ptr, access, "");
    l.read_sig = LLVMBuildLoad2(
        l.b, l.ptr, LLVMBuildGEP2(l.b, l.ptr, access, &one, 1, ""), "");
    l.page = LLVMBuildAlloca(l.b, l.i64, "page_tag");
    l.host = LLVMBuildAlloca(l.b, l.ptr, "host_page");
    LLVMBuildStore(l.b, constant(&l, UINT64_MAX), l.page);
    LLVMBuildStore(l.b, LLVMConstNull(l.ptr), l.host);
    LLVMSetTarget(l.mod, LLVMOrcLLJITGetTripleString(jit));
    LLVMSetDataLayout(l.mod, LLVMOrcLLJITGetDataLayoutStr(jit));
    bool ok = l.values && build_body(&l) &&
              !LLVMVerifyModule(l.mod, LLVMReturnStatusAction, &error);
    LLVMDisposeMessage(error);
    free(l.values);
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
