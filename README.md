# qemu-system-ia64

Full-system QEMU emulation for IA-64 guests.

> [!IMPORTANT]
> This fork is independent of upstream QEMU. Report issues for this fork here,
> not upstream.

## Quick start

### Prebuilt binaries

Download builds from
[GitHub Actions](https://github.com/syunnPC/qemu-system-ia64/actions/workflows/build.yml).
Builds are available for Windows AMD64 and Linux AMD64/AArch64.
[GitHub Releases](https://github.com/syunnPC/qemu-system-ia64/releases)
may be older than the latest Actions builds.

> [!WARNING]
> `x86-64-v2-optimized` AMD64 builds require an x86-64-v2 host, use aggressive
> optimizations, and disable debugging and hardening. Use a standard build if a
> guest is unstable.

### Build from source

Firmware builds require an IA-64 ELF cross toolchain with
`ia64-linux-gnu-` tools on `PATH`.

```sh
./configure --target-list=ia64-softmmu --enable-gtk
ninja -C build qemu-system-ia64 roms/ia64-firmware/ia64-firmware.bin
```

### Run

```sh
./build/qemu-system-ia64 \
  -machine itanium2-vpc \
  -drive file=/path/to/guest-media.iso,media=cdrom,format=raw,readonly=on \
  -display gtk
```

Use the `nvram=<path>` machine property for an NVRAM file, or `nvram=none` for
non-persistent EFI variables.

## Machine profiles

Select a machine explicitly with `-machine`.

| Machine | Default CPU | Max sockets | Max total cores / threads | Default VGA | Default RAM |
| --- | --- | ---: | ---: | --- | ---: |
| `hp-i2000` | `merced-800` | 2 | 2 | NVIDIA Quadro2 Pro | 2 GiB |
| `hp-zx6000` | `madison-1500` | 2 | 2 | ATI Radeon RV100 | 2 GiB |
| `hp-rx2660` | `montecito-9010` | 2 | 9010/9110n: 2 / 2; others: 4 / 8 | ATI RN50 | 8 GiB |
| `itanium2-vpc` | `montecito-9050` | 64 | 64 | ATI Rage 128 Pro | 2 GiB |
| `itanium-vpc` | `merced-800` | 64 | 64 | ATI Rage 128 Pro | 2 GiB |

For machine-specific CPU restrictions, see the
[IA-64 system emulator documentation](docs/system/target-ia64.rst).

Use `-vga quadro2` for NVIDIA Quadro2 Pro or `-vga ati` for an ATI adapter.
ATI models are `rage128p`, `rv100`, and `es1000`:

```sh
-vga ati -global ati-vga.model=es1000
```

Configure the preferred resolution and VRAM size (MiB) with:

```sh
# ATI
-vga ati \
  -global ati-vga.model=es1000 \
  -global ati-vga.xres=1920 \
  -global ati-vga.yres=1080 \
  -global ati-vga.vgamem_mb=32

# NVIDIA Quadro2 Pro
-vga quadro2 \
  -global nvidia-quadro2.xres=1920 \
  -global nvidia-quadro2.yres=1080 \
  -global nvidia-quadro2.vgamem_mb=32
```

Specify `xres` and `yres` together. `xres` must be a multiple of 8.

## CPU models

Select a model with `-cpu <model>`. List available CPU names and aliases with:

```sh
build/qemu-system-ia64 -cpu help
```

CPU selection does not set the topology. Configure it separately:

```sh
-smp cpus=N,sockets=S,cores=C,threads=T
```

For a fully populated topology, `N` is `S * C * T`; omit `cpus` to calculate it
from the other values. Use `-accel tcg,thread=multi` for more than one vCPU.

## Common options

| Purpose | Option |
| --- | --- |
| User-mode networking | `-nic user,model=e1000` |
| Disable networking | `-nic none` |
| Serial console | `-serial stdio` |
| AHCI HDD | `-drive ...,if=none,id=<id> -device ide-hd,drive=<id>,bus=ide.<number>` |
| AHCI CD | `-drive ...,media=cdrom,readonly=on,if=none,id=<id> -device ide-cd,drive=<id>,bus=ide.<number>` |

## Status

Instruction emulation, privileged behavior, floating-point handling,
and device support remain experimental.

## Related projects

- [IA-64 ATI XPDM driver](https://github.com/syunnPC/qemu-system-ia64-ati-xpdm)
- [IA-64 NVIDIA XPDM driver](https://github.com/syunnPC/qemu-system-ia64-nv-xpdm)

## Screenshots

<table align="center">
  <tr>
    <td width="50%">
      <img
        width="100%"
        alt="Microsoft Windows Server 2008 R2"
        src="https://github.com/user-attachments/assets/ff3563ff-7fb2-4245-bc42-6ec86ed51ce6"
      />
    </td>
    <td width="50%">
      <img
        width="100%"
        alt="Microsoft Windows Codename Longhorn build 4051"
        src="https://github.com/user-attachments/assets/bab22228-ff1e-421b-bc79-c314850c2cab"
      />
    </td>
  </tr>
  <tr>
    <td width="50%">
      <img
        width="100%"
        alt="Microsoft Windows Whistler Advanced Server 64-bit Edition"
        src="https://github.com/user-attachments/assets/3c2bf20f-51eb-4ef3-b560-7dc75a01f6ac"
      />
    </td>
    <td width="50%">
      <img
        width="100%"
        alt="Debian 7.11.0 with GUI"
        src="https://github.com/user-attachments/assets/d1e5cdaa-64d6-4f91-9215-277423e268a2"
      />
    </td>
  </tr>
  <tr>
    <td width="50%">
      <img
        width="100%"
        alt="HP-UX 11i v3"
        src="https://github.com/user-attachments/assets/a6df474a-ea5e-4cff-b5e1-d8ac91958a6e"
      />
    </td>
    <td width="100%">
     <img
       width="100%"
       alt="OpenVMS V8.4 IA-64"
       src="https://github.com/user-attachments/assets/72834d75-a537-447f-b0a1-ada4fee4a283" 
     />
    </td>
  </tr>
</table>

## Legal disclaimer

Proprietary guest operating-system images, installation media, and firmware
are not included. Users must supply them under the applicable licenses.

This project is independent and is not affiliated with, endorsed by, sponsored
by, or supported by Intel, HPE, the QEMU Project, or Microsoft Corporation.

QEMU is licensed under the GNU General Public License, version 2; see the
license files in this repository. Microsoft and Windows are trademarks of the
Microsoft group of companies. All other product and company names and
trademarks are the property of their respective owners.
