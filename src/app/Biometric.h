// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <QByteArray>
#include <QObject>
#include <functional>

namespace talos {

/// Releasing the app lock's data key to a fingerprint or face instead of
/// the passcode.
///
/// Android: the key is encrypted under a hardware-backed keystore key that
/// the system only lets be used after a successful biometric prompt, so the
/// release is enforced by the device, not by this app.
///
/// Linux: fprintd confirms the finger and the key is then read from the
/// login keyring. The keyring is what protects the key at rest; the
/// fingerprint check is this app's own gate in front of it. That is a
/// convenience unlock, weaker than Android's, and the settings say so.
///
/// macOS: the same arrangement with Touch ID in front of the login Keychain.
class Biometric : public QObject
{
    Q_OBJECT
public:
    using SealDone = std::function<void(bool ok, const QString &error)>;
    using OpenDone = std::function<void(const QByteArray &key, const QString &error)>;

    explicit Biometric(QObject *parent = nullptr);

    /// A usable sensor with something enrolled, and somewhere safe to keep
    /// the key.
    bool available() const;
    /// Why not, when it isn't.
    QString unavailableReason() const;
    /// What to call it in the interface.
    QString name() const;
    /// True when a key has been sealed for biometric release.
    bool enrolled() const;

    void seal(const QByteArray &key, SealDone done);
    void open(OpenDone done);
    void cancel();
    void clear();

private slots:
    void verifyStatus(const QString &result, bool done);

private:
    void finishVerify(bool ok, const QString &error);

    QString m_device;
    OpenDone m_pending;
    bool m_verifying = false;
};

} // namespace talos
