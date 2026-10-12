// SPDX-License-Identifier: GPL-2.0-only
#include "core/Profile.h"

#include <QHostAddress>
#include <QJsonArray>
#include <QRegularExpression>
#include <QUrlQuery>
#include <QUuid>

namespace talos {

using namespace Qt::StringLiterals;

bool Profile::isLoopback() const
{
    return host == "127.0.0.1"_L1 || host == "localhost"_L1 || host == "::1"_L1;
}

QString Profile::baseUrl() const
{
    const QString scheme = tls ? u"https"_s : u"http"_s;
    const QString h = host.contains(u':') ? u'[' + host + u']' : host;
    if (port == defaultPort(tls))
        return scheme + "://"_L1 + h;
    return scheme + "://"_L1 + h + u':' + QString::number(port);
}

QUrl Profile::url(const QString &path) const
{
    return QUrl(baseUrl() + path);
}

QString Profile::displayAddress() const
{
    if (local)
        return u"This computer"_s;
    return port == defaultPort(tls) ? host : host + u':' + QString::number(port);
}

std::optional<Profile> Profile::lanEndpoint() const
{
    const QString raw = localUrl.trimmed();
    if (raw.isEmpty())
        return std::nullopt;
    const HostInput in = parseHostInput(raw);
    if (in.host.isEmpty())
        return std::nullopt;
    Profile p = *this;
    // Off loopback the bridge serves TLS unless told otherwise.
    p.tls = in.tls != 0;
    p.host = in.host;
    p.port = in.port > 0 ? in.port : defaultPort(p.tls);
    p.localUrl.clear();
    return p;
}

namespace {

/// "203.0.113.7:443" or "[2001:db8::1]:443": an IP literal and a port.
bool splitDial(const QString &dial, QString *ip, int *port)
{
    const QUrl u(u"//"_s + dial.trimmed());
    QString host = u.host();
    if (!u.isValid() || host.isEmpty() || u.port() <= 0 || !u.path().isEmpty())
        return false;
    if (host.startsWith(u'['))
        host = host.mid(1, host.size() - 2);
    if (QHostAddress(host).isNull())
        return false;
    *ip = host;
    *port = u.port();
    return true;
}

} // namespace

QList<Endpoint> parseEndpoints(const QJsonObject &advertised, QString *version)
{
    if (version)
        *version = advertised["v"_L1].toString();
    QList<Endpoint> out;
    for (const QJsonValue v : advertised["list"_L1].toArray()) {
        const QJsonObject o = v.toObject();
        Endpoint e{o["url"_L1].toString().trimmed(), o["dial"_L1].toString().trimmed(),
                   o["label"_L1].toString().simplified().left(60)};
        const QUrl url(e.url);
        const QString scheme = url.scheme().toLower();
        if (!url.isValid() || url.host().isEmpty()
            || (scheme != "https"_L1 && scheme != "http"_L1))
            continue;
        QString ip;
        int port = 0;
        if (!e.dial.isEmpty() && !splitDial(e.dial, &ip, &port))
            continue;
        if (!out.contains(e))
            out << e;
        if (out.size() == 8)
            break;
    }
    return out;
}

std::optional<Profile> Profile::viaEndpoint(const Endpoint &endpoint) const
{
    // Trust on first use only ever happens at the configured address. A
    // fallback is believed because it shows the certificate already pinned.
    if (local || !tls || fingerprint.isEmpty())
        return std::nullopt;
    const QUrl url(endpoint.url);
    if (url.scheme().toLower() != "https"_L1 || url.host().isEmpty())
        return std::nullopt;
    Profile p = *this;
    p.host = url.host().toLower();
    p.port = url.port(defaultPort(true));
    p.tls = true;
    p.pinObserved = true;
    p.localUrl.clear();
    p.dial = endpoint.dial;
    return p;
}

QList<Endpoint> Profile::usableEndpoints() const
{
    QList<Endpoint> out;
    for (const Endpoint &e : endpoints) {
        const auto p = viaEndpoint(e);
        if (!p)
            continue;
        // The daemon lists its public address too; that is where we already are.
        if (p->dial.isEmpty() && p->host == host.toLower() && p->port == port)
            continue;
        out << e;
    }
    return out;
}

Profile Profile::wanEndpoint() const
{
    if (localUrl.trimmed().isEmpty())
        return *this;
    Profile p = *this;
    p.fingerprint.clear();
    p.pinObserved = false;
    p.localUrl.clear();
    return p;
}

QJsonObject Profile::secretsJson() const
{
    QJsonObject o;
    if (!token.isEmpty())
        o["token"_L1] = token;
    if (!clientP12.isEmpty()) {
        o["clientP12"_L1] = QString::fromLatin1(clientP12.toBase64());
        o["clientP12Password"_L1] = clientP12Password;
    }
    return o;
}

void Profile::applySecrets(const QJsonObject &s)
{
    token = s["token"_L1].toString();
    clientP12 = QByteArray::fromBase64(s["clientP12"_L1].toString().toLatin1());
    clientP12Password = s["clientP12Password"_L1].toString();
}

QJsonObject Profile::toJson(bool withSecrets) const
{
    QJsonObject o{
        {"id"_L1, id},
        {"name"_L1, name},
        {"local"_L1, local},
        {"manageDaemon"_L1, manageDaemon},
        {"launchCommand"_L1, launchCommand},
        {"launchArgs"_L1, QJsonArray::fromStringList(launchArgs)},
        {"host"_L1, host},
        {"port"_L1, port},
        {"tls"_L1, tls},
        {"fingerprint"_L1, fingerprint},
        {"pinObserved"_L1, pinObserved},
        {"localUrl"_L1, localUrl},
        {"hasClientCert"_L1, hasClientCert() || certStored},
    };
    if (!endpoints.isEmpty() || !endpointsVersion.isEmpty()) {
        QJsonArray list;
        for (const Endpoint &e : endpoints) {
            QJsonObject entry{{"url"_L1, e.url}};
            if (!e.dial.isEmpty())
                entry["dial"_L1] = e.dial;
            if (!e.label.isEmpty())
                entry["label"_L1] = e.label;
            list << entry;
        }
        o["endpoints"_L1] = QJsonObject{{"v"_L1, endpointsVersion}, {"list"_L1, list}};
    }
    if (withSecrets) {
        const QJsonObject s = secretsJson();
        for (auto it = s.begin(); it != s.end(); ++it)
            o[it.key()] = it.value();
    }
    return o;
}

Profile Profile::fromJson(const QJsonObject &j)
{
    Profile p;
    p.id = j["id"_L1].toString();
    p.name = j["name"_L1].toString();
    p.local = j["local"_L1].toBool(true);
    p.manageDaemon = j["manageDaemon"_L1].toBool(false);
    p.launchCommand = j["launchCommand"_L1].toString(u"talon"_s);
    if (j["launchArgs"_L1].isArray()) {
        p.launchArgs.clear();
        for (const auto &v : j["launchArgs"_L1].toArray())
            p.launchArgs << v.toString();
    }
    p.host = j["host"_L1].toString(u"127.0.0.1"_s);
    p.port = j["port"_L1].toInt(19880);
    p.tls = j["tls"_L1].toBool(false);
    p.fingerprint = normalizeFingerprint(j["fingerprint"_L1].toString());
    p.pinObserved = j["pinObserved"_L1].toBool(false) && !p.fingerprint.isEmpty();
    p.localUrl = j["localUrl"_L1].toString();
    p.endpoints = parseEndpoints(j["endpoints"_L1].toObject(), &p.endpointsVersion);
    p.applySecrets(j);
    p.certStored = j["hasClientCert"_L1].toBool() || p.hasClientCert();
    return p;
}

Profile Profile::defaults()
{
    Profile p;
    p.id = QUuid::createUuid().toString(QUuid::WithoutBraces);
#if defined(Q_OS_ANDROID) || defined(Q_OS_IOS)
    p.local = false;
    p.host.clear();
#endif
    return p;
}

HostInput parseHostInput(const QString &raw)
{
    HostInput out;
    QString s = raw.trimmed();
    if (s.isEmpty())
        return out;
    static const QRegularExpression schemeRe(u"^([a-zA-Z][a-zA-Z0-9+.-]*)://"_s);
    if (const auto m = schemeRe.match(s); m.hasMatch()) {
        const QString scheme = m.captured(1).toLower();
        if (scheme == "https"_L1 || scheme == "wss"_L1)
            out.tls = 1;
        else if (scheme == "http"_L1 || scheme == "ws"_L1)
            out.tls = 0;
        s = s.mid(m.capturedEnd());
    }
    // Authority only: drop path, query and fragment, and any userinfo.
    s = s.section(u'/', 0, 0).section(u'?', 0, 0).section(u'#', 0, 0).trimmed();
    if (const int at = s.lastIndexOf(u'@'); at >= 0)
        s = s.mid(at + 1);

    static const QRegularExpression v6Re(u"^\\[(.+)\\](?::(\\d+))?$"_s);
    if (const auto m = v6Re.match(s); m.hasMatch()) {
        s = m.captured(1);
        if (!m.captured(2).isEmpty())
            out.port = m.captured(2).toInt();
    } else if (s.count(u':') == 1) {
        // host:port. More than one colon is a bare IPv6 literal.
        bool ok = false;
        const int port = s.section(u':', 1).toInt(&ok);
        if (ok && port >= 1 && port <= 65535) {
            out.port = port;
            s = s.section(u':', 0, 0);
        }
    }
    out.host = s.trimmed();
    return out;
}

QString normalizeFingerprint(const QString &raw)
{
    QString s = raw.toLower();
    static const QRegularExpression sep(u"[\\s:]"_s);
    s.remove(sep);
    static const QRegularExpression hex(u"^[0-9a-f]{64}$"_s);
    return hex.match(s).hasMatch() ? s : QString();
}

QString formatFingerprint(const QString &fingerprint)
{
    const QString hex = fingerprint.toUpper();
    QStringList parts;
    for (int i = 0; i + 1 < hex.size(); i += 2)
        parts << hex.mid(i, 2);
    return parts.join(u':');
}

bool isPlainHostName(const QString &host)
{
    if (host.isEmpty() || host.size() > 253)
        return false;
    if (host.contains(u':')) {
        static const QRegularExpression v6(u"^[0-9a-f:.]+$"_s);
        QHostAddress a;
        return v6.match(host).hasMatch() && a.setAddress(host);
    }
    static const QRegularExpression name(
        u"^[a-z0-9]([a-z0-9-]*[a-z0-9])?(\\.[a-z0-9]([a-z0-9-]*[a-z0-9])?)*$"_s);
    return name.match(host).hasMatch();
}

bool isPrivateAddress(const QString &host)
{
    if (host == "localhost"_L1)
        return true;
    // A hostname is never private: whoever answers the DNS query decides
    // where it points, not the link that named it.
    QHostAddress a;
    if (!a.setAddress(host))
        return false;
    return a.isLoopback() || a.isLinkLocal() || a.isPrivateUse();
}

bool isDeviceCredential(const QString &token)
{
    static const QRegularExpression re(u"^tdc1\\.[0-9a-f]{16}\\.[A-Za-z0-9_-]{43}$"_s);
    return re.match(token).hasMatch();
}

QString findPairLink(const QString &text)
{
    static const QRegularExpression re(u"talon:(?://)?pair/?\\?[^\\s\"'<>]+"_s,
                                       QRegularExpression::CaseInsensitiveOption);
    const auto m = re.match(text);
    return m.hasMatch() ? m.captured(0) : QString();
}

bool isPairPageUrl(const QString &text)
{
    const QUrl u(text.trimmed());
    if (!u.isValid())
        return false;
    const QString scheme = u.scheme().toLower();
    QString path = u.path();
    path.remove(u'/');
    return (scheme == "https"_L1 || scheme == "http"_L1) && path == "pair"_L1
        && QUrlQuery(u).hasQueryItem(u"grant"_s);
}

std::optional<Profile> profileFromPairLink(const QString &link)
{
    const QUrl u(link.trimmed());
    if (!u.isValid() || u.scheme().toLower() != "talon"_L1)
        return std::nullopt;
    // `talon://pair?…` puts "pair" in the host, `talon:pair?…` in the path.
    QString target = u.host().isEmpty() ? u.path() : u.host();
    target.remove(u'/');
    if (target.toLower() != "pair"_L1)
        return std::nullopt;

    const QUrlQuery q(u);
    const QString bridge = q.queryItemValue(u"u"_s, QUrl::FullyDecoded).trimmed();
    if (bridge.isEmpty())
        return std::nullopt;
    const HostInput in = parseHostInput(bridge);
    // The scheme decides which safety rule applies, so it is never guessed.
    if (in.tls < 0)
        return std::nullopt;
    const QString host = in.host.toLower();
    if (!isPlainHostName(host))
        return std::nullopt;

    Profile p = Profile::defaults();
    p.local = false;
    p.manageDaemon = false;
    p.host = host;
    p.tls = in.tls == 1;
    p.port = in.port > 0 ? in.port : Profile::defaultPort(p.tls);
    p.token = q.queryItemValue(u"t"_s, QUrl::FullyDecoded).trimmed();
    if (p.tls) {
        p.fingerprint = normalizeFingerprint(q.queryItemValue(u"f"_s, QUrl::FullyDecoded));
        if (p.fingerprint.isEmpty())
            return std::nullopt;
    } else if (!isPrivateAddress(host)) {
        return std::nullopt;
    }
// The daemon's own label for the connection ("Car", "Phone"), when the
    // link carries one.
    const QString label = q.queryItemValue(u"n"_s, QUrl::FullyDecoded).simplified().left(60);
    p.name = label.isEmpty() ? host : label;
    return p;
}

QString redactSecrets(const QString &text)
{
    QString out = text;
    static const QRegularExpression query(u"([?&](?:token|transfer|grant|t)=)[^&\\s\"']+"_s,
                                          QRegularExpression::CaseInsensitiveOption);
    out.replace(query, u"\\1<redacted>"_s);
    static const QRegularExpression bearer(u"(Bearer\\s+)[A-Za-z0-9._~+/=-]+"_s,
                                           QRegularExpression::CaseInsensitiveOption);
    out.replace(bearer, u"\\1<redacted>"_s);
    static const QRegularExpression cred(u"tdc1\\.[0-9a-f]{16}\\.[A-Za-z0-9_-]{43}"_s);
    out.replace(cred, u"tdc1.<redacted>"_s);
    return out;
}

} // namespace talos
