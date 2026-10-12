// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "mesh/DeviceExec.h"

#include <QJsonObject>
#include <QLockFile>
#include <QObject>
#include <QPointer>
#include <QQueue>
#include <QTimer>
#include <QVariantList>
#include <functional>

class QGeoPositionInfoSource;

namespace talos {

class BridgeClient;
class Session;
class Store;

/// This device's side of the Talon mesh.
///
/// When sharing is on, the device registers with the daemon (and again
/// every minute, which is what "online" means there), reports its location
/// on request or on a schedule, and answers device commands. With device
/// control granted for this bridge it also runs shell and file commands and
/// streamed file transfers.
///
/// Every command is answered, whatever happens: a refusal, a failure and
/// "unsupported" all reach the daemon as a result, so the tool call waiting
/// on it resolves instead of timing out.
class MeshService : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool sharing READ sharing NOTIFY changed)
    Q_PROPERTY(bool registered READ registered NOTIFY changed)
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(bool deviceControl READ deviceControl WRITE setDeviceControl NOTIFY changed)
    Q_PROPERTY(bool sandboxed READ sandboxed CONSTANT)
    Q_PROPERTY(bool locationAvailable READ locationAvailable CONSTANT)
    Q_PROPERTY(QString deviceId READ deviceId NOTIFY changed)
    Q_PROPERTY(QString deviceName READ deviceName CONSTANT)
    Q_PROPERTY(int activeCommands READ activeCommands NOTIFY changed)
    Q_PROPERTY(int auditRevision READ auditRevision NOTIFY auditChanged)
    Q_PROPERTY(bool owner READ owner NOTIFY changed)
    Q_PROPERTY(bool screenControlAvailable READ screenControlAvailable NOTIFY changed)
    Q_PROPERTY(bool canUpdateSelf READ canUpdateSelf CONSTANT)
public:
    static constexpr int kHeartbeatMs = 60'000;
    static constexpr int kMaxRegisterBackoffMs = 5 * 60'000;
    static constexpr int kAuditKeep = 500;

    /// Asked before a device-control command runs. Call @p decide with an
    /// empty string to allow it, or with the refusal sent to the daemon.
    using Approver = std::function<void(const QString &command, const QString &detail,
                                        std::function<void(const QString &denial)> decide)>;

    MeshService(Store *store, Session *session, QObject *parent = nullptr);

    static QStringList coreCapabilities();
    static QStringList capabilitiesFor(bool deviceControl);
    /// Which of the three kinds a device-control command is: "exec",
    /// "read" or "write". Empty for commands with a switch of their own.
    static QString commandKind(const QString &command);
    /// Every path a command would touch.
    static QStringList commandPaths(const QString &command, const QJsonObject &params);
    /// True when @p path lies inside one of @p folders, following the links
    /// that exist on the way so a link can't lead out. No folders: anywhere.
    static bool pathAllowed(const QString &path, const QStringList &folders);
    /// Why the limits the user set refuse this command, or empty.
    QString policyDenial(const QString &command, const QJsonObject &params) const;
    /// What this device advertises right now, extras included.
    QStringList capabilities() const;
    /// Only one process per data directory answers mesh commands: the app or
    /// the background service, whichever holds the lock.
    bool owner() const { return m_owner; }
    /// Let the other process take the mesh over (the service was just
    /// enabled); this one stops answering and holds back for a while.
    Q_INVOKABLE void yield();
    bool screenControlAvailable() const { return bool(computerHandler); }
    bool canUpdateSelf() const;

    /// Screen control (`computer`), supplied by the app when the platform
    /// can do it. Returns false for a request it cannot take.
    std::function<void(const QJsonObject &params, DeviceExec::Done done)> computerHandler;
    /// Commands that run code or touch files: what local approval covers.
    static bool needsApproval(const QString &command);
    /// Delay before the next heartbeat after @p failures failed in a row.
    static int heartbeatDelayMs(int failures);

    bool sharing() const;
    bool registered() const { return m_registered; }
    QString state() const;
    bool deviceControl() const;
    void setDeviceControl(bool on);
    bool sandboxed() const;
    bool locationAvailable() const;
    QString deviceId() const;
    QString deviceName() const;
    int activeCommands() const { return m_inFlight; }
    int auditRevision() const { return m_auditRevision; }

    void attach(BridgeClient *client);
    void detach();
    /// Re-read the preferences and re-register (a toggle changed).
    Q_INVOKABLE void reconfigure();
    Q_INVOKABLE void reportLocationNow();
    Q_INVOKABLE QVariantList audit() const;
    Q_INVOKABLE void clearAudit();

    Approver approver;
    /// Feed a command event directly (tests).
    void handleEvent(const QJsonObject &event);

signals:
    void changed();
    void auditChanged();
    /// "Find my device": make noise and show @p message.
    void ringRequested(const QString &message);
    /// A pushed update was swapped in; start the new build.
    void restartRequested();

private:
    void registerNow();
    void scheduleHeartbeat(int delayMs);
    void configurePeriodic();
    void sendFix(bool live, std::function<void(bool sent)> done = {});
    void admit(const QJsonObject &event);
    void run(const QJsonObject &event);
    void execute(const QString &id, const QString &name, const QJsonObject &params);
    void answer(const QString &id, const QString &name, const QJsonObject &params,
                const CommandOutcome &outcome, qint64 startedMs);
    bool acquire();
    void updateSelf(const QJsonObject &params, DeviceExec::Done done);
    void installApk(const QJsonObject &params, DeviceExec::Done done);
    void uploadFile(const QJsonObject &params, DeviceExec::Done done);
    void downloadFile(const QJsonObject &params, DeviceExec::Done done);
    QJsonObject statusPayload() const;
    void record(const QString &id, const QString &name, const QJsonObject &params,
                const CommandOutcome &outcome, qint64 elapsedMs);
    QString auditPath() const;

    Store *m_store;
    Session *m_session;
    QPointer<BridgeClient> m_client;
    DeviceExec m_exec;
    QTimer m_heartbeat;
    QTimer m_periodic;
    QGeoPositionInfoSource *m_position = nullptr;
    bool m_registered = false;
    int m_registerFailures = 0;
    QString m_lastError;
    int m_inFlight = 0;
    QQueue<QJsonObject> m_queue;
    QQueue<QString> m_seenCommands;
    int m_auditRevision = 0;
    QLockFile m_lock;
    bool m_owner = false;
    qint64 m_yieldedUntil = 0;
    QTimer m_ownerRetry;
};

} // namespace talos
