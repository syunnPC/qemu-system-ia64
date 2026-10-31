.. _system-target-ia64:

IA-64 System emulator
=====================

QEMU's IA-64 system emulator provides virtual PC, HP workstation, and HP
server machine models.  It uses TCG and the project-provided EFI firmware.

Machine models
--------------

The machine models are grouped by processor generation:

``itanium-vpc`` and ``hp-i2000`` (Merced generation)
  ``itanium-vpc`` defaults to the ``merced-800`` CPU model.  ``hp-i2000``
  emulates the Intel 460GX-based HP workstation and requires the same CPU
  model.  ``itanium-vpc`` uses PS/2 input.  ``hp-i2000`` retains its PS/2
  controller and defaults to a USB keyboard and tablet.

``itanium2-vpc``, ``hp-zx2000`` and ``hp-zx6000`` (Itanium 2 generation)
  ``itanium2-vpc`` defaults to the ``montecito-9050`` CPU model.
  ``hp-zx2000`` defaults to ``mckinley-900``; ``hp-zx6000`` requires
  ``madison-1500``.  Both workstations use the HP zx1 chipset.
  All three default to a USB keyboard and tablet.

``hp-rx2660`` (Montecito generation)
  Provides an HP Integrity rx2660 server model.  It defaults to the
  ``montecito-9010`` CPU model, 8 GiB of RAM, and a USB keyboard and tablet.

``itanium2-vpc-iommu`` (Tukwila generation)
  Defaults to ``tukwila-9350`` and adds a 7500-class VT-d hub.

``ia64-vpc`` aliases ``itanium2-vpc``.  The virtual PC models support 64 CPUs,
``hp-zx2000`` one, ``hp-i2000`` and ``hp-zx6000`` two, and ``hp-rx2660``
eight.  Use ``-accel tcg,thread=multi`` for more than one CPU.

``-machine ...,usb1=on`` limits USB ports to low/full speed (1.5/12 Mb/s),
including ports on hubs and hotplugged controllers.  Both the port and the
device must support one of these speeds.  The default is ``usb1=off``.

Virtual PC DMA and PCI interrupts
---------------------------------

``itanium-vpc`` and ``itanium2-vpc`` optionally support
``-device virtio-iommu-pci,granule=4k`` on their PCI root bus; neither creates
an IOMMU by default.  The processor interrupt block at
``0xfee00000-0xfeefffff`` is reserved (type 0), so device accesses there fault.
Boot bypass defaults to on for firmware storage DMA.  The guest enables
translation through ``BYPASS_CONFIG`` after boot DMA ends.
The device cannot be hotplugged or unplugged.

There is no ACPI VIOT table; discover PCI vendor/device ``1af4:1057`` on
segment 0, bus 0.  Use ``addr`` to fix its slot.
Endpoint IDs are PCI requester IDs: ``(bus << 8) | (slot << 3) | function``.
Keep the IOMMU's own virtqueue DMA untranslated (the default
``iommu_platform=off``), never attach its endpoint, and disable its INTx if
using a polling driver.

``-machine itanium2-vpc,pci-intx-lines=8`` opts into eight PCI INTx lines at
GSI 16-23; the default is four lines at GSI 16-19.  For zero-based pins
(INTA=0 through INTD=3), routing is ``16 + (slot + pin) % pci-intx-lines``.
The Interrupt Line config byte and firmware ACPI ``_PRT`` use that routing.
The eight-line ``_PRT`` covers all 32 root-bus slots; four-line routing retains
the original firmware handoff and ACPI AML.  Both use the 24-pin IOSAPIC.

``-device e1000e`` and ``-device qemu-xhci`` add controllers alongside the
default devices.

CPU model names
---------------

Use ``-cpu generation-model_number`` to select a canonical CPU model, for
example ``-cpu montecito-9010``.  For generations without processor numbers,
the number is the clock in MHz.  A cache suffix is included only when models
within a generation share that clock: ``madison-1500``, but
``madison-1600-3m`` and ``madison-1600-9m``.

.. list-table:: Canonical CPU models and generation aliases
   :header-rows: 1
   :widths: 15 60 25

   * - Generation alias
     - Canonical models
     - Alias target
   * - ``merced``
     - ``merced-800``
     - ``merced-800``
   * - ``mckinley``
     - ``mckinley-900``, ``mckinley-1000``
     - ``mckinley-1000``
   * - ``deerfield``
     - ``deerfield-1000``
     - ``deerfield-1000``
   * - ``madison``
     - ``madison-1400-1.5m``, ``madison-1400-4m``, ``madison-1500``,
       ``madison-1600-3m``, ``madison-1600-9m``
     - ``madison-1600-3m``
   * - ``montecito``
     - ``montecito-9010``, ``montecito-9015``, ``montecito-9020``,
       ``montecito-9030``, ``montecito-9040``, ``montecito-9050``
     - ``montecito-9050``
   * - ``montvale``
     - ``montvale-9110n``, ``montvale-9120n``, ``montvale-9130m``,
       ``montvale-9140m``, ``montvale-9140n``, ``montvale-9150m``,
       ``montvale-9150n``, ``montvale-9152m``
     - ``montvale-9150n``
   * - ``tukwila``
     - ``tukwila-9310``, ``tukwila-9320``, ``tukwila-9330``,
       ``tukwila-9340``, ``tukwila-9350``
     - ``tukwila-9350``

``itanium`` aliases ``merced`` and ``itanium2`` aliases ``montecito``.
``-cpu help`` displays both canonical names and alias targets.

HP i2000 device layout
----------------------

The i2000 exposes PCI buses 00 through 03 and 460GX configuration functions
on bus 04.  Its fixed devices are the Intel programmable interrupt device at
``00:00.0``, PIIX5/IFB at ``00:03``, CS4281 audio at ``00:04.0``, Intel 82559
Ethernet at ``00:05.0``, QLogic ISP12160 at ``01:00.0``, IHPC functions at
``01:0f.0`` and ``02:0f.0``, and Quadro2 Pro at ``03:00.0``.

The programmable interrupt device's PCI function supplies its identity; the
interrupt delivery path uses the separate 460GX SAPIC model.  IHPC supports
slot power control, hotplug notifications, and guest-controlled removal.
The 460GX memory-card A/B configuration functions expose memory-error logs;
chipset errors are reported through the firmware RAS mailbox.
The CS4281 supports primary AC '97 playback and capture routed through any of its
four DMA channels, including continuous DMA and half/terminal-count interrupts.
Its legacy audio, FM synthesis, game port, MIDI, secondary codec and non-PCM
serial slots remain unimplemented.
The ISP12160 models mailboxes, queues, and SCSI I/O; its onboard RISC firmware
does not execute.  SIMPLE, HEAD and ORDERED tags control per-LUN dispatch.
IOCB timeouts include time spent waiting in the queue.  Target autosense,
per-command autosense suppression, initiator IDs, queue depth and execution
throttle settings are supported.  Queued requests and deadlines migrate.
The 82559 Flash aperture contains no Flash storage.
``-vga ati`` places an ATI adapter at ``03:00.0``.

HP zx2000 device layout
-----------------------

The zx2000 accepts ``mckinley-900`` (default) and ``madison-1400-1.5m``
with one CPU.  RAM ranges from 512 MiB to 8 GiB, with a 1 GiB default.
The model maps up to 1 GiB below the PCI aperture and the remainder at 4 GiB.

Its four zx1 roots start at buses 00, 80, a0, and c0 on ropes 0, 4, 5,
and 6.  Fixed devices are Radeon RV100 at ``00:00.0``, NEC USB at
``a0:01.0`` through ``a0:01.2``, CMD649 IDE at ``a0:02.0``, and Intel
82540EM Ethernet at ``a0:03.0``.  The default NIC model is ``e1000``.
The NEC model exposes five EHCI ports and two OHCI companions.

Disks and CD-ROMs default to IDE, with four devices across two channels.
The provided EFI firmware detects only the primary channel; attach boot
media using ``index=0`` or ``index=1``.

The model provides NVRAM, RTC and ACPI support with the provided EFI
firmware.  Its two PDH 16550 UARTs are at
``0xff5e0000`` and ``0xff5e2000``.  FM801 audio, its gameport and BMC
services are not emulated.  No onboard SCSI controller is instantiated.

EFI PCI root device paths use expanded ACPI nodes with ``PNP0A03`` as
the compatible ID.

HP zx6000 device layout
-----------------------

The zx6000 exposes zx1 roots at buses 00, 20, 40, 60, 80, and c0.  Its fixed
devices are NEC USB at ``00:01.0`` through ``00:01.2``, CMD649 IDE at
``00:02.0``, Intel 82550 at ``00:03.0``, LSI53C1030 at ``20:01.0`` and
``20:01.1``, BCM5701 at ``20:02.0``, and Radeon RV100 at ``80:00.0``.  The
NEC controller exposes five EHCI ports and two OHCI companions with 3+2 ports.
The 82550 Flash aperture contains no Flash storage.

HP Integrity rx2660
-------------------

The rx2660 accepts ``montecito-9010`` (default), ``montecito-9020``,
``montecito-9040``, ``montvale-9110n``, ``montvale-9120n``, and
``montvale-9140m``.  The 9010 and 9110n have one core and one thread per
socket; the other supported models have two cores and one or two threads per
core.  The machine supports up to two sockets.  CPU hotplug is unsupported,
so ``maxcpus`` must equal ``cpus``.

RAM ranges from 1 GiB to 32 GiB, with an 8 GiB default.  Default devices are
five PCI/PCI-X roots with ACPI UIDs 0, 0x200, 0x300, 0x600, and 0x700,
RN50/ES1000 VGA, two NEC OHCI functions and one EHCI function, an LSI SAS1068,
and two BCM5704 functions.  The MIO exposes zx2 IDs and four IOMMU contexts
selected through rope-group mappings.  The IOMMU translation engine and
PCI/PCI-X root adapters reuse zx1 behavior.  All rope groups initially use
context zero, which is configured through the common IOC register bank.

The onboard BCM5704 functions use subsystem identity ``103c:1311`` for guest
compatibility; this value has not been verified for physical rx2660 hardware.
Snapshots retain their saved PCI subsystem IDs.

PCIe, Core-I/O management, and iLO/BMC are not implemented.  Management
functions ``103c:1303`` and ``103c:1302`` enumerate at ``00:01`` but do not
provide management services.  The ``103c:1048`` console function implements
a 16550 UART at BAR1, including FIFOs, interrupts and migration.  It uses the
first serial backend, so ``-serial stdio`` connects this UART to the terminal.
The firmware console descriptor and EFI device path identify this PCI UART.
The two PDH UARTs use the second and third serial backends.

Storage completion and Ethernet
-------------------------------

The LSI53C895A honors HEAD and ORDERED tasks across disconnect/reselect.
Migration requires its requests to have completed; an active request returns
a migration error.  LSI53C1030 and SAS1068 implement IOC Page 1 reply coalescing
with a completion-count threshold and a timer in microseconds.  Reading the
reply FIFO through its empty indication acknowledges the notification.

The Intel 82550/82559 receive path supports CRC transfer and IEEE 802.3
padding stripping.  CRCs cover the original padded wire frame.  The e1000
implements RDTR/TIDV relative completion timers with RADV/TADV absolute caps,
in addition to ITR throttling.  Controller resets cancel pending notifications.

The BCM5701 and BCM5704 implement PCI configuration and power-management
capabilities, PHY discovery, EEPROM/NVRAM access, indirect register/SRAM access,
descriptor DMA, transmit/receive, VLAN insertion/removal, transmit checksums,
IPv4 TCP segmentation, statistics/status DMA and INTx interrupts.  Receive
descriptors carry IPv4 and TCP/UDP checksum results, including bad checksums;
fragments and UDP packets without a checksum do not claim transport checksum
validation.  Host coalescing applies RX/TX completion-count thresholds and
microsecond timers.  Pending coalescing state migrates.  Embedded
processor execution is not implemented; reset supplies the modeled board data
and firmware-mailbox handshake.  The option-ROM aperture contains no boot
firmware, and network boot is unavailable.

MMIO register and SRAM accesses honor the MISC_HOST_CTRL byte-swap bit,
including byte and halfword accesses.  The two MISC_HOST_CTRL word-swap
controls are not modeled; accesses retain the existing DWORD address layout.

The zx6000's default network backend is attached to its Intel 82550.  To use
the onboard Broadcom instead, select ``-nic user,model=bcm5701``.  The rx2660
defaults to ``bcm5704``.

Graphics coverage
-----------------

The ATI models support high-color/true-color scanout and VBE modes.
The HP bridge supplies matching ATI COMBIOS metadata in the legacy ROM shadow
and the default PCI option ROM, and initializes the default Radeon memory and
system clocks and CRT connection flags consistently with those tables.
Explicitly supplied option ROMs are preserved.  Radeon CRT detection and
DDC/EDID are implemented.
The Radeon command processor handles rectangle fills and copies, transparent
copies, connected lines, scanline spans, clipping, character bitmaps and
indexed host bitmap uploads, including the setup-only packets used before
character drawing.
Scaler palettes are separate from the display DAC palette and are preserved
across migration.  CRTC offset locking works through both register aliases.
ATI hardware cursors are composited into the display at the programmed
position.

Native ATI scanout preserves the legacy VGA register bank.  Snapshots from
implementations that overwrite this bank require the guest to reinitialize
its VGA mode after restore.

ATI 2D supports Bresenham lines, monochrome and color brushes, and Rage128
stretch blits.  Rage128 trapezoids use an integer-coordinate approximation.
Line and trapezoid setup registers retain their programmed values after drawing.
Rage128 stretch uses replication or generic bilinear filtering in the
destination pixel format.  Radeon tiled 2D and scanout support macrotiles in
8-, 16- and 32-bit formats, plus 32-bit destination microtiles.  Rage128 tiled
2D accesses are unimplemented and leave destination memory unchanged; tiled
scanout returns zero pixel data.  Guests using Rage128 must select linear
framebuffers for rendering and display.

Radeon CPU accesses through aperture 0 use the configured color-surface tile
mapping, consistently with 2D rendering and scanout.  Surface registers are
preserved across migration.  ATI migration formats before version 9 omit
surface registers; guests must reinitialize tiled surfaces after restore.

PLL atomic requests complete immediately, and the primary CRTC timer follows
the programmed pixel clock.  PLL settling and synchronization to vertical
sync are not modeled.  MONID GPIO participates in DDC, and GUI idle completion
latches its interrupt status.

Quadro2 supports NV10/NV15 PFB tile regions in scanout and 2D DMA.  Long FIFO
streams yield between commands and resume without reporting a hardware fault
solely because a processing budget was exhausted.  Individual operations
retain size and address bounds.

Graphics emulation remains partial.  ATI overlay output, packed 24-bit tiled
surfaces, 8-/16-bit Radeon destination microtiles, exact subpixel trapezoid
coverage, and parts of the 3D pipeline remain unsupported.  NV15 3D object
classes are not implemented.

Technical references for these models are recorded in
:doc:`../devel/device-emulation-provenance` and
:doc:`../devel/gpu-emulation-provenance`.

Building and running
--------------------

The firmware requires an ``ia64-linux-gnu-*`` ELF cross toolchain in
``PATH``::

  ./configure --target-list=ia64-softmmu
  ninja -C build qemu-system-ia64 \
      roms/ia64-firmware/ia64-firmware.bin

All machine models use ``ia64-firmware.bin`` by default.  QEMU searches its
firmware and data directories for this file.  Use ``-bios PATH`` to load a
different image, or ``-bios none`` when booting without firmware.

The in-tree ``ia64-firmware.bin`` is an ELF image whose symbol and relocation
tables must be retained.
Firmware loads at 1 MiB by default.  Use ``-machine ...,firmware-base=0x400000``
to load it at 4 MiB, or ``firmware-base=auto`` to select a location near the
top of low RAM.  An explicit address must be at least 1 MiB, aligned to 8 KiB,
and leave room for the complete firmware below the boot stack and handoff
storage, without overlapping the ACPI tables at 8--8.125 MiB or the platform
descriptor at 3 MiB on HP models.  QEMU rejects invalid placements before
starting the CPUs.
Legacy raw firmware images require the default 1 MiB address.
Migration requires matching firmware placement at both ends.

A typical invocation is::

  build/qemu-system-ia64 \
      -machine hp-i2000,nvram=/path/to/guest.nvram \
      -drive file=/path/to/guest-disk.qcow2,format=qcow2 \
      -display gtk

On ``hp-i2000`` and ``hp-zx6000``, disks without an explicit interface use
SCSI and CD-ROMs use IDE; an explicit ``if=scsi`` or ``if=ide`` takes
precedence.  On ``hp-zx2000``, both default to IDE.  On ``hp-rx2660``, both
default to SCSI and no IDE controller is present.  On both virtual PC models,
drives without an explicit interface use the LSI53C895A SCSI controller.
``itanium2-vpc`` also provides AHCI; attach
AHCI media with ``if=none`` and an explicit ``ide-hd`` or ``ide-cd`` device.

The i2000 firmware enumerates both ISP12160 channels, targets 0--15 except
initiator ID 7, and LUNs 0--31.  It also enumerates master and slave devices
on both IDE channels.  Disks and optical drives may be mixed.  Each device
has its own Block I/O handle; supported FAT partitions, whole-disk FAT,
ISO9660/UDF volumes and El Torito FAT images appear in the EFI shell's
``map`` output.  Use ``ls fsN:\`` to browse a volume and ``boot fsN:`` to
load its ``\EFI\BOOT\BOOTIA64.EFI``.

For example, attach a disk to the second SCSI channel at target 3, LUN 1::

  -drive if=none,id=disk1,file=/path/to/disk1.qcow2,format=qcow2 \
  -device scsi-hd,bus=isp12160-scsi.0,channel=1,scsi-id=3,lun=1,drive=disk1

In the i2000 shell, ``edd30`` shows the saved SCSI device-path mode.
``edd30 on`` selects native SCSI paths (the default); ``edd30 off`` selects
EDD vendor paths.  Changes take effect after a reset and persist when
NVRAM has a backing file.  Other machines report that the command is
unsupported.
EDD drive numbers are unique within each device class: disks use
0x80--0xdf and optical drives use 0xe0--0xff.  Devices beyond these ranges
remain available through native SCSI paths.  Adding or removing devices
can change EDD numbering and the shell's ``fsN:`` assignments.

Windows host clock resolution
-----------------------------

When guest time follows the host clock, interval timer resolution depends on
the Windows performance counter frequency.  For example, a 10 MHz host counter
makes a 400 MHz guest timer advance in multiples of 40 ticks.  Counter
frequencies vary by host; see Microsoft's
`high-resolution time-stamp documentation
<https://learn.microsoft.com/en-us/windows/win32/sysinfo/acquiring-high-resolution-time-stamps>`_.

ALAT model
----------

``alat=zero|full`` selects the IA-64 ALAT model.  Every machine and CPU model
defaults to ``zero``.  The ``zero`` model treats every ALAT check as a miss,
so check loads reload memory and advanced-load checks enter their recovery
paths.  The ``full`` model maintains a 32-entry table per CPU and supports SMP.

CPU stores publish per-CPU write sequences.  Advanced loads and ALAT checks
validate those sequences together with the external RAM-write generation;
ordinary stores do not update a shared CPU-writer counter.  Stores by the
same CPU invalidate overlapping physical addresses, while writes by another
CPU or a device conservatively invalidate the whole local ALAT.

The ``full`` model skips guest reloads and recovery code on ALAT hits, while
tracking and validation add host execution work.  Writable VFIO, vhost and
remote-device DMA mappings suppress entries while those mappings are active.
Direct writes by arbitrary processes to shared guest RAM are not observed.
On CPU models with native IA-32 support, ``full`` executes IA-32 compatibility
instructions exclusively when vCPUs would otherwise run in parallel.
Montecito/Montvale models do not support native IA-32 execution.

Virtual PC options
------------------

``pcie=on|off``
  Use a PCI Express root bus with extended configuration space when enabled.
  The default is ``off``.  The ``ia64-pcie-root-port`` device provides AER
  interrupts and platform error notifications.

``i8042=on|off``
  Select PS/2 input when enabled and USB input when disabled.  It defaults to
  ``on`` for ``itanium-vpc`` and ``off`` for ``itanium2-vpc``.

``firmware-ide-dma=on|off``
  Enable or disable firmware CMD646 bus-master DMA.  The default is ``on``.

``firmware-console=serial|vga``
  Select the console advertised by HCDP.  The default is ``vga``.

EFI variable storage
--------------------

All machine models default to ``nvram=auto``.  This uses a file named
``nvram`` beside the selected firmware.  Use ``nvram=PATH`` to select a file
or ``nvram=none`` to disable persistence.  A separate file should be used for
each virtual machine.

New backing files are 512 KiB.  Existing 64 KiB files remain 64 KiB when
updated.  The HP models expose a 512 KiB variable store, while the virtual PC
models use the first 64 KiB and preserve the rest of a larger backing file.

Tukwila and the 7500 VT-d test machine
--------------------------------------

For example::

  build/qemu-system-ia64 \
    -machine itanium2-vpc-iommu,firmware-console=serial,nvram=none \
    -m 2G -display none -serial stdio \
    -drive file=esp.img,format=raw

``vtd-coherent=on`` sets ECAP.C on both units.  ``vtd-high-mmio=on`` moves
VTBAR from ``0xfed90000`` to ``0x10000fed90000``.  Both default to off; DMAR
and EFI MMIO descriptors follow VTBAR.  With the low aperture and more than
about 3.75 GiB of RAM, its 8 KiB of displaced RAM are mapped above 4 GiB.
The EFI memory map and SRAT exclude the aperture from RAM.

Slot 7 holds the secondary bridge, bus ``ioh7500-secondary``.  Its descendants
use unit 1; all other devices use unit 0.  At startup and reset, the machine
numbers that hierarchy from bus 1 and assigns BARs and forwarding windows.
Other root bridges require operating-system configuration.  INTx follows
bridge swizzling, with ``pci-intx-lines=8`` by default because the four-line
ACPI ``_PRT`` stops at slot 6.  Four-line routing remains selectable.
A virtio IOMMU cannot be added to this machine's root bus.

``tukwila`` and ``itanium-9300`` alias ``tukwila-9350``.  The 9310 through
9350 models report nominal clocks of 1600, 1333, 1466, 1600, and 1733 MHz;
per-core L3 sizes are 5, 4, 5, 5, and 6 MiB, with package totals of 10, 16,
20, 20, and 24 MiB.  All report 512 KiB instruction L2, 256 KiB data L2,
E0 CPUID[3] ``0x20020404``, and 50 physical address bits.  See Intel's
`9300/9500/9700 datasheet, table 1-1
<https://www.intel.com/content/dam/www/public/us/en/documents/datasheets/9300-9500-9700-series-datasheet.pdf>`_
and `specification update, table 3
<https://www.intel.com/content/dam/www/public/us/en/documents/specification-updates/9300-9500-9700-series-spec-update.pdf>`_.
Execution, PAL controls, cache timing, and TLB organization retain Montecito
behavior.  ITC remains a virtual 400 MHz clock; QPI is a fixed 2.4 GHz without
link timing.  Turbo, VT-i, and PAL-based IA-32 translation are not implemented.

Intel's `7500 datasheet, sections 1.2.1.1, 18 and 21.14
<https://www.intel.com/content/dam/doc/datasheet/7500-chipset-datasheet.pdf>`_
defines the modeled VT-d 1.0 units at VTBAR and VTBAR+0x1000.  They support
four-level walks, 4 KiB pages, 48-bit IOVAs, 51-bit table and output addresses,
pass-through contexts, protected ranges, eight fault records,
and register and queued invalidation.  Translation starts disabled; only
unit 0 supports the leaf SNP bit.  The model uses the
`VT-d revision 1.3 specification
<https://kib.kiev.ua/x86docs/Intel/VT-d/D51397-005.pdf>`_ encodings for SAGAW
(``0x4``, four levels) and NFR (``7``, eight records), correcting the
datasheet's ``0x2`` and ``8`` defaults.

DMAR revision 1 is present in XSDT and RSDT on this machine only.  It has
51-bit host-address width, flags 0, and two DRHDs in segment 0: the secondary
bridge scope for unit 1, then INCLUDE_PCI_ALL for unit 0.  There are no RMRRs.

Interrupt remapping is not implemented: ECAP.IR and DMAR's interrupt-remapping
flag are clear.  The interrupt-remapping table and command fields are reserved,
and interrupt-entry cache invalidation descriptors raise a queue error.
Pass-through contexts must specify AW=2, the largest supported address width.
Compatible Itanium MSI writes bypass translation; DMA mappings into the
processor interrupt block are rejected.  Fault and completion events use
compatible MSI delivery.

Table reads are coherent even with ECAP.C clear; guests must still follow
the hardware cache-flush rules.  There is no translation cache, so
invalidations complete globally.  Timing, ATS, superpages, device assignment,
and external mapping notifiers are not supported.  Migration and snapshots
are blocked because the hub has no stable migration format.

Status
------

The IA-64 target remains incomplete and is intended for experimental use.
