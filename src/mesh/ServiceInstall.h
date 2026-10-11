// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <QString>

/// Keeping this device on the mesh without the app open: a per-user
/// background service that runs `talos --headless`.
///
/// On Linux that is a systemd user unit. It is the user's own unit, in the
/// user's own configuration, running as the user: installing it needs no
/// administrator rights.
namespace talos::service {

bool supported();
/// "active", "inactive", "failed" … or "not installed".
QString state();
bool installed();
/// The unit this would install, for @p exe. Exposed for tests.
QString unitText(const QString &exe);
QString unitPath();

/// Whether the user's services keep running after sign-out and from boot.
/// Turning that on is left to the user: `loginctl enable-linger`.
bool lingering();

bool install(QString *error);
bool uninstall(QString *error);

} // namespace talos::service

namespace talos {
class Store;
}

namespace talos::service {

/// Everything turning the service on involves: remember the choice, move
/// credentials to where a session-less process can read them, install the
/// unit. And the reverse. On failure nothing is left half done.
bool enable(Store *store, QString *error);
bool disable(Store *store, QString *error);

} // namespace talos::service
