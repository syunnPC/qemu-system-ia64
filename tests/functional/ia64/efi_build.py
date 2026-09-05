"""Build paths for IA-64 EFI test applications."""

# SPDX-License-Identifier: GPL-2.0-or-later

import os
from pathlib import Path
import shutil
import subprocess


APP_NAMES = ("smoke", "services", "tables", "exitbs", "storage", "input",
             "graphics", "smp", "smp-merced", "pci-bridge",
             "i2000-pci-root", "i2000-runtime", "i2000-loader",
             "start-image-child", "ras")
SOURCE_DIR = Path(__file__).resolve().parent
APPS_SOURCE_DIR = SOURCE_DIR / "apps"


def build_root() -> Path:
    configured = os.environ.get("MESON_BUILD_ROOT")
    if configured:
        return Path(configured).resolve()
    return SOURCE_DIR.parents[2] / "build"


def firmware_path() -> Path:
    override = os.environ.get("IA64_TEST_FIRMWARE")
    if override:
        path = Path(override).resolve()
    else:
        path = build_root() / "roms" / "ia64-firmware" / \
            "ia64-firmware.bin"
    if not path.is_file():
        raise FileNotFoundError(f"IA-64 firmware was not built: {path}")
    return path


def app_path(name: str) -> Path:
    if name not in APP_NAMES:
        raise ValueError(f"unknown IA-64 EFI test app: {name}")
    override = os.environ.get("IA64_TEST_APP_DIR")
    directory = (Path(override).resolve() if override else
                 build_root() / "tests" / "functional" / "ia64")
    path = directory / f"{name}-app.efi"
    if not path.is_file():
        raise FileNotFoundError(
            f"IA-64 EFI test app was not built: {path}; "
            "build the ia64-functional-apps target")
    return path


def toolchain_available() -> bool:
    return all(shutil.which(tool) for tool in (
        "ia64-linux-gnu-gcc", "ia64-linux-gnu-ld",
        "ia64-linux-gnu-objcopy"))


def build_app(name: str, output: str | Path) -> Path:
    """Build one app explicitly; functional tests use Meson's app targets."""
    if name not in APP_NAMES:
        raise ValueError(f"unknown IA-64 EFI test app: {name}")
    if not toolchain_available():
        raise RuntimeError("IA-64 cross compiler is not available")
    output = Path(output).resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    linker_script = ("i2000-loader-app.lds" if name == "i2000-loader"
                     else "ia64-test-app.lds")
    subprocess.run([
        str(APPS_SOURCE_DIR / "build-test-app.sh"), str(output),
        str(APPS_SOURCE_DIR / f"{name}-app.c"),
        str(APPS_SOURCE_DIR / linker_script),
    ], check=True)
    return output
