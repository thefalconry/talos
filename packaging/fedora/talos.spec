# SPDX-License-Identifier: GPL-2.0-only
#
# Builds against Fedora's Qt 6 by default. To build with a Qt from the Qt
# installer or aqt instead (the result still runs on Fedora's Qt), pass
#   --define "qt_prefix /path/to/Qt/6.x/gcc_64"
# together with --nodeps.

%global debug_package %{nil}

Name:           talos
Version:        0.1.0
Release:        1%{?dist}
Summary:        Desktop client for the Talon agent

License:        GPL-2.0-only AND (BSD-2-Clause OR CC0-1.0)
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  cmake >= 3.21
BuildRequires:  ninja-build
BuildRequires:  gcc-c++
BuildRequires:  desktop-file-utils
BuildRequires:  qt6-qtbase-devel >= 6.8
BuildRequires:  qt6-qtdeclarative-devel
BuildRequires:  qt6-qtmultimedia-devel
BuildRequires:  qt6-qtspeech-devel
BuildRequires:  qt6-qtpositioning-devel

# QML modules are loaded at run time, so nothing links against them.
Requires:       qt6-qtdeclarative%{?_isa} >= 6.8
Requires:       hicolor-icon-theme
# A speech engine for reading replies aloud, and fingerprint unlock.
Recommends:     qt6-qtspeech-speechd
Suggests:       fprintd

%description
Talos is a client for Talon, the multi-platform agentic AI harness. It talks
to a Talon daemon running the native bridge, on this computer or over the
network, including through a reverse proxy that requires a client
certificate.

It offers streaming chats with the model's reasoning and tool calls, file
attachments, per-chat model and effort, control of the daemon's settings,
plugins and skills, and lets this computer join Talon's device mesh.

%prep
%autosetup

%build
%cmake -G Ninja \
    -DCMAKE_BUILD_TYPE=Release \
    -DTALOS_BUILD_TESTS=OFF \
    -DCMAKE_SKIP_INSTALL_RPATH=ON \
    %{?qt_prefix:-DCMAKE_PREFIX_PATH=%{qt_prefix}}
%cmake_build

%install
%cmake_install

%check
desktop-file-validate %{buildroot}%{_datadir}/applications/dev.talos.client.desktop

%files
%license LICENSE third_party/monocypher/LICENCE.md
%doc README.md THIRD_PARTY.md
%{_bindir}/talos
%{_datadir}/applications/dev.talos.client.desktop
%{_datadir}/icons/hicolor/scalable/apps/dev.talos.client.svg

%changelog
* Sun Oct 11 2026 RisenID <94130108+RisenID@users.noreply.github.com> - 0.1.0-1
- First package
