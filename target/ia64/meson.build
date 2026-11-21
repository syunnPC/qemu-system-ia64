ia64_ss = ss.source_set()
ia64_llvm_builtin = false
if check_ia64_llvm and have_tcg and 'cpp' in all_languages
  jit_source = files('../../contrib/ia64-llvm/jit.cpp')
  foreach method : ['config-tool', 'cmake']
    llvm = dependency('llvm', method: method, modules: ['core', 'orcjit', 'native', 'passes'],
                      include_type: 'system', required: false)
    if llvm.found() and cxx.compiles(jit_source[0], dependencies: llvm,
                                    name: 'IA-64 LLVM JIT API')
      ia64_llvm_builtin = true
      break
    endif
  endforeach
  if ia64_llvm_builtin
    builtin_flag = '-DCONFIG_IA64_LLVM_BUILTIN'
    backend = static_library('ia64-llvm-backend',
      files('../../contrib/ia64-llvm/frontend.c', '../../contrib/ia64-llvm/lower.c'),
      jit_source, dependencies: llvm, include_directories: include_directories('../..'),
      c_args: builtin_flag, cpp_args: ['-fno-rtti'], gnu_symbol_visibility: 'hidden')
    ia64_ss.add(declare_dependency(link_with: backend, compile_args: builtin_flag))
  endif
endif
ia64_ss.add(files(
  'decoder.c',
  'dump.c',
  'exec-access.c',
  'gdbstub.c',
  'llvm.c',
  'arch/alat.c',
  'arch/exception.c',
  'arch/firmware.c',
  'arch/fp.c',
  'arch/interrupt.c',
  'arch/memory.c',
  'arch/mmu.c',
  'arch/pal.c',
  'arch/ras.c',
  'arch/rse.c',
  'arch/simd-ops.c',
  'arch/system.c',
  'decode/decode.c',
  '../i386/tcg/translate.c',
  '../i386/tcg/access.c',
  '../i386/tcg/cc_helper.c',
  '../i386/tcg/int_helper.c',
  '../i386/tcg/fpu_helper.c',
  '../i386/tcg/mem_helper.c',
  '../i386/tcg/misc_helper.c',
  '../i386/tcg/seg_helper.c',
  'ia32/state.c',
  'ia32/excp_helper.c',
  'ia32/system_helper.c',
  'ia32/transition_helper.c',
  'machine.c',
  'rse-migration.c',
  'translate/gen-branch.c',
  'translate/gen-fp.c',
  'translate/gen-integer.c',
  'translate/gen-memory.c',
  'translate/gen-simd.c',
  'translate/gen-system.c',
  'translate/translate.c',
  'cpu.c',
  'fpreg.c',
  'helper/helper-alat.c',
  'helper/helper-exception.c',
  'helper/helper-firmware.c',
  'helper/helper-fp.c',
  'helper/helper-interrupt.c',
  'helper/helper-memory.c',
  'helper/helper-mmu.c',
  'helper/helper-pal.c',
  'helper/helper-rse.c',
  'helper/helper-simd.c',
  'helper/helper-system.c',
))
target_arch += {'ia64': ia64_ss}
