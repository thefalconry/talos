// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <QString>
#include <QVariantMap>

/// The handful of facts about this device the mesh reports, behind one
/// seam so the platform-specific code stays in one file.
namespace talos::platform {

/// The mesh's name for this OS: linux, windows, macos, android or ios.
QString name();
bool isMobile();
QString deviceName();

struct Battery {
    int percent = -1;  ///< -1 when there is no battery or it can't be read
    int charging = -1; ///< -1 unknown, 0 no, 1 yes
};
Battery battery();

/// OS, hardware, locale, timezone and network for the `status` command.
QVariantMap systemInfo();

/// True inside Flatpak or Snap, where shell and file commands would reach
/// only the sandbox and never the host the daemon believes it is driving.
bool isSandboxed();

/// Keep screenshots and the app switcher from showing the window (Android).
void setSecureWindow(bool on);
/// Send the app to the background without ending it, the way the back
/// gesture leaves an app on a phone (Android). False where there is no such
/// thing and the caller should close the window instead.
bool moveToBackground();
/// Run the connection in a background service that outlives the window and
/// starts again at boot, or stop it (Android). A no-op elsewhere.
void setKeepAlive(bool on);
/// What another app shared to this one since the last call, as
/// `{"text": …, "files": [content URIs]}` JSON; empty when nothing (Android).
QString takeShared();
/// Ask for permission to post notifications where the system requires it.
void askForNotifications();
/// The window's process says whether it is in front, so the background
/// service knows when a notification would only repeat what is on screen.
void markUiActive(const QString &dataDir, bool active);
bool uiActive(const QString &dataDir);
/// Hand an APK to the system installer, which asks the person holding the
/// device (Android). Empty when it was handed over, otherwise the reason.
QString installPackage(const QString &path);
/// Post a system notification on platforms without a tray (Android).
bool notifyNative(const QString &title, const QString &body);

/// Play the device's own ringtone for a few seconds (Android). False where
/// the caller should make the noise itself.
bool ringNative();

} // namespace talos::platform
