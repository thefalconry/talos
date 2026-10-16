#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
#
# Build a Debian/Ubuntu .deb package into dist/.
#
#   packaging/debian/build-deb.sh [build-dir]
#
set -euo pipefail

root=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
version=$(sed -n 's/^ *VERSION \([0-9.]*\).*/\1/p' "$root/CMakeLists.txt" | head -1)
arch="amd64"
build_dir="${1:-${TALOS_BUILD_DIR:-$root/build}}"

if [[ ! -f "$build_dir/talos" && ! -f "$build_dir/bin/talos" ]]; then
    echo "Talos executable not found in $build_dir. Building..."
    cmake -S "$root" -B "$build_dir" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DTALOS_BUILD_TESTS=OFF
    cmake --build "$build_dir" --target talos
fi

stage=$(mktemp -d)
trap 'rm -rf "$stage"' EXIT

# Install using CMake
cmake --install "$build_dir" --prefix "$stage/usr"

# Create DEBIAN/control
mkdir -p "$stage/DEBIAN"
cat > "$stage/DEBIAN/control" <<EOF
Package: talos
Version: ${version}
Section: net
Priority: optional
Architecture: ${arch}
Maintainer: The Falconry <support@thefalconry.org>
Depends: libc6 (>= 2.34), libstdc++6 (>= 11)
Recommends: qt6-qpa-plugins, qt6-declarative
Description: Desktop client for the Talon agent
 Talos is a client for Talon, the multi-platform agentic AI harness. It talks
 to a Talon daemon running the native bridge, on this computer or over the
 network, including through a reverse proxy that requires a client certificate.
EOF

chmod -R u+w,go-w "$stage"
chmod 755 "$stage/DEBIAN"

dist="$root/dist"
mkdir -p "$dist"
out="$dist/talos_${version}_${arch}.deb"
dpkg-deb --build --root-owner-group "$stage" "$out"

echo "Built $out"
ls -lh "$out"
