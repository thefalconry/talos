// SPDX-License-Identifier: GPL-2.0-only
#pragma once

class QCoreApplication;

namespace talos {

/// Run with no window: the connection and this device's side of the mesh,
/// nothing else. This is what the background service starts, so the device
/// stays reachable (ring, status, locate, and the commands the user granted)
/// without the app being open.
int runHeadless(QCoreApplication &app);

} // namespace talos
