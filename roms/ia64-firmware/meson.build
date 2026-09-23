# SPDX-License-Identifier: GPL-2.0-or-later
# IA-64 firmware build rules

sh_prog = find_program('sh')
build_fw_script = files('build_firmware.sh')
firmware_manifest = 'firmware.sources'
firmware_inputs = fs.read(firmware_manifest).strip().split('\n')

ia64_firmware_bin = custom_target(
  'ia64-firmware.bin',
  output: [
    'ia64-firmware.bin',
    'ia64-firmware.elf',
    'ia64-firmware.map',
    'ia64-firmware.sections',
  ],
  input: files(firmware_inputs + [firmware_manifest, 'firmware_layout.py']),
  command: [
    sh_prog,
    build_fw_script,
    '@OUTPUT0@',
    '@OUTPUT1@',
    '@OUTPUT2@',
    '@OUTPUT3@',
    meson.current_source_dir(),
    '@DEPFILE@',
  ],
  depfile: 'ia64-firmware.d',
  env: {'PYTHON': python.full_path()},
  build_by_default: true,
  install: get_option('install_blobs'),
  install_dir: [qemu_datadir, false, false, false],
)
