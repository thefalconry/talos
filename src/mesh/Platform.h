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
/// Whether the system leaves this app its network while the phone sleeps
/// (Android's battery optimisation, which otherwise cuts the background
/// connection off). True where there is no such restriction.
bool batteryUnrestricted();
/// Ask the person, through the system's own prompt, to lift it (Android).
void askBatteryUnrestricted();
/// What another app shared to this one since the last call, as
/// `{"text": …, "files": [content URIs]}` JSON; empty when nothing (Android).
QString takeShared();
/// Dark status- and gesture-bar icons for a light theme, or light ones for
/// a dark theme (Android, which draws the app under those bars).
void setLightSystemBars(bool light);
/// Ask for permission to post notifications where the system requires it.
void askForNotifications();
/// The window's process says whether it is in front, so the background
/// service knows when a notification would only repeat what is on screen.
void markUiActive(const QString &dataDir, bool active);
bool uiActive(const QString &dataDir);
/// Hand an APK to the system installer, which asks the person holding the
/// device (Android). Empty when it was handed over, otherwise the reason.
QString installPackage(const QString &path);
/// Copy a file into the phone's shared Downloads folder, the one its Files
/// app shows, under `name` (Android 10 and later). Returns the new file's
/// content URI; empty elsewhere, or when it could not be done.
QString publishDownload(const QString &file, const QString &name);
/// Post a system notification on platforms without a tray (Android). One
/// given a chat replaces the last for that chat, and opens it when tapped.
bool notifyNative(const QString &title, const QString &body, const QString &chatId = {});
/// Take down the notification of a chat that is now on screen (Android).
void cancelChatNotification(const QString &chatId);
/// The chat whose notification was tapped since the last call (Android).
QString takeOpenChat();

/// Play the device's own ringtone for a few seconds (Android). False where
/// the caller should make the noise itself.
bool ringNative();

} // namespace talos::platform
