// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <QString>

/// Keeping this device on the mesh without the app open: a per-user
/// background service that runs `talos --headless`.
///
/// On Linux that is a systemd user unit, on macOS a LaunchAgent. Either is
/// the user's own, in the user's own configuration, running as the user:
/// installing it needs no administrator rights.
namespace talos::service {

bool supported();
/// "active", "inactive", "failed" … or "not installed".
QString state();
bool installed();
/// The unit this would install, for @p exe. Exposed for tests.
QString unitText(const QString &exe);
/// The LaunchAgent for @p exe: with @p home it runs against that data
/// directory, with @p log its messages are kept there. Exposed for tests.
QString agentText(const QString &exe, const QString &home, const QString &log);
/// The unit or agent file on this system (on a Mac, the boot-time daemon
/// instead when that is what is installed).
QString unitPath();

/// macOS: a LaunchDaemon in /Library/LaunchDaemons for @p exe that starts
/// at boot, before anyone signs in, and still runs as @p user with @p userHome
/// as HOME, never as root. Exposed for tests.
QString daemonText(const QString &exe, const QString &user, const QString &userHome,
                   const QString &talosHome, const QString &log);
/// Whether the boot-time daemon is what's installed (macOS).
bool atBoot();
/// Install the boot-time daemon in place of the LaunchAgent (macOS). Run as
/// the user, not root: sudo asks for a password, on the terminal, for the
/// one step that needs it. If the daemon doesn't start, the agent is put
/// back. Ported from talon PR 1374 by Dylan Neve.
bool installAtBoot(QString *error);

/// Whether the user's services keep running after sign-out and from boot.
/// Turning that on is left to the user: `loginctl enable-linger`. Never on
/// macOS, where a LaunchAgent lives and ends with the signed-in session.
bool lingering();

bool install(QString *error);
bool uninstall(QString *error);
/// Start the service again, so that it runs the build now on disk.
void restart();

} // namespace talos::service

namespace talos {
class Store;
}

namespace talos::service {

/// Everything turning the service on involves: remember the choice, move
/// credentials to where a session-less process can read them, install the
/// unit. And the reverse. On failure nothing is left half done.
bool enable(Store *store, QString *error);
/// enable(), with the boot-time daemon instead of the agent (macOS).
bool enableAtBoot(Store *store, QString *error);
bool disable(Store *store, QString *error);

} // namespace talos::service
