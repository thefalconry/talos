// SPDX-License-Identifier: GPL-2.0-only
#include "core/Session.h"

#include "core/AppLog.h"

#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QJSEngine>
#include <QJsonDocument>
#include <QMimeDatabase>
#include <QNetworkInformation>
#include <QProcess>
#include <QRegularExpression>
#include <memory>

namespace talos {

using namespace Qt::StringLiterals;

namespace {

qint64 nowMs()
{
    return QDateTime::currentMSecsSinceEpoch();
}

/// A query value, fully percent-encoded so `+`, `&` and `%` in file names
/// and search text survive the trip.
QString qv(const QString &value)
{
    return QString::fromLatin1(QUrl::toPercentEncoding(value));
}

QUrlQuery query(std::initializer_list<std::pair<QString, QString>> items)
{
    QUrlQuery q;
    for (const auto &[k, v] : items) {
        if (!v.isEmpty())
            q.addQueryItem(k, qv(v));
    }
    return q;
}

struct LocalBridge {
    int port = 0;
    QString token;
    QString scheme;
    QString fingerprint;
};

/// The discovery file a running daemon writes for same-machine clients.
std::optional<LocalBridge> readLocalBridge()
{
    QString root = qEnvironmentVariable("TALON_HOME");
    if (root.isEmpty())
        root = QDir::home().filePath(u".talon"_s);
    QFile f(QDir(root).filePath(u"native-bridge.json"_s));
    if (!f.open(QIODevice::ReadOnly))
        return std::nullopt;
    const QJsonObject o = QJsonDocument::fromJson(f.readAll()).object();
    LocalBridge b;
    b.port = o["port"_L1].toInt();
    if (b.port <= 0 || b.port > 65535)
        return std::nullopt;
    b.token = o["token"_L1].toString();
    b.scheme = o["scheme"_L1].toString(u"http"_s);
    b.fingerprint = normalizeFingerprint(o["fingerprint"_L1].toString());
    return b;
}

QVariantMap statusFromJson(const QJsonObject &j)
{
    QStringList caps;
    for (const auto &c : j["capabilities"_L1].toArray())
        caps << c.toString();
    return {
        {u"protocol"_s, j["protocol"_L1].toInt(1)},
        {u"botName"_s, j["botName"_L1].toString(u"Talon"_s)},
        {u"backend"_s, j["backend"_L1].toString()},
        {u"model"_s, j["model"_L1].toString()},
        {u"activeChats"_s, j["activeChats"_L1].toInt()},
        {u"startedAt"_s, j["startedAt"_L1].toString()},
        {u"capabilities"_s, caps},
    };
}

} // namespace

Session::Session(Store *store, QObject *parent, bool narrow)
    : QObject(parent), m_store(store), m_narrow(narrow)
{
    m_chatFilter.setSourceModel(&m_chats);
    m_chats.isUnread = [this](const Chat &c) {
        return c.id != m_selected && c.lastActive > m_store->lastRead(c.id);
    };
    m_chats.isRunning = [this](const Chat &c) { return isTurnRunning(c.id); };
    const auto countChanged = [this] { emit chatCountChanged(); };
    connect(&m_chats, &QAbstractItemModel::rowsInserted, this, countChanged);
    connect(&m_chats, &QAbstractItemModel::rowsRemoved, this, countChanged);
    connect(&m_chats, &QAbstractItemModel::modelReset, this, countChanged);

    m_retry.setSingleShot(true);
    connect(&m_retry, &QTimer::timeout, this, &Session::start);
    m_failback.setInterval(5 * 60 * 1000);
    connect(&m_failback, &QTimer::timeout, this, &Session::probeConfigured);

    m_snapshotTimer.setSingleShot(true);
    m_snapshotTimer.setInterval(2000);
    connect(&m_snapshotTimer, &QTimer::timeout, this, &Session::saveSnapshot);

    m_networkDebounce.setSingleShot(true);
    m_networkDebounce.setInterval(2000);
    connect(&m_networkDebounce, &QTimer::timeout, this, [this] {
        // A stream opened on the old interface goes half-open after a
        // switch and never errors by itself; reopen it on the new one. A
        // fatal stop stays stopped.
        if (m_conn == Connected || m_retry.isActive()) {
            AppLog::info("session", u"network changed; reconnecting"_s);
            m_backoffMs = 800;
            start();
        }
    });
    watchNetwork();
    hydrate();
}

Session::~Session()
{
    if (m_snapshotTimer.isActive())
        saveSnapshot();
}

void Session::watchNetwork()
{
    if (!QNetworkInformation::loadDefaultBackend())
        return;
    QNetworkInformation *net = QNetworkInformation::instance();
    if (!net)
        return;
    connect(net, &QNetworkInformation::reachabilityChanged, this,
            [this](QNetworkInformation::Reachability r) {
        if (r != QNetworkInformation::Reachability::Disconnected)
            m_networkDebounce.start();
    });
    connect(net, &QNetworkInformation::transportMediumChanged, this,
            [this] { m_networkDebounce.start(); });
}

void Session::reply(const QJSValue &cb, const QVariant &value) const
{
    if (!m_engine || !cb.isCallable())
        return;
    QJSValue fn = cb;
    fn.call({m_engine->toScriptValue(value)});
}

// ── Read-only state ─────────────────────────────────────────────────────────

QString Session::botName() const
{
    const QString name = m_status.value(u"botName"_s).toString();
    return name.isEmpty() ? u"Talon"_s : name;
}

Profile Session::activeEndpoint() const
{
    return m_client ? m_client->endpoint() : m_store->activeProfile();
}

QVariantMap Session::endpointVariant() const
{
    const Profile saved = m_store->activeProfileMeta();
    const Profile e = m_client ? m_client->endpoint() : saved;
    return {
        {u"id"_s, saved.id},
        {u"name"_s, saved.name},
        {u"local"_s, saved.local},
        {u"address"_s, saved.local ? u"This computer (port %1)"_s.arg(e.port)
                                   : e.displayAddress()},
        {u"baseUrl"_s, e.baseUrl()},
        {u"tls"_s, e.tls},
        {u"fingerprint"_s, formatFingerprint(e.fingerprint)},
        {u"pinEnforced"_s, e.enforcesPin()},
        {u"hasClientCert"_s, e.hasClientCert()},
        {u"deviceCredential"_s, isDeviceCredential(e.token)},
        {u"hasToken"_s, !e.token.isEmpty()},
        {u"bridgeKey"_s, saved.bridgeKey()},
        // On one of the daemon's fallback addresses instead of the one saved.
        {u"fallback"_s, m_client && m_endpointIndex > 0},
        {u"fallbacks"_s, int(saved.usableEndpoints().size())},
    };
}

QVariantMap Session::chat() const
{
    const Chat *c = m_chats.find(m_selected);
    if (!c)
        return {};
    QVariantMap m = c->toVariant();
    m[u"running"_s] = isTurnRunning(c->id);
    return m;
}

bool Session::historyLoading() const
{
    const auto it = m_data.find(m_selected);
    return it != m_data.end() && it->second.loading && it->second.messages.isEmpty();
}

bool Session::loadingOlder() const
{
    const auto it = m_data.find(m_selected);
    return it != m_data.end() && it->second.loadingOlder;
}

bool Session::hasMoreHistory() const
{
    const auto it = m_data.find(m_selected);
    return it != m_data.end() && it->second.loaded && !it->second.exhausted;
}

bool Session::hasCapability(const QString &name) const
{
    return m_status.value(u"capabilities"_s).toStringList().contains(name);
}

bool Session::isTurnRunning(const QString &chatId) const
{
    const auto it = m_data.find(chatId);
    return it != m_data.end() && it->second.turn.active;
}

const QList<Message> *Session::messagesOf(const QString &chatId) const
{
    const auto it = m_data.find(chatId);
    return it == m_data.end() ? nullptr : &it->second.messages;
}

void Session::setNarrow(bool narrow)
{
    if (m_narrow == narrow)
        return;
    m_narrow = narrow;
    emit narrowChanged();
    // Two panes with nothing open is dead space; one pane with nothing open
    // is the chat list, and stays that way.
    if (!narrow && m_selected.isEmpty() && !m_chats.chats().isEmpty())
        selectChat(m_chats.chats().first().id);
}

// ── Connection ──────────────────────────────────────────────────────────────

void Session::setConn(Conn conn, const QString &error, bool fatal)
{
    m_conn = conn;
    m_connError = redactSecrets(error);
    m_connFatal = fatal;
    if (conn != Connecting)
        m_connNote.clear();
    emit connChanged();
}

void Session::dropClient()
{
    for (const auto &reply : std::as_const(m_uploads)) {
        if (reply)
            reply->abort();
    }
    m_uploads.clear();
    if (!m_client)
        return;
    emit clientGone();
    BridgeClient *client = m_client;
    m_client = nullptr;
    client->disconnect(this);
    client->closeEvents();
    client->deleteLater();
}

void Session::start()
{
    m_retry.stop();
    const int epoch = ++m_epoch;
    if (!m_store->hasProfile()) {
        dropClient();
        setConn(Idle);
        return;
    }
    const Profile profile = m_store->activeProfile();
    setConn(Connecting);

    if (profile.local) {
        const auto bridge = readLocalBridge();
        if (!bridge) {
            dropClient();
            const qint64 now = nowMs();
            if (profile.manageDaemon) {
                if (now - m_lastLaunchMs > 60'000) {
                    m_lastLaunchMs = now;
                    AppLog::info("session", u"launching %1"_s.arg(profile.launchCommand));
                    if (!QProcess::startDetached(profile.launchCommand, profile.launchArgs)) {
                        setConn(Error, u"Couldn't run “%1”. Is Talon installed and on your PATH?"_s
                                           .arg(profile.launchCommand));
                        scheduleReconnect();
                        return;
                    }
                }
                if (now - m_lastLaunchMs < 30'000) {
                    m_connNote = u"Starting Talon…"_s;
                    emit connChanged();
                    m_retry.start(1200);
                    return;
                }
            }
            setConn(Error, u"Talon isn't running on this computer. Start it and this "
                           "connects by itself."_s);
            scheduleReconnect();
            return;
        }
        Profile e = profile;
        e.host = u"127.0.0.1"_s;
        e.port = bridge->port;
        e.token = bridge->token;
        e.tls = bridge->scheme == "https"_L1;
        // The daemon named its own certificate: enforced from the start, no
        // first-use adoption needed.
        e.fingerprint = e.tls ? bridge->fingerprint : QString();
        e.pinObserved = !e.fingerprint.isEmpty();
        connectTo(e, epoch, false);
        return;
    }

    if (m_endpointIndex > 0) {
        const QList<Endpoint> fallbacks = profile.usableEndpoints();
        const auto via = m_endpointIndex <= fallbacks.size()
            ? profile.viaEndpoint(fallbacks.at(m_endpointIndex - 1)) : std::nullopt;
        if (via) {
            // The pin is the profile's own; a fallback never sets one.
            connectTo(*via, epoch, false);
            return;
        }
        // The list changed under us.
        m_endpointIndex = 0;
    }

    if (const auto lan = profile.lanEndpoint()) {
        // Prefer the home-network address whenever it answers. The probe
        // carries no credential.
        Profile bare = *lan;
        bare.token.clear();
        auto *probe = new BridgeClient(bare, this);
        probe->health([this, probe, epoch, profile, lan = *lan](const ApiResult &r) {
            probe->deleteLater();
            if (epoch != m_epoch)
                return;
            if (r.ok())
                connectTo(lan, epoch, true);
            else
                connectTo(profile.wanEndpoint(), epoch, false);
        }, 1500);
        return;
    }
    connectTo(profile, epoch, true);
}

void Session::connectTo(const Profile &endpoint, int epoch, bool persistPin)
{
    dropClient();
    auto *client = new BridgeClient(endpoint, this);
    m_client = client;
    // Claim our mesh identity on the stream so device commands are
    // addressed to this client rather than broadcast.
    client->setDeviceId(m_store->prefString(u"meshDeviceId"_s));
    emit endpointChanged();
    AppLog::info("session", u"connecting to %1"_s.arg(endpoint.baseUrl()));

    // Identity first: a wrong address gets a crisp error instead of a
    // stream that silently never opens.
    client->health([this, client, epoch, persistPin](const ApiResult &r) {
        if (epoch != m_epoch || client != m_client)
            return;
        if (!r.ok()) {
            ApiResult f = r;
            if (!f.fatal()) {
                f.error = u"No Talon bridge answered at %1 (%2)"_s
                              .arg(client->endpoint().displayAddress(), r.error);
            }
            fail(f);
            return;
        }
        connect(client, &BridgeClient::eventsOpened, this, [this, client, epoch, persistPin] {
            if (epoch == m_epoch && client == m_client)
                onStreamOpen(epoch, persistPin);
        });
        connect(client, &BridgeClient::event, this, &Session::onEvent);
        connect(client, &BridgeClient::eventsClosed, this, [this, client](const ApiResult &why) {
            if (client == m_client)
                onStreamClosed(why);
        });
        client->openEvents();
    });
}

bool Session::reloadCredential(const ApiResult &why)
{
    // Another process sharing this profile (the background service, or the
    // app) may have traded the token for a new one, which retires the copy
    // this process holds. Read what is on disk and try once with that.
    if (why.kind != ApiResult::Unauthorized || m_retriedAuth || !m_client)
        return false;
    const QString used = m_client->endpoint().token;
    m_store->invalidateSecrets();
    if (m_store->activeProfileMeta().local || m_store->activeProfile().token == used)
        return false;
    m_retriedAuth = true;
    AppLog::info("session", u"the saved credential changed; retrying with it"_s);
    QTimer::singleShot(0, this, &Session::start);
    return true;
}

void Session::onStreamOpen(int epoch, bool persistPin)
{
    m_retriedAuth = false;
    adoptFingerprint(persistPin);
    m_backoffMs = 800;
    if (m_endpointIndex > 0)
        m_failback.start();
    else
        m_failback.stop();
    m_byeDelayMs = -1;
    // Whatever was streaming when the last connection dropped may have
    // finished unseen; a turn still running announces itself again.
    for (auto &[id, d] : m_data) {
        if (d.turn.active) {
            d.turn = {};
            m_chats.touch(id);
        }
    }
    m_turn.flush();
    setConn(Connected);
    emit chatChanged();
    emit clientReady(m_client);
    refreshChats();
    refreshModels();
    refreshCommands();
    refreshDevices();
    maybeUpgradeCredential(epoch);
}

void Session::onStreamClosed(const ApiResult &why)
{
    emit clientGone();
    if (reloadCredential(why))
        return;
    if (why.fatal()) {
        AppLog::warn("session", u"stopped: %1"_s.arg(why.error));
        setConn(Error, why.error, true);
        return;
    }
    setConn(Error, why.error);
    scheduleReconnect();
}

bool Session::advanceEndpoint(const ApiResult &why)
{
    const Profile profile = m_store->activeProfileMeta();
    const int fallbacks = profile.local ? 0 : int(profile.usableEndpoints().size());
    if (fallbacks == 0) {
        m_endpointIndex = 0;
        return false;
    }
    // Only a failure to get through moves on: a refused credential or an
    // application error would be the same at every address. A fallback
    // showing some other certificate is refused, and counts as unreachable.
    const bool unreachable = why.kind == ApiResult::Network || why.kind == ApiResult::Timeout
        || (why.kind == ApiResult::Http && (why.status == 502 || why.status == 503 || why.status == 504))
        || (why.kind == ApiResult::CertificateChanged && m_endpointIndex > 0);
    // A daemon that said it is restarting is down everywhere at once.
    if (!unreachable || nowMs() < m_fastRetryUntil)
        return false;
    m_endpointIndex = (m_endpointIndex + 1) % (fallbacks + 1);
    AppLog::info("session", m_endpointIndex == 0
                     ? u"no address answered; starting again from the configured one"_s
                     : u"trying fallback address %1 of %2"_s.arg(m_endpointIndex).arg(fallbacks));
    // Back at the start means every address was tried: wait out the backoff.
    return m_endpointIndex != 0;
}

void Session::probeConfigured()
{
    if (m_endpointIndex == 0 || m_conn != Connected) {
        m_failback.stop();
        return;
    }
    Profile bare = m_store->activeProfile().wanEndpoint();
    bare.token.clear();
    const int epoch = m_epoch;
    auto *probe = new BridgeClient(bare, this);
    probe->health([this, probe, epoch](const ApiResult &r) {
        probe->deleteLater();
        if (epoch != m_epoch || m_endpointIndex == 0 || !r.ok())
            return;
        AppLog::info("session", u"the configured address answers again; returning to it"_s);
        m_endpointIndex = 0;
        m_backoffMs = 800;
        start();
    }, 5000);
}

void Session::learnEndpoints(const QJsonObject &advertised)
{
    const Profile profile = m_store->activeProfileMeta();
    if (profile.local)
        return;
    QString version;
    const QList<Endpoint> list = parseEndpoints(advertised, &version);
    if (list == profile.endpoints && version == profile.endpointsVersion)
        return;
    m_store->setProfileEndpoints(profile.id, list, version);
    AppLog::info("session", u"the daemon advertises %1 address(es) for this bridge"_s.arg(list.size()));
    emit endpointChanged();
}

void Session::checkCredential()
{
    maybeUpgradeCredential(m_epoch);
}

void Session::fail(const ApiResult &why)
{
    if (reloadCredential(why))
        return;
    const bool onFallback = m_endpointIndex > 0;
    if (advanceEndpoint(why)) {
        setConn(Connecting);
        m_retry.start(250);
        return;
    }
    if (onFallback && why.kind == ApiResult::CertificateChanged) {
        // Not this bridge's certificate, so not this bridge. The pin, and
        // the configured address it came from, are untouched.
        setConn(Error, why.error);
        scheduleReconnect();
        return;
    }
    setConn(Error, why.error, why.fatal());
    // A wrong token or a changed certificate never heals by retrying.
    if (!why.fatal())
        scheduleReconnect();
}

void Session::scheduleReconnect()
{
    int delay;
    if (m_byeDelayMs >= 0) {
        delay = m_byeDelayMs;
        m_byeDelayMs = -1;
    } else if (nowMs() < m_fastRetryUntil) {
        // The daemon said it is restarting: poll briefly instead of
        // climbing the backoff.
        delay = 1500;
    } else {
        delay = m_backoffMs;
        m_backoffMs = qMin(15'000, int(m_backoffMs * 1.7));
    }
    AppLog::debug("session", u"reconnect in %1 ms"_s.arg(delay));
    m_retry.start(delay);
}

void Session::stop()
{
    ++m_epoch;
    m_retry.stop();
    dropClient();
    setConn(Idle);
}

void Session::resume()
{
    if (m_conn == Connecting || !m_store->hasProfile())
        return;
    if (m_connFatal)
        return;
    if (m_conn != Connected || m_retry.isActive()) {
        m_backoffMs = 800;
        start();
        return;
    }
    // The daemon pings every 25 s; a stream quieter than that across a
    // suspend may have died underneath us.
    if (m_client && nowMs() - m_client->lastRxMs() > 30'000) {
        AppLog::info("session", u"stream went quiet while away; reconnecting"_s);
        start();
    }
}

void Session::adoptFingerprint(bool persist)
{
    if (!m_client)
        return;
    const Profile e = m_client->endpoint();
    if (!e.tls)
        return;
    if (e.fingerprint.isEmpty()) {
        // Trust on first use: pin what this first handshake presented.
        const QString seen = m_client->seenFingerprint();
        if (seen.isEmpty())
            return;
        m_client->setPin(seen, true);
        if (persist) {
            Profile p = m_store->activeProfile();
            p.fingerprint = seen;
            p.pinObserved = true;
            m_store->saveProfile(p, true);
            AppLog::info("session", u"pinned bridge certificate %1"_s.arg(seen));
        }
    } else if (!e.pinObserved && m_client->pinMatched()) {
        // A pin from a pairing link, now seen for real: binding from here on.
        m_client->setPin(e.fingerprint, true);
        if (persist) {
            Profile p = m_store->activeProfile();
            if (p.fingerprint == e.fingerprint) {
                p.pinObserved = true;
                m_store->saveProfile(p, true);
            }
        }
    }
    emit endpointChanged();
}

void Session::maybeUpgradeCredential(int epoch)
{
    // A same-machine client reads the token from the daemon's own discovery
    // file on every connect, and keeps the shared token by design.
    if (m_credentialsUnsupported || m_upgrading || !m_client)
        return;
    if (m_store->activeProfileMeta().local)
        return;
    const QString token = m_client->endpoint().token;
    if (token.isEmpty())
        return;
    m_upgrading = true;
    BridgeClient *client = m_client;
    client->get(u"/auth/whoami"_s, {}, [this, client, epoch, token](const ApiResult &r) {
        const bool stale = epoch != m_epoch || client != m_client;
        if (r.status == 404)
            m_credentialsUnsupported = true;
        const QString action = r.json["action"_L1].toString();
        // Unknown actions are ignored: the daemon may grow new ones.
        const bool wanted = (action == "upgrade"_L1 && !isDeviceCredential(token))
            || (action == "rotate"_L1 && isDeviceCredential(token));
        if (stale || !r.ok() || !wanted) {
            m_upgrading = false;
            return;
        }
        const QString deviceId = m_store->deviceId();
        const QJsonObject body{
            {"deviceId"_L1, deviceId},
            {"client"_L1, u"companion"_s},
            {"scopes"_L1, QJsonArray{u"device"_s, u"client"_s, u"operator"_s}},
        };
        client->post(u"/auth/upgrade"_s, body,
                     [this, client, epoch, deviceId](const ApiResult &r2) {
            m_upgrading = false;
            if (epoch != m_epoch || client != m_client)
                return;
            const QString fresh = r2.json["token"_L1].toString();
            // A malformed reply must never replace a working token.
            if (!r2.ok() || !r2.json["ok"_L1].toBool() || !isDeviceCredential(fresh)
                || r2.json["deviceId"_L1].toString() != deviceId) {
                AppLog::warn("session", u"credential upgrade refused: %1"_s.arg(r2.error));
                return;
            }
            // Saved before it is used: the daemon keeps the old token valid
            // until the new one is first presented, so a crash in between
            // leaves a profile that still works.
            Profile p = m_store->activeProfile();
            p.token = fresh;
            m_store->saveProfile(p, true);
            client->setToken(fresh);
            client->setDeviceId(deviceId);
            emit endpointChanged();
            AppLog::info("session", u"now using this device's own credential %1"_s
                                        .arg(r2.json["credentialId"_L1].toString()));
        });
    });
}

void Session::resetStores()
{
    m_messages.bind(nullptr);
    m_turn.show(nullptr);
    m_selected.clear();
    m_data.clear();
    m_unusedChats.clear();
    m_chats.clear();
    m_models.clear();
    m_commands.clear();
    m_commandsBuiltIn = false;
    m_daemonConfig.clear();
    m_devices.clear();
    m_locations.clear();
    m_status.clear();
    m_credentialsUnsupported = false;
    emit selectionChanged();
    emit chatChanged();
    emit historyChanged();
    emit modelsChanged();
    emit commandsChanged();
    emit daemonConfigChanged();
    emit devicesChanged();
    emit statusChanged();
}

void Session::applyProfile(const Profile &profile)
{
    ++m_epoch;
    m_endpointIndex = 0;
    m_retry.stop();
    dropClient();
    m_store->saveProfile(profile, true);
    resetStores();
    hydrate();
    emit endpointChanged();
    m_backoffMs = 800;
    start();
}

void Session::switchProfile(const QString &id)
{
    if (id == m_store->activeProfileId())
        return;
    ++m_epoch;
    m_endpointIndex = 0;
    m_retry.stop();
    dropClient();
    m_store->setActiveProfile(id);
    resetStores();
    hydrate();
    emit endpointChanged();
    m_backoffMs = 800;
    start();
}

void Session::forgetEverything()
{
    ++m_epoch;
    m_endpointIndex = 0;
    m_retry.stop();
    dropClient();
    m_store->clearProfiles();
    resetStores();
    setConn(Idle);
    emit endpointChanged();
    AppLog::info("session", u"connection forgotten"_s);
}

// ── Events ──────────────────────────────────────────────────────────────────

void Session::turnChanged(const QString &chatId, bool shape)
{
    if (!isOpen(chatId))
        return;
    if (shape)
        m_turn.flush();
    else
        m_turn.poke();
}

void Session::clearContinuing(ChatData &d)
{
    ++d.continuingGen;
    d.turn.continuing = false;
}

void Session::onEvent(const QJsonObject &e)
{
    const QString kind = e["kind"_L1].toString();
    const QString chatId = e["chatId"_L1].toString();

    // The per-token path first: it runs for every streamed word.
    if (kind == "delta"_L1 || kind == "reasoning"_L1) {
        if (chatId.isEmpty())
            return;
        ChatData &d = data(chatId);
        TurnState::Data &t = d.turn;
        // A row appears (or "still working" gives way to text): repaint now.
        const bool shape = !t.active || t.continuing || !t.hasContent();
        if (!t.active) {
            t.active = true;
            t.startedMs = nowMs();
            m_chats.touch(chatId);
            if (isOpen(chatId))
                emit chatChanged();
        }
        clearContinuing(d);
        if (kind == "delta"_L1)
            t.draft += e["text"_L1].toString();
        else
            t.reasoning += e["text"_L1].toString();
        turnChanged(chatId, shape);
        return;
    }

    if (kind == "hello"_L1) {
        onHello(e);
    } else if (kind == "status"_L1) {
        m_status = statusFromJson(e["status"_L1].toObject());
        emit statusChanged();
    } else if (kind == "chat_created"_L1 || kind == "chat_updated"_L1) {
        if (!e["chat"_L1].isObject())
            return;
        upsertChat(Chat::fromJson(e["chat"_L1].toObject()));
        scheduleSnapshot();
    } else if (kind == "chat_deleted"_L1) {
        if (!chatId.isEmpty())
            removeChat(chatId);
        scheduleSnapshot();
    } else if (kind == "message"_L1) {
        if (chatId.isEmpty() || !e["message"_L1].isObject())
            return;
        onMessage(chatId, Message::fromJson(e["message"_L1].toObject()));
        scheduleSnapshot();
    } else if (kind == "message_edited"_L1 || kind == "message_deleted"_L1
               || kind == "reaction"_L1) {
        const QString messageId = e["messageId"_L1].toString();
        const auto it = m_data.find(chatId);
        if (it == m_data.end() || messageId.isEmpty())
            return;
        QList<Message> &rows = it->second.messages;
        for (int i = 0; i < rows.size(); ++i) {
            if (rows.at(i).id != messageId)
                continue;
            if (kind == "message_deleted"_L1) {
                if (isOpen(chatId))
                    m_messages.remove(i, [&] { rows.removeAt(i); });
                else
                    rows.removeAt(i);
            } else {
                if (kind == "message_edited"_L1) {
                    rows[i].text = e["text"_L1].toString();
                    rows[i].blocksValid = false;
                } else {
                    const QString emoji = e["emoji"_L1].toString();
                    if (!emoji.isEmpty() && !rows.at(i).reactions.contains(emoji))
                        rows[i].reactions << emoji;
                }
                if (isOpen(chatId))
                    m_messages.changed(i);
            }
            break;
        }
        scheduleSnapshot();
    } else if (kind == "turn_start"_L1) {
        if (chatId.isEmpty())
            return;
        ChatData &d = data(chatId);
        ++d.continuingGen;
        d.turn.reset(nowMs());
        m_chats.touch(chatId);
        if (isOpen(chatId))
            emit chatChanged();
        turnChanged(chatId, true);
    } else if (kind == "tool"_L1) {
        onTool(e);
    } else if (kind == "typing"_L1) {
        if (chatId.isEmpty())
            return;
        data(chatId).turn.typing = e["on"_L1].toBool();
        turnChanged(chatId, true);
    } else if (kind == "turn_end"_L1) {
        onTurnEnd(e);
        scheduleSnapshot();
    } else if (kind == "error"_L1) {
        if (!chatId.isEmpty())
            note(chatId, e["message"_L1].toString());
    } else if (kind == "bye"_L1) {
        // The daemon is going away. On a restart its successor comes up on
        // the same address: retry soon and often for a while.
        if (e["reason"_L1].toString() == "restart"_L1) {
            m_byeDelayMs = qBound(250, e["reconnectAfterMs"_L1].toInt(2000), 60'000);
            m_fastRetryUntil = nowMs() + m_byeDelayMs + 45'000;
            m_connNote = u"Talon is restarting…"_s;
        }
    }
    // `locate` and `device_command` belong to the mesh service, which
    // listens on the client itself. Unknown kinds are additive evolution.
}

void Session::onHello(const QJsonObject &e)
{
    if (!e["status"_L1].isObject())
        return;
    m_status = statusFromJson(e["status"_L1].toObject());
    emit statusChanged();
    setChats(e["chats"_L1].toArray());
    scheduleSnapshot();
}

void Session::setChats(const QJsonArray &raw)
{
    QList<Chat> chats;
    for (const auto &v : raw) {
        if (!v.isObject())
            continue;
        Chat c = Chat::fromJson(v.toObject());
        if (c.id.isEmpty())
            continue;
        // A chat first seen here is read up to now; only what happens after
        // counts as unread.
        if (m_store->lastRead(c.id) == 0)
            m_store->setLastRead(c.id, qMax<qint64>(1, c.lastActive));
        chats << c;
    }
    m_chats.setChats(chats);
    // Forget chats that no longer exist.
    for (auto it = m_data.begin(); it != m_data.end();) {
        if (it->first != m_selected && !m_chats.find(it->first))
            it = m_data.erase(it);
        else
            ++it;
    }
    reconcileSelection();
    emit chatChanged();
}

void Session::reconcileSelection()
{
    if (!m_selected.isEmpty() && !m_chats.find(m_selected)) {
        m_messages.bind(nullptr);
        m_turn.show(nullptr);
        m_data.erase(m_selected);
        m_selected.clear();
        emit selectionChanged();
        emit historyChanged();
    }
    // Opening a chat on the user's behalf is a two-pane convenience. With
    // one pane, "nothing open" is the chat list itself.
    if (!m_narrow && m_selected.isEmpty() && !m_chats.chats().isEmpty())
        selectChat(m_chats.chats().first().id);
}

void Session::upsertChat(const Chat &chat)
{
    if (chat.id.isEmpty())
        return;
    const bool known = m_chats.find(chat.id) != nullptr;
    if (!known && m_store->lastRead(chat.id) == 0)
        m_store->setLastRead(chat.id, qMax<qint64>(1, chat.lastActive));
    m_chats.upsert(chat);
    if (isOpen(chat.id)) {
        markRead(chat.id);
        emit chatChanged();
    }
    if (!known)
        reconcileSelection();
}

void Session::removeChat(const QString &chatId)
{
    m_unusedChats.remove(chatId);
    const bool wasOpen = isOpen(chatId);
    if (wasOpen) {
        m_messages.bind(nullptr);
        m_turn.show(nullptr);
        m_selected.clear();
    }
    m_chats.remove(chatId);
    m_data.erase(chatId);
    m_store->clearLastRead(chatId);
    if (wasOpen) {
        emit selectionChanged();
        emit chatChanged();
        emit historyChanged();
        reconcileSelection();
    }
}

void Session::onMessage(const QString &chatId, Message m)
{
    m_unusedChats.remove(chatId);
    ChatData &d = data(chatId);
    for (const Message &existing : std::as_const(d.messages)) {
        if (existing.id == m.id)
            return; // re-delivery
    }
    const bool fromAssistant = m.role == Message::Assistant;
    if (fromAssistant) {
        // A delivered reply supersedes what was streaming, but not the
        // turn: the model may send a message and keep working. Fold the
        // live tool calls into this row, clear the stream, and let the
        // definitive turn_end close the turn.
        TurnState::Data &t = d.turn;
        if (m.tools.isEmpty())
            m.tools = t.tools;
        t.typing = false;
        t.clearStream();
        t.continuing = false;
        const int gen = ++d.continuingGen;
        if (t.active) {
            QTimer::singleShot(kContinuingGraceMs, this, [this, chatId, gen] {
                const auto it = m_data.find(chatId);
                if (it == m_data.end() || it->second.continuingGen != gen)
                    return;
                TurnState::Data &cur = it->second.turn;
                if (cur.active && !cur.hasContent()) {
                    cur.continuing = true;
                    turnChanged(chatId, true);
                }
            });
        }
    }
    if (d.loading)
        d.arrivedDuringLoad.insert(m.id);
    const QString text = m.text;
    const int at = int(d.messages.size());
    if (isOpen(chatId)) {
        m_messages.insert(at, 1, [&] { d.messages.append(std::move(m)); });
        markRead(chatId);
        if (fromAssistant)
            m_turn.flush();
    } else {
        d.messages.append(std::move(m));
        m_chats.touch(chatId);
    }
    const Chat *c = m_chats.find(chatId);
    emit messageArrived(chatId, c ? c->title : QString(), text, fromAssistant);
}

void Session::onTool(const QJsonObject &e)
{
    const QString chatId = e["chatId"_L1].toString();
    const QString id = e["id"_L1].toString();
    if (chatId.isEmpty() || id.isEmpty())
        return;
    ChatData &d = data(chatId);
    TurnState::Data &t = d.turn;
    const qint64 now = nowMs();

    const auto finish = [&](ToolCall &call) {
        call.done = true;
        if (call.durationMs < 0 && call.startedMs > 0)
            call.durationMs = now - call.startedMs;
        call.error = e["error"_L1].toString();
        if (e["output"_L1].isString())
            call.output = e["output"_L1].toString();
    };

    if (e["phase"_L1].toString() == "result"_L1) {
        for (ToolCall &call : t.tools) {
            if (call.id == id) {
                finish(call);
                turnChanged(chatId, true);
                return;
            }
        }
        // The reply already arrived and took the tool calls with it before
        // this result landed: finish the copy in the message instead.
        for (int i = int(d.messages.size()) - 1; i >= 0; --i) {
            for (ToolCall &call : d.messages[i].tools) {
                if (call.id != id)
                    continue;
                finish(call);
                if (isOpen(chatId))
                    m_messages.changed(i);
                return;
            }
        }
        return;
    }

    for (const ToolCall &call : std::as_const(t.tools)) {
        if (call.id == id)
            return;
    }
    if (!t.active) {
        t.active = true;
        t.startedMs = now;
        m_chats.touch(chatId);
        if (isOpen(chatId))
            emit chatChanged();
    }
    clearContinuing(d);
    ToolCall call;
    call.id = id;
    call.name = e["name"_L1].toString(u"tool"_s);
    call.input = e["input"_L1].toObject();
    call.startedMs = now;
    t.tools << call;
    turnChanged(chatId, true);
}

void Session::onTurnEnd(const QJsonObject &e)
{
    const QString chatId = e["chatId"_L1].toString();
    if (chatId.isEmpty())
        return;
    ChatData &d = data(chatId);
    // Tool results can race the reply that carries them; this is the one
    // point where every call of the turn is certainly over. The turn's
    // stats go on its last reply.
    for (int i = int(d.messages.size()) - 1; i >= 0; --i) {
        Message &m = d.messages[i];
        if (m.role != Message::Assistant)
            continue;
        for (ToolCall &call : m.tools)
            call.done = true;
        if (e["durationMs"_L1].isDouble())
            m.durationMs = qint64(e["durationMs"_L1].toDouble());
        const QJsonObject usage = e["usage"_L1].toObject();
        if (usage["input"_L1].isDouble())
            m.tokensIn = qint64(usage["input"_L1].toDouble());
        if (usage["output"_L1].isDouble())
            m.tokensOut = qint64(usage["output"_L1].toDouble());
        if (isOpen(chatId))
            m_messages.changed(i);
        break;
    }
    ++d.continuingGen;
    d.turn = {};
    m_chats.touch(chatId);
    if (isOpen(chatId))
        emit chatChanged();
    turnChanged(chatId, true);
    emit turnEnded(chatId);
}

void Session::note(const QString &chatId, const QString &text)
{
    if (chatId.isEmpty() || text.isEmpty())
        return;
    ChatData &d = data(chatId);
    // A failure that repeats (a rate limit, a dead backend) would bury the
    // conversation under copies of itself.
    if (!d.messages.isEmpty() && d.messages.last().role == Message::System
        && d.messages.last().text == text)
        return;
    Message m;
    m.id = u"sys-%1"_s.arg(QDateTime::currentMSecsSinceEpoch() * 1000 + (m_uploadSeq++ % 1000));
    m.chatId = chatId;
    m.role = Message::System;
    m.text = redactSecrets(text);
    m.ts = nowMs();
    const int at = int(d.messages.size());
    if (isOpen(chatId))
        m_messages.insert(at, 1, [&] { d.messages.append(m); });
    else
        d.messages.append(m);
}

BridgeClient::Callback Session::noteOnFailure(const QString &chatId, const QString &what)
{
    return [this, chatId, what](const ApiResult &r) {
        if (!r.ok())
            note(chatId, u"%1 failed: %2"_s.arg(what, r.error));
    };
}

// ── Chats ───────────────────────────────────────────────────────────────────

void Session::bindSelection()
{
    if (m_selected.isEmpty()) {
        m_messages.bind(nullptr);
        m_turn.show(nullptr);
        return;
    }
    ChatData &d = data(m_selected);
    m_messages.bind(&d.messages);
    m_turn.show(&d.turn);
}

void Session::markRead(const QString &chatId)
{
    const Chat *c = m_chats.find(chatId);
    m_store->setLastRead(chatId, c ? qMax<qint64>(1, c->lastActive) : nowMs());
}

void Session::selectChat(const QString &chatId)
{
    if (chatId.isEmpty() || chatId == m_selected || !m_chats.find(chatId))
        return;
    const QString previous = m_selected;
    reapUnusedChats(chatId);
    m_selected = chatId;
    bindSelection();
    if (!previous.isEmpty())
        trimHistory(previous);
    markRead(chatId);
    m_chats.touch(previous);
    m_chats.touch(chatId);
    emit selectionChanged();
    emit chatChanged();
    emit historyChanged();
    if (m_conn == Connected && !data(chatId).loaded)
        loadHistory(chatId);
}

void Session::clearSelection()
{
    if (m_selected.isEmpty())
        return;
    const QString previous = m_selected;
    reapUnusedChats();
    m_selected.clear();
    bindSelection();
    trimHistory(previous);
    m_chats.touch(previous);
    emit selectionChanged();
    emit chatChanged();
    emit historyChanged();
}

void Session::trimHistory(const QString &chatId)
{
    // Scrollback paged in while reading would otherwise stay in memory for
    // the life of a tray-resident process.
    const auto it = m_data.find(chatId);
    if (it == m_data.end() || isOpen(chatId))
        return;
    QList<Message> &rows = it->second.messages;
    if (rows.size() <= kHistoryInitial)
        return;
    rows.remove(0, rows.size() - kHistoryInitial);
    it->second.exhausted = false;
}

void Session::newChat()
{
    if (!m_client || m_conn != Connected) {
        emit notice(u"Not connected to Talon"_s);
        return;
    }
    // Reuse an untouched chat instead of stacking empty ones.
    for (const QString &id : std::as_const(m_unusedChats)) {
        if (m_chats.find(id)) {
            selectChat(id);
            return;
        }
    }
    const int epoch = m_epoch;
    m_client->post(u"/chats"_s, {}, [this, epoch](const ApiResult &r) {
        if (epoch != m_epoch)
            return;
        if (!r.ok()) {
            emit notice(u"Couldn't start a chat: %1"_s.arg(r.error));
            return;
        }
        const Chat c = Chat::fromJson(r.json["chat"_L1].toObject());
        if (c.id.isEmpty())
            return;
        // The chat_created event arrives too; opening it here is snappier.
        m_store->setLastRead(c.id, qMax<qint64>(1, c.lastActive));
        m_chats.upsert(c);
        m_unusedChats.insert(c.id);
        data(c.id).loaded = true;
        data(c.id).exhausted = true;
        selectChat(c.id);
    });
}

void Session::reapUnusedChats(const QString &keep)
{
    // "New chat" creates a real chat on the daemon straight away, so
    // opening one and walking off would leave an empty row behind forever.
    // Only chats this client made, and only while they are still empty.
    const QSet<QString> candidates = m_unusedChats;
    for (const QString &id : candidates) {
        if (id == keep)
            continue;
        m_unusedChats.remove(id);
        const Chat *c = m_chats.find(id);
        if (!c || c->hasQueued || isTurnRunning(id))
            continue;
        const auto it = m_data.find(id);
        if (it != m_data.end() && !it->second.messages.isEmpty())
            continue;
        if (m_client)
            m_client->post(u"/chats/delete"_s, {{"chatId"_L1, id}}, {});
    }
}

void Session::renameChat(const QString &chatId, const QString &title)
{
    const QString clean = title.trimmed();
    Chat *c = m_chats.find(chatId);
    if (!c || clean.isEmpty() || !m_client)
        return;
    // Optimistic; the chat_updated event reconciles.
    Chat updated = *c;
    updated.title = clean;
    m_chats.upsert(updated);
    if (isOpen(chatId))
        emit chatChanged();
    m_client->post(u"/chats/rename"_s, {{"chatId"_L1, chatId}, {"title"_L1, clean}},
                   noteOnFailure(chatId, u"Rename"_s));
}

void Session::deleteChat(const QString &chatId)
{
    m_unusedChats.remove(chatId);
    if (!m_client)
        return;
    m_client->post(u"/chats/delete"_s, {{"chatId"_L1, chatId}},
                   noteOnFailure(chatId, u"Delete"_s));
}

void Session::resetChat(const QString &chatId)
{
    if (!m_client)
        return;
    m_client->post(u"/chats/reset"_s, {{"chatId"_L1, chatId}},
                   [this, chatId](const ApiResult &r) {
        if (!r.ok()) {
            note(chatId, u"Reset failed: %1"_s.arg(r.error));
            return;
        }
        // The daemon dropped the session and its transcript: so do we,
        // local notes included, then ask what (if anything) is left.
        const auto it = m_data.find(chatId);
        if (it == m_data.end())
            return;
        ChatData &d = it->second;
        if (isOpen(chatId))
            m_messages.reset([&] { d.messages.clear(); });
        else
            d.messages.clear();
        d.loaded = false;
        d.exhausted = false;
        if (m_conn == Connected)
            loadHistory(chatId);
    });
}

void Session::setPulse(const QString &chatId, bool on)
{
    Chat *c = m_chats.find(chatId);
    if (!c || !m_client)
        return;
    c->pulse = on;
    if (isOpen(chatId))
        emit chatChanged();
    m_client->post(u"/chats/pulse"_s, {{"chatId"_L1, chatId}, {"on"_L1, on}},
                   noteOnFailure(chatId, u"Pulse change"_s));
}

void Session::refresh()
{
    if (m_conn == Connected) {
        refreshChats();
        refreshModels(m_selected);
    } else if (!m_connFatal) {
        m_backoffMs = 800;
        start();
    }
}

void Session::refreshChats()
{
    if (!m_client)
        return;
    const int epoch = m_epoch;
    m_client->get(u"/chats"_s, {}, [this, epoch](const ApiResult &r) {
        if (epoch != m_epoch)
            return;
        if (r.ok())
            setChats(r.json["chats"_L1].toArray());
        // History may have moved on while we were away (a heartbeat reply,
        // another client): what was loaded is no longer known to be whole.
        for (auto &[id, d] : m_data) {
            d.loaded = false;
            d.exhausted = false;
        }
        if (!m_selected.isEmpty())
            loadHistory(m_selected);
        scheduleSnapshot();
    });
}

void Session::refreshModels(const QString &chatId)
{
    if (!m_client)
        return;
    const int epoch = m_epoch;
    m_client->get(u"/models"_s, query({{u"chatId"_s, chatId}}),
                  [this, epoch, chatId](const ApiResult &r) {
        if (epoch != m_epoch || !r.ok())
            return;
        m_models = r.json["models"_L1].toArray().toVariantList();
        emit modelsChanged();
        // The reply also names the chat's active model: the header is then
        // right even when the daemon's chat_updated is late.
        const QString active = r.json["active"_L1].toString();
        if (Chat *c = m_chats.find(chatId); c && !active.isEmpty() && c->model != active) {
            c->model = active;
            if (isOpen(chatId))
                emit chatChanged();
        }
    });
}

namespace {

/// What the composer offers on a daemon without the `commands` capability.
/// Such a daemon hands "/reset" to the model as ordinary text; these few
/// have routes of their own, so the client carries them out itself.
QVariantList builtInCommands()
{
    const auto command = [](const QString &name, const QString &description,
                            const QString &args = {}) {
        QVariantMap c{{u"name"_s, name}, {u"description"_s, description}};
        if (!args.isEmpty())
            c[u"args"_s] = args;
        return QVariant(c);
    };
    return {
        command(u"stop"_s, u"Stop the current response"_s),
        command(u"reset"_s, u"Clear the session and start fresh"_s),
        command(u"model"_s, u"Pick the model, backend and effort"_s),
        command(u"effort"_s, u"Set thinking effort"_s, u"[off|low|medium|high|max|adaptive]"_s),
        command(u"pulse"_s, u"Periodic check-ins for this chat"_s, u"[on|off]"_s),
    };
}

} // namespace

void Session::refreshCommands()
{
    if (!m_client)
        return;
    m_commandsBuiltIn = !hasCapability(u"commands"_s);
    if (m_commandsBuiltIn) {
        m_commands = builtInCommands();
        emit commandsChanged();
        return;
    }
    const int epoch = m_epoch;
    m_client->get(u"/commands"_s, {}, [this, epoch](const ApiResult &r) {
        if (epoch != m_epoch)
            return;
        // Without the list there is nothing to suggest, but the daemon still
        // answers a command that is typed out.
        m_commands = r.ok() ? r.json["commands"_L1].toArray().toVariantList() : QVariantList();
        emit commandsChanged();
    });
}

bool Session::runBuiltInCommand(const QString &chatId, const QString &text)
{
    static const QRegularExpression shape(u"^/([a-zA-Z]+)(?:\\s+(.*))?$"_s,
                                          QRegularExpression::DotMatchesEverythingOption);
    const auto m = shape.match(text);
    if (!m.hasMatch())
        return false;
    const QString name = m.captured(1).toLower();
    const QString arg = m.captured(2).trimmed().toLower();
    // Like the daemon's own parser: a name that isn't a command ("/etc/hosts",
    // a typo) is ordinary text for the model.
    if (name == "stop"_L1) {
        interrupt();
    } else if (name == "reset"_L1) {
        resetChat(chatId);
    } else if (name == "model"_L1 || (name == "effort"_L1 && arg.isEmpty())) {
        emit modelPickerRequested();
    } else if (name == "effort"_L1) {
        setEffort(chatId, arg);
        note(chatId, u"Thinking effort set to %1."_s.arg(arg));
    } else if (name == "pulse"_L1) {
        const Chat *c = m_chats.find(chatId);
        if (!arg.isEmpty() && arg != "on"_L1 && arg != "off"_L1) {
            note(chatId, u"Use /pulse on or /pulse off."_s);
            return true;
        }
        const bool on = arg.isEmpty() ? !(c && c->pulse) : arg == "on"_L1;
        setPulse(chatId, on);
        note(chatId, on ? u"Check-ins are on for this chat."_s
                        : u"Check-ins are off for this chat."_s);
    } else {
        return false;
    }
    return true;
}

void Session::loadHistory(const QString &chatId)
{
    if (!m_client)
        return;
    ChatData &d = data(chatId);
    if (d.loading)
        return;
    d.loading = true;
    d.arrivedDuringLoad.clear();
    if (isOpen(chatId))
        emit historyChanged();
    m_client->get(u"/history"_s,
                  query({{u"chatId"_s, chatId}, {u"limit"_s, QString::number(kHistoryInitial)}}),
                  [this, chatId](const ApiResult &r) {
        const auto it = m_data.find(chatId);
        if (it == m_data.end())
            return;
        ChatData &d = it->second;
        d.loading = false;
        if (r.ok()) {
            QList<Message> hist;
            QSet<QString> ids;
            qint64 newestId = -1;
            for (const auto &v : r.json["messages"_L1].toArray()) {
                Message m = Message::fromJson(v.toObject());
                if (m.id.isEmpty() || ids.contains(m.id))
                    continue;
                if (m.chatId.isEmpty())
                    m.chatId = chatId;
                ids.insert(m.id);
                qint64 n = 0;
                if (m.serverId(&n))
                    newestId = qMax(newestId, n);
                hist << m;
            }
            std::sort(hist.begin(), hist.end(), messageLess);
            d.exhausted = hist.size() < kHistoryInitial;
            const qint64 newestTs = hist.isEmpty() ? 0 : hist.last().ts;

            // The server's window is the truth. Two kinds of local row may
            // sit on top of it: a message delivered live while this fetch
            // was in flight (it is not in the reply yet), and a local note
            // that is still the newest thing in the chat. Nothing else: rows
            // from an old snapshot or an earlier page that the server did
            // not return are gone (reset elsewhere, restored from a backup)
            // or come back when the reader scrolls up.
            QList<Message> merged = hist;
            for (const Message &m : std::as_const(d.messages)) {
                if (ids.contains(m.id))
                    continue;
                const bool live = d.arrivedDuringLoad.contains(m.id);
                const bool freshNote = m.role == Message::System && m.ts >= newestTs;
                if (live || freshNote)
                    merged << m;
            }
            d.arrivedDuringLoad.clear();
            std::sort(merged.begin(), merged.end(), messageLess);
            if (isOpen(chatId))
                m_messages.reset([&] { d.messages = merged; });
            else
                d.messages = merged;
            d.loaded = true;
        }
        if (isOpen(chatId))
            emit historyChanged();
        scheduleSnapshot();
    });
}

void Session::loadOlder()
{
    const QString chatId = m_selected;
    const auto it = m_data.find(chatId);
    if (it == m_data.end() || !m_client || m_conn != Connected)
        return;
    ChatData &d = it->second;
    if (d.loadingOlder || d.exhausted || !d.loaded)
        return;
    // The smallest server id, not the first row's: only the same thing
    // while the list is in order.
    qint64 oldest = -1;
    for (const Message &m : std::as_const(d.messages)) {
        qint64 n = 0;
        if (m.serverId(&n) && (oldest < 0 || n < oldest))
            oldest = n;
    }
    if (oldest < 0)
        return;
    d.loadingOlder = true;
    emit historyChanged();
    m_client->get(u"/history"_s,
                  query({{u"chatId"_s, chatId},
                         {u"before"_s, QString::number(oldest)},
                         {u"limit"_s, QString::number(kHistoryPage)}}),
                  [this, chatId](const ApiResult &r) {
        const auto it = m_data.find(chatId);
        if (it == m_data.end())
            return;
        ChatData &d = it->second;
        d.loadingOlder = false;
        int added = 0;
        if (r.ok()) {
            const QJsonArray page = r.json["messages"_L1].toArray();
            if (page.size() < kHistoryPage)
                d.exhausted = true;
            QSet<QString> have;
            for (const Message &m : std::as_const(d.messages))
                have.insert(m.id);
            QList<Message> fresh;
            for (const auto &v : page) {
                Message m = Message::fromJson(v.toObject());
                if (!m.id.isEmpty() && !have.contains(m.id)) {
                    have.insert(m.id);
                    fresh << m;
                }
            }
            std::sort(fresh.begin(), fresh.end(), messageLess);
            added = int(fresh.size());
            const auto prepend = [&] { d.messages = fresh + d.messages; };
            if (isOpen(chatId))
                m_messages.insert(0, added, prepend);
            else
                prepend();
        }
        if (isOpen(chatId)) {
            emit historyChanged();
            emit olderLoaded(added);
        }
    });
}

QString Session::exportMarkdown(const QString &chatId) const
{
    const Chat *c = m_chats.find(chatId);
    QString out = u"# %1\n\n"_s.arg(c ? c->title : u"Talon chat"_s);
    const auto it = m_data.find(chatId);
    if (it == m_data.end())
        return out;
    for (const Message &m : it->second.messages) {
        if (m.role == Message::System)
            continue;
        out += u"**%1** · %2\n\n%3\n\n"_s.arg(
            m.role == Message::User ? u"You"_s : botName(),
            QDateTime::fromMSecsSinceEpoch(m.ts).toString(u"yyyy-MM-dd HH:mm"_s),
            m.text.trimmed());
    }
    return out;
}

QVariantList Session::chatSummaries() const
{
    QVariantList out;
    for (const Chat &c : m_chats.chats())
        out << QVariantMap{{u"id"_s, c.id}, {u"title"_s, c.title}, {u"preview"_s, c.preview}};
    return out;
}

// ── Turns ───────────────────────────────────────────────────────────────────

void Session::send(const QString &text, const QVariantList &attachments, QJSValue done)
{
    const QString chatId = m_selected;
    const QString body = text.trimmed();
    if (chatId.isEmpty() || (body.isEmpty() && attachments.isEmpty())) {
        reply(done, false);
        return;
    }
    if (!m_client || m_conn != Connected) {
        note(chatId, u"Not connected to Talon, so this wasn't sent."_s);
        reply(done, false);
        return;
    }
    if (m_commandsBuiltIn && attachments.isEmpty() && runBuiltInCommand(chatId, body)) {
        reply(done, true);
        return;
    }
    m_unusedChats.remove(chatId);
    QJsonObject payload{{"chatId"_L1, chatId}, {"text"_L1, body}};
    if (!attachments.isEmpty()) {
        // Only the identifying fields go up; the daemon resolves each one
        // against what it actually wrote to disk.
        QJsonArray refs;
        for (const QVariant &a : attachments) {
            const QVariantMap m = a.toMap();
            refs << QJsonObject{{"url"_L1, m.value(u"url"_s).toString()},
                                {"path"_L1, m.value(u"path"_s).toString()}};
        }
        payload["attachments"_L1] = refs;
    }
    // Always handed to the daemon: with a turn already running it parks the
    // message as the chat's queued follow-up and sends it when the turn ends.
    m_client->post(u"/send"_s, payload, [this, chatId, done](const ApiResult &r) {
        if (!r.ok())
            note(chatId, u"Couldn't send: %1"_s.arg(r.error));
        reply(done, r.ok());
    });
}

void Session::interrupt()
{
    if (m_client && !m_selected.isEmpty())
        m_client->post(u"/chats/interrupt"_s, {{"chatId"_L1, m_selected}}, {});
}

void Session::editQueued(const QString &text)
{
    if (!m_client || m_selected.isEmpty())
        return;
    const QString chatId = m_selected;
    m_client->post(u"/queue"_s, {{"chatId"_L1, chatId}, {"text"_L1, text.trimmed()}},
                   noteOnFailure(chatId, u"Updating the queued message"_s));
}

QString Session::upload(const QUrl &file)
{
    const QString id = u"up%1"_s.arg(++m_uploadSeq);
    const auto failLater = [this, id](const QString &error) {
        QTimer::singleShot(0, this, [this, id, error] { emit uploadFailed(id, error); });
        return id;
    };
    if (!m_client || m_conn != Connected)
        return failLater(u"Not connected to Talon"_s);

    // Android hands out content:// URIs, which QFile opens as they are.
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    auto *source = new QFile(path);
    if (!source->open(QIODevice::ReadOnly)) {
        const QString error = u"Couldn't read %1: %2"_s.arg(QFileInfo(path).fileName(),
                                                            source->errorString());
        delete source;
        return failLater(error);
    }
    QString name = QFileInfo(path).fileName();
    if (name.isEmpty())
        name = u"upload"_s;
    const QString mime = QMimeDatabase().mimeTypeForFile(name, QMimeDatabase::MatchExtension).name();
    const qint64 size = source->size();

    QNetworkReply *reply = m_client->upload(
        u"/upload"_s, query({{u"filename"_s, name}}), source, size, mime,
        [this, id](const ApiResult &r) {
            // A cancelled upload was already forgotten; say nothing.
            if (!m_uploads.remove(id))
                return;
            if (!r.ok() || r.json["ok"_L1].toBool(true) == false) {
                emit uploadFailed(id, r.error.isEmpty() ? u"Upload failed"_s : r.error);
                return;
            }
            emit uploadFinished(id, Attachment::fromJson(r.json).toVariant());
        },
        [this, id](qint64 sent, qint64 total) {
            if (total > 0)
                emit uploadProgress(id, double(sent) / double(total));
        });
    m_uploads.insert(id, reply);
    return id;
}

QString Session::uploadClipboardImage()
{
    const QImage image = QGuiApplication::clipboard()->image();
    if (image.isNull())
        return {};
    const QString path = QDir(m_store->cacheDir())
        .filePath(u"pasted-%1.png"_s.arg(QDateTime::currentMSecsSinceEpoch()));
    if (!image.save(path, "PNG"))
        return {};
    const QString id = upload(QUrl::fromLocalFile(path));
    if (const auto reply = m_uploads.value(id))
        connect(reply, &QNetworkReply::finished, this, [path] { QFile::remove(path); });
    else
        QFile::remove(path);
    return id;
}

void Session::cancelUpload(const QString &uploadId)
{
    const auto reply = m_uploads.take(uploadId);
    if (reply)
        reply->abort();
}

// ── Per-chat options ────────────────────────────────────────────────────────

void Session::fetchChatOptions(const QString &chatId, QJSValue done)
{
    if (!m_client) {
        reply(done, QVariantMap());
        return;
    }
    // Three small reads; answer once with whatever came back. A daemon
    // without one of the routes just leaves that part empty.
    auto acc = std::make_shared<QVariantMap>();
    auto left = std::make_shared<int>(3);
    const auto finish = [this, acc, left, done] {
        if (--*left == 0)
            reply(done, *acc);
    };
    const QUrlQuery q = query({{u"chatId"_s, chatId}});
    m_client->get(u"/models"_s, q, [acc, finish](const ApiResult &r) {
        (*acc)[u"models"_s] = r.json["models"_L1].toArray().toVariantList();
        (*acc)[u"model"_s] = r.json["active"_L1].toString();
        finish();
    });
    m_client->get(u"/backends"_s, q, [acc, finish](const ApiResult &r) {
        (*acc)[u"backends"_s] = r.json["backends"_L1].toArray().toVariantList();
        (*acc)[u"backend"_s] = r.json["active"_L1].toString();
        finish();
    });
    m_client->get(u"/effort"_s, q, [acc, finish](const ApiResult &r) {
        (*acc)[u"efforts"_s] = r.json["levels"_L1].toArray().toVariantList();
        (*acc)[u"effort"_s] = r.json["active"_L1].toString(u"adaptive"_s);
        finish();
    });
}

void Session::setModel(const QString &chatId, const QString &model)
{
    Chat *c = m_chats.find(chatId);
    if (!c || !m_client)
        return;
    c->model = model;
    if (isOpen(chatId))
        emit chatChanged();
    m_client->post(u"/model"_s, {{"chatId"_L1, chatId}, {"model"_L1, model}},
                   noteOnFailure(chatId, u"Model change"_s));
}

void Session::setEffort(const QString &chatId, const QString &effort)
{
    Chat *c = m_chats.find(chatId);
    if (!c || !m_client)
        return;
    c->effort = effort;
    if (isOpen(chatId))
        emit chatChanged();
    m_client->post(u"/effort"_s, {{"chatId"_L1, chatId}, {"effort"_L1, effort}},
                   noteOnFailure(chatId, u"Effort change"_s));
}

void Session::setBackend(const QString &chatId, const QString &backend, QJSValue done)
{
    if (!m_client) {
        reply(done, QVariantMap{{u"ok"_s, false}, {u"error"_s, u"Not connected"_s}});
        return;
    }
    // A rebind can take a while: the daemon may have to start the backend.
    m_client->post(u"/backend"_s, {{"chatId"_L1, chatId}, {"backend"_L1, backend}},
                   [this, chatId, backend, done](const ApiResult &r) {
        const bool ok = r.ok() && r.json["ok"_L1].toBool();
        if (ok) {
            if (Chat *c = m_chats.find(chatId)) {
                c->backend = backend;
                if (isOpen(chatId))
                    emit chatChanged();
            }
            // A different backend, a different model catalogue.
            refreshModels(chatId);
        }
        const QString error = r.ok() ? r.json["error"_L1].toString() : r.error;
        reply(done, QVariantMap{{u"ok"_s, ok}, {u"error"_s, error}});
    }, 60'000);
}

// ── Daemon surfaces ─────────────────────────────────────────────────────────

void Session::search(const QString &text, const QString &chatId, QJSValue done)
{
    const QString q = text.trimmed();
    if (!m_client || m_conn != Connected || q.isEmpty()) {
        reply(done, QVariantList());
        return;
    }
    m_client->get(u"/search"_s, query({{u"q"_s, q}, {u"chatId"_s, chatId}}),
                  [this, done](const ApiResult &r) {
        QVariantList out;
        for (const auto &v : r.json["results"_L1].toArray()) {
            const QJsonObject o = v.toObject();
            const Message m = Message::fromJson(o["message"_L1].toObject());
            out << QVariantMap{
                {u"chatId"_s, o["chatId"_L1].toString()},
                {u"chatTitle"_s, unescapeHtml(o["chatTitle"_L1].toString())},
                {u"messageId"_s, m.id},
                {u"text"_s, m.text.simplified().left(240)},
                {u"fromUser"_s, m.role == Message::User},
                {u"ts"_s, m.ts},
            };
        }
        reply(done, out);
    });
}

static QVariantMap configFromJson(const QJsonObject &j)
{
    QVariantMap m = j.toVariantMap();
    const QJsonObject h = j["health"_L1].toObject();
    m[u"healthy"_s] = h["healthy"_L1].toBool();
    m[u"uptime"_s] = formatDuration(qint64(h["uptimeMs"_L1].toDouble()));
    m[u"sessions"_s] = h["sessions"_L1].toInt();
    m[u"messagesProcessed"_s] = qint64(h["messages"_L1].toDouble());
    m[u"memoryMb"_s] = h["memoryMb"_L1].toInt();
    m[u"loaded"_s] = true;
    return m;
}

void Session::loadConfig()
{
    if (!m_client)
        return;
    const int epoch = m_epoch;
    m_client->get(u"/config"_s, {}, [this, epoch](const ApiResult &r) {
        if (epoch != m_epoch)
            return;
        m_daemonConfig = r.ok() ? configFromJson(r.json)
                                : QVariantMap{{u"loaded"_s, false}, {u"error"_s, r.error}};
        emit daemonConfigChanged();
    });
}

void Session::updateConfig(const QVariantMap &patch, QJSValue done)
{
    if (!m_client) {
        reply(done, false);
        return;
    }
    m_client->post(u"/config"_s, QJsonObject::fromVariantMap(patch),
                   [this, done](const ApiResult &r) {
        if (r.ok()) {
            m_daemonConfig = configFromJson(r.json);
            emit daemonConfigChanged();
        } else {
            emit notice(u"Couldn't change the setting: %1"_s.arg(r.error));
        }
        reply(done, r.ok());
    });
}

void Session::control(const QString &action, QJSValue done)
{
    if (!m_client) {
        reply(done, QVariantMap{{u"ok"_s, false}, {u"message"_s, u"Not connected"_s}});
        return;
    }
    m_client->post(u"/control"_s, {{"action"_L1, action}},
                   [this, action, done](const ApiResult &r) {
        const bool ok = r.ok() && r.json["ok"_L1].toBool();
        if (ok && action == "restart"_L1)
            m_fastRetryUntil = nowMs() + 60'000;
        reply(done, QVariantMap{
            {u"ok"_s, ok},
            {u"message"_s, r.ok() ? r.json["message"_L1].toString() : r.error},
        });
    });
}

void Session::fetchPlugins(QJSValue done)
{
    if (!m_client) {
        reply(done, QVariantMap{{u"error"_s, u"Not connected"_s}});
        return;
    }
    m_client->get(u"/plugins"_s, {}, [this, done](const ApiResult &r) {
        reply(done, QVariantMap{{u"error"_s, r.error},
                                {u"items"_s, r.json["plugins"_L1].toArray().toVariantList()}});
    });
}

void Session::togglePlugin(const QString &name, bool enabled, QJSValue done)
{
    if (!m_client)
        return;
    // Enabling can hot-load an MCP server; give it room.
    m_client->post(u"/plugins/toggle"_s, {{"name"_L1, name}, {"enabled"_L1, enabled}},
                   [this, done](const ApiResult &r) {
        reply(done, QVariantMap{
            {u"ok"_s, r.ok() && r.json["ok"_L1].toBool()},
            {u"error"_s, r.ok() ? r.json["error"_L1].toString() : r.error},
        });
    }, 60'000);
}

void Session::fetchSkills(QJSValue done)
{
    if (!m_client) {
        reply(done, QVariantMap{{u"error"_s, u"Not connected"_s}});
        return;
    }
    m_client->get(u"/skills"_s, {}, [this, done](const ApiResult &r) {
        reply(done, QVariantMap{{u"error"_s, r.error},
                                {u"items"_s, r.json["skills"_L1].toArray().toVariantList()}});
    });
}

void Session::toggleSkill(const QString &name, bool enabled, QJSValue done)
{
    if (!m_client)
        return;
    m_client->post(u"/skills/toggle"_s, {{"name"_L1, name}, {"enabled"_L1, enabled}},
                   [this, done](const ApiResult &r) {
        reply(done, QVariantMap{
            {u"ok"_s, r.ok() && r.json["ok"_L1].toBool()},
            {u"error"_s, r.ok() ? r.json["error"_L1].toString() : r.error},
        });
    });
}

void Session::fetchLogs(int lines, const QString &minLevel, const QString &component,
                        QJSValue done)
{
    if (!m_client) {
        reply(done, QVariantMap{{u"error"_s, u"Not connected"_s}});
        return;
    }
    m_client->get(u"/logs"_s,
                  query({{u"lines"_s, QString::number(qBound(1, lines, 1000))},
                         {u"level"_s, minLevel},
                         {u"component"_s, component}}),
                  [this, done](const ApiResult &r) {
        reply(done, QVariantMap{{u"error"_s, r.error},
                                {u"entries"_s, r.json["entries"_L1].toArray().toVariantList()}});
    });
}

void Session::fetchMemory(const QString &text, const QString &kind, int limit, QJSValue done)
{
    if (!m_client) {
        reply(done, QVariantMap{{u"error"_s, u"Not connected"_s}});
        return;
    }
    m_client->get(u"/memory"_s,
                  query({{u"q"_s, text.trimmed()},
                         {u"kind"_s, kind},
                         {u"limit"_s, limit > 0 ? QString::number(limit) : QString()}}),
                  [this, done](const ApiResult &r) {
        reply(done, QVariantMap{{u"error"_s, r.error},
                                {u"status"_s, r.status},
                                {u"rows"_s, r.json["rows"_L1].toArray().toVariantList()}});
    });
}

void Session::fetchMemoryWhy(int id, QJSValue done)
{
    if (!m_client)
        return;
    m_client->get(u"/memory/why"_s, query({{u"id"_s, QString::number(id)}}),
                  [this, done](const ApiResult &r) {
        QVariantMap m = r.json.toVariantMap();
        m[u"error"_s] = r.error;
        reply(done, m);
    });
}

void Session::refreshDevices()
{
    if (!m_client || m_conn != Connected)
        return;
    const int epoch = m_epoch;
    m_client->get(u"/devices"_s, {}, [this, epoch](const ApiResult &r) {
        if (epoch != m_epoch || !r.ok())
            return;
        const QString self = m_store->prefString(u"meshDeviceId"_s);
        QHash<QString, QVariantMap> where;
        m_locations.clear();
        for (const auto &v : r.json["locations"_L1].toArray()) {
            const QVariantMap loc = v.toObject().toVariantMap();
            const QString id = loc.value(u"deviceId"_s).toString();
            if (id.isEmpty())
                continue;
            where.insert(id, loc);
            m_locations << loc;
        }
        m_devices.clear();
        for (const auto &v : r.json["devices"_L1].toArray()) {
            QVariantMap dev = v.toObject().toVariantMap();
            const QString id = dev.value(u"id"_s).toString();
            if (id.isEmpty())
                continue;
            dev[u"self"_s] = id == self;
            dev[u"location"_s] = where.value(id);
            dev[u"hasLocation"_s] = where.contains(id);
            m_devices << dev;
        }
        emit devicesChanged();
    });
}

void Session::whoami(QJSValue done)
{
    if (!m_client) {
        reply(done, QVariantMap());
        return;
    }
    m_client->get(u"/auth/whoami"_s, {}, [this, done](const ApiResult &r) {
        QVariantMap m = r.json.toVariantMap();
        m[u"supported"_s] = r.status != 404;
        reply(done, m);
    });
}

// ── Offline snapshot ────────────────────────────────────────────────────────

void Session::hydrate()
{
    if (!m_store->hasProfile() || !m_chats.chats().isEmpty())
        return;
    const QByteArray raw = m_store->readSnapshot(m_store->activeProfileId());
    if (raw.isEmpty())
        return;
    const QJsonObject snap = QJsonDocument::fromJson(raw).object();
    QList<Chat> chats;
    for (const auto &v : snap["chats"_L1].toArray()) {
        const Chat c = Chat::fromJson(v.toObject());
        if (!c.id.isEmpty())
            chats << c;
    }
    const QJsonObject messages = snap["messages"_L1].toObject();
    for (auto it = messages.begin(); it != messages.end(); ++it) {
        QList<Message> rows;
        for (const auto &v : it.value().toArray())
            rows << Message::fromJson(v.toObject());
        std::sort(rows.begin(), rows.end(), messageLess);
        m_data[it.key()].messages = rows;
    }
    m_chats.setChats(chats);
    reconcileSelection();
    AppLog::info("session", u"restored %1 chats from the offline snapshot"_s.arg(chats.size()));
}

void Session::scheduleSnapshot()
{
    if (!m_snapshotTimer.isActive())
        m_snapshotTimer.start();
}

void Session::persist()
{
    m_snapshotTimer.stop();
    saveSnapshot();
    m_store->flush();
}

void Session::saveSnapshot()
{
    if (!m_store->hasProfile() || !m_snapshots)
        return;
    QJsonArray chats;
    for (const Chat &c : m_chats.chats())
        chats << c.toSnapshot();
    QJsonObject messages;
    for (const auto &[id, d] : m_data) {
        if (!m_chats.find(id))
            continue;
        // Local notes are transient and stay out; the newest few rows are
        // enough to paint a cold start before the bridge answers.
        QJsonArray rows;
        int kept = 0;
        for (int i = int(d.messages.size()) - 1; i >= 0 && kept < kSnapshotMessages; --i) {
            if (d.messages.at(i).role == Message::System)
                continue;
            rows.prepend(d.messages.at(i).toSnapshot());
            ++kept;
        }
        if (!rows.isEmpty())
            messages[id] = rows;
    }
    m_store->writeSnapshot(
        m_store->activeProfileId(),
        QJsonDocument(QJsonObject{{"chats"_L1, chats}, {"messages"_L1, messages}})
            .toJson(QJsonDocument::Compact));
}

} // namespace talos
