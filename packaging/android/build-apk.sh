#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
#
# Build and sign the Android APK into dist/.
#
#   packaging/android/build-apk.sh
#
# Environment (defaults in brackets):
#   QT_ANDROID      Qt for Android kit        [$HOME/Qt/6.11.2/android_arm64_v8a]
#   QT_HOST         desktop Qt of that version [$HOME/Qt/6.11.2/gcc_64]
#   ANDROID_SDK     Android SDK               [$HOME/Android/Sdk]
#   ANDROID_NDK     NDK                       [newest under $ANDROID_SDK/ndk]
#   TALOS_KEYSTORE  signing keystore          [$HOME/.android/talos-release.keystore]
#   TALOS_KEYSTORE_PASS_FILE  file holding its password
#                                             [$TALOS_KEYSTORE.pass]
#   TALOS_KEY_ALIAS key alias                 [talos]
#
# Android only installs an update over an app signed with the same key, so
# the keystore is the app's identity: back it up, and never commit it.
set -euo pipefail

root=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
qt_android=${QT_ANDROID:-$HOME/Qt/6.11.2/android_arm64_v8a}
qt_host=${QT_HOST:-$HOME/Qt/6.11.2/gcc_64}
sdk=${ANDROID_SDK:-$HOME/Android/Sdk}
ndk=${ANDROID_NDK:-$(ls -d "$sdk"/ndk/* | sort -V | tail -1)}
keystore=${TALOS_KEYSTORE:-$HOME/.android/talos-release.keystore}
passfile=${TALOS_KEYSTORE_PASS_FILE:-$keystore.pass}
alias=${TALOS_KEY_ALIAS:-talos}
tools=$(ls -d "$sdk"/build-tools/* | sort -V | tail -1)
version=$(sed -n 's/^    VERSION \([0-9.]*\)$/\1/p' "$root/CMakeLists.txt" | head -1)

[[ -f "$keystore" && -f "$passfile" ]] || {
    echo "No signing keystore at $keystore (with its password in $passfile)." >&2
    echo "Create one with keytool -genkeypair, or set TALOS_KEYSTORE." >&2
    exit 1
}

openssl=$(ANDROID_SDK="$sdk" ANDROID_NDK="$ndk" "$root/packaging/android/build-openssl.sh")

build="$root/build-android"
"$qt_android/bin/qt-cmake" -S "$root" -B "$build" -G Ninja \
    -DQT_HOST_PATH="$qt_host" -DANDROID_SDK_ROOT="$sdk" -DANDROID_NDK_ROOT="$ndk" \
    -DCMAKE_BUILD_TYPE=Release -DTALOS_BUILD_TESTS=OFF \
    -DTALOS_ANDROID_OPENSSL_DIR="$openssl"
cmake --build "$build"

unsigned="$build/android-build/build/outputs/apk/release/android-build-release-unsigned.apk"
mkdir -p "$root/dist"
out="$root/dist/talos-$version-arm64.apk"
"$tools/zipalign" -f -p 4 "$unsigned" "$out.aligned"
# A PKCS#12 keystore protects its key with the store's own password, which
# apksigner tries for the key when no separate one is given.
"$tools/apksigner" sign --ks "$keystore" --ks-key-alias "$alias" \
    --ks-pass "file:$passfile" --out "$out" "$out.aligned"
rm -f "$out.aligned" "$out.idsig"
# An APK without these still builds, installs and starts; it just can't
# speak TLS. Refuse to hand one over.
contents=$(unzip -Z1 "$out")
for lib in libcrypto_3.so libssl_3.so; do
    grep -qx "lib/arm64-v8a/$lib" <<<"$contents" || {
        echo "$out does not contain $lib" >&2
        exit 1
    }
done
"$tools/apksigner" verify --print-certs "$out" | head -3
echo "$out"
