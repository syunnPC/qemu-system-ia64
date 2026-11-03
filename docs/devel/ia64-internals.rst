.. _ia64-internals:

IA-64 target internals
======================

The IA-64 target separates decoding, TCG generation, architectural behavior,
and helper ABI adaptation.  Dependency direction is deliberately one-way::

  decode/  -> typed instruction data
      |                 |
      v                 v
  translate/ -------> arch/ <----- cpu.c and debugger integration
                          ^
                          |
                       helper/

``decode/`` does not include TCG headers.  ``translate/`` may emit TCG but
must not reinterpret raw encoding fields that belong in the decoder.
``arch/`` contains C architectural operations and has no ``HELPER()`` or
``GETPC`` dependency.  ``helper/`` is limited to thin adapters that preserve
the declarations and call flags in ``target/ia64/helper.h``.

CPU and state ownership
-----------------------

``IA64CPU`` owns ``CPUIA64State``, the ITM host timer, the selected CPU model,
and immutable boot information supplied by ``IA64BootInfo``.  Platform code
sets boot information through ``ia64_cpu_set_boot_info()`` and never writes
general, application, or control registers directly.

The subsystem state in ``target/ia64/internals.h`` records its lifetime in
comments:

* ``IA64ExceptionState`` owns restart-visible fault state and short-lived
  suppression bookkeeping.
* ``IA64MMUState`` owns reconstructible TLB/micro-TLB caches and purge state.
* ``IA64InterruptState`` owns local SAPIC state and derived host timer data.
* ``IA64PalState``, ``IA64RSEState``, and ``IA64AlatState`` own their
  corresponding architectural state.
* ``IA64FPState`` owns the authoritative register-format FP representation,
  SoftFloat execution cache, FPSWA handoff, and one-instruction rollback
  transaction.
* ``IA64FirmwareDebugState`` belongs to ``IA64CPU`` rather than
  ``CPUIA64State`` because it is a firmware/device bridge, not architectural
  CPU state.

CPU reset cancels the ITM timer, clears ``CPUIA64State``, restores architectural
constants, initializes derived caches, and finally applies pending boot
information.  Machine reset reapplies each CPU's boot information and resets
ACPI/watchdog state.  Machine-instance finalization owns its allocated NVRAM
paths; device and memory-region lifetime remains under QOM.

Decode and translation pipeline
-------------------------------

``decode/decode.c`` converts bundle slots into a typed ``Ia64Instruction`` and
``Ia64Opcode``.  ``translate/translate.c`` handles instruction-group restart
state, predication, NaT policy, and family dispatch.  Family generators live
in ``gen-branch.c``, ``gen-integer.c``, ``gen-memory.c``, ``gen-fp.c``,
``gen-simd.c``, and ``gen-system.c``.

``DisasContext`` contains explicit memory, restart, and branch substates.  A
family generator returns ``IA64_GEN_UNHANDLED``, ``IA64_GEN_CONTINUE``, or
``IA64_GEN_NORETURN``; only the common pipeline decides how the next slot and
instruction group are entered.

Architectural modules
---------------------

The modules under ``arch/`` are grouped by the state and specification area
they own: ALAT, exception, firmware bridge, floating point/FPSWA, interrupt,
memory, MMU, PAL, RSE, SIMD, and system-register behavior.  Public entry points
are declared in ``arch/arch.h``.  Keep TCG-specific values and call-site return
addresses in the helper adapter when extending these interfaces.

The human register dump, versioned ``IA64STATE`` test schema, and the 462-entry
GDB wire layout live in ``dump.c`` and ``gdbstub.c`` rather than in CPU core
logic.  Schema consumers must reject unknown ``IA64STATE`` versions.

Runtime trace events use the ``ia64_`` prefix and follow the architectural
subsystems: exception delivery, SAPIC/ITM, MMU purge serialization, RSE frame
transitions, PAL calls, and FPSWA calls.  They can be selected with QEMU's
ordinary ``-trace 'ia64_*'`` interface.

Machine and firmware boundary
-----------------------------

``IA64VpcMachineState`` owns all mutable board configuration, device pointers,
memory regions, notifiers, and NVRAM data.  The v9 firmware handoff is the
packed, 80-byte ``IA64VpcHandoff`` in
``include/hw/ia64/ia64_vpc_abi.h``.  That header intentionally has no hosted
QEMU dependency and is shared with the freestanding firmware.

The board Kconfig separates its required PCI, ISA, ACPI, serial, and loader
infrastructure from the ``GRAPHICS``, ``NETWORK``, ``PS2``, ``STORAGE``, and
``USB`` device groups.  The default configuration implies every group, while
``--with-devices-ia64=minimal`` explicitly disables them and keeps the serial
console.  Code outside a matching ``CONFIG_IA64_VPC_*`` guard must tolerate a
missing optional device pointer.

Firmware sources are independent translation units listed once in
``roms/ia64-firmware/firmware.sources``.  Meson and the Makefile wrapper invoke
the same build script, which emits compiler depfiles, an ELF map, and section
sizes.  IA-64-owned code must not include C source files directly.  Pure
firmware modules should keep their platform dependencies injectable: the EFI
decompressor, for example, is compiled natively with a memory-primitive stub
by ``test-ia64-firmware-decompress``.

Linux user mode
---------------

``qemu-ia64`` runs IA-64 Linux programs with the same decoder, translator and
architectural modules.  The machine, the firmware, PAL, the SAPIC and the
IA-32 execution engine are not built: a process has no physical memory, and
Linux keeps ``PSR.di`` set, so ``br.ia`` and ``jmpe`` fault before the IA-32
engine would run.  ``linux-user/ia64`` follows ``arch/ia64`` of Linux 6.6,
the last kernel with IA-64.

Region windows
  A process uses several IA-64 regions at once: shared objects and anonymous
  mappings in region 1, program text in region 2, data in region 3.
  linux-user keeps one contiguous guest address space, so
  ``target/ia64/user-vaddr.h`` gives each region a window of 2^40 bytes at
  ``region << 40``.  Registers, memory contents and system-call results hold
  the IA-64 address; the fold happens only where QEMU touches memory: the
  translator's loads and stores, instruction fetch and the pointers system
  calls pass to the host, through the CPU's ``untagged_addr`` hook.  An
  offset beyond its window folds into region 7's window, which never holds a
  user mapping.  Region 0 maps to itself, so a folded address folds to
  itself; region 0 addresses at or above 2^40 therefore alias the other
  windows.  TCG plugins see the folded address of a memory access.

Translations
  A process has no TLB.  ``ia64_tlb_find_cached()`` builds the entry for a
  page from its linux-user page flags, with the access rights, ``A``, ``D``
  and ``ED`` bits Linux puts in a user page table entry.  Speculation,
  ``probe``, alignment and fault checks then run the system-mode code
  unchanged; ``CR.DCR`` defers the speculative faults Linux would defer.

Interruptions
  Faults reach ``cpu_loop()`` instead of the IVT and become the signals
  ``ia64_fault()`` and ``ia64_bad_break()`` send.  ``break 0x100000`` is a
  system call: the number in ``r15``, the arguments in the caller's output
  registers, the result in ``r8`` and ``r10``.  Software-assist floating-point
  faults are completed as the kernel's FPSWA call would, and unaligned integer
  references through the same assist as the firmware's.

Signals
  The signal frame and ``struct sigcontext`` follow the Linux ABI.  Delivery
  covers the interrupted register frame, and ``rt_sigreturn`` restores it as
  ``rfi`` would.  The trampoline sits at ``0xa000000000000100`` in a gate
  page, where libgcc's unwinder recognises a signal frame.

Tests and adding instructions
-----------------------------

The architectural microprogram registry contains typed ``IA64Case`` objects.
Each case is registered in exactly one family and contains an executable
runner, optional structured bundles, and expected architectural state.  The
registry validates the preserved case-ID manifest and lints structured bundle
data.  Instruction encoders follow the translator boundaries in
``encoding_branch.py``,
``encoding_integer.py``,
``encoding_system.py``, ``encoding_memory.py``, ``encoding_fp.py``, and
``encoding_simd.py``; ``encoding.py`` remains a compatibility facade and the
cross-family microprogram harness.

When adding an instruction:

#. derive its fields and behavior from the IA-64 manuals;
#. add the typed opcode and operand decoding under ``decode/``;
#. add semantics to exactly one family generator and, if needed, one
   architectural subsystem;
#. add an independently derived case to the matching ``cases_*.py`` module;
#. run the IA-64 unit suite and the affected machine or firmware gate described
   in :doc:`testing/ia64`.
