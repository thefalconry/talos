// SPDX-License-Identifier: GPL-2.0-only
#include "core/Store.h"

#include "core/AppLog.h"
#include "core/Crypto.h"
#include "core/SecretStore.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QSaveFile>
#include <QStandardPaths>
#include <QUuid>

namespace talos {

using namespace Qt::StringLiterals;

namespace {

QJsonObject readJsonFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    return QJsonDocument::fromJson(f.readAll()).object();
}

bool writeFilePrivate(const QString &path, const QByteArray &bytes)
{
    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    f.write(bytes);
    return f.commit();
}

/// Defaults for every preference. A key missing here does not exist: QML
/// reads `App.prefs.<key>` and an unknown one would silently be undefined.
QVariantMap defaultPrefs()
{
    return {
        // Appearance
        {u"theme"_s, u"system"_s},          // system | light | dark
        {u"accent"_s, u"#C2622D"_s},
        {u"textScale"_s, 1.0},
        {u"sidebarOpen"_s, true},
        {u"sidebarWidth"_s, 292},
        {u"showReasoning"_s, true},
        {u"language"_s, QString()},          // "" follows the system; else en, es, hi
        // Composer
        {u"sendOnEnter"_s, true},
        // Notifications
        {u"notifications"_s, true},
        {u"notifyPreview"_s, true},
        // Desktop
        {u"closeToTray"_s, false},
        {u"autostart"_s, false},
        {u"quickShortcut"_s, false},        // a system-wide key opens the quick-ask window
        {u"screenPrivacy"_s, false},
        {u"windowWidth"_s, 1180},
        {u"windowHeight"_s, 780},
        // Mesh
        {u"meshSharing"_s, false},
        {u"meshPeriodic"_s, false},
        {u"meshIntervalSeconds"_s, 300},
        {u"meshMaxConcurrent"_s, 4},
        {u"meshMaxQueued"_s, 16},
        {u"meshMaxWriteGiB"_s, 4},
        // What device control covers. Folders empty means anywhere.
        {u"meshAllowExec"_s, true},
        {u"meshAllowRead"_s, true},
        {u"meshAllowWrite"_s, true},
        {u"meshFolders"_s, QStringList()},
        {u"meshService"_s, false},          // the background service is installed
        {u"meshBackground"_s, false},       // Android: stay connected with the window closed
        {u"meshScreenControl"_s, false},    // Talon may see and drive the screen
        {u"screenRestoreToken"_s, QString()}, // the desktop's remembered grant
        // App lock (the lock record itself lives in the secret store)
        {u"lockTimeoutMinutes"_s, 5},
        {u"lockEraseAfterFailures"_s, false},
        {u"lockGateCommands"_s, false},
        {u"lockBiometric"_s, false},
        // Voice
        {u"voiceAutoSpeak"_s, false},
        {u"voiceName"_s, QString()},
        {u"voiceRate"_s, 0.0},
        // Updates
        {u"updateRepo"_s, QStringLiteral(TALOS_UPDATE_REPO)},
        {u"updateAutoCheck"_s, true},
    };
}

} // namespace

Store::Store(const QString &root, QObject *parent) : QObject(parent)
{
    QString base = root;
    if (base.isEmpty())
        base = qEnvironmentVariable("TALOS_HOME");
    if (!base.isEmpty()) {
        m_configDir = QDir(base).filePath(u"config"_s);
        m_dataDir = QDir(base).filePath(u"data"_s);
        m_cacheDir = QDir(base).filePath(u"cache"_s);
    } else {
        m_configDir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
        m_dataDir = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
        m_cacheDir = QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
    }
    for (const QString &dir : {m_configDir, m_dataDir, m_cacheDir})
        QDir().mkpath(dir);
    // Chat snapshots and audit trails are nobody else's business.
    QFile::setPermissions(m_dataDir, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);
    QFile::setPermissions(m_configDir, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner);

    SecretStore::instance().setFallbackDir(m_configDir);
    // A relocated data directory is a test or a scratch profile: it keeps
    // its secrets in that directory too, and leaves the user's keyring alone.
    if (qEnvironmentVariable("TALOS_SECRETS") == "file"_L1 || !base.isEmpty())
        SecretStore::instance().setFileOnly(true);

    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(400);
    connect(&m_saveTimer, &QTimer::timeout, this, &Store::flush);

    loadPrefs();
    if (prefBool(u"meshService"_s))
        SecretStore::instance().setFileOnly(true);
    loadProfiles();
    const QJsonObject state = readJsonFile(QDir(m_dataDir).filePath(u"state.json"_s));
    const QJsonObject read = state["lastRead"_L1].toObject();
    for (auto it = read.begin(); it != read.end(); ++it)
        m_lastRead.insert(it.key(), qint64(it.value().toDouble()));

    connect(&m_prefs, &QQmlPropertyMap::valueChanged, this,
            [this](const QString &key, const QVariant &) {
        m_prefsDirty = true;
        scheduleSave();
        emit prefChanged(key);
    });
}

Store::~Store()
{
    flush();
}

void Store::loadPrefs()
{
    const QVariantMap defaults = defaultPrefs();
    const QJsonObject saved = readJsonFile(QDir(m_configDir).filePath(u"prefs.json"_s));
    for (auto it = defaults.begin(); it != defaults.end(); ++it) {
        QVariant value = it.value();
        if (saved.contains(it.key())) {
            QVariant stored = saved[it.key()].toVariant();
            // A hand-edited or older file must not change a value's type.
            if (stored.canConvert(value.metaType()) && stored.convert(value.metaType()))
                value = stored;
        }
        m_prefs.insert(it.key(), value);
    }
    // Per-bridge grants and the device id ride in the same file.
    for (const QString &key : saved.keys()) {
        if (key.startsWith("deviceControl/"_L1) || key == "meshDeviceId"_L1)
            m_prefs.insert(key, saved[key].toVariant());
    }
    if (!m_prefs.contains(u"meshDeviceId"_s))
        m_prefs.insert(u"meshDeviceId"_s, QString());
}

void Store::setPref(const QString &key, const QVariant &value)
{
    if (m_prefs.value(key) == value)
        return;
    m_prefs.insert(key, value);
    m_prefsDirty = true;
    scheduleSave();
    emit prefChanged(key);
}

void Store::scheduleSave()
{
    if (!m_saveTimer.isActive())
        m_saveTimer.start();
}

void Store::flush()
{
    m_saveTimer.stop();
    if (m_prefsDirty) {
        m_prefsDirty = false;
        QJsonObject o;
        for (const QString &key : m_prefs.keys())
            o[key] = QJsonValue::fromVariant(m_prefs.value(key));
        writeFilePrivate(QDir(m_configDir).filePath(u"prefs.json"_s),
                         QJsonDocument(o).toJson(QJsonDocument::Indented));
    }
    if (m_stateDirty) {
        m_stateDirty = false;
        QJsonObject read;
        for (auto it = m_lastRead.begin(); it != m_lastRead.end(); ++it)
            read[it.key()] = it.value();
        writeFilePrivate(QDir(m_dataDir).filePath(u"state.json"_s),
                         QJsonDocument(QJsonObject{{"lastRead"_L1, read}})
                             .toJson(QJsonDocument::Compact));
    }
}

// ── Profiles ────────────────────────────────────────────────────────────────

void Store::loadProfiles()
{
    const QJsonObject o = readJsonFile(QDir(m_configDir).filePath(u"profiles.json"_s));
    m_profiles.clear();
    for (const auto &v : o["profiles"_L1].toArray()) {
        Profile p = Profile::fromJson(v.toObject());
        if (!p.id.isEmpty())
            m_profiles << p;
    }
    m_activeId = o["active"_L1].toString();
    if (!m_activeId.isEmpty() && !hasProfile())
        m_activeId = m_profiles.isEmpty() ? QString() : m_profiles.first().id;
}

void Store::writeProfiles()
{
    QJsonArray arr;
    for (const Profile &p : m_profiles) {
        arr << p.toJson(false);
    }
    writeFilePrivate(QDir(m_configDir).filePath(u"profiles.json"_s),
                     QJsonDocument(QJsonObject{{"active"_L1, m_activeId}, {"profiles"_L1, arr}})
                         .toJson(QJsonDocument::Indented));
}

bool Store::hasProfile() const
{
    for (const Profile &p : m_profiles) {
        if (p.id == m_activeId)
            return true;
    }
    return false;
}

Profile Store::activeProfileMeta() const
{
    for (const Profile &p : m_profiles) {
        if (p.id == m_activeId)
            return p;
    }
    return Profile::defaults();
}

Profile Store::activeProfile() const
{
    if (m_activeCache && m_activeCache->id == m_activeId)
        return *m_activeCache;
    for (const Profile &p : m_profiles) {
        if (p.id != m_activeId)
            continue;
        Profile out = p;
        bool locked = false;
        out.applySecrets(readSecrets(p.id, &locked));
        // Locked, the stored flag is all there is to go on.
        out.certStored = locked ? p.certStored : out.hasClientCert();
        m_activeCache = out;
        return out;
    }
    return Profile::defaults();
}

std::optional<Profile> Store::profileById(const QString &id) const
{
    if (id == m_activeId && hasProfile())
        return activeProfile();
    for (const Profile &p : m_profiles) {
        if (p.id != id)
            continue;
        Profile out = p;
        bool locked = false;
        out.applySecrets(readSecrets(p.id, &locked));
        out.certStored = locked ? p.certStored : out.hasClientCert();
        return out;
    }
    return std::nullopt;
}

QVariantList Store::profilesVariant() const
{
    QVariantList out;
    for (const Profile &p : m_profiles) {
        QVariantMap m = p.toJson(false).toVariantMap();
        m[u"address"_s] = p.displayAddress();
        m[u"active"_s] = p.id == m_activeId;
        m[u"fingerprintPretty"_s] = formatFingerprint(p.fingerprint);
        out << m;
    }
    return out;
}

void Store::setProfileEndpoints(const QString &id, const QList<Endpoint> &endpoints,
                                const QString &version)
{
    for (Profile &existing : m_profiles) {
        if (existing.id != id)
            continue;
        if (existing.endpoints == endpoints && existing.endpointsVersion == version)
            return;
        existing.endpoints = endpoints;
        existing.endpointsVersion = version;
        m_activeCache.reset();
        writeProfiles();
        emit profilesChanged();
        return;
    }
}

void Store::saveProfile(const Profile &profile, bool makeActive)
{
    Profile p = profile;
    if (p.id.isEmpty())
        p.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
    if (p.name.trimmed().isEmpty())
        p.name = p.local ? u"This computer"_s : p.host;

    // Credentials that are sealed and can't be read right now are not this
    // save's to replace with nothing: a profile read while the app is
    // locked comes back without them.
    bool locked = false;
    readSecrets(p.id, &locked);
    const QJsonObject secrets = p.secretsJson();
    if (!locked || !secrets.isEmpty())
        writeSecrets(p.id, secrets);

    bool replaced = false;
    for (Profile &existing : m_profiles) {
        if (existing.id == p.id) {
            existing = p;
            replaced = true;
        }
    }
    if (!replaced)
        m_profiles << p;
    if (makeActive || m_activeId.isEmpty())
        m_activeId = p.id;
    // Only the non-secret half stays in memory and on disk.
    for (Profile &existing : m_profiles) {
        if (existing.id == p.id)
            existing.certStored = existing.hasClientCert();
        existing.token.clear();
        existing.clientP12.clear();
        existing.clientP12Password.clear();
    }
    m_activeCache.reset();
    writeProfiles();
    emit profilesChanged();
}

void Store::removeProfile(const QString &id)
{
    for (int i = 0; i < m_profiles.size(); ++i) {
        if (m_profiles.at(i).id != id)
            continue;
        m_prefs.insert(u"deviceControl/"_s + m_profiles.at(i).bridgeKey(), false);
        m_profiles.removeAt(i);
        break;
    }
    SecretStore::instance().remove(u"profile/"_s + id);
    m_secretMemo.remove(id);
    QFile::remove(snapshotPath(id, false));
    QFile::remove(snapshotPath(id, true));
    if (m_activeId == id)
        m_activeId = m_profiles.isEmpty() ? QString() : m_profiles.first().id;
    m_activeCache.reset();
    writeProfiles();
    emit profilesChanged();
}

void Store::setActiveProfile(const QString &id)
{
    if (id == m_activeId)
        return;
    m_activeId = id;
    m_activeCache.reset();
    writeProfiles();
    emit profilesChanged();
}

void Store::clearProfiles()
{
    const QList<Profile> all = m_profiles;
    for (const Profile &p : all)
        removeProfile(p.id);
    removeSnapshots();
}

QString Store::deviceId()
{
    QString id = m_prefs.value(u"meshDeviceId"_s).toString();
    if (id.isEmpty()) {
        id = QUuid::createUuid().toString(QUuid::WithoutBraces);
        setPref(u"meshDeviceId"_s, id);
        flush();
    }
    return id;
}

bool Store::deviceControl(const QString &bridgeKey) const
{
    return m_prefs.value(u"deviceControl/"_s + bridgeKey).toBool();
}

void Store::setDeviceControl(const QString &bridgeKey, bool on)
{
    setPref(u"deviceControl/"_s + bridgeKey, on);
}

// ── Read markers ────────────────────────────────────────────────────────────

void Store::setLastRead(const QString &chatId, qint64 ts)
{
    if (m_lastRead.value(chatId) >= ts)
        return;
    m_lastRead.insert(chatId, ts);
    m_stateDirty = true;
    scheduleSave();
}

void Store::clearLastRead(const QString &chatId)
{
    if (m_lastRead.remove(chatId)) {
        m_stateDirty = true;
        scheduleSave();
    }
}

// ── Offline snapshot ────────────────────────────────────────────────────────

QString Store::snapshotPath(const QString &profileId, bool sealed) const
{
    return QDir(m_dataDir).filePath(
        u"snapshot-%1.%2"_s.arg(profileId, sealed ? u"sealed"_s : u"json"_s));
}

QByteArray Store::readSnapshot(const QString &profileId) const
{
    if (snapshotMustSeal && snapshotMustSeal()) {
        const QByteArray key = snapshotKey ? snapshotKey() : QByteArray();
        if (key.isEmpty())
            return {}; // locked
        QFile f(snapshotPath(profileId, true));
        if (!f.open(QIODevice::ReadOnly))
            return {};
        return crypto::open(key, f.readAll(), profileId.toUtf8()).value_or(QByteArray());
    }
    QFile f(snapshotPath(profileId, false));
    return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
}

void Store::writeSnapshot(const QString &profileId, const QByteArray &json)
{
    if (snapshotMustSeal && snapshotMustSeal()) {
        // Never leave a readable copy beside the sealed one.
        QFile::remove(snapshotPath(profileId, false));
        const QByteArray key = snapshotKey ? snapshotKey() : QByteArray();
        if (key.isEmpty())
            return; // locked: keep the last sealed snapshot
        writeFilePrivate(snapshotPath(profileId, true),
                         crypto::seal(key, json, profileId.toUtf8()));
        return;
    }
    QFile::remove(snapshotPath(profileId, true));
    writeFilePrivate(snapshotPath(profileId, false), json);
}

void Store::removeSnapshots()
{
    const QDir dir(m_dataDir);
    for (const QString &name : dir.entryList({u"snapshot-*"_s}, QDir::Files))
        QFile::remove(dir.filePath(name));
}

QJsonObject Store::readSecrets(const QString &id, bool *locked) const
{
    if (locked)
        *locked = false;
    bool found = false;
    const QByteArray raw = SecretStore::instance().read(u"profile/"_s + id, &found);
    if (!found)
        return {};
    const QJsonObject stored = QJsonDocument::fromJson(raw).object();
    if (!stored.contains("sealed"_L1)) {
        m_secretMemo.insert(id, stored);
        return stored;
    }
    const QByteArray sealed = QByteArray::fromBase64(stored["sealed"_L1].toString().toLatin1());
    if (const auto plain = secretOpen ? secretOpen(sealed) : std::nullopt) {
        const QJsonObject secrets = QJsonDocument::fromJson(*plain).object();
        m_secretMemo.insert(id, secrets);
        return secrets;
    }
    if (const auto held = m_secretMemo.constFind(id); held != m_secretMemo.constEnd())
        return *held;
    if (locked)
        *locked = true;
    return {};
}

void Store::writeSecrets(const QString &id, const QJsonObject &secrets)
{
    const QString key = u"profile/"_s + id;
    m_secretMemo.insert(id, secrets);
    if (secrets.isEmpty()) {
        SecretStore::instance().remove(key);
        return;
    }
    QByteArray bytes = QJsonDocument(secrets).toJson(QJsonDocument::Compact);
    if (secretsSealed && secretsSealed() && secretSeal) {
        const QByteArray sealed = secretSeal(bytes);
        if (!sealed.isEmpty()) {
            bytes = QJsonDocument(QJsonObject{{"sealed"_L1, QString::fromLatin1(sealed.toBase64())}})
                        .toJson(QJsonDocument::Compact);
        }
    }
    SecretStore::instance().write(key, bytes);
}

void Store::invalidateSecrets()
{
    m_secretMemo.clear();
    m_activeCache.reset();
}

bool Store::secretsLocked() const
{
    if (!hasProfile())
        return false;
    bool locked = false;
    readSecrets(m_activeId, &locked);
    return locked;
}

void Store::resealSecrets()
{
    for (const Profile &p : std::as_const(m_profiles)) {
        bool locked = false;
        const QJsonObject secrets = readSecrets(p.id, &locked);
        if (!locked && !secrets.isEmpty())
            writeSecrets(p.id, secrets);
    }
    m_activeCache.reset();
}

void Store::setSecretsInFile(bool inFile)
{
    SecretStore &secrets = SecretStore::instance();
    QStringList keys{u"applock"_s};
    for (const Profile &p : m_profiles)
        keys << u"profile/"_s + p.id;
    // A relocated data directory is always file-only; nothing to move.
    const bool forced = qEnvironmentVariable("TALOS_SECRETS") == "file"_L1
        || qEnvironmentVariableIsSet("TALOS_HOME");
    if (!forced) {
        secrets.setFileOnly(false);
        for (const QString &key : keys)
            secrets.moveKey(key, inFile);
        secrets.setFileOnly(inFile);
    }
    m_activeCache.reset();
    emit secretBackendChanged();
}

QString Store::prefsPath() const
{
    return QDir(m_configDir).filePath(u"prefs.json"_s);
}

void Store::reloadPrefs()
{
    const QJsonObject saved = readJsonFile(prefsPath());
    for (auto it = saved.begin(); it != saved.end(); ++it) {
        const QVariant value = it.value().toVariant();
        if (m_prefs.value(it.key()) == value)
            continue;
        m_prefs.insert(it.key(), value);
        emit prefChanged(it.key());
    }
    m_activeCache.reset();
    loadProfiles();
    emit profilesChanged();
}

QString Store::secretBackend() const
{
    return SecretStore::instance().backendName();
}

} // namespace talos
