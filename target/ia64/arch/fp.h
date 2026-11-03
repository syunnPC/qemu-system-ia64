/*
 * Internal IA-64 floating-point architecture API.
 *
 * These declarations are intentionally separate from helper.h: helper.h is
 * the stable TCG ABI, while these entry points are directly unit-testable C.
 */

#ifndef TARGET_IA64_ARCH_FP_H
#define TARGET_IA64_ARCH_FP_H

#include "cpu.h"

void ia64_fp_fpswa_dispatch(CPUIA64State *arg0, uintptr_t ra);
void ia64_fp_fadd(CPUIA64State *arg0, uint32_t arg1, uint32_t arg2,
                  uint32_t arg3, uint32_t arg4);
void ia64_fp_fsub(CPUIA64State *arg0, uint32_t arg1, uint32_t arg2,
                  uint32_t arg3, uint32_t arg4);
void ia64_fp_fmpy(CPUIA64State *arg0, uint32_t arg1, uint32_t arg2,
                  uint32_t arg3, uint32_t arg4);
void ia64_fp_xma(CPUIA64State *arg0, uint32_t arg1, uint32_t arg2,
                 uint32_t arg3, uint32_t arg4, uint32_t arg5);
void ia64_fp_fma(CPUIA64State *arg0, uint32_t arg1, uint32_t arg2,
                 uint32_t arg3);
void ia64_fp_fma4(CPUIA64State *arg0, uint32_t arg1, uint32_t arg2,
                  uint32_t arg3, uint32_t arg4, uint32_t arg5);
void ia64_fp_fcmp(CPUIA64State *arg0, uint32_t arg1, uint32_t arg2,
                  uint32_t arg3, uint32_t arg4, uint32_t arg5, uint32_t arg6);
void ia64_fp_fmin(CPUIA64State *arg0, uint32_t arg1, uint32_t arg2,
                  uint32_t arg3, uint32_t arg4);
void ia64_fp_fmax(CPUIA64State *arg0, uint32_t arg1, uint32_t arg2,
                  uint32_t arg3, uint32_t arg4);
void ia64_fp_famin(CPUIA64State *arg0, uint32_t arg1, uint32_t arg2,
                   uint32_t arg3, uint32_t arg4);
void ia64_fp_famax(CPUIA64State *arg0, uint32_t arg1, uint32_t arg2,
                   uint32_t arg3, uint32_t arg4);
void ia64_fp_frcpa(CPUIA64State *arg0, uint32_t arg1, uint32_t arg2,
                   uint32_t arg3, uint32_t arg4, uint32_t arg5);
void ia64_fp_fprcpa(CPUIA64State *arg0, uint32_t arg1, uint32_t arg2,
                    uint32_t arg3, uint32_t arg4, uint32_t arg5);
void ia64_fp_fclass(CPUIA64State *arg0, uint32_t arg1, uint32_t arg2,
                    uint32_t arg3, uint32_t arg4);
void ia64_fp_fmerge_ns(CPUIA64State *arg0, uint32_t arg1, uint32_t arg2,
                       uint32_t arg3);
void ia64_fp_fmerge_s(CPUIA64State *arg0, uint32_t arg1, uint32_t arg2,
                      uint32_t arg3);
void ia64_fp_fmerge_se(CPUIA64State *arg0, uint32_t arg1, uint32_t arg2,
                       uint32_t arg3);
void ia64_fp_flogical_and(CPUIA64State *arg0, uint32_t arg1, uint32_t arg2,
                          uint32_t arg3);
void ia64_fp_flogical_andcm(CPUIA64State *arg0, uint32_t arg1, uint32_t arg2,
                            uint32_t arg3);
void ia64_fp_flogical_or(CPUIA64State *arg0, uint32_t arg1, uint32_t arg2,
                         uint32_t arg3);
void ia64_fp_flogical_xor(CPUIA64State *arg0, uint32_t arg1, uint32_t arg2,
                          uint32_t arg3);
void ia64_fp_fswap(CPUIA64State *arg0, uint32_t arg1, uint32_t arg2,
                   uint32_t arg3, uint32_t arg4);
void ia64_fp_fmix(CPUIA64State *arg0, uint32_t arg1, uint32_t arg2,
                  uint32_t arg3, uint32_t arg4);
void ia64_fp_fsxt(CPUIA64State *arg0, uint32_t arg1, uint32_t arg2,
                  uint32_t arg3, uint32_t arg4);
void ia64_fp_fpmerge(CPUIA64State *arg0, uint32_t arg1, uint32_t arg2,
                     uint32_t arg3, uint32_t arg4);
void ia64_fp_fpminmax(CPUIA64State *arg0, uint32_t arg1, uint32_t arg2,
                      uint32_t arg3, uint32_t arg4, uint32_t arg5,
                      uint32_t arg6);
void ia64_fp_fpcmp(CPUIA64State *arg0, uint32_t arg1, uint32_t arg2,
                   uint32_t arg3, uint32_t arg4, uint32_t arg5);
void ia64_fp_fpcvt(CPUIA64State *arg0, uint32_t arg1, uint32_t arg2,
                   uint32_t arg3, uint32_t arg4, uint32_t arg5);
void ia64_fp_fpma(CPUIA64State *arg0, uint32_t arg1, uint32_t arg2,
                  uint32_t arg3, uint32_t arg4, uint32_t arg5, uint32_t arg6);
void ia64_fp_fsetc(CPUIA64State *arg0, uint32_t arg1, uint32_t arg2,
                   uint32_t arg3);
void ia64_fp_fclrf(CPUIA64State *arg0, uint32_t arg1);
uint64_t ia64_fp_fchkf(CPUIA64State *arg0, uint32_t arg1);
void ia64_fp_fms(CPUIA64State *arg0, uint32_t arg1, uint32_t arg2,
                 uint32_t arg3, uint32_t arg4, uint32_t arg5);
void ia64_fp_fnma(CPUIA64State *arg0, uint32_t arg1, uint32_t arg2,
                  uint32_t arg3);
void ia64_fp_fnma4(CPUIA64State *arg0, uint32_t arg1, uint32_t arg2,
                   uint32_t arg3, uint32_t arg4, uint32_t arg5);
void ia64_fp_fselect(CPUIA64State *arg0, uint32_t arg1, uint32_t arg2,
                     uint32_t arg3, uint32_t arg4);
void ia64_fp_fnorm(CPUIA64State *arg0, uint32_t arg1, uint32_t arg2,
                   uint32_t arg3, uint32_t arg4);
void ia64_fp_fpabs(CPUIA64State *arg0, uint32_t arg1, uint32_t arg2);
void ia64_fp_fpneg(CPUIA64State *arg0, uint32_t arg1, uint32_t arg2);
void ia64_fp_fpnegabs(CPUIA64State *arg0, uint32_t arg1, uint32_t arg2);
void ia64_fp_fcvt_xf(CPUIA64State *arg0, uint32_t arg1, uint32_t arg2);
void ia64_fp_fcvt_fx(CPUIA64State *arg0, uint32_t arg1, uint32_t arg2,
                     uint32_t arg3, uint32_t arg4, uint32_t arg5);
uint64_t ia64_fp_getf(CPUIA64State *arg0, uint32_t arg1, uint32_t arg2);
void ia64_fp_setf_exp(CPUIA64State *arg0, uint32_t arg1, uint64_t arg2);
void ia64_fp_setf_s(CPUIA64State *arg0, uint32_t arg1, uint64_t arg2);
void ia64_fp_fmov(CPUIA64State *arg0, uint32_t arg1, uint32_t arg2);
void ia64_fp_fprsqrta(CPUIA64State *arg0, uint32_t arg1, uint32_t arg2,
                      uint32_t arg3, uint32_t arg4);
void ia64_fp_frsqrta(CPUIA64State *arg0, uint32_t arg1, uint32_t arg2,
                     uint32_t arg3, uint32_t arg4);
void ia64_fp_fpack(CPUIA64State *arg0, uint32_t arg1, uint32_t arg2,
                   uint32_t arg3);
void ia64_fp_ldfe(CPUIA64State *arg0, uint32_t arg1, uint64_t arg2,
                  uintptr_t ra);
void ia64_fp_ldf_fill(CPUIA64State *arg0, uint32_t arg1, uint64_t arg2,
                      uintptr_t ra);
void ia64_fp_stfe(CPUIA64State *arg0, uint64_t arg1, uint32_t arg2,
                  uintptr_t ra);
void ia64_fp_stf_spill(CPUIA64State *arg0, uint64_t arg1, uint32_t arg2,
                       uintptr_t ra);

#endif /* TARGET_IA64_ARCH_FP_H */
