#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Derive firmware lifetime sections and virtual fixups from the linked ELF."""

import bisect
import collections
import pathlib
import re
import struct
import sys


EFI_ROOTS = '''rs_get_time rs_set_time rs_get_wakeup_time rs_set_wakeup_time
rs_get_variable rs_get_next_var_name rs_set_variable
rs_get_next_high_monotonic_count rs_reset_system rs_query_variable_info
fpswa_emulation_entry'''.split()
PHYSICAL_ROOTS = '''pal_proc_entry sal_proc_entry sal_proc_gp_anchor
start_secondary firmware_ap_main
fw_mca_entry fw_init_entry fw_debug_exception_entry
fw_debug_support_dispatch'''.split()
TABLE_ROOTS = '''mSystemTable mRuntimeServices mConfigTables mFirmwareVendor
mSalSystemTable mSmbiosEntryPoint mSmbiosTable mDebugImageInfoHeader
mDebugImageInfoTable mDebugImageInfoNormal mLoadedImageProto
mFpswaLoadedImageProto mFpswaProto mPlatformConsoleInputDevicePath
mPlatformConsoleOutputDevicePath mKeyboardConsoleDevicePath
mAcpiSerialDevicePath mMmioSerialDevicePath mPciSerialDevicePath
mPublishedOpticalSetupDevicePath'''.split()
# These initializers install data pointers that have no static relocations.
STATE_INITIALIZERS = '''efi_apply_platform_variable_profile
efi_init_system_table
efi_init_debug_image_info_table efi_init_loaded_image_proto
fw_platform_runtime_resources_init'''.split()
ABSOLUTE_RELOCS = {0x23, 0x27, 0x47}
SUPPORTED_RELOCS = {0, 0x23, 0x27, 0x2a, 0x2b, 0x47, 0x49, 0x4f, 0x52, 0x5f}


class Elf:
    def __init__(self, filename):
        self.data = pathlib.Path(filename).read_bytes()
        h = struct.unpack_from('<16sHHIQQQIHHHHHH', self.data)
        if h[0][:6] != b'\x7fELF\x02\x01' or h[2] != 50:
            raise ValueError('expected little-endian IA-64 ELF64')
        self.sections = []
        for i in range(h[12]):
            values = struct.unpack_from('<IIQQQQIIQQ', self.data,
                                        h[6] + i * h[11])
            self.sections.append(dict(zip(
                ('name_offset', 'type', 'flags', 'addr', 'offset', 'size',
                 'link', 'info', 'alignment', 'entsize'), values)))
        strings = self.content(self.sections[h[13]])
        for s in self.sections:
            s['name'] = self.string(strings, s['name_offset'])
        self.symbols = []
        for s in self.sections:
            if s['type'] != 2:
                continue
            strings = self.content(self.sections[s['link']])
            for off in range(s['offset'], s['offset'] + s['size'], 24):
                name, info, _, section, value, size = struct.unpack_from(
                    '<IBBHQQ', self.data, off)
                self.symbols.append(dict(name=self.string(strings, name),
                                         kind=info & 15, section=section,
                                         addr=value, size=size))
        self.relocs = []
        for s in self.sections:
            if s['type'] != 4:
                continue
            target = self.sections[s['info']]
            if not target['flags'] & 2:
                continue
            for off in range(s['offset'], s['offset'] + s['size'], 24):
                addr, info, addend = struct.unpack_from('<QQq', self.data, off)
                kind = info & 0xffffffff
                if kind not in SUPPORTED_RELOCS:
                    raise ValueError(f'unsupported relocation {kind:#x}')
                sym = self.symbols[info >> 32]
                if kind and sym['section'] == 0:
                    raise ValueError(f'unresolved symbol {sym["name"]}')
                self.relocs.append(dict(addr=addr, kind=kind, symbol=sym,
                                        target=sym['addr'] + addend))

    def content(self, section):
        return self.data[section['offset']:section['offset'] + section['size']]

    @staticmethod
    def string(data, offset):
        return data[offset:data.index(0, offset)].decode()

    def symbol(self, name):
        return next((s for s in self.symbols if s['name'] == name), None)

    def section(self, name):
        return next(s for s in self.sections if s['name'] == name)


class Layout:
    def __init__(self, elf, mapfile):
        self.elf = elf
        self.nodes = []
        pending = None
        for line in pathlib.Path(mapfile).read_text().splitlines():
            match = re.match(r'^ (\.[^\s]+)(?:\s+(0x[0-9a-f]+)\s+'
                             r'(0x[0-9a-f]+)\s+(.+))?$', line)
            if match:
                if not match[2]:
                    pending = match[1]
                    continue
                name, addr, size, owner = match.groups()
                pending = None
            else:
                match = re.match(r'^\s+(0x[0-9a-f]+)\s+'
                                 r'(0x[0-9a-f]+)\s+(.+)$', line)
                if not pending or not match:
                    pending = None
                    continue
                name = pending
                addr, size, owner = match.groups()
                pending = None
            addr, size = int(addr, 16), int(size, 16)
            if size and any(s['flags'] & 2 and s['addr'] <= addr and
                            addr + size <= s['addr'] + s['size']
                            for s in elf.sections):
                self.nodes.append(dict(name=name, addr=addr, size=size,
                                       owner=owner, kind='input'))
        opd = elf.section('.opd')
        got = elf.section('.got')
        for offset in range(0, opd['size'], 16):
            self.nodes.append(dict(name='.opd', addr=opd['addr'] + offset,
                                   size=16, kind='opd'))
        for offset in range(0, got['size'], 8):
            self.nodes.append(dict(name='.got', addr=got['addr'] + offset,
                                   size=8, kind='got'))
        self.nodes.sort(key=lambda n: n['addr'])
        self.bases = [n['addr'] for n in self.nodes]
        self.graph = collections.defaultdict(set)
        self.opd_by_entry = {}
        self.got_by_opd = {}
        for offset in range(0, opd['size'], 16):
            addr = opd['addr'] + offset
            entry, gp = struct.unpack_from('<QQ', elf.content(opd), offset)
            if gp != elf.symbol('__gp')['addr']:
                raise ValueError('unexpected function descriptor GP')
            self.opd_by_entry[entry] = self.node(addr)
            self.edge(addr, entry)
        for offset in range(0, got['size'], 8):
            addr = got['addr'] + offset
            value, = struct.unpack_from('<Q', elf.content(got), offset)
            if not opd['addr'] <= value < opd['addr'] + opd['size']:
                raise ValueError('unexpected firmware GOT entry')
            self.got_by_opd[self.node(value)] = self.node(addr)
            self.edge(addr, value)
        self.unclassified = []
        for reloc in elf.relocs:
            if (reloc['kind'] == 0 or
                    reloc['symbol']['name'] in ('__gp', '_end') or
                    reloc['symbol']['name'].startswith(
                        ('__firmware_', '__runtime_', '__boot_',
                         '__virtual_fixups_'))):
                continue
            src = self.node(reloc['addr'])
            dest = self.node(reloc['target'])
            if reloc['kind'] == 0x47:
                dest = self.opd_by_entry.get(reloc['target'])
            elif reloc['kind'] == 0x52:
                dest = self.got_by_opd.get(
                    self.opd_by_entry.get(reloc['target']))
            if src is None or dest is None:
                self.unclassified.append((src, reloc))
            else:
                self.graph[src].add(dest)
        self.efi = self.closure(self.roots(EFI_ROOTS))
        self.physical = self.closure(self.roots(PHYSICAL_ROOTS))
        state = self.roots(TABLE_ROOTS +
                           [s['name'] for s in elf.symbols
                            if s['name'].startswith('fw_vendor.')] +
                           ['rs_set_virtual_address_map',
                                         'rs_convert_pointer'])
        for name in STATE_INITIALIZERS:
            sym = elf.symbol(name)
            if sym:
                state.update(n for n in self.graph[self.node(sym['addr'])]
                             if not self.nodes[n]['name'].startswith('.text'))
        self.retained = self.closure(self.efi | self.physical | state)
        for src, reloc in self.unclassified:
            if src in self.retained and reloc['symbol']['name'] not in (
                    '__firmware_start', '__firmware_payload_end',
                    '__runtime_code_start', '__runtime_data_start',
                    '__runtime_end', '__boot_start', '__boot_end', '_end',
                    '__virtual_fixups_start', '__virtual_fixups_end'):
                raise ValueError(f'unclassified retained reference: {reloc}')
        for reloc in elf.relocs:
            if (reloc['kind'] in ABSOLUTE_RELOCS and
                    self.node(reloc['addr']) in self.efi & self.physical):
                raise ValueError(
                    'absolute pointer shared by EFI and physical code: '
                    f'{reloc}')

    def node(self, address):
        index = bisect.bisect_right(self.bases, address) - 1
        if (index >= 0 and
                address < self.bases[index] + self.nodes[index]['size']):
            return index
        return None

    def edge(self, source, target):
        src, dst = self.node(source), self.node(target)
        if src is None or dst is None:
            raise ValueError(f'unclassified generated reference {source:#x}')
        self.graph[src].add(dst)

    def roots(self, names):
        result = set()
        for name in names:
            sym = self.elf.symbol(name)
            if sym:
                node = self.node(sym['addr'])
                if node is not None:
                    result.add(node)
                if sym['addr'] in self.opd_by_entry:
                    result.add(self.opd_by_entry[sym['addr']])
        return result

    def closure(self, roots):
        seen = set()
        todo = list(roots)
        while todo:
            node = todo.pop()
            if node not in seen:
                seen.add(node)
                todo.extend(self.graph[node] - seen)
        return seen

    def selectors(self, prefixes, physical=False):
        domain = self.physical - self.efi
        selected = domain if physical else self.retained - domain
        names = sorted({self.nodes[n]['name'] for n in selected
                        if self.nodes[n]['kind'] == 'input' and
                        self.nodes[n]['name'].startswith(prefixes)})
        return '\n'.join(f'        *({name})' for name in names)

    def linker_script(self, template):
        text = pathlib.Path(template).read_text()
        for kind, prefixes in [('TEXT', ('.text',)),
                               ('RODATA', ('.rodata',)),
                               ('DATA', ('.data', '.sdata')),
                               ('BSS', ('.bss', '.sbss'))]:
            text = text.replace('/* RETAIN_' + kind + ' */',
                                self.selectors(prefixes))
            text = text.replace('/* PHYSICAL_' + kind + ' */',
                                self.selectors(prefixes, physical=True))
        return text

    def fixups(self):
        # Mutable table fields are converted from their current values in C.
        result = []
        for reloc in self.elf.relocs:
            node = self.node(reloc['addr'])
            if node not in self.efi or reloc['kind'] not in ABSOLUTE_RELOCS:
                continue
            name = self.nodes[node]['name']
            if name.startswith('.rodata'):
                result.append((reloc['addr'], reloc['kind']))
            elif name.startswith('.text'):
                raise ValueError(
                    'EFI code contains an absolute instruction address')
        for node in self.efi:
            if self.nodes[node]['kind'] == 'opd':
                result.extend((self.nodes[node]['addr'] + offset, 0x27)
                              for offset in (0, 8))
            if self.nodes[node]['kind'] == 'got':
                result.append((self.nodes[node]['addr'], 0x27))
        return sorted(set(result))


def main():
    mode, elf, mapfile, output = sys.argv[1:5]
    layout = Layout(Elf(elf), mapfile)
    if mode == 'linker':
        pathlib.Path(output).write_text(layout.linker_script(sys.argv[5]))
    elif mode == 'fixups':
        base = layout.elf.symbol('__firmware_start')['addr']
        start = layout.elf.symbol('__virtual_fixups_start')['addr']
        end = layout.elf.symbol('__virtual_fixups_end')['addr']
        runtime_end = layout.elf.symbol('__runtime_end')['addr']
        for n in layout.retained:
            node = layout.nodes[n]
            if node['addr'] + node['size'] > runtime_end:
                raise ValueError(
                    f'retained reference reaches boot section: {node}')
        fixups = layout.fixups()
        data = struct.pack('<II', 1, len(fixups))
        data += b''.join(struct.pack('<II', addr - base, kind)
                         for addr, kind in fixups)
        if len(data) > end - start:
            raise ValueError('virtual fixup table capacity exceeded')
        pathlib.Path(output).write_bytes(data.ljust(end - start, b'\0'))
    else:
        raise ValueError('unknown mode')


if __name__ == '__main__':
    main()
