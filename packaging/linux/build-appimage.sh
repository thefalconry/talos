#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
#
# Build an AppImage for Linux into dist/.
#
#   packaging/linux/build-appimage.sh [build-dir]
#
set -euo pipefail

root=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
version=$(sed -n 's/^ *VERSION \([0-9.]*\).*/\1/p' "$root/CMakeLists.txt" | head -1)
arch="x86_64"
build_dir="${1:-${TALOS_BUILD_DIR:-$root/build}}"

if [[ ! -f "$build_dir/talos" && ! -f "$build_dir/bin/talos" ]]; then
    echo "Talos executable not found in $build_dir. Building..."
    cmake -S "$root" -B "$build_dir" -G Ninja \
        -DCMAKE_BUILD_TYPE=Release \
        -DTALOS_BUILD_TESTS=OFF
    cmake --build "$build_dir" --target talos
fi

work=$(mktemp -d "${TMPDIR:-/tmp}/talos-appimage.XXXXXX")
trap 'rm -rf "$work"' EXIT

appdir="$work/AppDir"
mkdir -p "$appdir"
cmake --install "$build_dir" --prefix "$appdir/usr"

# Download linuxdeploy and linuxdeploy-plugin-qt
tools_dir="$work/tools"
mkdir -p "$tools_dir"

if [[ ! -x "$tools_dir/linuxdeploy" ]]; then
    curl -fL --retry 3 -o "$tools_dir/linuxdeploy.AppImage" \
        "https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-$arch.AppImage"
    chmod +x "$tools_dir/linuxdeploy.AppImage"
    (cd "$tools_dir" && ./linuxdeploy.AppImage --appimage-extract >/dev/null && mv squashfs-root linuxdeploy-root)
    printf '#!/bin/sh\nexec "%s/linuxdeploy-root/AppRun" "$@"\n' "$tools_dir" > "$tools_dir/linuxdeploy"
    chmod +x "$tools_dir/linuxdeploy"
fi

if [[ ! -x "$tools_dir/linuxdeploy-plugin-qt" ]]; then
    curl -fL --retry 3 -o "$tools_dir/linuxdeploy-plugin-qt.AppImage" \
        "https://github.com/linuxdeploy/linuxdeploy-plugin-qt/releases/download/continuous/linuxdeploy-plugin-qt-$arch.AppImage"
    chmod +x "$tools_dir/linuxdeploy-plugin-qt.AppImage"
    (cd "$tools_dir" && ./linuxdeploy-plugin-qt.AppImage --appimage-extract >/dev/null && mv squashfs-root linuxdeploy-plugin-qt-root)
    printf '#!/bin/sh\nexec "%s/linuxdeploy-plugin-qt-root/AppRun" "$@"\n' "$tools_dir" > "$tools_dir/linuxdeploy-plugin-qt"
    chmod +x "$tools_dir/linuxdeploy-plugin-qt"
fi

export PATH="$tools_dir:$PATH"
export QML_SOURCES_PATHS="${QML_SOURCES_PATHS:-$root/qml:$root/src}"
# Provide dummy qmldir for Talos.Native so linuxdeploy-plugin-qt resolves it
mkdir -p "$work/qml_modules/Talos/Native"
cat << 'EOF' > "$work/qml_modules/Talos/Native/qmldir"
module Talos.Native
typeinfo plugins.qmltypes
EOF
export QML_MODULES_PATHS="$work/qml_modules:${QML_MODULES_PATHS:-}"
if [[ -n "${QT_ROOT_DIR:-}" ]]; then
    export QT_DIR="$QT_ROOT_DIR"
    export QMAKE="$QT_ROOT_DIR/bin/qmake"
    export PATH="$QT_ROOT_DIR/bin:$PATH"
fi

dist="$root/dist"
mkdir -p "$dist"

export APPIMAGE_EXTRACT_AND_RUN=1
cd "$work"
"$tools_dir/linuxdeploy" \
    --appdir "$appdir" \
    -d "$root/packaging/linux/dev.talos.client.desktop" \
    -i "$root/assets/talos.svg" \
    --plugin qt \
    --output appimage

out="$dist/Talos-${version}-${arch}.AppImage"
mv "$work"/*.AppImage "$out"

echo "Built $out"
ls -lh "$out"
