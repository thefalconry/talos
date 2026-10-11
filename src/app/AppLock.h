// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "app/Biometric.h"

#include <QJSValue>
#include <QObject>
#include <QTimer>

class QJSEngine;

namespace talos {

class Store;

/// An optional passcode in front of the app.
///
/// The passcode is never stored. Argon2id turns it into a key that unwraps
/// a random data key; the wrapped data key, the salt and the cost
/// parameters are the whole record, kept in the platform secret store.
/// Unwrapping succeeding *is* the verification. While unlocked the data key
/// seals the offline chat snapshot, so the cache is unreadable at rest.
///
/// Failed attempts back off (1 s, 2 s, 4 s … up to a minute) and the count
/// survives a restart. Erasing the connection after ten failures is opt-in.
class AppLock : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool enabled READ enabled NOTIFY changed)
    Q_PROPERTY(bool locked READ locked NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(int failedAttempts READ failedAttempts NOTIFY changed)
    Q_PROPERTY(int retryInSeconds READ retryInSeconds NOTIFY tick)
    Q_PROPERTY(bool biometricAvailable READ biometricAvailable NOTIFY changed)
    Q_PROPERTY(bool biometricEnabled READ biometricEnabled NOTIFY changed)
    Q_PROPERTY(QString biometricName READ biometricName CONSTANT)
    Q_PROPERTY(QString biometricUnavailableReason READ biometricUnavailableReason NOTIFY changed)
public:
    static constexpr int kEraseAfter = 10;
    static constexpr int kMinPasscodeLength = 6;

    explicit AppLock(Store *store, QObject *parent = nullptr);
    void setScriptEngine(QJSEngine *engine) { m_engine = engine; }

    bool enabled() const { return m_enabled; }
    bool locked() const { return m_enabled && m_dataKey.isEmpty(); }
    bool busy() const { return m_busy; }
    int failedAttempts() const { return m_failures; }
    int retryInSeconds() const;
    /// The key that seals the snapshot; empty while locked or disabled.
    QByteArray dataKey() const { return m_dataKey; }

    /// Each callback receives `{ok, error}`.
    Q_INVOKABLE void enable(const QString &passcode, QJSValue done);
    Q_INVOKABLE void unlock(const QString &passcode, QJSValue done);
    Q_INVOKABLE void change(const QString &current, const QString &next, QJSValue done);
    Q_INVOKABLE void disable(const QString &passcode, QJSValue done);
    Q_INVOKABLE void lock();
    /// Forgotten passcode: drop the lock. The caller erases the connection.
    Q_INVOKABLE void reset();
    bool biometricAvailable() const { return m_bio.available(); }
    bool biometricEnabled() const;
    QString biometricName() const { return m_bio.name(); }
    QString biometricUnavailableReason() const { return m_bio.unavailableReason(); }
    /// Let a fingerprint or face release the lock. Needs the app unlocked.
    Q_INVOKABLE void enableBiometric(QJSValue done);
    Q_INVOKABLE void disableBiometric();
    Q_INVOKABLE void unlockWithBiometric(QJSValue done);
    Q_INVOKABLE void cancelBiometric() { m_bio.cancel(); }

    /// The user did something; restarts the idle countdown.
    void touch();
    /// The app was hidden or shown again.
    void setAppActive(bool active);

    static QString validate(const QString &passcode);

signals:
    void changed();
    void tick();
    void unlocked();
    /// Too many failed attempts with erase-on-failure on.
    void eraseRequested();

private:
    struct Record {
        QByteArray salt;
        quint32 memoryKiB = 0;
        quint32 passes = 0;
        QByteArray wrapped;
        /// A constant sealed under the data key: lets a key released by a
        /// biometric be recognised as the right one.
        QByteArray check;
    };
    void load();
    void save() const;
    void derive(const QString &passcode, const Record &record,
                std::function<void(const QByteArray &kek)> then);
    void reply(const QJSValue &cb, bool ok, const QString &error = {}) const;
    void noteFailure();
    void setBusy(bool busy);

    Store *m_store;
    QJSEngine *m_engine = nullptr;
    bool m_enabled = false;
    bool m_busy = false;
    Record m_record;
    QByteArray m_dataKey;
    int m_failures = 0;
    qint64 m_lastFailureMs = 0;
    qint64 m_lastActivityMs = 0;
    qint64 m_hiddenSinceMs = 0;
    QTimer m_idle;
    QTimer m_ticker;
    Biometric m_bio;
};

} // namespace talos
