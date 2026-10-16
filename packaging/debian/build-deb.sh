#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
#
# Build a self-contained Debian/Ubuntu .deb package into dist/ with Qt bundled
# under /usr/lib/talos so it runs on Debian 12 / Ubuntu 24.04 without requiring
# system Qt 6.8+.
#
#   packaging/debian/build-deb.sh [build-dir]
#
set -euo pipefail

root=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
version=$(sed -n 's/^ *VERSION \([0-9.]*\).*/\1/p' "$root/CMakeLists.txt" | head -1)
arch="amd64"
deploy_arch="x86_64"
build_dir="${1:-${TALOS_BUILD_DIR:-$root/build}}"

if [[ ! -f "$build_dir/talos" && ! -f "$build_dir/bin/talos" ]]; then
    echo "Talos executable not found in $build_dir. Building..."
    cmake -S "$root" -B "$build_dir" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DTALOS_BUILD_TESTS=OFF \
        -DCMAKE_SKIP_INSTALL_RPATH=ON \
        -DTALOS_UPDATE_REPO="${TALOS_UPDATE_REPO:-thefalconry/talos}"
    cmake --build "$build_dir" --target talos
fi

work=$(mktemp -d "${TMPDIR:-/tmp}/talos-deb.XXXXXX")
trap 'rm -rf "$work"' EXIT

appdir="$work/AppDir"
stage="$work/stage"
mkdir -p "$appdir" "$stage"

# Install into AppDir and run linuxdeploy + linuxdeploy-plugin-qt to bundle Qt 6.8+
cmake --install "$build_dir" --prefix "$appdir/usr"

tools_dir="$work/tools"
mkdir -p "$tools_dir"

if [[ ! -x "$tools_dir/linuxdeploy" ]]; then
    curl -fL --retry 3 -o "$tools_dir/linuxdeploy.AppImage" \
        "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-$deploy_arch.AppImage"
    chmod +x "$tools_dir/linuxdeploy.AppImage"
    (cd "$tools_dir" && ./linuxdeploy.AppImage --appimage-extract >/dev/null && mv squashfs-root linuxdeploy-root)
    printf '#!/bin/sh\nexec "%s/linuxdeploy-root/AppRun" "$@"\n' "$tools_dir" > "$tools_dir/linuxdeploy"
    chmod +x "$tools_dir/linuxdeploy"
fi

if [[ ! -x "$tools_dir/linuxdeploy-plugin-qt" ]]; then
    curl -fL --retry 3 -o "$tools_dir/linuxdeploy-plugin-qt.AppImage" \
        "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-$deploy_arch.AppImage"
    chmod +x "$tools_dir/linuxdeploy-plugin-qt.AppImage"
    (cd "$tools_dir" && ./linuxdeploy-plugin-qt.AppImage --appimage-extract >/dev/null && mv squashfs-root linuxdeploy-plugin-qt-root)
    printf '#!/bin/sh\nexec "%s/linuxdeploy-plugin-qt-root/AppRun" "$@"\n' "$tools_dir" > "$tools_dir/linuxdeploy-plugin-qt"
    chmod +x "$tools_dir/linuxdeploy-plugin-qt"
fi

export PATH="$tools_dir:$PATH"
export QML_SOURCES_PATHS="${QML_SOURCES_PATHS:-$root/qml:$root/src}"
export EXTRA_PLATFORM_PLUGINS="libqoffscreen.so"
# Provide dummy qmldir for Talos.Native so linuxdeploy-plugin-qt resolves it
mkdir -p "$work/qml_modules/Talos/Native"
cat << 'EOF' > "$work/qml_modules/Talos/Native/qmldir"
module Talos.Native
EOF
export QML_MODULES_PATHS="$work/qml_modules:${QML_MODULES_PATHS:-}"
if [[ -n "${QT_ROOT_DIR:-}" ]]; then
    export QT_DIR="$QT_ROOT_DIR"
    export QMAKE="$QT_ROOT_DIR/bin/qmake"
    export PATH="$QT_ROOT_DIR/bin:$PATH"
fi

export APPIMAGE_EXTRACT_AND_RUN=1
(
    cd "$work"
    "$tools_dir/linuxdeploy" \
        --appdir "$appdir" \
        -d "$root/packaging/linux/dev.talos.client.desktop" \
        -i "$root/assets/talos.svg" \
        --plugin qt
)

# Relocate the deployed bundle under /usr/lib/talos with /usr/bin/talos symlink
mkdir -p "$stage/usr/lib/talos" "$stage/usr/bin" "$stage/usr/share"
for subdir in bin lib plugins qml translations; do
    if [[ -d "$appdir/usr/$subdir" ]]; then
        mv "$appdir/usr/$subdir" "$stage/usr/lib/talos/$subdir"
    fi
done
for subdir in applications icons; do
    if [[ -d "$appdir/usr/share/$subdir" ]]; then
        mv "$appdir/usr/share/$subdir" "$stage/usr/share/$subdir"
    fi
done
ln -s ../lib/talos/bin/talos "$stage/usr/bin/talos"

# Create DEBIAN/control
mkdir -p "$stage/DEBIAN"
cat > "$stage/DEBIAN/control" <<EOF
Package: talos
Version: ${version}
Section: net
Priority: optional
Architecture: ${arch}
Maintainer: The Falconry <support@thefalconry.org>
Depends: libc6 (>= 2.34), libstdc++6 (>= 11), libgcc-s1, libgl1, libegl1, libopengl0, libx11-6, libx11-xcb1, libxcb1, libfontconfig1, libfreetype6, libdbus-1-3, libglib2.0-0t64 | libglib2.0-0, libssl3t64 | libssl3, zlib1g
Recommends: speech-dispatcher, fprintd
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
