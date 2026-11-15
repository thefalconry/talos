// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <QString>
#include <QStringList>

/// Putting a downloaded release in place of the running install.
///
/// Only a download that matched the release's published checksum gets
/// here. What it replaces is left whole until the new copy is complete
/// beside it, so a failure at any step leaves the old program working.
namespace talos::selfupdate {

/// How a release asset named @p asset reaches this install:
/// "swap" (put where the running program is, which then restarts),
/// "setup" (Windows' installer, run without its questions),
/// "package" (the system's package manager, once the person authorises it),
/// or empty when the file can only be handed over.
QString releaseMode(const QString &asset);

/// The application bundle this process runs from, or empty outside one.
QString ownBundle();

struct Applied {
    bool ok = false;
    QString error;
    QString program;       ///< what to start so the new build takes over
    QStringList arguments;
    QString discard;       ///< the replaced copy, removed once this process has left
    bool afterExit = true; ///< false when @p program itself waits for this process
};

/// Install @p file, a release of @p version. Blocks while it copies or
/// while the person answers the system's prompt: call it off the GUI thread.
Applied applyRelease(const QString &file, const QString &version);

/// Swap the bundle at @p bundle for the one inside the disk image @p dmg.
Applied replaceBundle(const QString &bundle, const QString &dmg, const QString &version);
/// Put @p file where @p exe is, as one rename.
bool replaceFile(const QString &exe, const QString &file, QString *error);

/// Start @p program once this process is gone (a second instance would
/// only hand over to the first), removing @p discard on the way.
bool startAfterExit(const QString &program, const QStringList &arguments = {},
                    const QString &discard = {});

} // namespace talos::selfupdate
