.. _system-target-ia64:

IA-64 System emulator
=====================

QEMU's IA-64 system emulator provides virtual PC, HP workstation, and HP
server machine models.  It uses TCG and the project-provided EFI firmware.

Machine models
--------------

The machine models are grouped by processor generation:

``itanium-vpc`` and ``hp-i2000`` (Merced generation)
  ``itanium-vpc`` defaults to the ``merced`` CPU model.  ``hp-i2000``
  emulates the Intel 460GX-based HP workstation and requires the same CPU
  model.  ``itanium-vpc`` uses PS/2 input.  ``hp-i2000`` retains its PS/2
  controller and defaults to a USB keyboard and tablet.

``itanium2-vpc`` and ``hp-zx6000`` (Itanium 2 generation)
  ``itanium2-vpc`` defaults to the ``montecito`` CPU model.  ``hp-zx6000``
  emulates the HP zx1-based workstation and requires ``madison-zx6000``.
  Both default to a USB keyboard and tablet.

``hp-rx2660`` (Montecito generation)
  Provides an HP Integrity rx2660 server model.  It defaults to the
  ``montecito-9010`` CPU model, 8 GiB of RAM, and a USB keyboard and tablet.

``ia64-vpc`` aliases ``itanium2-vpc``.  The virtual PC models support 64 CPUs,
``hp-i2000`` and ``hp-zx6000`` two, and ``hp-rx2660`` eight.  Use
``-accel tcg,thread=multi`` for more than one CPU.

HP i2000 device layout
----------------------

The i2000 exposes PCI buses 00 through 03 and 460GX configuration functions
on bus 04.  Its fixed devices are the Intel programmable interrupt device at
``00:00.0``, PIIX5/IFB at ``00:03``, CS4281 audio at ``00:04.0``, Intel 82559
Ethernet at ``00:05.0``, QLogic ISP12160 at ``01:00.0``, IHPC functions at
``01:0f.0`` and ``02:0f.0``, and Quadro2 Pro at ``03:00.0``.

The programmable interrupt device's PCI function supplies its identity; the
interrupt delivery path uses the separate 460GX SAPIC model.  IHPC and most
460GX configuration functions implement enumeration and register storage only.
The 460GX memory-card A/B configuration functions are not implemented.  The
CS4281 supports primary AC '97 playback and capture routed through any of its
four DMA channels, including continuous DMA and half/terminal-count interrupts.
Its legacy audio, FM synthesis, game port, MIDI, secondary codec and non-PCM
serial slots remain unimplemented.
The ISP12160 models mailboxes, queues, and SCSI I/O; its onboard RISC firmware
does not execute.  The 82559 Flash aperture contains no Flash storage.
``-vga ati`` places an ATI adapter at ``03:00.0``.

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

The rx2660 accepts ``montecito-9010`` (one core, 6 MiB L3) and
``montecito-9040`` (two cores, 18 MiB L3), both at 1.6 GHz.  The 9010 has one
thread per core; the 9040 supports one or two.  The machine supports up to two
sockets.  CPU hotplug is unsupported, so ``maxcpus`` must equal ``cpus``.

RAM ranges from 1 GiB to 32 GiB, with an 8 GiB default.  Default devices are
five PCI/PCI-X roots with ACPI UIDs 0, 0x200, 0x300, 0x600, and 0x700,
RN50/ES1000 VGA, two NEC OHCI functions and one EHCI function, an LSI SAS1068,
and two BCM5704 functions.  The MIO exposes zx2 IDs, but its registers and the
root adapters reuse zx1 behavior; zx2 multi-rope LBA grouping is not
implemented.

PCIe, Core-I/O management, and iLO/BMC are not implemented.  Management
functions ``103c:1303`` and ``103c:1302`` enumerate at ``00:01`` but do not
provide management services.  The ``103c:1048`` console function implements
a 16550 UART at BAR1, including FIFOs, interrupts and migration.  It uses the
third serial backend; for example ``-serial none -serial none -serial stdio``
connects this UART to the terminal while leaving the two PDH UARTs disconnected.

Broadcom Ethernet
-----------------

The BCM5701 and BCM5704 implement PCI configuration and power-management
capabilities, PHY discovery, EEPROM/NVRAM access, indirect register/SRAM access,
descriptor DMA, transmit/receive, VLAN insertion/removal, transmit checksums,
IPv4 TCP segmentation, statistics/status DMA and INTx interrupts.  Embedded
processor execution is not implemented; reset supplies the modeled board data
and firmware-mailbox handshake.  The option-ROM aperture contains no boot
firmware, and network boot is unavailable.

The zx6000's default network backend is attached to its Intel 82550.  To use
the onboard Broadcom instead, select ``-nic user,model=bcm5701``.  The rx2660
defaults to ``bcm5704``.

Graphics coverage
-----------------

The ATI models support high-color/true-color scanout and VBE modes.
The HP bridge supplies matching ATI COMBIOS metadata in the legacy ROM shadow
and the default PCI option ROM, and initializes the default Radeon memory and
system clocks consistently with those tables.  Explicitly supplied option ROMs
are preserved.  Radeon CRT detection and DDC/EDID are implemented.
The Radeon command processor handles rectangle fills and copies, transparent
copies, scanline spans, clipping, character bitmaps and indexed host bitmap
uploads, including the setup-only packets used before character drawing.
Scaler palettes are separate from the display DAC palette and are preserved
across migration.  CRTC offset locking works through both register aliases.
ATI hardware cursors are composited into the display at the programmed
position.

Graphics emulation remains partial.  ATI overlay/scaler output, tiled scanout,
some 2D operations and parts of the 3D pipeline remain unimplemented.  Quadro2
supports framebuffer/VBE and part of its 2D engine, but NV15 3D object classes
and tiled VRAM access are not implemented.

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

A typical invocation is::

  build/qemu-system-ia64 \
      -machine hp-i2000,nvram=/path/to/guest.nvram \
      -drive file=/path/to/guest-disk.qcow2,format=qcow2 \
      -display gtk

On ``hp-i2000`` and ``hp-zx6000``, disks without an explicit interface use
SCSI and CD-ROMs use IDE; an explicit ``if=scsi`` or ``if=ide`` takes
precedence.  On ``hp-rx2660``, both default to SCSI and no IDE controller is
present.  On both virtual PC models, drives without an explicit interface use
the LSI53C895A SCSI controller.  ``itanium2-vpc`` also provides AHCI; attach
AHCI media with ``if=none`` and an explicit ``ide-hd`` or ``ide-cd`` device.

ALAT model
----------

``alat=zero|full`` selects the IA-64 ALAT model.  Every machine and CPU model
defaults to ``zero``.  The ``full`` model is restricted to one CPU;
multi-CPU configurations warn and use ``zero``.
Writable VFIO DMA mappings suppress entries in the ``full`` model while those
mappings are active.  Direct writes by arbitrary processes to shared guest RAM
are not observed.  IA-32 compatibility instructions execute exclusively when
the ``full`` model is active.

Virtual PC options
------------------

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

Status
------

The IA-64 target remains incomplete and is intended for experimental use.
