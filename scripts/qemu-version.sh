#!/bin/sh

set -eu

dir="$1"
source_commit=$(sh "$dir/scripts/git-source-commit.sh" "$dir")
pkgversion="$2"
version="$3"

if [ -z "$pkgversion" ]; then
    cd "$dir"
    if [ -e .git ]; then
        pkgversion=$(git describe --match 'v*' --dirty) || :
    fi
fi

if [ -n "$pkgversion" ]; then
    fullversion="$version ($pkgversion)"
else
    fullversion="$version"
fi

cat <<EOF
#define QEMU_BUILD_COMMIT "$source_commit"
#define QEMU_PKGVERSION "$pkgversion"
#define QEMU_FULL_VERSION "$fullversion"
EOF
