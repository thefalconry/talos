// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <QByteArray>
#include <QString>

namespace talos {

/// Where credentials live: the bridge token, the client certificate and the
/// app-lock record. Backed by the platform's own store where there is one:
///
///   - Linux:   the Secret Service (GNOME Keyring, KWallet) over D-Bus
///   - macOS:   the login Keychain
///   - Windows: DPAPI, bound to the signed-in user
///   - Android: the app's private storage (sandboxed per app by the OS)
///
/// and by an owner-only file when that store is unavailable (a headless
/// Linux session with no keyring). backendName() says which is in use, so
/// the settings screen never claims more than is true.
class SecretStore
{
public:
    static SecretStore &instance();

    bool write(const QString &key, const QByteArray &value);
    QByteArray read(const QString &key, bool *found = nullptr);
    void remove(const QString &key);

    QString backendName() const;
    bool fileOnly() const { return m_fileOnly; }
    /// Move one key to the private file (@p toFile) or back to the platform
    /// store, whichever backend is in force right now.
    void moveKey(const QString &key, bool toFile);

    /// Directory for the fallback file. Set once at startup.
    void setFallbackDir(const QString &dir) { m_dir = dir; }
    /// Skip the platform store and use the file (tests, TALOS_SECRETS=file).
    void setFileOnly(bool on) { m_fileOnly = on; }

private:
    SecretStore() = default;
    bool platformWrite(const QString &key, const QByteArray &value);
    bool platformRead(const QString &key, QByteArray *value, bool *found);
    bool platformRemove(const QString &key);
    bool platformAvailable() const;

    bool fileWrite(const QString &key, const QByteArray *value);
    QByteArray fileRead(const QString &key, bool *found) const;

    QString m_dir;
    bool m_fileOnly = false;
    mutable int m_platformOk = -1; ///< -1 unknown, 0 unavailable, 1 available
};

} // namespace talos
