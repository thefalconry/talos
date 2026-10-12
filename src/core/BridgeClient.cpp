// SPDX-License-Identifier: GPL-2.0-only
#include "core/BridgeClient.h"

#include "core/AppLog.h"

#include <QBuffer>
#include <QCryptographicHash>
#include <QDateTime>
#include <QJsonDocument>
#include <QLibrary>
#include <QRegularExpression>
#include <QSslCertificate>
#include <QSslKey>
#include <QSslSocket>

namespace talos {

using namespace Qt::StringLiterals;

namespace {

const char kPinRejected[] = "talosPinRejected";

/// Older certificate bundles, and anything exported with `openssl pkcs12
/// -legacy`, are encrypted with RC2 or 3DES. OpenSSL 3 keeps those in a
/// "legacy" provider that is off unless asked for. Qt loads OpenSSL at run
/// time instead of linking it, so the provider is switched on the same way:
/// through the library Qt already has open.
bool enableLegacyCiphers()
{
    static const bool enabled = [] {
        if (QSslSocket::activeBackend() != "openssl"_L1)
            return false;
#if defined(Q_OS_ANDROID)
        QLibrary crypto(u"crypto_3"_s);
#else
        QLibrary crypto(u"crypto"_s, 3);
#endif
        using Load = void *(*)(void *, const char *);
        const auto load = reinterpret_cast<Load>(crypto.resolve("OSSL_PROVIDER_load"));
        // Naming any provider stops OpenSSL from supplying the default one
        // by itself, so both are asked for.
        return load && load(nullptr, "default") && load(nullptr, "legacy");
    }();
    return enabled;
}

bool readPkcs12(const QByteArray &p12, const QString &password, QSslKey *key,
                QSslCertificate *cert, QList<QSslCertificate> *chain)
{
    const auto attempt = [&] {
        QBuffer buf;
        buf.setData(p12);
        buf.open(QIODevice::ReadOnly);
        return QSslCertificate::importPkcs12(&buf, key, cert, chain, password.toUtf8())
            && !key->isNull() && !cert->isNull();
    };
    // Qt doesn't say why a bundle failed to open, so a wrong password also
    // gets the second attempt.
    return attempt() || (enableLegacyCiphers() && attempt());
}

/// TLS alerts a reverse proxy sends when the handshake lacked a client
/// certificate it accepts (Caddy, Traefik, HAProxy, …).
bool isClientCertAlert(const QString &error)
{
    const QString e = error.toLower();
    return e.contains("certificate required"_L1) || e.contains("certificate_required"_L1)
        || e.contains("alert bad certificate"_L1) || e.contains("alert unknown ca"_L1)
        || e.contains("alert certificate unknown"_L1);
}

QString bodyError(const QByteArray &body)
{
    const QJsonDocument doc = QJsonDocument::fromJson(body);
    return doc.isObject() ? doc.object()["error"_L1].toString() : QString();
}

} // namespace

BridgeClient::BridgeClient(const Profile &endpoint, QObject *parent)
    : QObject(parent), m_endpoint(endpoint)
{
    qRegisterMetaType<talos::ApiResult>();
    m_nam.setAutoDeleteReplies(false);
    m_nam.setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
    rebuildTls();

    m_idle.setSingleShot(true);
    m_idle.setInterval(kEventIdleMs);
    connect(&m_idle, &QTimer::timeout, this, [this] {
        ApiResult r;
        r.kind = ApiResult::Timeout;
        r.error = u"Event stream idle: nothing received for %1 s"_s.arg(kEventIdleMs / 1000);
        endEvents(r);
    });
    m_connectTimer.setSingleShot(true);
    connect(&m_connectTimer, &QTimer::timeout, this, [this] {
        ApiResult r;
        r.kind = ApiResult::Timeout;
        r.error = u"Timed out connecting to the event stream"_s;
        endEvents(r);
    });
}

BridgeClient::~BridgeClient()
{
    if (m_events) {
        m_events->disconnect(this);
        m_events->abort();
        delete m_events;
    }
}

void BridgeClient::setEndpoint(const Profile &endpoint)
{
    const bool tlsChanged = endpoint.clientP12 != m_endpoint.clientP12
        || endpoint.clientP12Password != m_endpoint.clientP12Password;
    m_endpoint = endpoint;
    if (tlsChanged) {
        rebuildTls();
        m_nam.clearConnectionCache();
    }
}

void BridgeClient::setPin(const QString &fingerprint, bool observed)
{
    m_endpoint.fingerprint = fingerprint;
    m_endpoint.pinObserved = observed && !fingerprint.isEmpty();
}

QString BridgeClient::fingerprintOf(const QSslCertificate &cert)
{
    if (cert.isNull())
        return {};
    return QString::fromLatin1(
        QCryptographicHash::hash(cert.toDer(), QCryptographicHash::Sha256).toHex());
}

bool BridgeClient::checkPkcs12(const QByteArray &p12, const QString &password, QString *error)
{
    QSslKey key;
    QSslCertificate cert;
    QList<QSslCertificate> chain;
    if (readPkcs12(p12, password, &key, &cert, &chain))
        return true;
    if (error) {
        *error = QSslSocket::supportsSsl()
            ? u"Couldn't open the certificate. Check the password, and that the file is "
              "a PKCS#12 bundle (.p12 or .pfx)."_s
            : u"This build of Talos has no TLS support, so it can't open certificates "
              "or make https connections."_s;
    }
    return false;
}

void BridgeClient::rebuildTls()
{
    m_tls = QSslConfiguration::defaultConfiguration();
    if (!m_endpoint.hasClientCert())
        return;
    QSslKey key;
    QSslCertificate cert;
    QList<QSslCertificate> chain;
    if (readPkcs12(m_endpoint.clientP12, m_endpoint.clientP12Password, &key, &cert, &chain)) {
        QList<QSslCertificate> full{cert};
        full += chain;
        m_tls.setLocalCertificateChain(full);
        m_tls.setPrivateKey(key);
    } else {
        // The proxy's refusal is then the error the user sees, and it says
        // what to fix.
        AppLog::warn("bridge", u"client certificate no longer loads"_s);
    }
}

QNetworkRequest BridgeClient::request(const QString &path, const QUrlQuery &query,
                                      int timeoutMs) const
{
    QUrl url = m_endpoint.url(path);
    if (!query.isEmpty())
        url.setQuery(query);
    // A fallback dialled at a fixed address: connect there, but stay the
    // same server as far as TLS and the reverse proxy are concerned.
    const QUrl dial(u"//"_s + m_endpoint.dial);
    const QByteArray named = url.authority(QUrl::FullyEncoded).toLatin1();
    const QString serverName = url.host();
    const bool dialled = !m_endpoint.dial.isEmpty() && dial.isValid() && dial.port() > 0;
    if (dialled) {
        url.setHost(dial.host());
        url.setPort(dial.port());
    }
    QNetworkRequest req(url);
    if (dialled) {
        req.setPeerVerifyName(serverName);
        req.setRawHeader("Host", named);
        // HTTP/2 takes its :authority from the URL, which now holds the
        // address; HTTP/1.1 sends the Host header above.
        req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);
    }
    // The credential always travels in the header, never the URL: URLs end
    // up in proxy logs and in error text.
    if (!m_endpoint.token.isEmpty())
        req.setRawHeader("Authorization", "Bearer " + m_endpoint.token.toUtf8());
    if (m_endpoint.tls)
        req.setSslConfiguration(m_tls);
    req.setTransferTimeout(timeoutMs);
    req.setHeader(QNetworkRequest::UserAgentHeader,
                  u"talos/" TALOS_VERSION ""_s);
    return req;
}

void BridgeClient::guard(QNetworkReply *reply)
{
    if (!m_endpoint.tls)
        return;
    // Runs after the handshake and before any request bytes are written, so
    // a refused certificate never sees the bearer token.
    connect(reply, &QNetworkReply::encrypted, this, [this, reply] {
        const QString fp = fingerprintOf(reply->sslConfiguration().peerCertificate());
        const QString &pin = m_endpoint.fingerprint;
        if (pin.isEmpty())
            return;
        if (fp == pin) {
            m_pinMatched = true;
        } else if (m_endpoint.enforcesPin()) {
            reply->setProperty(kPinRejected, true);
            reply->abort();
        }
    });
    connect(reply, &QNetworkReply::sslErrors, this,
            [this, reply](const QList<QSslError> &errors) {
        QSslCertificate cert = reply->sslConfiguration().peerCertificate();
        if (cert.isNull() && !errors.isEmpty())
            cert = errors.first().certificate();
        const QString fp = fingerprintOf(cert);
        const QString &pin = m_endpoint.fingerprint;
        if (fp.isEmpty()) {
            reply->setProperty(kPinRejected, !pin.isEmpty());
            return;
        }
        if (pin.isEmpty()) {
            // Trust on first use: the caller decides whether to adopt it.
            m_seenFingerprint = fp;
            reply->ignoreSslErrors();
        } else if (fp == pin) {
            m_pinMatched = true;
            reply->ignoreSslErrors();
        } else {
            reply->setProperty(kPinRejected, true);
        }
    });
}

ApiResult BridgeClient::diagnose(QNetworkReply *reply, bool wantJson) const
{
    ApiResult r;
    r.status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
    const QByteArray body = wantJson || r.status >= 400 ? reply->readAll() : QByteArray();

    if (reply->property(kPinRejected).toBool()) {
        r.kind = ApiResult::CertificateChanged;
        r.error = u"The bridge's certificate no longer matches the pinned fingerprint. "
                  "If Talon was reinstalled, clear the pin in the connection settings "
                  "and reconnect."_s;
        return r;
    }

    const auto clientCert = [&] {
        r.kind = ApiResult::ClientCertRequired;
        r.error = m_endpoint.hasClientCert()
            ? u"The server didn't accept this device's client certificate. Import a "
              "valid one in the connection settings."_s
            : u"This server requires a client certificate. Import one in the "
              "connection settings."_s;
    };

    if (r.status == 0) {
        const QString text = reply->errorString();
        if (isClientCertAlert(text)) {
            clientCert();
        } else if (reply->error() == QNetworkReply::TimeoutError
                   || reply->error() == QNetworkReply::OperationCanceledError) {
            r.kind = ApiResult::Timeout;
            r.error = u"The bridge did not answer in time"_s;
        } else {
            r.kind = ApiResult::Network;
            r.error = redactSecrets(text);
        }
        return r;
    }

    // A proxy that wanted a client certificate and answered over HTTP:
    // nginx sends 400 "No required SSL certificate was sent", Cloudflare a
    // 403 page carrying cf-ray.
    static const QRegularExpression sslBody(u"SSL certificate"_s,
                                            QRegularExpression::CaseInsensitiveOption);
    if ((r.status == 403 && reply->hasRawHeader("cf-ray"))
        || (r.status == 400 && sslBody.match(QString::fromUtf8(body.left(2048))).hasMatch())) {
        clientCert();
        return r;
    }
    if (r.status == 401) {
        r.kind = ApiResult::Unauthorized;
        r.error = u"Unauthorized. Check the bridge token."_s;
        return r;
    }
    if (r.status >= 400) {
        r.kind = ApiResult::Http;
        const QString detail = bodyError(body);
        r.error = detail.isEmpty() ? u"Request failed (%1)"_s.arg(r.status)
                                   : u"%1 (%2)"_s.arg(detail).arg(r.status);
        r.json = QJsonDocument::fromJson(body).object();
        return r;
    }
    if (reply->error() != QNetworkReply::NoError) {
        r.kind = reply->error() == QNetworkReply::TimeoutError ? ApiResult::Timeout
                                                               : ApiResult::Network;
        r.error = redactSecrets(reply->errorString());
        return r;
    }
    if (wantJson && !body.isEmpty()) {
        const QJsonDocument doc = QJsonDocument::fromJson(body);
        if (!doc.isObject()) {
            r.kind = ApiResult::Http;
            r.error = u"The bridge sent something that isn't JSON"_s;
            return r;
        }
        r.json = doc.object();
    }
    return r;
}

void BridgeClient::finish(QNetworkReply *reply, Callback cb)
{
    guard(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply, cb = std::move(cb)] {
        reply->deleteLater();
        const ApiResult r = diagnose(reply);
        if (!r.ok()) {
            AppLog::debug("bridge", u"%1 %2 failed: %3"_s
                .arg(reply->operation() == QNetworkAccessManager::GetOperation ? u"GET"_s : u"POST"_s,
                     reply->url().path(), r.error));
        }
        if (cb)
            cb(r);
    });
}

void BridgeClient::health(Callback cb, int timeoutMs)
{
    get(u"/health"_s, {}, [cb = std::move(cb)](const ApiResult &in) {
        ApiResult r = in;
        if (r.ok() && r.json["app"_L1].toString() != "talon-bridge"_L1) {
            r.kind = ApiResult::Http;
            r.error = u"Something answered, but it isn't a Talon bridge"_s;
        }
        cb(r);
    }, timeoutMs);
}

QNetworkReply *BridgeClient::get(const QString &path, const QUrlQuery &query, Callback cb,
                                 int timeoutMs)
{
    QNetworkReply *reply = m_nam.get(request(path, query, timeoutMs));
    finish(reply, std::move(cb));
    return reply;
}

QNetworkReply *BridgeClient::post(const QString &path, const QJsonObject &body, Callback cb,
                                  int timeoutMs)
{
    QNetworkRequest req = request(path, {}, timeoutMs);
    req.setHeader(QNetworkRequest::ContentTypeHeader, u"application/json"_s);
    QNetworkReply *reply = m_nam.post(req, QJsonDocument(body).toJson(QJsonDocument::Compact));
    finish(reply, std::move(cb));
    return reply;
}

QNetworkReply *BridgeClient::upload(const QString &path, const QUrlQuery &query, QIODevice *body,
                                    qint64 length, const QString &contentType, Callback cb,
                                    std::function<void(qint64, qint64)> progress)
{
    QNetworkRequest req = request(path, query, kTransferIdleMs);
    req.setHeader(QNetworkRequest::ContentTypeHeader, contentType);
    req.setHeader(QNetworkRequest::ContentLengthHeader, length);
    QNetworkReply *reply = m_nam.post(req, body);
    body->setParent(reply);
    if (progress)
        connect(reply, &QNetworkReply::uploadProgress, this, progress);
    finish(reply, std::move(cb));
    return reply;
}

QNetworkReply *BridgeClient::postRaw(const QString &path, const QUrlQuery &query,
                                     const QByteArray &body, const QString &contentType,
                                     std::function<void(const ApiResult &, const QByteArray &)> cb)
{
    QNetworkRequest req = request(path, query, kRequestTimeoutMs);
    req.setHeader(QNetworkRequest::ContentTypeHeader, contentType);
    QNetworkReply *reply = m_nam.post(req, body);
    guard(reply);
    connect(reply, &QNetworkReply::finished, this, [this, reply, cb = std::move(cb)] {
        reply->deleteLater();
        const QByteArray bytes = reply->peek(1024 * 1024);
        const ApiResult r = diagnose(reply, false);
        if (cb)
            cb(r, bytes);
    });
    return reply;
}

QNetworkReply *BridgeClient::rawGet(const QString &path, const QUrlQuery &query, int idleTimeoutMs)
{
    QNetworkReply *reply = m_nam.get(request(path, query, idleTimeoutMs));
    guard(reply);
    return reply;
}

// ── Event stream ────────────────────────────────────────────────────────────

void BridgeClient::openEvents(int connectTimeoutMs)
{
    closeEvents();
    QUrlQuery q;
    if (!m_deviceId.isEmpty())
        q.addQueryItem(u"deviceId"_s, m_deviceId);
    QNetworkRequest req = request(u"/events"_s, q, 0);
    req.setRawHeader("Accept", "text/event-stream");
    req.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);
    // One long response: HTTP/2 multiplexing buys nothing here, and a
    // dedicated HTTP/1.1 socket keeps the stream out of the request pool.
    req.setAttribute(QNetworkRequest::Http2AllowedAttribute, false);

    AppLog::info("bridge", u"opening event stream %1"_s.arg(m_endpoint.baseUrl()));
    QNetworkReply *reply = m_nam.get(req);
    m_events = reply;
    m_parser.reset();
    guard(reply);
    m_connectTimer.start(connectTimeoutMs);

    connect(reply, &QNetworkReply::metaDataChanged, this, [this, reply] {
        if (reply != m_events || m_eventsOpen)
            return;
        if (reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200)
            return; // the body says why; `finished` reports it
        m_connectTimer.stop();
        m_eventsOpen = true;
        m_lastRx = QDateTime::currentMSecsSinceEpoch();
        m_idle.start();
        AppLog::info("bridge", u"event stream open"_s);
        emit eventsOpened();
    });
    connect(reply, &QNetworkReply::readyRead, this, [this, reply] {
        if (reply != m_events || !m_eventsOpen)
            return;
        m_lastRx = QDateTime::currentMSecsSinceEpoch();
        m_idle.start();
        const QByteArrayList frames = m_parser.feed(reply->readAll());
        for (const QByteArray &frame : frames) {
            const QJsonDocument doc = QJsonDocument::fromJson(frame);
            // An unparseable frame is dropped, never fatal: the protocol
            // evolves additively and one bad frame must not end the stream.
            if (doc.isObject())
                emit event(doc.object());
            if (reply != m_events)
                return; // a handler closed the stream
        }
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        if (reply != m_events)
            return;
        ApiResult r = diagnose(reply, !m_eventsOpen);
        if (r.ok()) {
            r.kind = ApiResult::Network;
            r.error = u"The event stream closed"_s;
        } else if (r.kind == ApiResult::Http && r.status != 0 && !m_eventsOpen
                   && r.error.startsWith("Request failed"_L1)) {
            r.error = u"Event stream rejected (%1)"_s.arg(r.status);
        }
        endEvents(r);
    });
}

void BridgeClient::endEvents(const ApiResult &why)
{
    if (!m_events)
        return;
    QNetworkReply *reply = m_events;
    m_events = nullptr;
    m_eventsOpen = false;
    m_idle.stop();
    m_connectTimer.stop();
    reply->disconnect(this);
    reply->abort();
    reply->deleteLater();
    AppLog::warn("bridge", u"event stream ended: %1"_s.arg(why.error));
    emit eventsClosed(why);
}

void BridgeClient::closeEvents()
{
    if (!m_events)
        return;
    QNetworkReply *reply = m_events;
    m_events = nullptr;
    m_eventsOpen = false;
    m_idle.stop();
    m_connectTimer.stop();
    reply->disconnect(this);
    reply->abort();
    reply->deleteLater();
}

void BridgeClient::probeCertificate(const Profile &candidate, QObject *context,
                                    std::function<void(const QString &, bool)> done)
{
    Profile bare = candidate;
    bare.token.clear();
    bare.fingerprint.clear();
    bare.pinObserved = false;
    auto *probe = new BridgeClient(bare, context);
    probe->health([probe, done = std::move(done)](const ApiResult &r) {
        // A proxy demanding a client certificate still answered; the connect
        // that follows reports that properly.
        const bool reached = r.ok() || r.kind == ApiResult::ClientCertRequired;
        done(probe->seenFingerprint(), reached);
        probe->deleteLater();
    }, 6000);
}

} // namespace talos
