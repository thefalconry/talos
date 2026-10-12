// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QList>
#include <QString>
#include <QStringList>
#include <QUrl>
#include <optional>

namespace talos {

/// Pieces pulled out of whatever a user typed into a "server" field: a bare
/// IP, host:port, or a whole URL with a path.
struct HostInput {
    QString host;
    int port = -1; ///< -1 when none was written
    int tls = -1;  ///< -1 unknown, 0 http, 1 https
};

/// One more way to reach the same bridge, advertised by the daemon in its
/// reply to a mesh registration: a second name, or the same name dialled
/// at a fixed address so a DNS outage doesn't take the connection down.
struct Endpoint {
    /// Base URL. Its host is the TLS server name and the Host header, so a
    /// reverse proxy still routes the request to the bridge.
    QString url;
    /// "ip:port" to connect to instead of resolving the URL's host.
    QString dial;
    QString label;

    bool operator==(const Endpoint &) const = default;
};

/// The usable entries of a register reply's `endpoints` object (at most
/// eight; anything malformed is dropped). @p version receives its `v`.
QList<Endpoint> parseEndpoints(const QJsonObject &advertised, QString *version);

/// How this client reaches one Talon daemon.
///
/// A *local* profile reads the bridge address and token from the discovery
/// file the daemon writes (`~/.talon/native-bridge.json`), optionally
/// launching the daemon when none is running. A *remote* profile carries the
/// address and credentials itself.
struct Profile {
    QString id;
    QString name;

    bool local = true;          ///< discover the bridge on this machine
    bool manageDaemon = false;  ///< local only: launch Talon when it isn't running
    QString launchCommand = QStringLiteral("talon");
    QStringList launchArgs{QStringLiteral("start")};

    QString host = QStringLiteral("127.0.0.1");
    int port = 19880;
    bool tls = false;

    /// Shared bridge token or this device's own `tdc1.…` credential.
    QString token;

    /// Pinned bridge certificate: SHA-256 of its DER, lowercase hex. Empty
    /// until the first TLS connect adopts one.
    QString fingerprint;
    /// Set once the bridge has been seen presenting exactly the pinned
    /// certificate. From then on the pin is enforced on every certificate,
    /// including one a public CA would vouch for.
    bool pinObserved = false;

    /// PKCS#12 client certificate for a reverse proxy doing mutual TLS.
    QByteArray clientP12;
    QString clientP12Password;
    /// Display only: a certificate is in the secret store for this profile,
    /// whether or not its bytes are loaded right now.
    bool certStored = false;

    /// The bridge's address on the home network; preferred whenever it
    /// answers. The pin belongs to this address when it is set.
    QString localUrl;

    /// Fallback addresses learned from the daemon, and the version it gave
    /// the list. Never the configured address itself.
    QList<Endpoint> endpoints;
    QString endpointsVersion;
    /// Set only on a profile made by viaEndpoint: "ip:port" to connect to
    /// while still presenting `host` as the server name.
    QString dial;

    bool isLoopback() const;
    bool hasClientCert() const { return !clientP12.isEmpty(); }
    bool enforcesPin() const { return tls && !fingerprint.isEmpty() && pinObserved; }
    static int defaultPort(bool tls) { return tls ? 443 : 80; }
    QString baseUrl() const;
    QUrl url(const QString &path) const;
    QString displayAddress() const;
    /// Identity of the bridge, for grants that must not carry over to another.
    QString bridgeKey() const { return host.toLower() + QLatin1Char(':') + QString::number(port); }

    /// This profile pointed at its LAN address, when it has one.
    std::optional<Profile> lanEndpoint() const;
    /// The main address; the pin stays with the LAN address when there is one.
    Profile wanEndpoint() const;
    /// This profile pointed at a learned fallback, or nothing when that
    /// would be unsafe: a fallback must be https and must present the
    /// certificate already pinned, so without a pin there are none.
    std::optional<Profile> viaEndpoint(const Endpoint &endpoint) const;
    /// The learned fallbacks this profile may actually use, in order.
    QList<Endpoint> usableEndpoints() const;

    QJsonObject toJson(bool withSecrets) const;
    QJsonObject secretsJson() const;
    void applySecrets(const QJsonObject &secrets);
    static Profile fromJson(const QJsonObject &json);
    static Profile defaults();
};

HostInput parseHostInput(const QString &raw);

/// Lowercase hex without separators, or empty when it isn't a SHA-256 digest.
QString normalizeFingerprint(const QString &raw);
/// `AA:BB:…` for a person to compare against what the daemon prints.
QString formatFingerprint(const QString &fingerprint);

bool isPlainHostName(const QString &host);
bool isPrivateAddress(const QString &host);
bool isDeviceCredential(const QString &token);

/// The first `talon://pair?…` link inside pasted text, or empty.
QString findPairLink(const QString &text);
/// True for the daemon's pairing *page* (`https://…/pair?grant=…`), whose
/// single-use grant only a browser can redeem.
bool isPairPageUrl(const QString &text);
/// A profile from a `talon://pair` link. Only a link that pins what it
/// points at is accepted: https needs a certificate fingerprint, plain http
/// is allowed on private addresses only.
std::optional<Profile> profileFromPairLink(const QString &link);

/// Strip credentials (tokens in URLs, bearer values) from text bound for a
/// log line or an error banner.
QString redactSecrets(const QString &text);

} // namespace talos
