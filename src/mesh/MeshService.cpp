// SPDX-License-Identifier: GPL-2.0-only
#include "mesh/MeshService.h"

#include "core/AppLog.h"
#include "core/Files.h"
#include "core/BridgeClient.h"
#include "core/Session.h"
#include "core/Store.h"
#include "mesh/Platform.h"
#include "mesh/SelfUpdate.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QRegularExpression>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QtConcurrentRun>
#include <memory>

#if defined(TALOS_HAVE_POSITIONING)
#  include <QGeoPositionInfoSource>
#  include <QPermissions>
#endif

namespace talos {

using namespace Qt::StringLiterals;

namespace {

qint64 nowMs()
{
    return QDateTime::currentMSecsSinceEpoch();
}

QUrlQuery transferQuery(const QString &token, const QString &deviceId)
{
    QUrlQuery q;
    q.addQueryItem(u"transfer"_s, QString::fromLatin1(QUrl::toPercentEncoding(token)));
    if (!deviceId.isEmpty())
        q.addQueryItem(u"deviceId"_s, QString::fromLatin1(QUrl::toPercentEncoding(deviceId)));
    return q;
}

QByteArray sha256File(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QCryptographicHash hash(QCryptographicHash::Sha256);
    hash.addData(&f);
    return hash.result().toHex();
}

/// What a command acted on, for the audit trail. Never the payload: no
/// file bodies, no transfer tokens.
QString auditDetail(const QString &name, const QJsonObject &params)
{
    if (name == "exec"_L1) {
        QString cmd = params["cmd"_L1].toString().simplified();
        if (cmd.size() > 240)
            cmd = cmd.left(239) + u'…';
        return cmd;
    }
    if (name == "move"_L1)
        return params["from"_L1].toString() + u" → "_s + params["to"_L1].toString();
    if (name == "ring"_L1)
        return params["message"_L1].toString();
    return params["path"_L1].toString();
}

} // namespace

MeshService::MeshService(Store *store, Session *session, QObject *parent)
    : QObject(parent), m_store(store), m_session(session),
      m_lock(QDir(store->dataDir()).filePath(u"mesh.lock"_s))
{
    m_lock.setStaleLockTime(0); // held for the life of the process
    m_ownerRetry.setInterval(20'000);
    connect(&m_ownerRetry, &QTimer::timeout, this, [this] {
        // The other process went away: pick the mesh up.
        if (!m_owner && m_client && sharing() && acquire()) {
            AppLog::info("mesh", u"taking over the mesh"_s);
            registerNow();
            configurePeriodic();
            emit changed();
        }
    });
    m_ownerRetry.start();
    m_exec.writeLimit = [this] {
        const qint64 gib = qBound(1, m_store->prefInt(u"meshMaxWriteGiB"_s), 1024);
        return gib * 1024 * 1024 * 1024;
    };
    m_heartbeat.setSingleShot(true);
    connect(&m_heartbeat, &QTimer::timeout, this, [this] {
        if (sharing() && m_client)
            registerNow();
    });
    connect(&m_periodic, &QTimer::timeout, this, [this] { sendFix(false); });

    connect(m_store, &Store::prefChanged, this, [this](const QString &key) {
        if (key.startsWith("mesh"_L1) || key.startsWith("deviceControl/"_L1))
            reconfigure();
    });
    connect(m_store, &Store::profilesChanged, this, &MeshService::changed);
    connect(session, &Session::clientReady, this, &MeshService::attach);
    connect(session, &Session::clientGone, this, &MeshService::detach);
}

QStringList MeshService::coreCapabilities()
{
    return {u"locate"_s, u"ring"_s, u"status"_s};
}

QStringList MeshService::capabilitiesFor(bool deviceControl)
{
    QStringList caps = coreCapabilities();
    if (deviceControl)
        caps << DeviceExec::capabilities() << u"upload_file"_s << u"download_file"_s;
    return caps;
}

QStringList MeshService::capabilities() const
{
    const bool control = deviceControl();
    QStringList caps = capabilitiesFor(control);
    if (control && canUpdateSelf())
        caps << u"update_node"_s;
#if defined(Q_OS_ANDROID)
    if (control)
        caps << u"install_apk"_s;
#endif
    if (control && computerHandler && m_store->prefBool(u"meshScreenControl"_s))
        caps << u"computer"_s;
    return caps;
}

bool MeshService::canUpdateSelf() const
{
    return !sandboxed() && !platform::isMobile() && selfupdate::canReplaceSelf();
}

bool MeshService::acquire()
{
    if (m_owner)
        return true;
    if (QDateTime::currentMSecsSinceEpoch() < m_yieldedUntil)
        return false;
    m_owner = m_lock.tryLock(0);
    return m_owner;
}

void MeshService::yield()
{
    m_yieldedUntil = QDateTime::currentMSecsSinceEpoch() + 90'000;
    if (!m_owner)
        return;
    m_heartbeat.stop();
    m_periodic.stop();
    m_registered = false;
    m_queue.clear();
    m_owner = false;
    m_lock.unlock();
    emit changed();
}

bool MeshService::needsApproval(const QString &command)
{
    return DeviceExec::capabilities().contains(command) || command == "upload_file"_L1
        || command == "download_file"_L1 || command == "update_node"_L1
        || command == "computer"_L1 || command == "install_apk"_L1;
}

int MeshService::heartbeatDelayMs(int failures)
{
    if (failures <= 0)
        return kHeartbeatMs;
    // 30 s, 60 s, 2 min, 4 min, then capped.
    const qint64 ms = 30'000LL << qMin(failures - 1, 10);
    return int(qMin<qint64>(ms, kMaxRegisterBackoffMs));
}

bool MeshService::sharing() const
{
    return m_store->prefBool(u"meshSharing"_s);
}

bool MeshService::sandboxed() const
{
    return platform::isSandboxed();
}

bool MeshService::deviceControl() const
{
    return !sandboxed() && m_store->deviceControl(m_store->activeProfileMeta().bridgeKey());
}

void MeshService::setDeviceControl(bool on)
{
    m_store->setDeviceControl(m_store->activeProfileMeta().bridgeKey(), on && !sandboxed());
}

bool MeshService::locationAvailable() const
{
#if defined(TALOS_HAVE_POSITIONING)
    return !QGeoPositionInfoSource::availableSources().isEmpty();
#else
    return false;
#endif
}

QString MeshService::deviceId() const
{
    return m_store->prefString(u"meshDeviceId"_s);
}

QString MeshService::deviceName() const
{
    return platform::deviceName();
}

QString MeshService::state() const
{
    if (!sharing())
        return u"Off"_s;
    if (!m_client)
        return u"Waiting for a connection"_s;
    if (!m_owner)
        return u"Handled by Talos running in the background"_s;
    if (m_registered)
        return u"Registered with Talon"_s;
    return m_lastError.isEmpty() ? u"Registering…"_s : m_lastError;
}

// ── Lifecycle ───────────────────────────────────────────────────────────────

void MeshService::attach(BridgeClient *client)
{
    detach();
    m_client = client;
    if (!client)
        return;
    connect(client, &BridgeClient::eventReceived, this, &MeshService::handleEvent);
    m_registerFailures = 0;
    if (sharing() && acquire()) {
        registerNow();
        configurePeriodic();
    }
    emit changed();
}

void MeshService::detach()
{
    if (m_client)
        m_client->disconnect(this);
    m_client = nullptr;
    m_heartbeat.stop();
    m_periodic.stop();
    m_registered = false;
    m_queue.clear();
    emit changed();
}

void MeshService::reconfigure()
{
    if (!m_client) {
        emit changed();
        return;
    }
    if (!sharing()) {
        m_heartbeat.stop();
        m_periodic.stop();
        m_registered = false;
        emit changed();
        return;
    }
    if (!acquire()) {
        emit changed();
        return;
    }
    // Re-register so the daemon sees capabilities appear or disappear.
    registerNow();
    configurePeriodic();
    emit changed();
}

void MeshService::scheduleHeartbeat(int delayMs)
{
    if (m_client && sharing())
        m_heartbeat.start(delayMs);
}

void MeshService::configurePeriodic()
{
    m_periodic.stop();
    if (!sharing() || !m_store->prefBool(u"meshPeriodic"_s))
        return;
    m_periodic.start(qBound(30, m_store->prefInt(u"meshIntervalSeconds"_s), 86'400) * 1000);
}

void MeshService::registerNow()
{
    if (!m_client || !sharing())
        return;
    const QString id = m_store->deviceId();
    // The stream this client opens next claims the id too.
    m_client->setDeviceId(id);
    const platform::Battery battery = platform::battery();
    QJsonObject body{
        {"id"_L1, id},
        {"name"_L1, platform::deviceName()},
        {"platform"_L1, platform::name()},
        {"appVersion"_L1, QStringLiteral(TALOS_VERSION)},
        {"capabilities"_L1, QJsonArray::fromStringList(capabilities())},
    };
    if (battery.percent >= 0)
        body["battery"_L1] = battery.percent;
    if (battery.charging >= 0)
        body["charging"_L1] = battery.charging == 1;

    const QPointer<BridgeClient> client = m_client;
    m_client->post(u"/devices/register"_s, body, [this, client](const ApiResult &r) {
        if (client != m_client)
            return;
        if (r.ok()) {
            if (m_registerFailures > 0)
                AppLog::info("mesh", u"registration recovered"_s);
            const bool first = !m_registered;
            m_registerFailures = 0;
            m_registered = true;
            m_lastError.clear();
            if (first)
                m_session->refreshDevices();
            // Present only when the daemon has a list to give; an empty one
            // means forget what was learned.
            if (r.json["endpoints"_L1].isObject())
                m_session->learnEndpoints(r.json["endpoints"_L1].toObject());
            // The same hint /auth/whoami gives at connect, repeated on the
            // heartbeat so a rotation doesn't wait for the next reconnect.
            if (r.json["credential"_L1].isObject())
                m_session->checkCredential();
        } else {
            ++m_registerFailures;
            m_registered = false;
            m_lastError = u"Registration failed: %1"_s.arg(r.error);
            // One warning per outage, not one a minute.
            if (m_registerFailures == 1)
                AppLog::warn("mesh", m_lastError);
        }
        scheduleHeartbeat(heartbeatDelayMs(m_registerFailures));
        emit changed();
    });
}

// ── Location ────────────────────────────────────────────────────────────────

void MeshService::reportLocationNow()
{
    sendFix(true, [this](bool sent) {
        if (sent)
            m_session->refreshDevices();
    });
}

void MeshService::sendFix(bool live, std::function<void(bool)> done)
{
    const auto finish = [done](bool sent) {
        if (done)
            done(sent);
    };
#if defined(TALOS_HAVE_POSITIONING)
    if (!m_client || !sharing()) {
        finish(false);
        return;
    }
    QLocationPermission permission;
    permission.setAccuracy(QLocationPermission::Precise);
    permission.setAvailability(QLocationPermission::WhenInUse);
    switch (qApp->checkPermission(permission)) {
    case Qt::PermissionStatus::Undetermined:
        qApp->requestPermission(permission, this, [this, live, done](const QPermission &p) {
            if (p.status() == Qt::PermissionStatus::Granted)
                sendFix(live, done);
            else if (done)
                done(false);
        });
        return;
    case Qt::PermissionStatus::Denied:
        finish(false);
        return;
    case Qt::PermissionStatus::Granted:
        break;
    }
    if (!m_position)
        m_position = QGeoPositionInfoSource::createDefaultSource(this);
    if (!m_position) {
        finish(false);
        return;
    }

    const auto post = [this, finish](const QGeoPositionInfo &info) {
        if (!m_client || !info.isValid()) {
            finish(false);
            return;
        }
        const QGeoCoordinate c = info.coordinate();
        QJsonObject body{
            {"deviceId"_L1, m_store->deviceId()},
            {"lat"_L1, c.latitude()},
            {"lon"_L1, c.longitude()},
            {"ts"_L1, info.timestamp().isValid() ? info.timestamp().toMSecsSinceEpoch() : nowMs()},
            {"provider"_L1, m_position->sourceName()},
        };
        if (info.hasAttribute(QGeoPositionInfo::HorizontalAccuracy))
            body["accuracyM"_L1] = info.attribute(QGeoPositionInfo::HorizontalAccuracy);
        if (c.type() == QGeoCoordinate::Coordinate3D)
            body["altitudeM"_L1] = c.altitude();
        if (info.hasAttribute(QGeoPositionInfo::GroundSpeed))
            body["speedMps"_L1] = info.attribute(QGeoPositionInfo::GroundSpeed);
        if (info.hasAttribute(QGeoPositionInfo::Direction))
            body["headingDeg"_L1] = info.attribute(QGeoPositionInfo::Direction);
        const platform::Battery battery = platform::battery();
        if (battery.percent >= 0)
            body["batteryPct"_L1] = battery.percent;
        m_client->post(u"/location"_s, body, [finish](const ApiResult &r) { finish(r.ok()); });
    };

    // A scheduled report reuses a fix under a minute old instead of waking
    // the receiver; "find my device" always asks for a fresh one.
    if (!live) {
        const QGeoPositionInfo last = m_position->lastKnownPosition();
        if (last.isValid() && last.timestamp().isValid()
            && last.timestamp().msecsTo(QDateTime::currentDateTime()) < 60'000) {
            post(last);
            return;
        }
    }
    auto answered = std::make_shared<bool>(false);
    auto *scope = new QObject(this);
    connect(m_position, &QGeoPositionInfoSource::positionUpdated, scope,
            [scope, answered, post](const QGeoPositionInfo &info) {
        if (*answered)
            return;
        *answered = true;
        scope->deleteLater();
        post(info);
    });
    connect(m_position, &QGeoPositionInfoSource::errorOccurred, scope,
            [scope, answered, finish](QGeoPositionInfoSource::Error) {
        if (*answered)
            return;
        *answered = true;
        scope->deleteLater();
        finish(false);
    });
    m_position->requestUpdate(live ? 15'000 : 10'000);
#else
    Q_UNUSED(live);
    finish(false);
#endif
}

// ── Commands ────────────────────────────────────────────────────────────────

void MeshService::handleEvent(const QJsonObject &event)
{
    if (!sharing() || !m_owner)
        return;
    const QString kind = event["kind"_L1].toString();
    const QString target = event["deviceId"_L1].toString();
    const QString mine = deviceId();
    if (kind == "locate"_L1) {
        // An untargeted locate asks every device.
        if (target.isEmpty() || target == mine)
            sendFix(true);
    } else if (kind == "device_command"_L1) {
        const QString id = event["id"_L1].toString();
        if (target.isEmpty() || target != mine || id.isEmpty())
            return;
        // A command is run once, however many times its frame arrives.
        if (m_seenCommands.contains(id))
            return;
        m_seenCommands.enqueue(id);
        if (m_seenCommands.size() > 256)
            m_seenCommands.dequeue();
        admit(event);
    }
}

void MeshService::admit(const QJsonObject &event)
{
    // Bounds what a burst of frames can pile onto this device.
    const int maxRunning = qBound(1, m_store->prefInt(u"meshMaxConcurrent"_s), 64);
    const int maxQueued = qBound(0, m_store->prefInt(u"meshMaxQueued"_s), 1024);
    if (m_inFlight < maxRunning) {
        run(event);
    } else if (m_queue.size() < maxQueued) {
        m_queue.enqueue(event);
    } else if (m_client) {
        m_client->post(u"/devices/command-result"_s,
                       {{"commandId"_L1, event["id"_L1].toString()},
                        {"deviceId"_L1, deviceId()},
                        {"ok"_L1, false},
                        {"message"_L1, u"Device is busy (%1 commands running, %2 queued). "
                                       "Try again shortly."_s.arg(maxRunning).arg(maxQueued)}},
                       {});
    }
}

void MeshService::run(const QJsonObject &event)
{
    ++m_inFlight;
    emit changed();
    const QString id = event["id"_L1].toString();
    const QString name = event["name"_L1].toString();
    const QJsonObject params = event["params"_L1].toObject();
    const qint64 started = nowMs();

    // Gated before anything runs, so a refusal can never half-execute.
    if (deviceControl() && needsApproval(name)) {
        const auto decide = [this, id, name, params, started](const QString &denial) {
            if (denial.isEmpty())
                execute(id, name, params);
            else
                answer(id, name, params, CommandOutcome::fail(denial), started);
        };
        if (approver) {
            approver(name, auditDetail(name, params), decide);
        } else if (m_store->prefBool(u"lockGateCommands"_s)) {
            decide(u"Denied on the device: it requires local approval for device-control "
                   "commands, and none could be requested."_s);
        } else {
            decide({});
        }
        return;
    }
    execute(id, name, params);
}

void MeshService::execute(const QString &id, const QString &name, const QJsonObject &params)
{
    const qint64 started = nowMs();
    const DeviceExec::Done done = [this, id, name, params, started](const CommandOutcome &o) {
        answer(id, name, params, o, started);
    };
    const bool control = deviceControl();

    if (name == "locate"_L1) {
        sendFix(true, [done](bool sent) {
            CommandOutcome o;
            o.ok = sent;
            o.message = sent ? u"Fresh fix reported."_s
                             : u"This device has no location to report."_s;
            done(o);
        });
    } else if (name == "ring"_L1) {
        emit ringRequested(params["message"_L1].toString());
        done(CommandOutcome::done());
    } else if (name == "status"_L1) {
        done(CommandOutcome::done(statusPayload()));
    } else if (!control) {
        done(CommandOutcome::fail(u"Device control is disabled on this device."_s));
    } else if (name == "upload_file"_L1) {
        uploadFile(params, done);
    } else if (name == "download_file"_L1) {
        downloadFile(params, done);
    } else if (name == "update_node"_L1) {
        updateSelf(params, done);
    } else if (name == "install_apk"_L1) {
        installApk(params, done);
    } else if (name == "computer"_L1) {
        if (computerHandler && m_store->prefBool(u"meshScreenControl"_s))
            computerHandler(params, done);
        else
            done(CommandOutcome::fail(u"Screen control is turned off on this device."_s));
    } else if (!m_exec.handle(name, params, done)) {
        done(CommandOutcome::fail(u"This client does not support “%1”."_s.arg(name)));
    }
}

void MeshService::answer(const QString &id, const QString &name, const QJsonObject &params,
                         const CommandOutcome &outcome, qint64 startedMs)
{
    if (m_client) {
        QJsonObject body{
            {"commandId"_L1, id},
            {"deviceId"_L1, deviceId()},
            {"ok"_L1, outcome.ok},
        };
        if (!outcome.message.isEmpty())
            body["message"_L1] = outcome.message;
        if (!outcome.data.isEmpty())
            body["data"_L1] = outcome.data;
        m_client->post(u"/devices/command-result"_s, body, [name](const ApiResult &r) {
            if (!r.ok())
                AppLog::warn("mesh", u"result for “%1” was not delivered: %2"_s.arg(name, r.error));
        }, 30'000);
    }
    record(id, name, params, outcome, nowMs() - startedMs);
    m_inFlight = qMax(0, m_inFlight - 1);
    if (!m_queue.isEmpty())
        run(m_queue.dequeue());
    emit changed();
}

QJsonObject MeshService::statusPayload() const
{
    QJsonObject o{
        {"name"_L1, platform::deviceName()},
        {"platform"_L1, platform::name()},
        {"appVersion"_L1, QStringLiteral(TALOS_VERSION)},
        {"client"_L1, u"talos (Qt)"_s},
        {"execPrivilege"_L1, u"user"_s},
        {"deviceControl"_L1, deviceControl() ? u"on"_s : u"off"_s},
        {"meshSharing"_L1, sharing() ? u"on"_s : u"off"_s},
        {"periodicReporting"_L1, m_store->prefBool(u"meshPeriodic"_s)
             ? u"every %1s"_s.arg(m_store->prefInt(u"meshIntervalSeconds"_s))
             : u"off"_s},
    };
    const platform::Battery battery = platform::battery();
    if (battery.percent >= 0)
        o["battery"_L1] = u"%1%"_s.arg(battery.percent);
    if (battery.charging >= 0)
        o["charging"_L1] = battery.charging == 1 ? u"yes"_s : u"no"_s;
    const QVariantMap info = platform::systemInfo();
    for (auto it = info.begin(); it != info.end(); ++it)
        o[it.key()] = it.value().toString();
    return o;
}

// ── Installing a package (Android) ──────────────────────────────────────────

void MeshService::installApk(const QJsonObject &params, DeviceExec::Done done)
{
    const QString path = params["path"_L1].toString();
    if (path.isEmpty()) {
        done(CommandOutcome::fail(u"install_apk needs the path of the APK on this device."_s));
        return;
    }
    const QString expected = params["sha256"_L1].toString().trimmed().toLower();
    if (!expected.isEmpty()) {
        static const QRegularExpression digest(u"^[0-9a-f]{64}$"_s);
        if (!digest.match(expected).hasMatch()) {
            done(CommandOutcome::fail(u"APK integrity check failed: that is not a SHA-256 "
                                      "digest. Nothing was installed."_s));
            return;
        }
        QFile apk(path);
        QCryptographicHash hash(QCryptographicHash::Sha256);
        if (!apk.open(QIODevice::ReadOnly) || !hash.addData(&apk)) {
            done(CommandOutcome::fail(u"No readable APK at %1."_s.arg(path)));
            return;
        }
        const QString actual = QString::fromLatin1(hash.result().toHex());
        if (actual != expected) {
            done(CommandOutcome::fail(u"APK integrity check failed: expected %1, got %2. "
                                      "Nothing was installed."_s.arg(expected, actual)));
            return;
        }
    }
    const QString error = platform::installPackage(path);
    if (!error.isEmpty()) {
        done(CommandOutcome::fail(u"install_apk: %1"_s.arg(error)));
        return;
    }
    CommandOutcome o;
    o.ok = true;
    // Android installs nothing without a tap unless the app has root or
    // Shizuku, which this one does not use.
    o.message = u"Handed to the system installer. The person holding the device has to confirm "
                "it on screen; check appVersion with get_device_status afterwards."_s;
    o.data = {{"confirmation"_L1, u"required"_s}};
    done(o);
}

// ── Updating this program ───────────────────────────────────────────────────

void MeshService::updateSelf(const QJsonObject &params, DeviceExec::Done done)
{
    const QString path = params["path"_L1].toString();
    if (path.isEmpty()) {
        done(CommandOutcome::fail(u"update_node needs the path of the pushed build."_s));
        return;
    }
    if (!canUpdateSelf()) {
        done(CommandOutcome::fail(u"update_node: this install can't replace itself (it is owned "
                                  "by the system or sandboxed). Update it through its package "
                                  "manager."_s));
        return;
    }
    const QString exe = selfupdate::ownExecutable();
    selfupdate::Request request;
    request.source = path;
    request.sha256 = params["sha256"_L1].toString();
    request.running = QStringLiteral(TALOS_VERSION);
    request.allowDowngrade = params["allow_downgrade"_L1].toBool();
    QString error;
    const QString pushed = selfupdate::install(exe, request, &error);
    if (pushed.isEmpty()) {
        done(CommandOutcome::fail(u"update_node: %1"_s.arg(error)));
        return;
    }
    if (path != exe)
        QFile::remove(path);
    CommandOutcome o;
    o.ok = true;
    o.message = u"Update staged at %1. Restarting into the new build now; confirm with "
                "get_device_status once appVersion changes."_s.arg(exe);
    o.data = {{"installedTo"_L1, exe}, {"restarting"_L1, true},
              {"previousVersion"_L1, QStringLiteral(TALOS_VERSION)}, {"newVersion"_L1, pushed}};
    done(o);
    // After the answer has had time to leave.
    QTimer::singleShot(1500, this, &MeshService::restartRequested);
}

// ── Streamed transfers ──────────────────────────────────────────────────────

void MeshService::uploadFile(const QJsonObject &params, DeviceExec::Done done)
{
    const QString token = params["token"_L1].toString();
    const QString path = params["path"_L1].toString();
    if (token.isEmpty() || path.isEmpty()) {
        done(CommandOutcome::fail(u"upload_file needs token and path."_s));
        return;
    }
    if (!QFileInfo(path).isFile()) {
        done(CommandOutcome::fail(u"No such file: %1"_s.arg(path)));
        return;
    }
    // Hashed off the UI thread, then sent as one raw request. The daemon
    // checks what arrived against this digest.
    auto *watcher = new QFutureWatcher<QByteArray>(this);
    connect(watcher, &QFutureWatcher<QByteArray>::finished, this,
            [this, watcher, token, path, done] {
        watcher->deleteLater();
        const QString digest = QString::fromLatin1(watcher->result());
        auto *source = new QFile(path);
        if (!m_client || digest.isEmpty() || !source->open(QIODevice::ReadOnly)) {
            delete source;
            done(CommandOutcome::fail(u"Couldn't read %1"_s.arg(path)));
            return;
        }
        const qint64 size = source->size();
        m_client->upload(u"/devices/file"_s, transferQuery(token, deviceId()), source, size,
                         u"application/octet-stream"_s,
                         [done, size, digest](const ApiResult &r) {
            if (!r.ok()) {
                done(CommandOutcome::fail(u"Upload rejected: %1"_s.arg(r.error)));
                return;
            }
            done(CommandOutcome::done({{"bytes"_L1, size}, {"sha256"_L1, digest}}));
        });
    });
    watcher->setFuture(QtConcurrent::run(sha256File, path));
}

void MeshService::downloadFile(const QJsonObject &params, DeviceExec::Done done)
{
    const QString token = params["token"_L1].toString();
    const QString path = params["path"_L1].toString();
    const QString wantSha = params["sha256"_L1].toString().trimmed().toLower();
    if (token.isEmpty() || path.isEmpty() || !m_client) {
        done(CommandOutcome::fail(u"download_file needs token and path."_s));
        return;
    }
    QDir().mkpath(QFileInfo(path).absolutePath());

    // Written to a side file and renamed into place, so a dropped
    // connection can never leave a half-written destination.
    struct Transfer {
        QFile part;
        QCryptographicHash hash{QCryptographicHash::Sha256};
        qint64 received = 0;
        QString failure;
    };
    auto t = std::make_shared<Transfer>();
    t->part.setFileName(path + u".part"_s);
    if (!t->part.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        done(CommandOutcome::fail(u"Couldn't write %1: %2"_s.arg(path, t->part.errorString())));
        return;
    }
    const qint64 cap = m_exec.writeLimit();
    const QPointer<BridgeClient> client = m_client;
    QNetworkReply *reply = client->rawGet(u"/devices/file"_s, transferQuery(token, deviceId()));

    connect(reply, &QNetworkReply::readyRead, this, [reply, t, cap] {
        if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200)
            return;
        const QByteArray chunk = reply->readAll();
        t->received += chunk.size();
        if (t->received > cap) {
            t->failure = u"download exceeds the %1-byte write cap"_s.arg(cap);
            reply->abort();
            return;
        }
        t->hash.addData(chunk);
        if (t->part.write(chunk) != chunk.size()) {
            t->failure = u"disk write failed: %1"_s.arg(t->part.errorString());
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::finished, this, [reply, t, client, path, wantSha, done] {
        reply->deleteLater();
        t->part.close();
        const auto abandon = [&](const QString &why) {
            t->part.remove();
            done(CommandOutcome::fail(why));
        };
        if (!t->failure.isEmpty())
            return abandon(u"Download failed: %1"_s.arg(t->failure));
        ApiResult r;
        if (client) {
            r = client->diagnose(reply, false);
        } else {
            r.kind = ApiResult::Network;
            r.error = u"disconnected"_s;
        }
        if (!r.ok())
            return abandon(u"Download rejected: %1"_s.arg(r.error));
        const QVariant length = reply->header(QNetworkRequest::ContentLengthHeader);
        if (length.isValid() && length.toLongLong() != t->received) {
            return abandon(u"Download truncated: got %1 of %2 bytes."_s
                               .arg(t->received).arg(length.toLongLong()));
        }
        const QString digest = QString::fromLatin1(t->hash.result().toHex());
        if (!wantSha.isEmpty() && wantSha != digest) {
            return abandon(u"Integrity check failed (expected sha256 %1, got %2). The "
                           "download was discarded."_s.arg(wantSha, digest));
        }
        if (!moveOver(t->part.fileName(), path))
            return abandon(u"Couldn't move the download into place at %1"_s.arg(path));
        done(CommandOutcome::done({{"bytesWritten"_L1, t->received}, {"sha256"_L1, digest}}));
    });
}

// ── Audit trail ─────────────────────────────────────────────────────────────

QString MeshService::auditPath() const
{
    return QDir(m_store->dataDir()).filePath(u"mesh-audit.jsonl"_s);
}

void MeshService::record(const QString &id, const QString &name, const QJsonObject &params,
                         const CommandOutcome &outcome, qint64 elapsedMs)
{
    const QJsonObject entry{
        {"ts"_L1, nowMs()},
        {"id"_L1, id},
        {"name"_L1, name},
        {"detail"_L1, redactSecrets(auditDetail(name, params))},
        {"ok"_L1, outcome.ok},
        {"message"_L1, redactSecrets(outcome.message)},
        {"exitCode"_L1, outcome.data["exitCode"_L1]},
        {"elapsedMs"_L1, elapsedMs},
    };
    QFile f(auditPath());
    const bool fresh = !f.exists();
    if (f.open(QIODevice::Append)) {
        if (fresh)
            f.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
        f.write(QJsonDocument(entry).toJson(QJsonDocument::Compact) + '\n');
        // Compact occasionally rather than on every command.
        if (f.size() > 512 * 1024) {
            f.close();
            const QVariantList keep = audit();
            QFile out(auditPath());
            if (out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                for (auto it = keep.rbegin(); it != keep.rend(); ++it) {
                    out.write(QJsonDocument(QJsonObject::fromVariantMap(it->toMap()))
                                  .toJson(QJsonDocument::Compact) + '\n');
                }
            }
        }
    }
    ++m_auditRevision;
    emit auditChanged();
}

QVariantList MeshService::audit() const
{
    QFile f(auditPath());
    if (!f.open(QIODevice::ReadOnly))
        return {};
    QVariantList newestFirst;
    const QByteArrayList lines = f.readAll().split('\n');
    for (auto it = lines.rbegin(); it != lines.rend() && newestFirst.size() < kAuditKeep; ++it) {
        if (it->trimmed().isEmpty())
            continue;
        const QJsonDocument doc = QJsonDocument::fromJson(*it);
        if (doc.isObject())
            newestFirst << doc.object().toVariantMap();
    }
    return newestFirst;
}

void MeshService::clearAudit()
{
    QFile::remove(auditPath());
    ++m_auditRevision;
    emit auditChanged();
}

} // namespace talos
