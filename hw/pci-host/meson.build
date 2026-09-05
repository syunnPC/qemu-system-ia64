pci_ss = ss.source_set()
pci_ss.add(when: 'CONFIG_PAM', if_true: files('pam.c'))
pci_ss.add(when: 'CONFIG_PCI_BONITO', if_true: files('bonito.c'))
pci_ss.add(when: 'CONFIG_GT64120', if_true: files('gt64120.c'))
pci_ss.add(when: 'CONFIG_PCI_EXPRESS_ASPEED', if_true: files('aspeed_pcie.c'))
pci_ss.add(when: 'CONFIG_PCI_EXPRESS_DESIGNWARE', if_true: files('designware.c'))
pci_ss.add(when: 'CONFIG_PCI_EXPRESS_GENERIC_BRIDGE', if_true: files('gpex.c'))
pci_ss.add(when: ['CONFIG_PCI_EXPRESS_GENERIC_BRIDGE', 'CONFIG_ACPI'], if_true: files('gpex-acpi.c'))
pci_ss.add(when: 'CONFIG_PCI_EXPRESS_Q35', if_true: files('q35.c'))
pci_ss.add(when: 'CONFIG_PCI_EXPRESS_XILINX', if_true: files('xilinx-pcie.c'))
pci_ss.add(when: 'CONFIG_PCI_I440FX', if_true: files('i440fx.c'))
pci_ss.add(when: 'CONFIG_PCI_SABRE', if_true: files('sabre.c'))
pci_ss.add(when: 'CONFIG_XEN_IGD_PASSTHROUGH', if_true: files('xen_igd_pt.c'))
pci_ss.add(when: 'CONFIG_REMOTE_PCIHOST', if_true: files('remote.c'))
pci_ss.add(when: 'CONFIG_SH_PCI', if_true: files('sh_pci.c'))

# PPC devices
pci_ss.add(when: 'CONFIG_PPC4XX_PCI', if_true: files('ppc4xx_pci.c'))
pci_ss.add(when: 'CONFIG_PPC440_PCIX', if_true: files('ppc440_pcix.c'))
pci_ss.add(when: 'CONFIG_RAVEN_PCI', if_true: files('raven.c'))
pci_ss.add(when: 'CONFIG_GRACKLE_PCI', if_true: files('grackle.c'))
# NewWorld PowerMac
pci_ss.add(when: 'CONFIG_UNIN_PCI', if_true: files('uninorth.c'))
# PowerPC E500 boards
pci_ss.add(when: 'CONFIG_PPCE500_PCI', if_true: files('ppce500.c'))
# AmigaOne
pci_ss.add(when: 'CONFIG_ARTICIA', if_true: files('articia.c'))
# Pegasos2
pci_ss.add(when: 'CONFIG_MV64361', if_true: files('mv64361.c'))

# ARM devices
pci_ss.add(when: 'CONFIG_PCI_EXPRESS_FSL_IMX8M_PHY', if_true: files('fsl_imx8m_phy.c'))
pci_ss.add(when: 'CONFIG_VERSATILE_PCI', if_true: files('versatile.c'))

# HP system bus adapters
system_ss.add(when: 'CONFIG_HP_LBA_CONFIG',
              if_true: files('hp-lba-config.c'))
system_ss.add(when: 'CONFIG_HP_IO_SAPIC',
              if_true: files('hp-io-sapic.c'))
system_ss.add(when: 'CONFIG_HP_SBA_IOMMU',
              if_true: files('hp-sba-iommu.c'))
system_ss.add(when: 'CONFIG_HP_SBA_ROUTING',
              if_true: files('hp-sba-routing.c'))
system_ss.add(when: 'CONFIG_HP_ZX1_IOMMU',
              if_true: files('hp-zx1-iommu.c'))
system_ss.add(when: 'CONFIG_HP_ZX1_MIO',
              if_true: files('hp-zx1-mio-regs.c', 'hp-zx2-mio-regs.c',
                             'hp-zx1-mio.c'))
system_ss.add(when: 'CONFIG_HP_ZX1_IOA',
              if_true: files('hp-zx1-ioa-regs.c', 'hp-zx1-ioa.c'))

# HPPA devices
specific_ss.add(when: 'CONFIG_ASTRO', if_true: files('astro.c'))
pci_ss.add(when: 'CONFIG_DINO', if_true: files('dino.c'))

system_ss.add_all(when: 'CONFIG_PCI', if_true: pci_ss)

specific_ss.add(when: 'CONFIG_PCI_POWERNV', if_true: files(
  'pnv_phb3.c',
  'pnv_phb3_msi.c',
  'pnv_phb3_pbcq.c',
  'pnv_phb4.c',
  'pnv_phb4_pec.c',
  'pnv_phb.c',
))
