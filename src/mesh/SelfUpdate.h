// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <QJsonObject>
#include <QString>

/// Replacing this program with a build the daemon pushed (`update_node`).
///
/// The pushed file must match the SHA-256 the daemon named, and must be a
/// Talos build: every build carries a version marker, and a file without
/// one (a talon-node binary, say, which the daemon picks by default for
/// this command) is refused rather than installed over the app.
namespace talos::selfupdate {

/// -1, 0 or 1 comparing dotted versions. A leading "v" or tag prefix is
/// ignored and a pre-release suffix sorts before its release.
int compareVersions(const QString &a, const QString &b);

/// The version a Talos binary announces in its own bytes, or empty when
/// @p file is not a Talos build.
QString versionOf(const QString &file);

/// The file this process runs from: the AppImage when inside one, else the
/// resolved executable.
QString ownExecutable();

/// True when ownExecutable() can be replaced by this user.
bool canReplaceSelf();

struct Request {
    QString source;         ///< the pushed file
    QString sha256;         ///< what the daemon says it should hash to
    QString running;        ///< this build's version
    bool allowDowngrade = false;
};

/// Verify @p request and swap it in for @p exe. Returns the new version on
/// success; on failure returns empty and sets @p error.
QString install(const QString &exe, const Request &request, QString *error);

} // namespace talos::selfupdate
