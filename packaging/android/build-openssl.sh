#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
#
# Build OpenSSL for the Android APK.
#
#   packaging/android/build-openssl.sh
#
# Qt has no TLS of its own on Android: its backend loads libssl_3.so and
# libcrypto_3.so at run time, and an APK without them cannot make an https
# connection or open a client certificate. This fetches the official source,
# checks it against the SHA-256 pinned below, and builds those two libraries
# with the NDK into build-openssl/<abi>/.
#
# Environment (defaults in brackets):
#   ANDROID_SDK   Android SDK   [$HOME/Android/Sdk]
#   ANDROID_NDK   NDK           [newest under $ANDROID_SDK/ndk]
#   ANDROID_ABI   arm64-v8a | armeabi-v7a | x86_64 | x86   [arm64-v8a]
#   ANDROID_API   minimum API level, matching the app's    [28]
set -euo pipefail

# To move to a newer release, change both lines; the checksum is the one
# published beside the tarball on the OpenSSL release page.
version=3.5.9
sha256=603f5602e2eef00d77fbd429d34dcd5822bb301757a1bc9cdb24c670f1eb859a

root=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
sdk=${ANDROID_SDK:-$HOME/Android/Sdk}
ndk=${ANDROID_NDK:-$(ls -d "$sdk"/ndk/* | sort -V | tail -1)}
abi=${ANDROID_ABI:-arm64-v8a}
api=${ANDROID_API:-28}

case "$abi" in
    arm64-v8a)   target=android-arm64 ;;
    armeabi-v7a) target=android-arm ;;
    x86_64)      target=android-x86_64 ;;
    x86)         target=android-x86 ;;
    *) echo "Unknown ANDROID_ABI: $abi" >&2; exit 1 ;;
esac

work="$root/build-openssl"
out="$work/$abi"
stamp="$out/.version"
if [[ -f "$out/libssl_3.so" && -f "$out/libcrypto_3.so" && "$(cat "$stamp" 2>/dev/null)" == "$version" ]]; then
    echo "$out"
    exit 0
fi

mkdir -p "$work"
tarball="$work/openssl-$version.tar.gz"
if [[ ! -f "$tarball" ]]; then
    curl -fL --retry 3 -o "$tarball.part" \
        "https://github.com/openssl/openssl/releases/download/openssl-$version/openssl-$version.tar.gz"
    mv "$tarball.part" "$tarball"
fi
echo "$sha256  $tarball" | sha256sum -c - >&2

src="$work/src-$abi"
rm -rf "$src"
mkdir -p "$src"
tar -xzf "$tarball" -C "$src" --strip-components=1

export ANDROID_NDK_ROOT="$ndk"
export PATH="$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin:$PATH"
(
    cd "$src"
    # no-module builds the legacy provider into libcrypto instead of as a
    # separate file, so older RC2/3DES certificate bundles can still be
    # opened (see enableLegacyCiphers in BridgeClient.cpp). The page-size
    # flag is what 16 KB-page devices require of every native library.
    ./Configure "$target" -D__ANDROID_API__="$api" shared no-module no-tests no-docs \
        -Wl,-z,max-page-size=16384 >&2
    # Android has no versioned sonames; Qt looks for these exact names.
    make -j"$(nproc)" SHLIB_VERSION_NUMBER= SHLIB_EXT=_3.so build_libs >&2
)

mkdir -p "$out"
strip="$ndk/toolchains/llvm/prebuilt/linux-x86_64/bin/llvm-strip"
for lib in libcrypto_3.so libssl_3.so; do
    "$strip" --strip-unneeded -o "$out/$lib" "$src/$lib"
done
echo "$version" > "$stamp"
rm -rf "$src"
echo "$out"
