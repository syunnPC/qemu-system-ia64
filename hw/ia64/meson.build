ia64_ss = ss.source_set()
ia64_ss.add(files('hp_ia64.c', 'ia64_common.c', 'ia64_loader.c'))
ia64_ss.add(files('ia64_ras.c'))
ia64_ss.add(when: 'CONFIG_ATI_VGA', if_true: files('hp_int10.c'))
ia64_ss.add(files('ia64_platform.c'))
ia64_ss.add(files('ia64_platform_rom.c'))
ia64_ss.add(files('ia64_platform_device.c'))
ia64_ss.add(when: 'CONFIG_IA64_I2000_460GX_TEST',
            if_true: files('ia64_i2000_460gx_test_layout.c'))
ia64_ss.add(when: 'CONFIG_IA64_I2000_460GX_TEST',
            if_true: files('ia64_i2000_460gx_test.c'))
ia64_ss.add(when: 'CONFIG_IA64_I2000_IO_TEST_LAYOUT',
            if_true: files('ia64_i2000_io_test_layout.c'))
ia64_ss.add(when: 'CONFIG_IA64_I2000_IO_TEST',
            if_true: files('ia64_i2000_io_test.c',
                           'ia64_i2000_rtc_bank1.c'))
ia64_ss.add(when: 'CONFIG_IA64_I2000_EFI_TEST',
            if_true: files('ia64_i2000_efi_test.c'))
ia64_ss.add(when: 'CONFIG_IA64_ISP12160_TEST',
            if_true: files('ia64_isp12160_test.c'))
ia64_ss.add(when: 'CONFIG_IA64_ZX6000_ZX1_TEST',
            if_true: files('ia64_zx6000_zx1_test_layout.c',
                           'ia64_zx6000_zx1_test.c'))
ia64_ss.add(when: 'CONFIG_IA64_ZX6000_EFI_TEST',
            if_true: files('ia64_zx6000_efi_test.c'))
ia64_ss.add(when: 'CONFIG_HP_ZX6000_PDH',
            if_true: files('hp_zx6000_pdh.c'))
ia64_ss.add(when: 'CONFIG_HP_ZX6000', if_true: files('hp_zx6000.c'))
ia64_ss.add(when: 'CONFIG_IA64_460GX_PID',
            if_true: files('intel_460gx_pid.c'))
ia64_ss.add(when: 'CONFIG_IA64_460GX_HOST',
            if_true: files('intel_460gx_host.c',
                           'intel_460gx_host_cfg.c',
                           'intel_460gx_root.c',
                           'intel_460gx_dma.c'))
ia64_ss.add(when: 'CONFIG_IA64_460GX_CHIPSET',
            if_true: files('intel_460gx_chipset.c',
                           'intel_460gx_ihpc.c'))
ia64_ss.add(when: 'CONFIG_HP_I2000', if_true: files('hp_i2000.c'))
ia64_ss.add(when: 'CONFIG_IA64_460GX_DMA_TEST',
            if_true: files('intel_460gx_dma_test.c'))
ia64_ss.add(when: 'CONFIG_IA64_VPC', if_true: files(
  'ia64_vpc.c',
  'ia64_pci.c',
  'ia64_iosapic.c',
))
hw_arch += {'ia64': ia64_ss}
