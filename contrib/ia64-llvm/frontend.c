/* SPDX-License-Identifier: GPL-2.0-or-later */
#include <stdlib.h>
#include <string.h>
#include "engine/tier1/form.h"
#include "engine/tier1/ir.h"
#include "engine/cache/validity.h"
#include "ia64vmm/cpu_model.h"
#include "lower.h"
#include "engine/tier1/memory.h"

#define CAPTURE_BASE UINT64_C(0x1000)
static _Thread_local const uint64_t *captured;
static _Thread_local unsigned captured_bundles;

struct ia64_result code_source_read_physical(gpa_t pa, uint64_t *lo,
                                             uint64_t *hi, uint64_t *generation)
{
    uint64_t offset = pa - CAPTURE_BASE;
    if (!captured || (offset & 15) || offset / 16 >= captured_bundles) {
        return (struct ia64_result) { .kind = IA64_STEP_EXIT };
    }
    *lo = captured[offset / 8];
    *hi = captured[offset / 8 + 1];
    *generation = 1;
    return (struct ia64_result) { .kind = IA64_STEP_COMPLETE };
}

/* The byte copy is immutable for the entire synchronous compile request. */
bool code_page_has_snapshot(gpa_t pa)
{
    return captured && pa == CAPTURE_BASE;
}
bool code_page_tracked_fast(gpa_t pa)
{
    return code_page_has_snapshot(pa);
}
uint64_t code_source_generation(gpa_t pa)
{
    return code_page_has_snapshot(pa);
}

/* Donor workspace and code-snapshot services for a private compiler thread. */
static _Thread_local unsigned lock_depth;
void code_lock(void)
{
    ++lock_depth;
}
void code_unlock(void)
{
    --lock_depth;
}
bool code_lock_held(void)
{
    return lock_depth != 0;
}
void *t1_ws_alloc(struct t1_workspace *ws, size_t pages, unsigned flags)
{
    return calloc(pages, 4096);
}
void t1_ws_free(struct t1_workspace *ws, void *ptr, size_t pages)
{
    free(ptr);
}

static bool supported(const struct t1_insn *ti)
{
    const Ia64Instruction *insn = &ti->insn;
    if (insn->qp || !insn->valid || insn->reserved_field ||
        insn->placement_illegal || insn->reg_base_update ||
        insn->imm_base_update || insn->mem_acquire || insn->mem_release) {
        return false;
    }
    switch (insn->opcode) {
    case IA64_OP_NOP:
#define OP(name) case IA64_OP_##name:
#include "ops.inc"
#undef OP
        return true;
    default:
        return false;
    }
}

__attribute__((visibility("default"))) bool
ia64_llvm_compile(unsigned abi, const uint64_t *words, unsigned bundles,
                  IA64LLVMCode *out)
{
    struct t1_compile_request rq = {
        .ip = CAPTURE_BASE,
        .entry_pa = CAPTURE_BASE,
        .psr = IA64_PSR_IC,
        .cfm = 96 | (96 << 7),
        .cpu_model = IA64_CPU_MADISON,
        .npages = 1,
        .page = { { .va = CAPTURE_BASE, .pa = CAPTURE_BASE } },
    };
    const char *why = NULL;
    char error[192];
    unsigned failed = 0, max_reg = 0;
    bool ok = false;
    const uint16_t byte_order = 1;
    if (sizeof(uintptr_t) != 8 || *(const uint8_t *)&byte_order != 1 ||
        !words || !out) {
        return false;
    }
    if (abi != IA64_LLVM_ABI || !bundles || bundles > IA64_LLVM_MAX_BUNDLES) {
        return false;
    }
    struct t1_ir *ir = calloc(1, sizeof(*ir));
    struct t1_form *form = calloc(1, sizeof(*form));
    struct ia64_state *names = calloc(1, sizeof(*names));
    *out = (IA64LLVMCode) { 0 };
    if (!ir || !form || !names) {
        goto done;
    }
    captured = words;
    captured_bundles = bundles;
    if (t1_form_request(&rq, names, 3 * bundles, form, &why) != T1_COMPILED ||
        form->ninsns != 3 * bundles || form->loop || form->nfs != 1) {
        goto done;
    }
    for (unsigned i = 0; i < form->ninsns; i++) {
        const struct t1_insn *ti = &form->insn[i];
        if (!supported(ti) || ti->insn.address != CAPTURE_BASE + 16 * (i / 3) ||
            ti->insn.slot != i % 3) {
            goto done;
        }
        bool fp = ti->insn.opcode == IA64_OP_GETF_SIG;
        const unsigned regs[] = { ti->dst, fp ? 0 : ti->src1, fp ? 0 : ti->src2,
                                  fp ? 0 : ti->base };
        for (unsigned j = 0; j < sizeof(regs) / sizeof(regs[0]); j++) {
            if (regs[j] > max_reg) {
                max_reg = regs[j];
            }
        }
        if (ti->dst >= 32 && ia64_opcode_info(ti->insn.opcode)->writes_gr) {
            unsigned bit = ti->dst - 32;
            out->dirty[bit / 64] |= UINT64_C(1) << (bit % 64);
        }
        if (ti->insn.opcode == IA64_OP_GETF_SIG) {
            unsigned f = ti->fr[1];
            out->fr[f / 64] |= UINT64_C(1) << (f % 64);
        }
        out->mixes += ti->insn.opcode == IA64_OP_MIX4_L ||
                      ti->insn.opcode == IA64_OP_MIX4_R;
    }
    out->sof = max_reg >= 32 ? max_reg - 31 : 0;
    if (t1_lift(form, ir, &failed) &&
        t1_ir_verify(ir, 0, error, sizeof(error))) {
        ok = ia64_llvm_lower(ir, out);
    }
done:
    captured = NULL;
    free(names);
    free(form);
    free(ir);
    return ok;
}
