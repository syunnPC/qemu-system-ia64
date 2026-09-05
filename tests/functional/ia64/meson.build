# SPDX-License-Identifier: GPL-2.0-or-later

if 'ia64-softmmu' not in target_dirs
  subdir_done()
endif

ia64_app_builder = files('apps/build-test-app.sh')
ia64_app_linker_script = files('apps/ia64-test-app.lds')
ia64_app_header = files('apps/ia64-test.h')
ia64_services_common = files('apps/common/services.c')
ia64_services_header = files('apps/common/services.h')
ia64_app_sh = find_program('sh')
ia64_functional_test_apps = []

foreach app : [
  'smoke', 'services', 'tables', 'exitbs', 'storage', 'input', 'graphics',
  'smp', 'smp-merced', 'pci-bridge', 'i2000-pci-root', 'i2000-runtime',
  'i2000-loader', 'ras',
]
  app_source = files('apps/' + app + '-app.c')
  app_linker_script = ia64_app_linker_script
  if app == 'i2000-loader'
    app_linker_script = files('apps/i2000-loader-app.lds')
  endif
  app_depend_files = [
    ia64_app_builder,
    app_linker_script,
    ia64_app_header,
  ]
  app_command = [
    ia64_app_sh,
    ia64_app_builder,
    '@OUTPUT@',
    '@INPUT@',
    app_linker_script,
  ]
  if app == 'services' or app == 'tables'
    app_depend_files += [ia64_services_common, ia64_services_header]
    app_command += ['0x4000000', ia64_services_common]
  endif
  app_target = custom_target(
    'ia64-functional-' + app + '-app',
    output: app + '-app.efi',
    input: app_source,
    depend_files: app_depend_files,
    command: app_command,
    build_by_default: false,
  )
  ia64_functional_test_apps += app_target
endforeach

ia64_child_linker_script = files('apps/ia64-test-child.lds')
ia64_start_image_child = custom_target(
  'ia64-functional-start-image-child-app',
  output: 'start-image-child-app.efi',
  input: files('apps/start-image-child-app.c'),
  depend_files: [
    ia64_app_builder,
    ia64_child_linker_script,
    ia64_app_header,
  ],
  command: [
    ia64_app_sh,
    ia64_app_builder,
    '@OUTPUT@',
    '@INPUT@',
    ia64_child_linker_script,
    # Keep this fixed-address child immediately above the early loader window.
    # It verifies that an image with stripped relocations may still request an
    # exact address below the automatic allocation floor.
    '0x5002000',
  ],
  build_by_default: false,
)
ia64_functional_test_apps += ia64_start_image_child

alias_target('ia64-functional-apps', ia64_functional_test_apps)

# tests/functional/meson.build adds this target-specific list to the common
# functional-test dependencies.  Keep the applications out of the default
# build, but ensure that both firmware and applications exist before any
# IA-64 functional test starts.
test_ia64_system_deps = ia64_functional_test_apps + [ia64_firmware_bin]

tests_ia64_system_quick = [
  'host',
  'smoke',
  'shell',
  'services',
  'tables',
  'smp',
  'smp_merced',
  'ras',
] + (config_all_devices.has_key('CONFIG_IA64_ZX6000_EFI_TEST') ?
     ['zx6000_efi'] : []) + \
    (config_all_devices.has_key('CONFIG_HP_I2000') ?
     ['hp_i2000'] : []) + \
    (config_all_devices.has_key('CONFIG_HP_ZX6000') ?
     ['hp_zx6000'] : []) + \
    (config_all_devices.has_key('CONFIG_HP_RX2660') ?
     ['hp_rx2660'] : [])

tests_ia64_system_thorough = [
  'storage',
  'input',
]

test_ia64_timeouts = {
  'host': 30,
  'smoke': 120,
  'shell': 180,
  'services': 90,
  'tables': 90,
  'smp': 120,
  'smp_merced': 900,
  'ras': 120,
  'zx6000_efi': 90,
  'hp_i2000': 120,
  'hp_zx6000': 120,
  'hp_rx2660': 120,
  'storage': 300,
  'input': 120,
}
