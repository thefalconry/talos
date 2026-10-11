// SPDX-License-Identifier: GPL-2.0-only
#include "core/BridgeLinks.h"

#include "core/AppLog.h"
#include "core/Models.h"
#include "core/Session.h"

#include <QDesktopServices>
#include <QDir>
#include <QFileInfo>
#include <QJSEngine>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <memory>

namespace talos {

using namespace Qt::StringLiterals;

namespace {

/// scheme://host:port with default ports made explicit, for comparing.
QString originOf(const QUrl &url)
{
    const QString scheme = url.scheme().toLower();
    if (scheme != "http"_L1 && scheme != "https"_L1)
        return {};
    return u"%1://%2:%3"_s.arg(scheme, url.host().toLower())
        .arg(url.port(scheme == "https"_L1 ? 443 : 80));
}

QString nameFromDisposition(const QByteArray &header)
{
    static const QRegularExpression star(u"filename\\*=(?:UTF-8'')?([^;]+)"_s,
                                         QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression plain(u"filename=\"?([^\";]+)\"?"_s,
                                          QRegularExpression::CaseInsensitiveOption);
    const QString h = QString::fromUtf8(header);
    if (const auto m = star.match(h); m.hasMatch())
        return QUrl::fromPercentEncoding(m.captured(1).trimmed().toUtf8());
    if (const auto m = plain.match(h); m.hasMatch())
        return m.captured(1).trimmed();
    return {};
}

/// A name that is only ever a file name: what a server suggests must never
/// steer the write somewhere else.
QString safeFileName(const QString &suggested)
{
    QString name = QFileInfo(suggested).fileName().trimmed();
    static const QRegularExpression bad(u"[\\x00-\\x1f<>:\"/\\\\|?*]"_s);
    name.replace(bad, u"_"_s);
    while (name.startsWith(u'.'))
        name.remove(0, 1);
    return name.left(180);
}

QString uniquePath(const QString &dir, const QString &name)
{
    const QFileInfo info(name);
    QString candidate = QDir(dir).filePath(name);
    for (int i = 1; QFileInfo::exists(candidate); ++i) {
        candidate = QDir(dir).filePath(
            info.suffix().isEmpty()
                ? u"%1 (%2)"_s.arg(info.completeBaseName()).arg(i)
                : u"%1 (%2).%3"_s.arg(info.completeBaseName()).arg(i).arg(info.suffix()));
    }
    return candidate;
}

} // namespace

BridgeLinks::BridgeLinks(Session *session, Store *store, QObject *parent)
    : QObject(parent), m_session(session), m_store(store)
{
}

void BridgeLinks::reply(const QJSValue &cb, const QVariant &value) const
{
    if (!m_engine || !cb.isCallable())
        return;
    QJSValue fn = cb;
    fn.call({m_engine->toScriptValue(value)});
}

bool BridgeLinks::isBridgeUrl(const QUrl &url) const
{
    const QString origin = originOf(url);
    if (origin.isEmpty() || !m_session->client())
        return false;
    // The address in use, the profile's public address and its LAN address
    // all reach the same bridge.
    const Profile saved = m_store->activeProfileMeta();
    QStringList mine{originOf(QUrl(m_session->activeEndpoint().baseUrl()))};
    if (!saved.local) {
        mine << originOf(QUrl(saved.baseUrl()));
        if (const auto lan = saved.lanEndpoint())
            mine << originOf(QUrl(lan->baseUrl()));
    }
    return mine.contains(origin);
}

QString BridgeLinks::downloadDir() const
{
    if (!m_downloadDir.isEmpty())
        return m_downloadDir;
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    return dir.isEmpty() ? QDir::homePath() : dir;
}

QVariantMap BridgeLinks::inspect(const QString &link) const
{
    const QString text = link.trimmed();
    if (!findPairLink(text).isEmpty())
        return {{u"kind"_s, u"pair"_s}, {u"link"_s, findPairLink(text)}};
    const QUrl url(text);
    if (!isBridgeUrl(url))
        return {{u"kind"_s, u"external"_s}, {u"link"_s, text}};
    const bool secret = url.path() == "/secret"_L1 && QUrlQuery(url).hasQueryItem(u"grant"_s);
    return {
        {u"kind"_s, secret ? u"secret"_s : u"bridge"_s},
        {u"link"_s, text},
        {u"path"_s, url.path()},
    };
}

void BridgeLinks::download(const QString &link, QJSValue done)
{
    const QUrl url(link.trimmed());
    BridgeClient *client = m_session->client();
    if (!client || !isBridgeUrl(url)) {
        reply(done, QVariantMap{{u"ok"_s, false}, {u"error"_s, u"Not connected to that server"_s}});
        return;
    }
    if (m_active.contains(link))
        return;

    // Only the path and query are taken from the link; the request goes to
    // the endpoint that is connected, with its certificate and pin.
    QNetworkReply *net = client->rawGet(url.path().isEmpty() ? u"/"_s : url.path(),
                                        QUrlQuery(url.query(QUrl::FullyEncoded)));
    m_active.insert(link, net);

    struct State {
        std::unique_ptr<QSaveFile> file;
        QString path;
        QString name;
        QByteArray page;
        bool decided = false;
        bool html = false;
        QString failure;
        qint64 received = 0;
    };
    auto st = std::make_shared<State>();
    const QString dir = downloadDir();

    const auto decide = [st, net, url, dir] {
        if (st->decided)
            return;
        st->decided = true;
        const QString type =
            net->header(QNetworkRequest::ContentTypeHeader).toString().toLower();
        st->html = type.startsWith("text/html"_L1);
        if (st->html)
            return;
        QString name = safeFileName(nameFromDisposition(net->rawHeader("Content-Disposition")));
        if (name.isEmpty())
            name = safeFileName(url.path().section(u'/', -1));
        if (name.isEmpty())
            name = u"talon-download"_s;
        QDir().mkpath(dir);
        st->path = uniquePath(dir, name);
        st->name = QFileInfo(st->path).fileName();
        st->file = std::make_unique<QSaveFile>(st->path);
        if (!st->file->open(QIODevice::WriteOnly))
            st->failure = u"Couldn't write to %1"_s.arg(dir);
    };

    connect(net, &QNetworkReply::readyRead, this, [this, net, st, decide, link] {
        if (net->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() != 200)
            return;
        decide();
        const QByteArray chunk = net->readAll();
        st->received += chunk.size();
        if (st->html) {
            // A page is shown, not saved; cap what a broken server can send.
            if (st->page.size() < 2 * 1024 * 1024)
                st->page += chunk;
            return;
        }
        if (!st->failure.isEmpty() || st->file->write(chunk) != chunk.size()) {
            if (st->failure.isEmpty())
                st->failure = u"The disk refused the write"_s;
            net->abort();
            return;
        }
        const qint64 total = net->header(QNetworkRequest::ContentLengthHeader).toLongLong();
        emit progress(link, total > 0 ? double(st->received) / double(total) : -1.0,
                      total > 0 ? u"%1 of %2"_s.arg(formatBytes(st->received), formatBytes(total))
                                : formatBytes(st->received));
    });

    const QPointer<BridgeClient> owner = client;
    connect(net, &QNetworkReply::finished, this, [this, net, st, owner, link, done] {
        net->deleteLater();
        const bool cancelled = !m_active.remove(link);
        ApiResult r;
        if (owner) {
            r = owner->diagnose(net, false);
        } else {
            r.kind = ApiResult::Network;
            r.error = u"Disconnected"_s;
        }
        QString error = cancelled ? u"Cancelled"_s : st->failure;
        if (error.isEmpty() && !r.ok())
            error = r.status == 404 ? u"That link has expired or was already used."_s : r.error;
        if (error.isEmpty() && !st->html) {
            const QVariant length = net->header(QNetworkRequest::ContentLengthHeader);
            if (st->file && length.isValid() && length.toLongLong() != st->received)
                error = u"The download was cut short"_s;
            else if (!st->file)
                error = u"The server sent nothing"_s;
        }
        if (!error.isEmpty()) {
            if (st->file)
                st->file->cancelWriting();
            reply(done, QVariantMap{{u"ok"_s, false}, {u"error"_s, error},
                                    {u"cancelled"_s, cancelled}});
            return;
        }
        if (st->html) {
            const QString html = QString::fromUtf8(st->page);
            reply(done, QVariantMap{{u"ok"_s, true}, {u"html"_s, html},
                                    {u"pairLink"_s, findPairLink(html)}});
            return;
        }
        if (!st->file->commit()) {
            reply(done, QVariantMap{{u"ok"_s, false}, {u"error"_s, u"Couldn't finish writing the file"_s}});
            return;
        }
        AppLog::info("links", u"downloaded %1 through the bridge connection"_s.arg(st->name));
        reply(done, QVariantMap{{u"ok"_s, true}, {u"file"_s, st->path}, {u"name"_s, st->name},
                                {u"size"_s, formatBytes(st->received)}});
    });
}

void BridgeLinks::cancel(const QString &link)
{
    if (const auto net = m_active.take(link))
        net->abort();
}

void BridgeLinks::checkSecretDrop(const QString &link, QJSValue done)
{
    const QUrl url(link.trimmed());
    BridgeClient *client = m_session->client();
    if (!client || !isBridgeUrl(url)) {
        reply(done, QVariantMap{{u"live"_s, false}, {u"error"_s, u"Not connected to that server"_s}});
        return;
    }
    // GET shows the form without spending the grant.
    QNetworkReply *net = client->rawGet(u"/secret"_s, QUrlQuery(url.query(QUrl::FullyEncoded)),
                                        BridgeClient::kRequestTimeoutMs);
    const QPointer<BridgeClient> owner = client;
    connect(net, &QNetworkReply::finished, this, [this, net, owner, done] {
        net->deleteLater();
        const QString page = QString::fromUtf8(net->peek(256 * 1024));
        ApiResult r;
        if (owner)
            r = owner->diagnose(net, false);
        // The form names what is being asked for; lift that for the dialog.
        static const QRegularExpression title(u"<h1[^>]*>(.*?)</h1>"_s,
                                              QRegularExpression::DotMatchesEverythingOption);
        static const QRegularExpression tags(u"<[^>]+>"_s);
        QString heading = title.match(page).captured(1);
        heading.remove(tags);
        reply(done, QVariantMap{
            {u"live"_s, owner && r.ok()},
            {u"heading"_s, unescapeHtml(heading.simplified())},
            {u"error"_s, r.status == 404 ? u"That link has expired or was already used."_s
                                         : r.error},
        });
    });
}

void BridgeLinks::submitSecretDrop(const QString &link, const QString &value, QJSValue done)
{
    const QUrl url(link.trimmed());
    BridgeClient *client = m_session->client();
    if (!client || !isBridgeUrl(url) || value.isEmpty()) {
        reply(done, QVariantMap{{u"ok"_s, false}, {u"error"_s, u"Nothing to send"_s}});
        return;
    }
    client->postRaw(u"/secret"_s, QUrlQuery(url.query(QUrl::FullyEncoded)), value.toUtf8(),
                    u"text/plain; charset=utf-8"_s,
                    [this, done](const ApiResult &r, const QByteArray &) {
        // The grant is spent even when the write fails: a failure means
        // "ask for a new link", never "try this one again".
        QString error = r.error;
        if (r.status == 404)
            error = u"That link has expired or was already used."_s;
        reply(done, QVariantMap{{u"ok"_s, r.ok()}, {u"error"_s, error}});
    });
}

void BridgeLinks::openExternally(const QString &link)
{
    const QUrl url(link.trimmed());
    static const QStringList allowed{u"http"_s, u"https"_s, u"mailto"_s, u"tel"_s, u"geo"_s};
    // Only what a browser or the system would open anyway: never a local
    // file or a custom scheme a message smuggled in.
    if (url.isValid() && allowed.contains(url.scheme().toLower()))
        QDesktopServices::openUrl(url);
}

void BridgeLinks::openFile(const QString &file)
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(file));
}

void BridgeLinks::revealFile(const QString &file)
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(file).absolutePath()));
}

} // namespace talos
