// SPDX-License-Identifier: GPL-2.0-only
#include "app/Biometric.h"

#include "core/AppLog.h"
#include "core/SecretStore.h"

#include <QCoreApplication>
#include <QTimer>

#if defined(Q_OS_ANDROID)
#  include <QHash>
#  include <QJniEnvironment>
#  include <QJniObject>
#  include <QMutex>
#elif defined(TALOS_HAVE_DBUS)
#  include <QDBusConnection>
#  include <QDBusInterface>
#  include <QDBusObjectPath>
#  include <QDBusReply>
#endif

namespace talos {

using namespace Qt::StringLiterals;

Biometric::Biometric(QObject *parent) : QObject(parent) {}

#if defined(Q_OS_ANDROID)

// ── Android: keystore key released by BiometricPrompt ───────────────────────

namespace {

const char kClass[] = "dev/talos/client/Biometric";

QMutex g_mutex;
qint64 g_nextHandle = 1;
QHash<qint64, Biometric::SealDone> g_sealers;
QHash<qint64, Biometric::OpenDone> g_openers;

// Called by Java on Android's UI thread; hand the result to Qt's.
void jniSealed(JNIEnv *env, jclass, jlong handle, jboolean ok, jstring error)
{
    const QString why = error ? QJniObject(error).toString() : QString();
    Q_UNUSED(env);
    QMetaObject::invokeMethod(qApp, [handle, ok, why] {
        Biometric::SealDone done;
        {
            QMutexLocker lock(&g_mutex);
            done = g_sealers.take(handle);
        }
        if (done)
            done(ok, why);
    }, Qt::QueuedConnection);
}

void jniOpened(JNIEnv *env, jclass, jlong handle, jbyteArray secret, jstring error)
{
    QByteArray key;
    if (secret) {
        const jsize n = env->GetArrayLength(secret);
        key.resize(n);
        env->GetByteArrayRegion(secret, 0, n, reinterpret_cast<jbyte *>(key.data()));
    }
    const QString why = error ? QJniObject(error).toString() : QString();
    QMetaObject::invokeMethod(qApp, [handle, key, why] {
        Biometric::OpenDone done;
        {
            QMutexLocker lock(&g_mutex);
            done = g_openers.take(handle);
        }
        if (done)
            done(key, why);
    }, Qt::QueuedConnection);
}

void registerNatives()
{
    static bool done = false;
    if (done)
        return;
    done = true;
    const JNINativeMethod methods[] = {
        {"sealed", "(JZLjava/lang/String;)V", reinterpret_cast<void *>(jniSealed)},
        {"opened", "(J[BLjava/lang/String;)V", reinterpret_cast<void *>(jniOpened)},
    };
    QJniEnvironment env;
    env.registerNativeMethods(kClass, methods, 2);
}

QJniObject activity()
{
    return QNativeInterface::QAndroidApplication::context();
}

} // namespace

bool Biometric::available() const
{
    return QJniObject::callStaticMethod<jboolean>(kClass, "available",
                                                  "(Landroid/content/Context;)Z",
                                                  activity().object());
}

QString Biometric::unavailableReason() const
{
    return available() ? QString()
                       : u"No fingerprint or face is set up on this device (a strong "
                         "biometric is required)."_s;
}

QString Biometric::name() const
{
    return u"Fingerprint or face"_s;
}

bool Biometric::enrolled() const
{
    return QJniObject::callStaticMethod<jboolean>(kClass, "enrolled",
                                                  "(Landroid/content/Context;)Z",
                                                  activity().object());
}

void Biometric::seal(const QByteArray &key, SealDone done)
{
    registerNatives();
    qint64 handle;
    {
        QMutexLocker lock(&g_mutex);
        handle = g_nextHandle++;
        g_sealers.insert(handle, std::move(done));
    }
    QJniEnvironment env;
    jbyteArray bytes = env->NewByteArray(key.size());
    env->SetByteArrayRegion(bytes, 0, key.size(), reinterpret_cast<const jbyte *>(key.constData()));
    QJniObject::callStaticMethod<void>(kClass, "seal", "(Landroid/app/Activity;[BJ)V",
                                       activity().object(), bytes, jlong(handle));
    env->DeleteLocalRef(bytes);
}

void Biometric::open(OpenDone done)
{
    registerNatives();
    qint64 handle;
    {
        QMutexLocker lock(&g_mutex);
        handle = g_nextHandle++;
        g_openers.insert(handle, std::move(done));
    }
    QJniObject::callStaticMethod<void>(kClass, "open", "(Landroid/app/Activity;J)V",
                                       activity().object(), jlong(handle));
}

void Biometric::cancel()
{
    QJniObject::callStaticMethod<void>(kClass, "cancel", "()V");
}

void Biometric::clear()
{
    QJniObject::callStaticMethod<void>(kClass, "clear", "(Landroid/content/Context;)V",
                                       activity().object());
}

void Biometric::verifyStatus(const QString &, bool) {}
void Biometric::finishVerify(bool, const QString &) {}

#elif defined(TALOS_HAVE_DBUS)

// ── Linux: fprintd in front of the login keyring ────────────────────────────

namespace {

const QString kFprint = u"net.reactivated.Fprint"_s;
const QString kDeviceIface = u"net.reactivated.Fprint.Device"_s;
const QString kBioKey = u"applock-bio"_s;

QString userName()
{
    return qEnvironmentVariable("USER");
}

/// The default reader, when there is one with a finger enrolled.
QString usableDevice()
{
    QDBusInterface manager(kFprint, u"/net/reactivated/Fprint/Manager"_s,
                           u"net.reactivated.Fprint.Manager"_s, QDBusConnection::systemBus());
    if (!manager.isValid())
        return {};
    const QDBusReply<QDBusObjectPath> device = manager.call(u"GetDefaultDevice"_s);
    if (!device.isValid())
        return {};
    QDBusInterface reader(kFprint, device.value().path(), kDeviceIface,
                          QDBusConnection::systemBus());
    const QDBusReply<QStringList> fingers = reader.call(u"ListEnrolledFingers"_s, userName());
    return fingers.isValid() && !fingers.value().isEmpty() ? device.value().path() : QString();
}

} // namespace

bool Biometric::available() const
{
    // The key has to rest somewhere a fingerprint is not the only guard.
    return !SecretStore::instance().fileOnly() && !usableDevice().isEmpty();
}

QString Biometric::unavailableReason() const
{
    if (SecretStore::instance().fileOnly())
        return u"Needs the system keyring. Credentials are kept in a private file here "
               "(as they are while the background service is installed), which would leave "
               "the key with no guard but the fingerprint check."_s;
    if (usableDevice().isEmpty())
        return u"No fingerprint reader with an enrolled finger was found. Enrol one in the "
               "system's user settings."_s;
    return {};
}

QString Biometric::name() const
{
    return u"Fingerprint"_s;
}

bool Biometric::enrolled() const
{
    bool found = false;
    if (!SecretStore::instance().fileOnly())
        SecretStore::instance().read(kBioKey, &found);
    return found;
}

void Biometric::seal(const QByteArray &key, SealDone done)
{
    if (!available()) {
        done(false, unavailableReason());
        return;
    }
    // Prove the finger works before relying on it.
    open([key, done](const QByteArray &, const QString &error) {
        // open() answers "nothing enrolled here" when the finger matched
        // but no key is stored yet, which is exactly the state we are in.
        if (!error.isEmpty() && error != "__matched"_L1) {
            done(false, error);
            return;
        }
        done(SecretStore::instance().write(kBioKey, key),
             u"Couldn't store the key in the keyring."_s);
    });
}

void Biometric::open(OpenDone done)
{
    if (m_verifying) {
        done({}, u"A fingerprint check is already running."_s);
        return;
    }
    m_device = usableDevice();
    if (m_device.isEmpty()) {
        done({}, unavailableReason());
        return;
    }
    QDBusConnection bus = QDBusConnection::systemBus();
    QDBusInterface reader(kFprint, m_device, kDeviceIface, bus);
    const QDBusReply<void> claimed = reader.call(u"Claim"_s, userName());
    if (!claimed.isValid()) {
        done({}, u"The fingerprint reader is busy."_s);
        return;
    }
    bus.connect(kFprint, m_device, kDeviceIface, u"VerifyStatus"_s, this,
                SLOT(verifyStatus(QString, bool)));
    const QDBusReply<void> started = reader.call(u"VerifyStart"_s, u"any"_s);
    if (!started.isValid()) {
        reader.call(u"Release"_s);
        bus.disconnect(kFprint, m_device, kDeviceIface, u"VerifyStatus"_s, this,
                       SLOT(verifyStatus(QString, bool)));
        done({}, u"Couldn't start the fingerprint reader."_s);
        return;
    }
    m_pending = std::move(done);
    m_verifying = true;
    QTimer::singleShot(30'000, this, [this, device = m_device] {
        if (m_verifying && device == m_device)
            finishVerify(false, u"No finger was presented."_s);
    });
}

void Biometric::verifyStatus(const QString &result, bool done)
{
    if (!m_verifying)
        return;
    if (result == "verify-match"_L1)
        finishVerify(true, {});
    else if (result == "verify-no-match"_L1)
        finishVerify(false, u"That fingerprint wasn't recognised."_s);
    else if (done)
        finishVerify(false, u"The fingerprint couldn't be read."_s);
    // Anything else (swipe too short, retry) leaves the reader waiting.
}

void Biometric::finishVerify(bool ok, const QString &error)
{
    if (!m_verifying)
        return;
    m_verifying = false;
    QDBusConnection bus = QDBusConnection::systemBus();
    bus.disconnect(kFprint, m_device, kDeviceIface, u"VerifyStatus"_s, this,
                   SLOT(verifyStatus(QString, bool)));
    QDBusInterface reader(kFprint, m_device, kDeviceIface, bus);
    reader.call(u"VerifyStop"_s);
    reader.call(u"Release"_s);
    const OpenDone done = std::move(m_pending);
    m_pending = nullptr;
    if (!done)
        return;
    if (!ok) {
        done({}, error);
        return;
    }
    bool found = false;
    const QByteArray key = SecretStore::instance().read(kBioKey, &found);
    if (found)
        done(key, {});
    else
        done({}, u"__matched"_s);
}

void Biometric::cancel()
{
    finishVerify(false, u"Cancelled"_s);
}

void Biometric::clear()
{
    SecretStore::instance().remove(kBioKey);
}

#elif !defined(Q_OS_MACOS) // the Mac's is BiometricMac.mm

// ── No backend on this platform yet ─────────────────────────────────────────

bool Biometric::available() const { return false; }
QString Biometric::unavailableReason() const
{
    return u"Biometric unlock isn't available on this platform yet."_s;
}
QString Biometric::name() const { return u"Biometrics"_s; }
bool Biometric::enrolled() const { return false; }
void Biometric::seal(const QByteArray &, SealDone done) { done(false, unavailableReason()); }
void Biometric::open(OpenDone done) { done({}, unavailableReason()); }
void Biometric::cancel() {}
void Biometric::clear() {}
void Biometric::verifyStatus(const QString &, bool) {}
void Biometric::finishVerify(bool, const QString &) {}

#endif

} // namespace talos
