.. SPDX-License-Identifier: GPL-2.0-or-later

IA-64 LLVM region compiler
==========================

The optional compiler translates frequently executed, straight-line IA-64
bundles on a little-endian 64-bit host. It uses QEMU's IA-64 decoder and
lifts instructions to LLVM IR. LLVM verifies and optimizes the IR at O2,
and ORC JIT compiles it in a worker thread.

Build the shared library using the LLVM C headers and the matching
``llvm-config``. LLVM 21 is supported. Run these commands from the QEMU
source directory::

    make -C contrib/ia64-llvm BUILD="$PWD/build/ia64-llvm"
    IA64_LLVM_LIBRARY="$PWD/build/ia64-llvm/ia64-llvm.so" \
        build/qemu-system-ia64 ...

The binary must have GModule support. Set ``LLVM_CONFIG`` when using a
versioned LLVM installation. To exercise the lifter, native code, and
memory fallback contract, run the following with IA-64 binutils on PATH::

    make -C contrib/ia64-llvm check \
        BUILD="$PWD/build/ia64-llvm" CROSS_IA64=ia64-linux-gnu-

The ``check-qemu`` target runs hot regions in a full-system QEMU with both
ALAT models. It checks self-modifying code, ALAT invalidation, and precise
restart after a store followed by a fault. Set ``QEMU`` to the executable
path when using another build directory.

Execution
---------

Identical bundle bytes share compiled code across CPUs and guest addresses
using the same ALAT model.
Each candidate contains at most 64 bundles from one code page. Translation
blocks cover the complete native range for instruction-cache invalidation
and can link directly to a known successor on the same page.
The compiler handles unconditional integer arithmetic, shifts, comparisons,
selected packed operations, significand transfers, and ordinary aligned RAM
loads and stores. Generated code checks the QEMU software TLB for each
destination and uses QEMU's store notification when the full ALAT is active.
TCG handles flagged write mappings, MMIO, and misaligned accesses. If an
access needs TCG after a native store, the region commits the register state
preceding that access and resumes at its bundle and slot. An earlier check
can retry the entire region.

Set ``IA64_LLVM_STATS`` to an output filename to collect counters and sampled
region addresses. The second output line gives totals for ready entries,
native runs, native bundles, native mix instructions, memory refusals,
processor-state refusals, cold or queued entries, and unsupported entries, in
that order. Leave statistics disabled when timing guest execution.
