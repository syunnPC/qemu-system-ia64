#!/bin/sh
# SPDX-License-Identifier: GPL-2.0-or-later
#
# Install the IA-64 Linux cross toolchain that tests/tcg/ia64 builds with:
#
#   scripts/ia64-user-toolchain.sh DIR
#   ./configure --target-list=ia64-linux-user \
#       --cross-cc-ia64=DIR/bin/ia64-linux-glibc-gcc
#   make && make check-tcg
#
# The compiler is kernel.org's crosstool GCC 13.2.0, which has no C
# library.  The C library, the kernel headers and libunwind come from the
# EPIC Slackware IA-64 tree.  Every download is checked against the SHA-256
# below; a package the tree has replaced fails here, and its new name and
# checksum go in this list.
#
# DIR/bin/ia64-linux-glibc-gcc compiles programs against DIR/sysroot; a
# dynamically linked one runs with "qemu-ia64 -L DIR/sysroot".
# DIR/bin/ia64-linux-gnu-* are the plain tools, which also build
# roms/ia64-firmware.

set -eu

if [ $# -ne 1 ]; then
    echo "usage: $0 DIR" >&2
    exit 2
fi

mkdir -p "$1"
dir=$(cd "$1" && pwd)
downloads=$dir/downloads
sysroot=$dir/sysroot
mkdir -p "$downloads" "$dir/bin"

crosstool=https://www.kernel.org/pub/tools/crosstool/files/bin/x86_64/13.2.0
epic=https://ftp.machine-hall.org/pub/slackware/epic-slack-current/slackware64

gcc_tar=x86_64-gcc-13.2.0-nolibc-ia64-linux.tar.xz
gcc_sha=7611004606e79185247babeb7b1fbb9387068ca67457e109533298d26d1b8d12

glibc=glibc-2.42-ia64-2.txz
glibc_sha=30258f148dad4d5f8d5be0d7644ddf7fa05a8b212b396546f7e5b0aadfcbdfa7
headers=kernel-headers-6.6.30-ia64-1.txz
headers_sha=59376ffa0046bd82158021fe363922079d1732bdc8400d98b32c2d0f2763ca1d
gccpkg=gcc-15.3.0-ia64-1.txz
gccpkg_sha=3c5a80ce8cb96512c3d8e59e521077d8240b3abb3c3d5297afea6869a9c66630

fetch() {
    url=$1
    file=$downloads/$2
    sha=$3

    if [ -f "$file" ] && echo "$sha  $file" | sha256sum -c --status; then
        return
    fi
    echo "fetching $url"
    curl -fL --retry 3 -o "$file.part" "$url"
    if ! echo "$sha  $file.part" | sha256sum -c --status; then
        echo "$2: SHA-256 is $(sha256sum "$file.part" | cut -d' ' -f1)," \
             "expected $sha" >&2
        exit 1
    fi
    mv "$file.part" "$file"
}

fetch "$crosstool/$gcc_tar" "$gcc_tar" "$gcc_sha"
fetch "$epic/l/$glibc" "$glibc" "$glibc_sha"
fetch "$epic/d/$headers" "$headers" "$headers_sha"
fetch "$epic/d/$gccpkg" "$gccpkg" "$gccpkg_sha"

rm -rf "$dir/gcc" "$sysroot"
mkdir -p "$dir/gcc" "$sysroot"
tar -xJf "$downloads/$gcc_tar" -C "$dir/gcc" --strip-components=2

# The glibc package keeps its shared objects in lib/incoming until its
# install script runs; the links below are what that script and ldconfig
# would make.
tar -xJf "$downloads/$glibc" -C "$sysroot" \
    --exclude='install/*' --exclude='usr/doc/*' --exclude='usr/man/*' \
    --exclude='usr/info/*' --exclude='usr/share/*'
tar -xJf "$downloads/$headers" -C "$sysroot" \
    --exclude='install/*'
# libunwind provides the unwinder that GCC's libgcc.a calls into.
tar -xJf "$downloads/$gccpkg" -C "$sysroot" \
    usr/lib/libunwind.a usr/lib/libunwind.so.7 usr/lib/libgcc_s.so.1
rmdir "$sysroot/install" 2>/dev/null || true
mv "$sysroot"/lib/incoming/* "$sysroot/lib/"
rmdir "$sysroot/lib/incoming"
readelf=$dir/gcc/bin/ia64-linux-readelf
for lib in "$sysroot"/lib/*.so; do
    soname=$("$readelf" -d "$lib" | sed -n 's/.*(SONAME).*\[\(.*\)\]/\1/p')
    if [ -n "$soname" ] && [ "$soname" != "$(basename "$lib")" ]; then
        ln -sfn "$(basename "$lib")" "$sysroot/lib/$soname"
    fi
done
ln -sfn asm-ia64 "$sysroot/usr/include/asm"

cat > "$dir/bin/ia64-linux-glibc-gcc" <<EOF
#!/bin/sh
# GCC 13.2.0 for IA-64 against $sysroot
# (written by scripts/ia64-user-toolchain.sh).
link=yes
for arg in "\$@"; do
    case \$arg in
    -c|-S|-E|-M|-MM) link=no ;;
    esac
done
set -- --sysroot="$sysroot" -isystem "$sysroot/usr/include" \\
    -B"$sysroot/usr/lib" -L"$sysroot/usr/lib" "\$@"
if [ \$link = yes ]; then
    # A static libc reaches the unwinder through libgcc's libunwind calls.
    set -- "\$@" -Wl,--start-group -lc -lunwind -Wl,--end-group
fi
exec "$dir/gcc/bin/ia64-linux-gcc" "\$@"
EOF
chmod +x "$dir/bin/ia64-linux-glibc-gcc"
for tool in ar as gcc ld nm objcopy objdump ranlib readelf size strip; do
    ln -sfn "../gcc/bin/ia64-linux-$tool" "$dir/bin/ia64-linux-gnu-$tool"
done

echo "IA-64 toolchain ready: $dir/bin/ia64-linux-glibc-gcc"
