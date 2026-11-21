.. SPDX-License-Identifier: GPL-2.0-or-later

IA-64 LLVM region compiler
==========================

The optional compiler translates frequently executed, straight-line IA-64
bundles on a little-endian 64-bit host. It uses the decoder, region formation,
SSA lifter, and verifier from an ia64-vmm-new source tree. LLVM optimizes the
resulting IR at O2 and ORC JIT compiles it in a worker thread. The QEMU binary
can also run independently with its usual TCG translator.

Build the shared library using the LLVM C headers and the matching
``llvm-config``. LLVM 21 is supported. Run these commands from the QEMU
source directory, with ``VMM_SRC`` set to the ia64-vmm-new source directory::

    make -C contrib/ia64-llvm VMM_SRC="$VMM_SRC" BUILD="$PWD/build/ia64-llvm"
    IA64_LLVM_LIBRARY="$PWD/build/ia64-llvm/ia64-llvm.so" \
        build/qemu-system-ia64 ...

The binary must have GModule support. Set ``LLVM_CONFIG`` when using a
versioned LLVM installation. To exercise the lifter, native code, and
memory fallback contract, run the following with IA-64 binutils on PATH::

    make -C contrib/ia64-llvm check VMM_SRC="$VMM_SRC" \
        BUILD="$PWD/build/ia64-llvm" CROSS_IA64=ia64-linux-gnu-

The ``check-qemu`` target also runs hot regions in a full-system QEMU. It
checks instruction-cache synchronization after code changes and the exact
bundle and slot of a misaligned load. Set ``QEMU`` to the executable path
when using another build directory.

Execution
---------

Identical bundle bytes share compiled code across CPUs and guest addresses.
Each candidate covers a single code page and at most 64 bundles. Translation
blocks cover the complete native range for instruction-cache invalidation.
The compiler handles unconditional integer arithmetic, shifts, comparisons,
selected packed operations, significand transfers, and ordinary aligned RAM
loads. TCG handles other instructions and resumes at the region entry when a
register, processor-state, or memory check fails. Native register results
become visible after all memory checks succeed.

Set ``IA64_LLVM_STATS`` to an output filename to collect counters and sampled
region addresses. The second output line gives totals for ready entries,
native runs, native bundles, native mix instructions, memory refusals,
processor-state refusals, cold or queued entries, and unsupported entries, in
that order. Leave statistics disabled when timing guest execution.
