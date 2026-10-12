// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "core/Profile.h"

#include <QHash>
#include <QObject>
#include <QQmlPropertyMap>
#include <QTimer>
#include <QVariant>
#include <functional>

namespace talos {

/// The preference map QML binds to. A subclass only because QQmlPropertyMap
/// wants to be told its dynamic type at construction.
class Prefs : public QQmlPropertyMap
{
    Q_OBJECT
public:
    explicit Prefs(QObject *parent = nullptr) : QQmlPropertyMap(this, parent) {}
};

/// Everything the client keeps on disk: preferences, connection profiles
/// (their credentials go to the SecretStore), per-chat read markers and the
/// offline snapshot.
///
/// Preferences are a QQmlPropertyMap, so QML binds to `App.prefs.theme`
/// directly and a write from either side is persisted and notified.
class Store : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QQmlPropertyMap *prefs READ prefs CONSTANT)
    Q_PROPERTY(QVariantList profiles READ profilesVariant NOTIFY profilesChanged)
    Q_PROPERTY(QString activeProfileId READ activeProfileId NOTIFY profilesChanged)
    Q_PROPERTY(bool hasProfile READ hasProfile NOTIFY profilesChanged)
    Q_PROPERTY(QString secretBackend READ secretBackend NOTIFY secretBackendChanged)
public:
    /// @p root overrides the platform locations (tests, TALOS_HOME).
    explicit Store(const QString &root = {}, QObject *parent = nullptr);
    ~Store() override;

    QQmlPropertyMap *prefs() { return &m_prefs; }
    QVariant pref(const QString &key) const { return m_prefs.value(key); }
    bool prefBool(const QString &key) const { return m_prefs.value(key).toBool(); }
    int prefInt(const QString &key) const { return m_prefs.value(key).toInt(); }
    QString prefString(const QString &key) const { return m_prefs.value(key).toString(); }
    Q_INVOKABLE void setPref(const QString &key, const QVariant &value);

    const QList<Profile> &profiles() const { return m_profiles; }
    QVariantList profilesVariant() const;
    QString activeProfileId() const { return m_activeId; }
    bool hasProfile() const;
    /// The active profile with its credentials loaded (read from the secret
    /// store once, then cached until the profiles change).
    Profile activeProfile() const;
    /// The active profile without credentials: cheap, for display and keys.
    Profile activeProfileMeta() const;
    /// Any saved profile with its credentials, or nullopt.
    std::optional<Profile> profileById(const QString &id) const;
    void saveProfile(const Profile &profile, bool makeActive);
    /// Replace a profile's learned fallback addresses. Its secrets and the
    /// active profile are left alone.
    void setProfileEndpoints(const QString &id, const QList<Endpoint> &endpoints,
                             const QString &version);
    Q_INVOKABLE void removeProfile(const QString &id);
    void setActiveProfile(const QString &id);
    void clearProfiles();

    /// This install's stable mesh device id, minted on first use.
    QString deviceId();

    /// Device control is granted per bridge, never carried to another.
    bool deviceControl(const QString &bridgeKey) const;
    void setDeviceControl(const QString &bridgeKey, bool on);

    qint64 lastRead(const QString &chatId) const { return m_lastRead.value(chatId); }
    void setLastRead(const QString &chatId, qint64 ts);
    void clearLastRead(const QString &chatId);

    /// The offline snapshot for a profile. Sealed under the app lock's data
    /// key when @ref snapshotKey yields one; unreadable while locked.
    QByteArray readSnapshot(const QString &profileId) const;
    void writeSnapshot(const QString &profileId, const QByteArray &json);
    void removeSnapshots();
    /// Yields the key to seal the snapshot with, empty when the lock is off.
    std::function<QByteArray()> snapshotKey;
    /// True when a lock is configured (so plain snapshots must not be written).
    std::function<bool()> snapshotMustSeal;

    QString configDir() const { return m_configDir; }
    QString dataDir() const { return m_dataDir; }
    QString cacheDir() const { return m_cacheDir; }
    QString secretBackend() const;
    /// Keep credentials in the private file instead of the system keyring.
    /// The background service runs with no desktop session and so no
    /// unlocked keyring: while it is installed, both it and the app use the
    /// file. Moves what is stored across.
    void setSecretsInFile(bool inFile);
    /// Re-read the preference file (another process changed it).
    void reloadPrefs();
    QString prefsPath() const;
    /// Forget cached credentials so the next read goes to the store.
    void invalidateSecrets() { m_activeCache.reset(); }

    void flush();

signals:
    void profilesChanged();
    void prefChanged(const QString &key);
    void secretBackendChanged();

private:
    void loadPrefs();
    void loadProfiles();
    void writeProfiles();
    void scheduleSave();
    QString snapshotPath(const QString &profileId, bool sealed) const;

    QString m_configDir;
    QString m_dataDir;
    QString m_cacheDir;

    Prefs m_prefs;
    QList<Profile> m_profiles;
    QString m_activeId;
    mutable std::optional<Profile> m_activeCache;
    QHash<QString, qint64> m_lastRead;
    QTimer m_saveTimer;
    bool m_prefsDirty = false;
    bool m_stateDirty = false;
};

} // namespace talos
