// SPDX-License-Identifier: GPL-2.0-only
#include "app/AppLock.h"

#include "core/AppLog.h"
#include "core/Crypto.h"
#include "core/SecretStore.h"
#include "core/Store.h"
#include "mesh/Platform.h"

#include <QDateTime>
#include <QFutureWatcher>
#include <QJSEngine>
#include <QJsonDocument>
#include <QJsonObject>
#include <QtConcurrentRun>

namespace talos {

using namespace Qt::StringLiterals;

namespace {
const QString kRecordKey = u"applock"_s;

qint64 nowMs()
{
    return QDateTime::currentMSecsSinceEpoch();
}

int backoffSeconds(int failures)
{
    if (failures <= 0)
        return 0;
    return int(qMin<qint64>(60, 1LL << qMin(failures - 1, 6)));
}
} // namespace

AppLock::AppLock(Store *store, QObject *parent) : QObject(parent), m_store(store)
{
    load();
    m_lastActivityMs = nowMs();

    m_idle.setInterval(10'000);
    connect(&m_idle, &QTimer::timeout, this, [this] {
        const int minutes = m_store->prefInt(u"lockTimeoutMinutes"_s);
        if (!m_enabled || locked() || minutes <= 0)
            return;
        if (nowMs() - m_lastActivityMs > qint64(minutes) * 60'000)
            lock();
    });
    m_idle.start();

    m_ticker.setInterval(1000);
    connect(&m_ticker, &QTimer::timeout, this, [this] {
        emit tick();
        if (retryInSeconds() == 0)
            m_ticker.stop();
    });
    if (retryInSeconds() > 0)
        m_ticker.start();
}

QString AppLock::validate(const QString &passcode)
{
    if (passcode.size() < kMinPasscodeLength)
        return u"Use at least %1 characters."_s.arg(kMinPasscodeLength);
    if (passcode.size() > 256)
        return u"That is too long."_s;
    return {};
}

void AppLock::load()
{
    bool found = false;
    const QByteArray raw = SecretStore::instance().read(kRecordKey, &found);
    const QJsonObject o = found ? QJsonDocument::fromJson(raw).object() : QJsonObject();
    m_record.salt = QByteArray::fromBase64(o["salt"_L1].toString().toLatin1());
    m_record.memoryKiB = quint32(o["memoryKiB"_L1].toInt());
    m_record.passes = quint32(o["passes"_L1].toInt());
    m_record.wrapped = QByteArray::fromBase64(o["wrapped"_L1].toString().toLatin1());
    m_record.check = QByteArray::fromBase64(o["check"_L1].toString().toLatin1());
    m_record.boxPublic = QByteArray::fromBase64(o["boxPublic"_L1].toString().toLatin1());
    m_record.boxSecret = QByteArray::fromBase64(o["boxSecret"_L1].toString().toLatin1());
    m_failures = o["failures"_L1].toInt();
    m_lastFailureMs = qint64(o["lastFailureMs"_L1].toDouble());
    m_enabled = !m_record.wrapped.isEmpty() && !m_record.salt.isEmpty();
}

void AppLock::save() const
{
    if (!m_enabled) {
        SecretStore::instance().remove(kRecordKey);
        return;
    }
    const QJsonObject o{
        {"v"_L1, 1},
        {"salt"_L1, QString::fromLatin1(m_record.salt.toBase64())},
        {"memoryKiB"_L1, int(m_record.memoryKiB)},
        {"passes"_L1, int(m_record.passes)},
        {"wrapped"_L1, QString::fromLatin1(m_record.wrapped.toBase64())},
        {"check"_L1, QString::fromLatin1(m_record.check.toBase64())},
        {"boxPublic"_L1, QString::fromLatin1(m_record.boxPublic.toBase64())},
        {"boxSecret"_L1, QString::fromLatin1(m_record.boxSecret.toBase64())},
        {"failures"_L1, m_failures},
        {"lastFailureMs"_L1, double(m_lastFailureMs)},
    };
    SecretStore::instance().write(kRecordKey, QJsonDocument(o).toJson(QJsonDocument::Compact));
}

int AppLock::retryInSeconds() const
{
    const qint64 until = m_lastFailureMs + qint64(backoffSeconds(m_failures)) * 1000;
    const qint64 left = until - nowMs();
    // A clock set back must not lock the user out for longer than the cap.
    return int(qBound<qint64>(qint64(0), (left + 999) / 1000, qint64(60)));
}

void AppLock::reply(const QJSValue &cb, bool ok, const QString &error) const
{
    if (!m_engine || !cb.isCallable())
        return;
    QJSValue fn = cb;
    fn.call({m_engine->toScriptValue(QVariantMap{{u"ok"_s, ok}, {u"error"_s, error}})});
}

void AppLock::setBusy(bool busy)
{
    m_busy = busy;
    emit changed();
}

void AppLock::derive(const QString &passcode, const Record &record,
                     std::function<void(const QByteArray &)> then)
{
    // Argon2id is deliberately slow and memory-hungry; keep it off the UI
    // thread so the lock screen stays alive while it runs.
    setBusy(true);
    auto *watcher = new QFutureWatcher<QByteArray>(this);
    connect(watcher, &QFutureWatcher<QByteArray>::finished, this, [this, watcher, then] {
        watcher->deleteLater();
        QByteArray kek = watcher->result();
        m_busy = false;
        then(kek);
        crypto::wipe(kek);
        emit changed();
    });
    watcher->setFuture(QtConcurrent::run(
        [pass = passcode.toUtf8(), salt = record.salt, mem = record.memoryKiB,
         passes = record.passes] { return crypto::argon2id(pass, salt, mem, passes); }));
}

void AppLock::enable(const QString &passcode, QJSValue done)
{
    if (m_enabled || m_busy) {
        reply(done, false, u"App lock is already on."_s);
        return;
    }
    if (const QString problem = validate(passcode); !problem.isEmpty()) {
        reply(done, false, problem);
        return;
    }
    Record record;
    record.salt = crypto::randomBytes(crypto::kSaltBytes);
    // 64 MiB on desktop, 32 MiB on phones; three passes either way.
    record.memoryKiB = platform::isMobile() ? 32 * 1024 : 64 * 1024;
    record.passes = 3;
    derive(passcode, record, [this, record, done](const QByteArray &kek) mutable {
        if (kek.isEmpty()) {
            reply(done, false, u"Couldn't set up the lock on this device."_s);
            return;
        }
        m_dataKey = crypto::randomBytes(crypto::kKeyBytes);
        record.wrapped = crypto::seal(kek, m_dataKey, "talos-applock-v1");
        record.check = crypto::seal(m_dataKey, "talos", "talos-applock-check");
        m_record = record;
        m_enabled = true;
        m_failures = 0;
        m_lastFailureMs = 0;
        m_lastActivityMs = nowMs();
        openBoxKey();
        save();
        AppLog::info("lock", u"app lock enabled"_s);
        reply(done, true);
        emit unlocked();
    });
}

void AppLock::noteFailure()
{
    ++m_failures;
    m_lastFailureMs = nowMs();
    save();
    m_ticker.start();
    emit tick();
    if (m_failures >= kEraseAfter && m_store->prefBool(u"lockEraseAfterFailures"_s)) {
        AppLog::warn("lock", u"too many failed attempts; erasing the connection"_s);
        reset();
        emit eraseRequested();
    }
}

void AppLock::unlock(const QString &passcode, QJSValue done)
{
    if (!m_enabled || !locked()) {
        reply(done, true);
        return;
    }
    if (m_busy)
        return;
    if (const int wait = retryInSeconds(); wait > 0) {
        reply(done, false, u"Try again in %1 s."_s.arg(wait));
        return;
    }
    derive(passcode, m_record, [this, done](const QByteArray &kek) {
        const auto key = crypto::open(kek, m_record.wrapped, "talos-applock-v1");
        if (!key) {
            noteFailure();
            reply(done, false, m_enabled ? u"That passcode isn't right."_s
                                         : u"Too many attempts. The connection was erased."_s);
            return;
        }
        m_dataKey = *key;
        m_lastActivityMs = nowMs();
        const bool hadBox = !m_record.boxPublic.isEmpty();
        openBoxKey();
        // Records made before biometric unlock, or before credentials were
        // sealed, gain what they lack here.
        if (m_failures != 0 || m_record.check.isEmpty() || !hadBox) {
            m_failures = 0;
            m_lastFailureMs = 0;
            m_record.check = crypto::seal(m_dataKey, "talos", "talos-applock-check");
            save();
        }
        reply(done, true);
        emit unlocked();
    });
}

void AppLock::change(const QString &current, const QString &next, QJSValue done)
{
    if (!m_enabled || m_busy)
        return;
    if (const QString problem = validate(next); !problem.isEmpty()) {
        reply(done, false, problem);
        return;
    }
    if (const int wait = retryInSeconds(); wait > 0) {
        reply(done, false, u"Try again in %1 s."_s.arg(wait));
        return;
    }
    derive(current, m_record, [this, next, done](const QByteArray &kek) {
        const auto key = crypto::open(kek, m_record.wrapped, "talos-applock-v1");
        if (!key) {
            noteFailure();
            reply(done, false, u"The current passcode isn't right."_s);
            return;
        }
        // Same data key under a new passcode: nothing sealed needs redoing.
        Record record = m_record;
        record.salt = crypto::randomBytes(crypto::kSaltBytes);
        const QByteArray dataKey = *key;
        derive(next, record, [this, record, dataKey, done](const QByteArray &fresh) mutable {
            // Wrapping under nothing would leave a lock no passcode opens.
            if (fresh.isEmpty()) {
                reply(done, false, u"Couldn't change the passcode on this device."_s);
                return;
            }
            record.wrapped = crypto::seal(fresh, dataKey, "talos-applock-v1");
            m_record = record;
            m_dataKey = dataKey;
            m_failures = 0;
            save();
            reply(done, true);
        });
    });
}

void AppLock::disable(const QString &passcode, QJSValue done)
{
    if (!m_enabled || m_busy)
        return;
    if (const int wait = retryInSeconds(); wait > 0) {
        reply(done, false, u"Try again in %1 s."_s.arg(wait));
        return;
    }
    derive(passcode, m_record, [this, done](const QByteArray &kek) {
        if (!crypto::open(kek, m_record.wrapped, "talos-applock-v1")) {
            noteFailure();
            reply(done, false, u"That passcode isn't right."_s);
            return;
        }
        // While the lock can still open what it sealed.
        emit aboutToDisable();
        reset();
        reply(done, true);
    });
}

void AppLock::lock()
{
    if (!m_enabled || locked())
        return;
    crypto::wipe(m_dataKey);
    crypto::wipe(m_boxSecret);
    emit changed();
    emit lockedNow();
}

void AppLock::openBoxKey()
{
    if (m_dataKey.isEmpty())
        return;
    if (!m_record.boxPublic.isEmpty()) {
        if (const auto secret = crypto::open(m_dataKey, m_record.boxSecret, "talos-applock-box"))
            m_boxSecret = *secret;
        return;
    }
    crypto::BoxKeys keys = crypto::boxKeys();
    m_record.boxPublic = keys.publicKey;
    m_record.boxSecret = crypto::seal(m_dataKey, keys.secretKey, "talos-applock-box");
    m_boxSecret = keys.secretKey;
}

QByteArray AppLock::sealSecret(const QByteArray &plain) const
{
    return canSeal() ? crypto::sealTo(m_record.boxPublic, plain) : QByteArray();
}

std::optional<QByteArray> AppLock::openSecret(const QByteArray &sealed) const
{
    if (m_boxSecret.isEmpty())
        return std::nullopt;
    return crypto::openBox(m_boxSecret, sealed);
}

bool AppLock::biometricEnabled() const
{
    return m_enabled && m_store->prefBool(u"lockBiometric"_s) && m_bio.enrolled();
}

void AppLock::enableBiometric(QJSValue done)
{
    if (!m_enabled || locked()) {
        reply(done, false, u"Unlock the app first."_s);
        return;
    }
    m_bio.seal(m_dataKey, [this, done](bool ok, const QString &error) {
        if (ok)
            m_store->setPref(u"lockBiometric"_s, true);
        emit changed();
        reply(done, ok, ok ? QString() : error);
    });
}

void AppLock::disableBiometric()
{
    m_bio.clear();
    m_store->setPref(u"lockBiometric"_s, false);
    emit changed();
}

void AppLock::unlockWithBiometric(QJSValue done)
{
    if (!locked()) {
        reply(done, true);
        return;
    }
    if (!biometricEnabled()) {
        reply(done, false, u"Biometric unlock isn't set up."_s);
        return;
    }
    m_bio.open([this, done](const QByteArray &key, const QString &error) {
        // The released key must open the check sealed under the real one.
        if (key.isEmpty() || !crypto::open(key, m_record.check, "talos-applock-check")) {
            reply(done, false, error.isEmpty() || error.startsWith("__"_L1)
                                   ? u"That didn't unlock it. Use the passcode."_s : error);
            return;
        }
        m_dataKey = key;
        m_lastActivityMs = nowMs();
        const bool hadBox = !m_record.boxPublic.isEmpty();
        openBoxKey();
        if (!hadBox)
            save();
        reply(done, true);
        emit changed();
        emit unlocked();
    });
}

void AppLock::reset()
{
    m_bio.clear();
    m_store->setPref(u"lockBiometric"_s, false);
    crypto::wipe(m_dataKey);
    crypto::wipe(m_boxSecret);
    m_enabled = false;
    m_record = {};
    m_failures = 0;
    m_lastFailureMs = 0;
    save();
    emit changed();
    emit tick();
}

void AppLock::touch()
{
    m_lastActivityMs = nowMs();
}

void AppLock::setAppActive(bool active)
{
    if (!active) {
        if (m_hiddenSinceMs == 0)
            m_hiddenSinceMs = nowMs();
        return;
    }
    const qint64 away = m_hiddenSinceMs ? nowMs() - m_hiddenSinceMs : 0;
    m_hiddenSinceMs = 0;
    const int minutes = m_store->prefInt(u"lockTimeoutMinutes"_s);
    if (m_enabled && minutes > 0 && away > qint64(minutes) * 60'000)
        lock();
    else
        touch();
}

} // namespace talos
