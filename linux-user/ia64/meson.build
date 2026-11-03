# SPDX-License-Identifier: GPL-2.0-or-later
# Linux numbers the IA-64 system calls from 1024 (__NR_Linux).
syscall_nr_generators += {
  'ia64': generator(sh,
                    arguments: [ meson.current_source_dir() / 'syscallhdr.sh', '@INPUT@', '@OUTPUT@', '@EXTRA_ARGS@',
                                 '', '1024' ],
                    output: '@BASENAME@_nr.h')
}
