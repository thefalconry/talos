#!/bin/bash
# SPDX-License-Identifier: GPL-2.0-only
#
# Builds dist/Talos-<version>-<arch>.dmg: the app with Qt inside it, on a
# disk image that opens as a window with the app beside the Applications
# folder.
#
#   packaging/macos/build-dmg.sh
#
# Needs Xcode's command line tools, CMake, Ninja and a Qt 6.8+ for macOS
# (Homebrew's `qt`, or one from the Qt installer or aqt named by
# CMAKE_PREFIX_PATH). The image runs on the macOS version Qt was built
# for and later: Homebrew's Qt targets the Mac it is installed on, the Qt
# Company's builds go back several releases.
#
# Signing. Without TALOS_SIGN_IDENTITY the app gets an ad-hoc signature,
# which is enough to run on the Mac that built it and on others after the
# person approves it under Privacy & Security. For an image anyone can open
# with a double click, set
#
#   TALOS_SIGN_IDENTITY="Developer ID Application: Your Name (TEAMID)"
#   TALOS_NOTARY_PROFILE=<a keychain profile made with `xcrun notarytool store-credentials`>
#
# and the app is signed with the hardened runtime and the image notarised
# and stapled.
set -euo pipefail

here="$(cd "$(dirname "$0")" && pwd)"
root="$(cd "$here/../.." && pwd)"
build="${TALOS_BUILD_DIR:-$root/build-macos}"
dist="$root/dist"
identity="${TALOS_SIGN_IDENTITY:--}"

version="$(sed -n 's/^ *VERSION \([0-9][0-9.]*\).*/\1/p' "$root/CMakeLists.txt" | head -1)"
arch="$(uname -m)"
name="Talos-$version-$arch"
volume="Talos"

say() { printf '\n==> %s\n' "$*"; }

say "Building Talos $version ($arch)"
cmake -S "$root" -B "$build" -G Ninja -DCMAKE_BUILD_TYPE=Release -DTALOS_BUILD_TESTS=OFF \
    ${TALOS_UPDATE_REPO:+-DTALOS_UPDATE_REPO="$TALOS_UPDATE_REPO"} >/dev/null
cmake --build "$build"

work="$(mktemp -d "${TMPDIR:-/tmp}/talos-dmg.XXXXXX")"
mount=""
cleanup() {
    [ -n "$mount" ] && hdiutil detach "$mount" -force -quiet 2>/dev/null || true
    rm -rf "$work"
}
trap cleanup EXIT

stage="$work/stage"
mkdir -p "$stage/.background"
cp -R "$build/Talos.app" "$stage/Talos.app"
app="$stage/Talos.app"

say "Bundling Qt"
# The tools of the Qt the app was built against, not whichever is on PATH.
qt="$(cd "$(sed -n 's/^Qt6_DIR:[A-Z]*=//p' "$build/CMakeCache.txt")/../../.." && pwd)"
deploy="$qt/bin/macdeployqt"
[ -x "$deploy" ] || { echo "macdeployqt was not found in $qt/bin" >&2; exit 1; }
# It copies every module the interface could import, among them ones this
# Qt has but the bundle will not get (and says so at length). What it
# leaves broken is removed below, and what matters is checked after that.
"$deploy" "$app" -qmldir="$root/qml" -always-overwrite -verbose=1 >"$work/deploy.log" 2>&1 || {
    cat "$work/deploy.log" >&2
    exit 1
}

frameworks="$app/Contents/Frameworks"
plugins="$app/Contents/PlugIns"
qml="$app/Contents/Resources/qml"

# A QML module's library lives under PlugIns with a link beside its qmldir:
# removing the module means removing both.
drop_module() {
    local dir="$qml/$1" link
    [ -d "$dir" ] || return 0
    find "$dir" -type l -name '*.dylib' | while read -r link; do
        rm -f "$(cd "$(dirname "$link")" && cd "$(dirname "$(readlink "$link")")" && pwd)/$(basename "$link")"
    done
    rm -rf "$dir"
}
# The interface uses the Basic style and nothing below.
for module in QtQuick/Controls/FluentWinUI3 QtQuick/Controls/Fusion QtQuick/Controls/Imagine \
        QtQuick/Controls/Material QtQuick/Controls/Universal QtQuick/Controls/iOS \
        QtQuick/Controls/macOS QtQuick/Controls/designer QtQuick/NativeStyle \
        QtQuick/LocalStorage QtQuick/Particles QtQuick/Pdf QtQuick/Scene2D QtQuick/Scene3D \
        QtQuick/Timeline QtQuick/VirtualKeyboard QtQuick/tooling QtQml/StateMachine \
        QtQml/XmlListModel; do
    drop_module "$module"
done
# Plugins for things the app never does: databases, an on-screen keyboard,
# image formats nobody sends, GPS receivers on a serial port.
rm -rf "$plugins/sqldrivers" "$plugins/platforminputcontexts" "$plugins/iconengines"
for plugin in imageformats/libqjp2 imageformats/libqmng imageformats/libqpdf imageformats/libqtga \
        imageformats/libqwbmp networkinformation/libqglib position/libqtposition_nmea \
        position/libqtposition_positionpoll texttospeech/libqtexttospeech_mock; do
    rm -f "$plugins/$plugin.dylib"
done

# OpenSSL keeps RC2 and 3DES, which older certificate bundles are sealed
# with, in a module of its own that it looks for where it was built. When
# Qt's OpenSSL came into the bundle, that module comes too; the app tells
# OpenSSL where it is.
if ls "$frameworks"/libcrypto.*.dylib >/dev/null 2>&1; then
    crypto="$(basename "$(ls "$frameworks"/libcrypto.*.dylib | head -1)")"
    backend="$("$qt/bin/qtpaths" --query QT_INSTALL_PLUGINS 2>/dev/null || true)/tls/libqopensslbackend.dylib"
    origin="$(otool -L "$backend" 2>/dev/null | awk -v c="$crypto" 'index($1, c) {print $1}' | head -1 || true)"
    legacy="$(dirname "${origin:-/nonexistent}")/ossl-modules/legacy.dylib"
    if [ -f "$legacy" ]; then
        mkdir -p "$plugins/ossl-modules"
        cp "$legacy" "$plugins/ossl-modules/legacy.dylib"
        chmod u+w "$plugins/ossl-modules/legacy.dylib"
        # It names the library by wherever its own build found it.
        otool -L "$legacy" | awk -v c="$crypto" 'index($1, c) {print $1}' | while read -r was; do
            install_name_tool -change "$was" "@executable_path/../Frameworks/$crypto" \
                "$plugins/ossl-modules/legacy.dylib" 2>/dev/null
        done
    fi
fi

# What a binary loads from inside the bundle, as names under Frameworks.
needs() {
    otool -L "$1" 2>/dev/null | tail -n +2 | awk '{print $1}' \
        | sed -n -e 's|^@rpath/||p' -e 's|^@executable_path/\.\./Frameworks/||p' \
                 -e 's|^@loader_path/\.\./\.\./\.\./||p' | cut -d/ -f1 | sort -u
}
# A plugin whose library did not come along cannot load: out it goes.
find "$plugins" "$qml" -type f -name '*.dylib' | while read -r binary; do
    for need in $(needs "$binary"); do
        if [ ! -e "$frameworks/$need" ]; then
            rm -f "$binary"
            break
        fi
    done
done
find "$qml" -type l ! -exec test -e {} \; -delete
# And a library nothing loads any more is dead weight. Start from the
# program and the plugins, follow what each loads, keep what was reached.
: >"$work/reached"
{ echo "$app/Contents/MacOS/Talos"; find "$plugins" -type f -name '*.dylib'; } >"$work/todo"
while [ -s "$work/todo" ]; do
    : >"$work/next"
    while read -r binary; do
        for need in $(needs "$binary"); do
            grep -qxF "$need" "$work/reached" && continue
            echo "$need" >>"$work/reached"
            case "$need" in
                *.framework) echo "$frameworks/$need/Versions/A/${need%.framework}" >>"$work/next" ;;
                *) echo "$frameworks/$need" >>"$work/next" ;;
            esac
        done
    done <"$work/todo"
    mv "$work/next" "$work/todo"
done
for item in "$frameworks"/*; do
    grep -qxF "$(basename "$item")" "$work/reached" || rm -rf "$item"
done

# Nothing may still point outside the bundle, or the app only starts on a
# Mac that happens to have the same Qt in the same place.
stray="$(find "$app" -type f \( -perm -u+x -o -name '*.dylib' \) | while read -r binary; do
    own="$(otool -D "$binary" 2>/dev/null | tail -n +2)"
    otool -L "$binary" 2>/dev/null | tail -n +2 | awk '{print $1}' \
        | grep -E '^(/opt/|/usr/local/|/Users/)' | grep -vxF "${own:-none}" | sed "s|^|$binary: |" \
        || true
done | head -5)"
if [ -n "$stray" ]; then
    echo "these still load from outside the bundle:" >&2
    echo "$stray" >&2
    exit 1
fi
echo "bundle: $(du -sh "$app" | cut -f1)"

say "Signing ($([ "$identity" = "-" ] && echo "ad hoc" || echo "$identity"))"
sign=(codesign --force --timestamp=none --sign "$identity")
[ "$identity" != "-" ] && sign=(codesign --force --timestamp --options runtime --sign "$identity")
# Inside out: every library and plugin, then the frameworks, then the app.
find "$app/Contents" -type f \( -name '*.dylib' -o -name '*.so' \) -print0 | xargs -0 "${sign[@]}" 2>/dev/null
find "$app/Contents/Frameworks" -maxdepth 1 -name '*.framework' -print0 2>/dev/null \
    | xargs -0 -n1 "${sign[@]}" 2>/dev/null
"${sign[@]}" "$app"
codesign --verify --deep --strict "$app"

say "Making the disk image"
cp "$here/dmg-background.tiff" "$stage/.background/background.tiff"
ln -s /Applications "$stage/Applications"
cp "$here/Talos.icns" "$stage/.VolumeIcon.icns"

rw="$work/rw.dmg"
megabytes=$(( $(du -sm "$stage" | cut -f1) + 24 ))
hdiutil create -quiet -srcfolder "$stage" -volname "$volume" -fs HFS+ -format UDRW \
    -size "${megabytes}m" -ov "$rw"
# Mounted without Finder showing it: nothing but this script should touch
# the volume. (Newer systems grumble that the option has a new spelling.)
mount="$(hdiutil attach "$rw" -readwrite -noverify -noautoopen -nobrowse 2>/dev/null \
    | sed -n 's|^/dev/.*\(/Volumes/.*\)$|\1|p' | head -1)"
[ -d "$mount" ] || { echo "the image did not mount" >&2; exit 1; }

swift "$here/dmg-layout.swift" "$mount" Talos.app .background/background.tiff
# The volume shows the app's icon instead of a plain disk: the icon file is
# there, and this flag on the volume's root says to use it.
xattr -wx com.apple.FinderInfo \
    0000000000000000040000000000000000000000000000000000000000000000 "$mount"
rm -rf "$mount/.fseventsd" "$mount/.Trashes"
sync
hdiutil detach "$mount" -quiet
mount=""

mkdir -p "$dist"
out="$dist/$name.dmg"
rm -f "$out"
hdiutil convert "$rw" -quiet -format UDZO -imagekey zlib-level=9 -o "$out"

if [ "$identity" != "-" ]; then
    codesign --force --timestamp --sign "$identity" "$out"
    if [ -n "${TALOS_NOTARY_PROFILE:-}" ]; then
        say "Notarising"
        xcrun notarytool submit "$out" --keychain-profile "$TALOS_NOTARY_PROFILE" --wait
        xcrun stapler staple "$out"
    fi
fi

# The updater checks a download against a file like this one.
(cd "$dist" && shasum -a 256 "$name.dmg" > "$name.dmg.sha256")

say "Done"
ls -lh "$out"
cat "$dist/$name.dmg.sha256"
