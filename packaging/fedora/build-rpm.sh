#!/usr/bin/env bash
# SPDX-License-Identifier: GPL-2.0-only
#
# Build the Fedora RPM from the committed tree into dist/.
#
#   packaging/fedora/build-rpm.sh
#
# With Fedora's Qt -devel packages installed this is an ordinary build.
# Without them, point QT_PREFIX at another Qt 6 (the Qt installer's, aqt's):
#
#   QT_PREFIX=$HOME/Qt/6.11.2/gcc_64 packaging/fedora/build-rpm.sh
#
# The package still depends on, and runs with, Fedora's own Qt.
set -euo pipefail

root=$(git -C "$(dirname "$0")" rev-parse --show-toplevel)
version=$(sed -n 's/^Version:[[:space:]]*//p' "$root/packaging/fedora/talos.spec")
top=$(mktemp -d)
trap 'rm -rf "$top"' EXIT
mkdir -p "$top"/{SOURCES,SPECS,BUILD,RPMS,SRPMS}

git -C "$root" archive --format=tar.gz --prefix="talos-$version/" \
    -o "$top/SOURCES/talos-$version.tar.gz" HEAD

args=(--define "_topdir $top")
if [[ -n "${QT_PREFIX:-}" ]]; then
    args+=(--nodeps --define "qt_prefix $QT_PREFIX")
fi
rpmbuild -bb "${args[@]}" "$root/packaging/fedora/talos.spec"

mkdir -p "$root/dist"
find "$top/RPMS" -name '*.rpm' -exec cp -v {} "$root/dist/" \;
