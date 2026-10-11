// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "core/Profile.h"
#include "core/SseParser.h"

#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QPointer>
#include <QSslConfiguration>
#include <QTimer>
#include <QUrlQuery>
#include <functional>

namespace talos {

/// Outcome of one bridge request, already diagnosed.
struct ApiResult {
    enum Kind {
        Ok,
        Network,             ///< could not reach the bridge
        Timeout,
        Unauthorized,        ///< 401: wrong or missing token
        CertificateChanged,  ///< the pinned certificate did not match
        ClientCertRequired,  ///< a proxy wants a (valid) client certificate
        Http,                ///< any other >= 400 answer
    };
    Kind kind = Ok;
    int status = 0;
    QJsonObject json;
    QString error;

    bool ok() const { return kind == Ok; }
    /// True for failures no retry will fix: the user has to act.
    bool fatal() const
    {
        return kind == Unauthorized || kind == CertificateChanged || kind == ClientCertRequired;
    }
};

/// Transport for the Talon Client Bridge Protocol (v1): JSON over HTTP for
/// commands, one long-lived Server-Sent-Events stream for everything the
/// daemon pushes. It owns no application state and never reconnects on its
/// own; the session decides that.
///
/// TLS trust is a certificate pin. With no pin yet, a certificate the
/// platform doesn't already trust is accepted and reported through
/// seenFingerprint() for the caller to adopt. With a pin, an untrusted
/// certificate must match it; once the profile enforces its pin, every
/// certificate must.
class BridgeClient : public QObject
{
    Q_OBJECT
public:
    using Callback = std::function<void(const ApiResult &)>;

    /// The daemon pings every 25 s, so a stream this quiet is dead: a
    /// half-open socket delivers neither bytes nor an error.
    static constexpr int kEventIdleMs = 70'000;
    static constexpr int kRequestTimeoutMs = 12'000;
    static constexpr int kTransferIdleMs = 45'000;

    explicit BridgeClient(const Profile &endpoint, QObject *parent = nullptr);
    ~BridgeClient() override;

    const Profile &endpoint() const { return m_endpoint; }
    void setEndpoint(const Profile &endpoint);
    void setToken(const QString &token) { m_endpoint.token = token; }
    void setPin(const QString &fingerprint, bool observed);

    /// This device's mesh id. Claimed on the event stream so the daemon
    /// addresses device commands to this client alone.
    void setDeviceId(const QString &id) { m_deviceId = id; }
    QString deviceId() const { return m_deviceId; }

    /// Certificate of the last handshake the platform did not trust by
    /// itself: the candidate for first-use adoption.
    QString seenFingerprint() const { return m_seenFingerprint; }
    /// True once a handshake presented exactly the pinned certificate.
    bool pinMatched() const { return m_pinMatched; }
    /// When the event stream last delivered anything, keep-alives included.
    qint64 lastRxMs() const { return m_lastRx; }
    bool eventsOpen() const { return m_eventsOpen; }

    /// `GET /health`. Ok only when a Talon bridge answered.
    void health(Callback cb, int timeoutMs = 4000);

    void openEvents(int connectTimeoutMs = 10'000);
    void closeEvents();

    QNetworkReply *get(const QString &path, const QUrlQuery &query, Callback cb,
                       int timeoutMs = kRequestTimeoutMs);
    QNetworkReply *get(const QString &path, Callback cb) { return get(path, {}, std::move(cb)); }
    QNetworkReply *post(const QString &path, const QJsonObject &body, Callback cb,
                        int timeoutMs = kRequestTimeoutMs);
    /// Stream @p body up as one raw request. The reply takes ownership of
    /// the device.
    QNetworkReply *upload(const QString &path, const QUrlQuery &query, QIODevice *body,
                          qint64 length, const QString &contentType, Callback cb,
                          std::function<void(qint64 sent, qint64 total)> progress = {});
    /// POST a raw body. The reply is not parsed as JSON: the callback gets
    /// the status and, through @p bodyOut when given, the response bytes.
    QNetworkReply *postRaw(const QString &path, const QUrlQuery &query, const QByteArray &body,
                           const QString &contentType,
                           std::function<void(const ApiResult &, const QByteArray &)> cb);
    /// A raw, authenticated, pin-guarded GET whose body the caller reads.
    QNetworkReply *rawGet(const QString &path, const QUrlQuery &query,
                          int idleTimeoutMs = kTransferIdleMs);

    /// Diagnose a finished reply (and parse its JSON body when @p wantJson).
    ApiResult diagnose(QNetworkReply *reply, bool wantJson = true) const;

    /// Handshake with @p candidate and report the certificate it presents,
    /// without sending any credential. Lets the user confirm a fingerprint
    /// before the token goes anywhere.
    static void probeCertificate(const Profile &candidate, QObject *context,
                                 std::function<void(const QString &fingerprint, bool reached)> done);

    static QString fingerprintOf(const QSslCertificate &cert);
    /// Validate a PKCS#12 bundle opens with @p password.
    static bool checkPkcs12(const QByteArray &p12, const QString &password, QString *error);

signals:
    void eventsOpened();
    void event(const QJsonObject &event);
    /// The stream ended or never opened. Always follows openEvents() exactly
    /// once unless closeEvents() was called first.
    void eventsClosed(const talos::ApiResult &why);

private:
    QNetworkRequest request(const QString &path, const QUrlQuery &query, int timeoutMs) const;
    void guard(QNetworkReply *reply);
    void finish(QNetworkReply *reply, Callback cb);
    void rebuildTls();
    void endEvents(const ApiResult &why);

    Profile m_endpoint;
    QString m_deviceId;
    QNetworkAccessManager m_nam;
    QSslConfiguration m_tls;

    QString m_seenFingerprint;
    bool m_pinMatched = false;

    QPointer<QNetworkReply> m_events;
    SseParser m_parser;
    QTimer m_idle;
    QTimer m_connectTimer;
    bool m_eventsOpen = false;
    qint64 m_lastRx = 0;
};

} // namespace talos

Q_DECLARE_METATYPE(talos::ApiResult)
