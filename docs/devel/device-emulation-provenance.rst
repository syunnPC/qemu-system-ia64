.. SPDX-License-Identifier: GPL-2.0-or-later

Device emulation source notice
==============================

The following public sources are technical references for
the device models that link to this notice:

* Linux
  `sound/pci/cs4281.c <https://github.com/torvalds/linux/blob/master/sound/pci/cs4281.c>`__
  for CS4281 BA0 registers, the four DMA and FIFO channels, serial-slot
  routing, and sample-rate conversion.
* Linux
  `drivers/net/ethernet/broadcom/tg3.h <https://github.com/torvalds/linux/blob/master/drivers/net/ethernet/broadcom/tg3.h>`__
  and
  `tg3.c <https://github.com/torvalds/linux/blob/master/drivers/net/ethernet/broadcom/tg3.c>`__
  for BCM5701/BCM5704 registers, SRAM and mailbox layout, PHY access, DMA
  descriptors, packet offloads, status blocks, and interrupts.
* Linux
  `drivers/scsi/qla1280.h <https://github.com/torvalds/linux/blob/master/drivers/scsi/qla1280.h>`__
  and
  `qla1280.c <https://github.com/torvalds/linux/blob/master/drivers/scsi/qla1280.c>`__
  for ISP12160 mailbox, target, queue and autosense controls;
  `drivers/net/ethernet/intel/e100.c <https://github.com/torvalds/linux/blob/master/drivers/net/ethernet/intel/e100.c>`__
  for 82550/82559 configuration byte 18 receive CRC and stripping controls.
* Intel's `8254x Family of Gigabit Ethernet Controllers Software Developer's
  Manual <https://www.intel.com/content/dam/doc/manual/pci-pci-x-family-gbe-controllers-software-dev-manual.pdf>`__,
  sections 3.2.7 and 3.4.3, for receive and transmit interrupt timers.
* The public PCI ID Repository
  `pci.ids <https://github.com/pciutils/pciids/blob/master/pci.ids>`__
  for the HP RMP-3 management-function identities.  Linux
  `drivers/tty/serial/8250/8250_pci.c <https://github.com/torvalds/linux/blob/master/drivers/tty/serial/8250/8250_pci.c>`__
  and
  `include/linux/pci_ids.h <https://github.com/torvalds/linux/blob/master/include/linux/pci_ids.h>`__
  for the HP Diva RMP3 PCI identifiers and its single 16550 UART in BAR1.
* FreeBSD
  `sys/dev/mpt/mpilib/mpi.h <https://github.com/freebsd/freebsd-src/blob/main/sys/dev/mpt/mpilib/mpi.h>`__
  for the LSI Fusion-MPT interface definitions, and Linux
  `drivers/message/fusion/mptbase.c <https://github.com/torvalds/linux/blob/master/drivers/message/fusion/mptbase.c>`__
  for the IOC reset doorbell functions and transition to the READY state.

The IA-64 firmware's PCI controller handles and device paths follow the
`UEFI 2.11 Device Path Protocol
<https://uefi.org/specs/UEFI/2.11/10_Protocols_Device_Path_Protocol.html>`__
and
`PCI I/O Protocol
<https://uefi.org/specs/UEFI/2.11/14_Protocols_PCI_Bus_Support.html>`__.
The public EDK II
`PciDeviceSupport.c
<https://github.com/tianocore/edk2/blob/master/MdeModulePkg/Bus/Pci/PciBusDxe/PciDeviceSupport.c>`__
is an implementation reference for publishing a PCI controller handle with
both protocols.
