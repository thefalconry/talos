// SPDX-License-Identifier: GPL-2.0-only
#include "core/SecretStore.h"

#include "core/AppLog.h"

#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>

#if defined(TALOS_HAVE_DBUS)
#  include <QDBusArgument>
#  include <QDBusConnection>
#  include <QDBusInterface>
#  include <QDBusMessage>
#  include <QDBusMetaType>
#  include <QDBusObjectPath>
#  include <QDBusReply>
#  include <QEventLoop>
#  include <QTimer>
#elif defined(Q_OS_MACOS)
#  include <CoreFoundation/CoreFoundation.h>
#  include <Security/Security.h>
#elif defined(Q_OS_WIN)
#  include <windows.h>
#  include <wincrypt.h>
#endif

namespace talos {

using namespace Qt::StringLiterals;

namespace {
const QString kService = u"talos"_s;
}

SecretStore &SecretStore::instance()
{
    static SecretStore store;
    return store;
}

// ── Fallback: one owner-only JSON file ──────────────────────────────────────

static QString fallbackPath(const QString &dir)
{
    return QDir(dir).filePath(u"secrets.json"_s);
}

QByteArray SecretStore::fileRead(const QString &key, bool *found) const
{
    if (found)
        *found = false;
    QFile f(fallbackPath(m_dir));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    if (!o.contains(key))
        return {};
    if (found)
        *found = true;
    return QByteArray::fromBase64(o[key].toString().toLatin1());
}

bool SecretStore::fileWrite(const QString &key, const QByteArray *value)
{
    if (m_dir.isEmpty())
        return false;
    QDir().mkpath(m_dir);
    const QString path = fallbackPath(m_dir);
    // The app and its background service share this file; one read-modify-
    // write at a time, or the slower writer undoes the faster one's change.
    QLockFile lock(path + u".lock"_s);
    lock.setStaleLockTime(10'000);
    if (!lock.tryLock(3000))
        AppLog::warn("secrets", u"the secrets file is busy; writing anyway"_s);
    QJsonObject o;
    if (QFile f(path); f.open(QIODevice::ReadOnly))
        o = QJsonDocument::fromJson(f.readAll()).object();
    if (value)
        o[key] = QString::fromLatin1(value->toBase64());
    else if (!o.contains(key))
        return true;
    else
        o.remove(key);

    QSaveFile out(path);
    if (!out.open(QIODevice::WriteOnly))
        return false;
    // Owner-only before any secret byte is written.
    out.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    out.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
    return out.commit();
}

// ── Platform stores ─────────────────────────────────────────────────────────

#if defined(TALOS_HAVE_DBUS)

namespace {

struct DBusSecret {
    QDBusObjectPath session;
    QByteArray parameters;
    QByteArray value;
    QString contentType;
};

QDBusArgument &operator<<(QDBusArgument &arg, const DBusSecret &s)
{
    arg.beginStructure();
    arg << s.session << s.parameters << s.value << s.contentType;
    arg.endStructure();
    return arg;
}

const QDBusArgument &operator>>(const QDBusArgument &arg, DBusSecret &s)
{
    arg.beginStructure();
    arg >> s.session >> s.parameters >> s.value >> s.contentType;
    arg.endStructure();
    return arg;
}

using SecretMap = QMap<QDBusObjectPath, DBusSecret>;
using StringMap = QMap<QString, QString>;

} // namespace
} // namespace talos

Q_DECLARE_METATYPE(talos::DBusSecret)
Q_DECLARE_METATYPE(talos::SecretMap)

namespace talos {
namespace {

const QString kBus = u"org.freedesktop.secrets"_s;
const QString kPath = u"/org/freedesktop/secrets"_s;
const QString kDefault = u"/org/freedesktop/secrets/aliases/default"_s;

class PromptWaiter : public QObject
{
    Q_OBJECT
public:
    PromptWaiter(QEventLoop *loop, bool *dismissed) : m_loop(loop), m_dismissed(dismissed) {}
public slots:
    void completed(bool dismissed, const QDBusVariant &)
    {
        *m_dismissed = dismissed;
        m_loop->quit();
    }

private:
    QEventLoop *m_loop;
    bool *m_dismissed;
};

/// One conversation with the Secret Service, using the unencrypted "plain"
/// session: the bus is the user's own and the peer is their keyring.
class SecretService
{
public:
    SecretService()
        : m_service(kBus, kPath, u"org.freedesktop.Secret.Service"_s,
                    QDBusConnection::sessionBus())
    {
        qDBusRegisterMetaType<DBusSecret>();
        qDBusRegisterMetaType<SecretMap>();
        qDBusRegisterMetaType<StringMap>();
        if (!m_service.isValid())
            return;
        const QDBusMessage reply = m_service.call(
            u"OpenSession"_s, u"plain"_s, QVariant::fromValue(QDBusVariant(QString())));
        if (reply.type() == QDBusMessage::ReplyMessage && reply.arguments().size() == 2)
            m_session = reply.arguments().at(1).value<QDBusObjectPath>();
    }

    bool ok() const { return !m_session.path().isEmpty(); }

    StringMap attributes(const QString &key) const
    {
        return {{u"application"_s, kService}, {u"key"_s, key}};
    }

    /// Wait out a keyring prompt (unlock, create). False when dismissed.
    bool prompt(const QDBusObjectPath &path)
    {
        if (path.path().isEmpty() || path.path() == "/"_L1)
            return true;
        QEventLoop loop;
        bool dismissed = true;
        QDBusConnection bus = QDBusConnection::sessionBus();
        PromptWaiter waiter(&loop, &dismissed);
        bus.connect(kBus, path.path(), u"org.freedesktop.Secret.Prompt"_s, u"Completed"_s,
                    &waiter, SLOT(completed(bool, QDBusVariant)));
        QDBusInterface p(kBus, path.path(), u"org.freedesktop.Secret.Prompt"_s, bus);
        p.call(u"Prompt"_s, QString());
        QTimer::singleShot(120'000, &loop, &QEventLoop::quit);
        loop.exec();
        return !dismissed;
    }

    bool unlock(const QDBusObjectPath &object)
    {
        const QDBusMessage reply = m_service.call(
            u"Unlock"_s, QVariant::fromValue(QList<QDBusObjectPath>{object}));
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().size() < 2)
            return false;
        return prompt(reply.arguments().at(1).value<QDBusObjectPath>());
    }

    QList<QDBusObjectPath> search(const QString &key, bool unlockLocked)
    {
        const QDBusMessage reply =
            m_service.call(u"SearchItems"_s, QVariant::fromValue(attributes(key)));
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().size() < 2)
            return {};
        auto items = qdbus_cast<QList<QDBusObjectPath>>(reply.arguments().at(0));
        const auto locked = qdbus_cast<QList<QDBusObjectPath>>(reply.arguments().at(1));
        if (unlockLocked) {
            for (const QDBusObjectPath &p : locked) {
                if (unlock(p))
                    items << p;
            }
        }
        return items;
    }

    bool read(const QString &key, QByteArray *value, bool *found)
    {
        *found = false;
        const auto items = search(key, true);
        if (items.isEmpty())
            return true;
        const QDBusMessage reply = m_service.call(
            u"GetSecrets"_s, QVariant::fromValue(items), QVariant::fromValue(m_session));
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty())
            return false;
        const SecretMap secrets = qdbus_cast<SecretMap>(reply.arguments().at(0));
        if (secrets.isEmpty())
            return false;
        *value = secrets.first().value;
        *found = true;
        return true;
    }

    bool write(const QString &key, const QByteArray &value)
    {
        const QDBusObjectPath collection(kDefault);
        unlock(collection);
        QDBusInterface c(kBus, kDefault, u"org.freedesktop.Secret.Collection"_s,
                         QDBusConnection::sessionBus());
        const QVariantMap properties{
            {u"org.freedesktop.Secret.Item.Label"_s, u"Talos: %1"_s.arg(key)},
            {u"org.freedesktop.Secret.Item.Attributes"_s, QVariant::fromValue(attributes(key))},
        };
        const DBusSecret secret{m_session, {}, value, u"application/octet-stream"_s};
        const QDBusMessage reply =
            c.call(u"CreateItem"_s, properties, QVariant::fromValue(secret), true);
        if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().size() < 2)
            return false;
        return prompt(reply.arguments().at(1).value<QDBusObjectPath>());
    }

    bool remove(const QString &key)
    {
        for (const QDBusObjectPath &item : search(key, false)) {
            QDBusInterface i(kBus, item.path(), u"org.freedesktop.Secret.Item"_s,
                             QDBusConnection::sessionBus());
            i.call(u"Delete"_s);
        }
        return true;
    }

private:
    QDBusInterface m_service;
    QDBusObjectPath m_session;
};

} // namespace

bool SecretStore::platformAvailable() const
{
    if (m_platformOk < 0) {
        m_platformOk = SecretService().ok() ? 1 : 0;
        if (!m_platformOk)
            AppLog::info("secrets", u"no Secret Service on the session bus; using a private file"_s);
    }
    return m_platformOk == 1;
}

bool SecretStore::platformWrite(const QString &key, const QByteArray &value)
{
    SecretService s;
    return s.ok() && s.write(key, value);
}

bool SecretStore::platformRead(const QString &key, QByteArray *value, bool *found)
{
    SecretService s;
    return s.ok() && s.read(key, value, found);
}

bool SecretStore::platformRemove(const QString &key)
{
    SecretService s;
    return s.ok() && s.remove(key);
}

QString SecretStore::backendName() const
{
    return !m_fileOnly && platformAvailable() ? u"the system keyring"_s
                                              : u"a private file on this device"_s;
}

#elif defined(Q_OS_MACOS)

namespace {
CFMutableDictionaryRef baseQuery(const QString &key)
{
    CFMutableDictionaryRef q = CFDictionaryCreateMutable(
        nullptr, 0, &kCFTypeDictionaryKeyCallBacks, &kCFTypeDictionaryValueCallBacks);
    CFStringRef service = kService.toCFString();
    CFStringRef account = key.toCFString();
    CFDictionarySetValue(q, kSecClass, kSecClassGenericPassword);
    CFDictionarySetValue(q, kSecAttrService, service);
    CFDictionarySetValue(q, kSecAttrAccount, account);
    CFRelease(service);
    CFRelease(account);
    return q;
}
} // namespace

bool SecretStore::platformAvailable() const { return true; }

bool SecretStore::platformWrite(const QString &key, const QByteArray &value)
{
    CFMutableDictionaryRef q = baseQuery(key);
    SecItemDelete(q);
    CFDataRef data = CFDataCreate(nullptr, reinterpret_cast<const UInt8 *>(value.constData()),
                                  value.size());
    CFDictionarySetValue(q, kSecValueData, data);
    const OSStatus rc = SecItemAdd(q, nullptr);
    CFRelease(data);
    CFRelease(q);
    return rc == errSecSuccess;
}

bool SecretStore::platformRead(const QString &key, QByteArray *value, bool *found)
{
    *found = false;
    CFMutableDictionaryRef q = baseQuery(key);
    CFDictionarySetValue(q, kSecReturnData, kCFBooleanTrue);
    CFDictionarySetValue(q, kSecMatchLimit, kSecMatchLimitOne);
    CFTypeRef result = nullptr;
    const OSStatus rc = SecItemCopyMatching(q, &result);
    CFRelease(q);
    if (rc == errSecItemNotFound)
        return true;
    if (rc != errSecSuccess || !result)
        return false;
    CFDataRef data = static_cast<CFDataRef>(result);
    *value = QByteArray(reinterpret_cast<const char *>(CFDataGetBytePtr(data)),
                        CFDataGetLength(data));
    CFRelease(result);
    *found = true;
    return true;
}

bool SecretStore::platformRemove(const QString &key)
{
    CFMutableDictionaryRef q = baseQuery(key);
    const OSStatus rc = SecItemDelete(q);
    CFRelease(q);
    return rc == errSecSuccess || rc == errSecItemNotFound;
}

QString SecretStore::backendName() const
{
    return m_fileOnly ? u"a private file on this device"_s : u"the macOS Keychain"_s;
}

#elif defined(Q_OS_WIN)

// DPAPI seals a blob to the signed-in Windows user; the sealed blob itself
// is kept in the same JSON file the fallback uses.
bool SecretStore::platformAvailable() const { return true; }

bool SecretStore::platformWrite(const QString &key, const QByteArray &value)
{
    DATA_BLOB in{DWORD(value.size()), reinterpret_cast<BYTE *>(const_cast<char *>(value.constData()))};
    DATA_BLOB out{};
    if (!CryptProtectData(&in, L"Talos", nullptr, nullptr, nullptr,
                          CRYPTPROTECT_UI_FORBIDDEN, &out))
        return false;
    const QByteArray sealed(reinterpret_cast<const char *>(out.pbData), int(out.cbData));
    LocalFree(out.pbData);
    return fileWrite(u"dpapi:"_s + key, &sealed);
}

bool SecretStore::platformRead(const QString &key, QByteArray *value, bool *found)
{
    const QByteArray sealed = fileRead(u"dpapi:"_s + key, found);
    if (!*found)
        return true;
    DATA_BLOB in{DWORD(sealed.size()), reinterpret_cast<BYTE *>(const_cast<char *>(sealed.constData()))};
    DATA_BLOB out{};
    if (!CryptUnprotectData(&in, nullptr, nullptr, nullptr, nullptr,
                            CRYPTPROTECT_UI_FORBIDDEN, &out)) {
        *found = false;
        return false;
    }
    *value = QByteArray(reinterpret_cast<const char *>(out.pbData), int(out.cbData));
    SecureZeroMemory(out.pbData, out.cbData);
    LocalFree(out.pbData);
    return true;
}

bool SecretStore::platformRemove(const QString &key)
{
    return fileWrite(u"dpapi:"_s + key, nullptr);
}

QString SecretStore::backendName() const
{
    return m_fileOnly ? u"a private file on this device"_s
                      : u"Windows data protection (this user account)"_s;
}

#else

// Android and anything else: the app's private storage is the store.
bool SecretStore::platformAvailable() const { return false; }
bool SecretStore::platformWrite(const QString &, const QByteArray &) { return false; }
bool SecretStore::platformRead(const QString &, QByteArray *, bool *) { return false; }
bool SecretStore::platformRemove(const QString &) { return false; }

QString SecretStore::backendName() const
{
#  if defined(Q_OS_ANDROID)
    return u"this app's private storage"_s;
#  else
    return u"a private file on this device"_s;
#  endif
}

#endif

// ── Public API ──────────────────────────────────────────────────────────────

bool SecretStore::write(const QString &key, const QByteArray &value)
{
    if (!m_fileOnly && platformAvailable()) {
        if (platformWrite(key, value)) {
            // A value that moved into the keyring must not linger in the file.
#if !defined(Q_OS_WIN)
            fileWrite(key, nullptr);
#endif
            return true;
        }
        AppLog::warn("secrets", u"platform store refused a write; using the private file"_s);
    }
    return fileWrite(key, &value);
}

QByteArray SecretStore::read(const QString &key, bool *found)
{
    bool have = false;
    if (!m_fileOnly && platformAvailable()) {
        QByteArray value;
        if (platformRead(key, &value, &have) && have) {
            if (found)
                *found = true;
            return value;
        }
    }
    return fileRead(key, found);
}

void SecretStore::moveKey(const QString &key, bool toFile)
{
    bool found = false;
    QByteArray value;
    if (toFile) {
        if (!platformAvailable() || !platformRead(key, &value, &found) || !found)
            return;
        if (fileWrite(key, &value))
            platformRemove(key);
    } else {
        value = fileRead(key, &found);
        if (!found || !platformAvailable())
            return;
        if (platformWrite(key, value))
            fileWrite(key, nullptr);
    }
}

void SecretStore::remove(const QString &key)
{
    if (!m_fileOnly && platformAvailable())
        platformRemove(key);
    fileWrite(key, nullptr);
}

} // namespace talos

#if defined(TALOS_HAVE_DBUS)
#  include "SecretStore.moc"
#endif
