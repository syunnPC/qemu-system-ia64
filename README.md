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

| Machine | CPU models | Max sockets | Max total cores / threads | Default VGA | Default RAM |
| --- | --- | ---: | ---: | --- | ---: |
| `hp-i2000` | `merced` | 2 | 2 | NVIDIA Quadro2 Pro | 2 GiB |
| `hp-zx6000` | `madison-zx6000` | 2 | 2 | ATI Radeon RV100 | 2 GiB |
| `hp-rx2660` | Selected Montecito/Montvale models (see below) | 2 | 9010/9110n: 2 / 2; others: 4 / 8 | ATI RN50 | 8 GiB |
| `itanium2-vpc` | Any model below; `montecito` by default | 64 | 64 | ATI Rage 128 Pro | 2 GiB |
| `itanium-vpc` | Any model below; `merced` by default | 64 | 64 | ATI Rage 128 Pro | 2 GiB |

`hp-rx2660` accepts `montecito-9010` (default), `montecito-9020`,
`montecito-9040`, `montvale-9110n`, `montvale-9120n`, and `montvale-9140m`.

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

Select a model with `-cpu <model>`. Use `-cpu help` to list available models.

| Model | Generation | Cores/socket | Threads/core | Clock | L3/socket |
| --- | --- | ---: | ---: | ---: | ---: |
| `merced` | Merced | 1 | 1 | 800 MHz | 4 MiB |
| `mckinley`, `mckinley-1000` | McKinley | 1 | 1 | 1.0 GHz | 3 MiB |
| `mckinley-900` | McKinley | 1 | 1 | 900 MHz | 1.5 MiB |
| `deerfield` | Madison/Deerfield | 1 | 1 | 1.0 GHz | 1.5 MiB |
| `madison` | Madison | 1 | 1 | 1.6 GHz | 3 MiB |
| `madison-1.5m` | Madison | 1 | 1 | 1.4 GHz | 1.5 MiB |
| `madison-3m` | Madison | 1 | 1 | 1.6 GHz | 3 MiB |
| `madison-4m` | Madison | 1 | 1 | 1.4 GHz | 4 MiB |
| `madison-6m` | Madison | 1 | 1 | 1.5 GHz | 6 MiB |
| `madison-9m` | Madison 9M | 1 | 1 | 1.6 GHz | 9 MiB |
| `madison-zx6000` | Madison | 1 | 1 | 1.5 GHz | 6 MiB |
| `montecito` | Montecito | 2 | 2 | 1.6 GHz | 24 MiB (12 MiB/core) |
| `montecito-9010` | Montecito | 1 | 1 | 1.6 GHz | 6 MiB |
| `montecito-9015` | Montecito | 2 | 1-2 | 1.4 GHz | 12 MiB (6 MiB/core) |
| `montecito-9020` | Montecito | 2 | 1-2 | 1.42 GHz | 12 MiB (6 MiB/core) |
| `montecito-9030` | Montecito | 2 | 1-2 | 1.6 GHz | 8 MiB (4 MiB/core) |
| `montecito-9040` | Montecito | 2 | 1-2 | 1.6 GHz | 18 MiB (9 MiB/core) |
| `montecito-9050` | Montecito | 2 | 1-2 | 1.6 GHz | 24 MiB (12 MiB/core) |
| `montvale`, `montvale-9150n` | Montvale | 2 | 1-2 | 1.6 GHz | 24 MiB (12 MiB/core) |
| `montvale-9110n` | Montvale | 1 | 1 | 1.6 GHz | 12 MiB |
| `montvale-9120n` | Montvale | 2 | 1-2 | 1.42 GHz | 12 MiB (6 MiB/core) |
| `montvale-9130m` | Montvale | 2 | 1 | 1.666 GHz | 8 MiB (4 MiB/core) |
| `montvale-9140m` | Montvale | 2 | 1-2 | 1.666 GHz | 18 MiB (9 MiB/core) |
| `montvale-9140n` | Montvale | 2 | 1-2 | 1.6 GHz | 18 MiB (9 MiB/core) |
| `montvale-9150m`, `montvale-9152m` | Montvale | 2 | 1-2 | 1.666 GHz | 24 MiB (12 MiB/core) |

Reported processor clocks and cache sizes do not set the host execution rate.
The interval timer counter advances at the CPU model's configured ITC frequency.

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
