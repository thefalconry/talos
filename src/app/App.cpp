// SPDX-License-Identifier: GPL-2.0-only
#include "app/App.h"

#include "mesh/Platform.h"
#include "mesh/SelfUpdate.h"
#include "mesh/ServiceInstall.h"

#include <QApplication>
#include <QClipboard>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QLocale>
#include <QMimeData>
#include <QPalette>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QQmlEngine>
#include <QQuickWindow>
#include <QSaveFile>
#include <QSslSocket>
#include <QStyleHints>
#include <QTimer>
#include <QUrlQuery>

namespace talos {

using namespace Qt::StringLiterals;

App::App(QObject *parent)
    : QObject(parent),
      m_session(&m_store, nullptr,
                platform::isMobile() || qEnvironmentVariableIsSet("TALOS_MOBILE")),
      m_mesh(&m_store, &m_session),
      m_media(&m_session, &m_store),
      m_links(&m_session, &m_store),
      m_lock(&m_store),
      m_updater(&m_store),
      m_voice(&m_store),
      m_screen(&m_store)
{
    // Without a TLS library nothing over https works. Say so once at start
    // instead of leaving it to whichever request fails first.
    if (QSslSocket::supportsSsl()) {
        AppLog::info("tls", QSslSocket::sslLibraryVersionString());
    } else {
        AppLog::error("tls", u"no TLS library was found: https connections and client "
                             "certificates will not work"_s);
    }

    // Screen control is offered to the mesh only where the desktop has a
    // way to grant it.
    if (m_screen.available()) {
        m_mesh.computerHandler = [this](const QJsonObject &params, DeviceExec::Done done) {
            m_screen.handle(params, std::move(done));
        };
    }
    connect(&m_mesh, &MeshService::restartRequested, this, [this] {
        // A pushed update replaced the program on disk: start it and leave.
        m_session.persist();
        QProcess::startDetached(selfupdate::ownExecutable(), {});
        quit();
    });

    // The offline snapshot is sealed under the lock's data key whenever a
    // lock is configured, and unreadable while it is locked.
    m_store.snapshotMustSeal = [this] { return m_lock.enabled(); };
    m_store.snapshotKey = [this] { return m_lock.dataKey(); };
    // Credentials are sealed to the lock too, unless something has to read
    // them with nobody there to unlock it: the background service.
    m_store.secretsSealed = [this] {
        return m_lock.canSeal() && !m_unsealing && !m_store.prefBool(u"meshService"_s)
            && !m_store.prefBool(u"meshBackground"_s);
    };
    m_store.secretSeal = [this](const QByteArray &plain) { return m_lock.sealSecret(plain); };
    m_store.secretOpen = [this](const QByteArray &sealed) { return m_lock.openSecret(sealed); };
    connect(&m_lock, &AppLock::unlocked, this, [this] {
        // Covers a lock just switched on, and one from before credentials
        // were sealed.
        m_store.resealSecrets();
        m_session.restoreSnapshot();
        m_session.persist();
        // Started locked: this is the first moment there is a credential.
        if (m_session.conn() == Session::Idle && m_store.hasProfile())
            m_session.start();
    });
    connect(&m_lock, &AppLock::aboutToDisable, this, [this] {
        m_unsealing = true;
        m_store.resealSecrets();
        m_unsealing = false;
    });
    connect(&m_lock, &AppLock::lockedNow, this, [this] {
        // Attachments are cached as ordinary files; with the app locked
        // they go, and are fetched again when next looked at.
        m_media.clear();
    });
    if (m_lock.locked())
        m_media.clear();
    connect(&m_lock, &AppLock::changed, this, [this] {
        // Turning the lock off must not leave the snapshot sealed under a
        // key that no longer exists.
        if (!m_lock.enabled())
            m_session.persist();
    });
    connect(&m_lock, &AppLock::eraseRequested, this, &App::forgetEverything);

    connect(&m_session, &Session::messageArrived, this, &App::onMessage);
    connect(&m_session, &Session::notice, this, &App::toast);
    connect(&m_media, &MediaStore::notice, this, &App::toast);
    connect(&m_session, &Session::connChanged, this, &App::updateTray);

    connect(&m_mesh, &MeshService::ringRequested, this, [this](const QString &message) {
        m_desktop.ring(message);
        showWindow();
        emit toast(message.isEmpty() ? u"Talon rang this device"_s
                                     : u"Talon rang this device: %1"_s.arg(message));
    });
    m_mesh.approver = [this](const QString &command, const QString &detail,
                             std::function<void(const QString &)> decide) {
        if (!m_store.prefBool(u"lockGateCommands"_s)) {
            decide({});
            return;
        }
        // Never run a gated command unapproved: with nobody to ask, refuse.
        if (!m_window) {
            decide(u"Denied on the device: it requires local approval, and nobody "
                   "could be asked."_s);
            return;
        }
        const int id = ++m_approvalSeq;
        m_pending.append({id, std::move(decide)});
        m_approvals << QVariantMap{{u"id"_s, id}, {u"command"_s, command}, {u"detail"_s, detail}};
        emit approvalsChanged();
        m_desktop.notify(u"Talon wants to run a command"_s,
                         u"Open the app to approve or deny “%1”."_s.arg(command));
        QTimer::singleShot(90'000, this, [this, id] { resolveApproval(id, false); });
    };

    connect(&m_desktop, &Desktop::activateRequested, this, &App::showWindow);
    connect(&m_desktop, &Desktop::quitRequested, this, &App::quit);
    // Before any of the interface is built.
    applyLanguage();
    connect(&m_shortcut, &GlobalShortcut::activated, this, &App::quickAskRequested);
    // The desktop remembers the keys; asking again at start is how a
    // session gets them back.
    if (m_store.prefBool(u"quickShortcut"_s))
        QTimer::singleShot(0, &m_shortcut, &GlobalShortcut::bind);
    connect(&m_desktop, &Desktop::openChatRequested, this, [this](const QString &chatId) {
        showWindow();
        if (!m_lock.locked())
            emit openChatRequested(chatId);
    });
    connect(&m_desktop, &Desktop::replyRequested, this,
            [this](const QString &chatId, const QString &text) {
        if (!m_lock.locked())
            m_session.sendTo(chatId, text);
    });
    connect(&m_desktop, &Desktop::newChatRequested, this, [this] {
        showWindow();
        m_session.newChat();
    });

    connect(&m_store, &Store::prefChanged, this, [this](const QString &key) {
        if (key == "closeToTray"_L1)
            updateTray();
        else if (key == "screenPrivacy"_L1)
            platform::setSecureWindow(m_store.prefBool(key));
        else if (key == "meshSharing"_L1 || key == "meshBackground"_L1)
            updateKeepAlive();
        else if (key == "language"_L1)
            applyLanguage();
        else if (key == "quickShortcut"_L1)
            m_store.prefBool(key) ? m_shortcut.bind() : m_shortcut.release();
        // Whether credentials may stay sealed depends on these two.
        if (key == "meshService"_L1 || key == "meshBackground"_L1)
            m_store.resealSecrets();
        else if (key == "autostart"_L1)
            m_desktop.setAutostart(m_store.prefBool(key));
        else if (key == "voiceName"_L1 || key == "voiceRate"_L1)
            m_voice.applySettings();
    });

    connect(qApp->styleHints(), &QStyleHints::colorSchemeChanged, this,
            &App::systemDarkChanged);
    connect(qApp, &QGuiApplication::applicationStateChanged, this,
            [this](Qt::ApplicationState state) {
        const bool active = state == Qt::ApplicationActive;
        if (active != m_active) {
            m_active = active;
            emit activeChanged();
        }
        if (active) {
            m_lock.setAppActive(true);
            m_session.resume();
            // Android only lets the service start while the app is in
            // front, so this is where a start that was refused is retried.
            updateKeepAlive();
        } else if (state == Qt::ApplicationHidden || state == Qt::ApplicationSuspended) {
            // Losing focus on a desktop is not "away"; being put in the
            // background on a phone is.
            m_lock.setAppActive(false);
            m_session.persist();
        }
    });
    qApp->installEventFilter(this);

    // A scratch profile can point downloads away from the real folder.
    if (qEnvironmentVariableIsSet("TALOS_DOWNLOADS"))
        m_links.setDownloadDir(qEnvironmentVariable("TALOS_DOWNLOADS"));
    m_media.prune();
    if (m_store.prefBool(u"screenPrivacy"_s))
        platform::setSecureWindow(true);
}

App::~App()
{
    m_session.persist();
}

void App::attach(QQmlEngine *engine, QQuickWindow *window)
{
    m_engine = engine;
    m_window = window;
    m_session.setScriptEngine(engine);
    m_links.setScriptEngine(engine);
    m_lock.setScriptEngine(engine);
    updateTray();
}

bool App::serviceSupported() const
{
    return service::supported();
}

QString App::serviceState() const
{
    return service::state();
}

bool App::serviceLingering() const
{
    return service::supported() && service::lingering();
}

QString App::setBackgroundService(bool on)
{
    QString error;
    if (on) {
        // Hand the mesh to the service rather than answer every command twice.
        if (service::enable(&m_store, &error))
            m_mesh.yield();
    } else {
        service::disable(&m_store, &error);
        m_mesh.reconfigure();
    }
    emit serviceChanged();
    return error;
}

QString App::platform() const
{
    return platform::name();
}

bool App::mobile() const
{
    return platform::isMobile() || qEnvironmentVariableIsSet("TALOS_MOBILE");
}

bool App::systemDark() const
{
    return qApp->styleHints()->colorScheme() == Qt::ColorScheme::Dark;
}

bool App::eventFilter(QObject *watched, QEvent *event)
{
    switch (event->type()) {
    case QEvent::MouseButtonPress:
    case QEvent::KeyPress:
    case QEvent::TouchBegin:
    case QEvent::Wheel:
        m_lock.touch();
        break;
    default:
        break;
    }
    return QObject::eventFilter(watched, event);
}

void App::updateTray()
{
    const QString state = m_session.conn() == Session::Connected ? u"Connected"_s
        : m_session.conn() == Session::Connecting ? u"Connecting…"_s : u"Not connected"_s;
    m_desktop.showTray(m_store.prefBool(u"closeToTray"_s), u"Talos · %1"_s.arg(state));
}

void App::onMessage(const QString &chatId, const QString &title, const QString &text,
                    bool fromAssistant)
{
    if (!fromAssistant)
        return;
    const bool onScreen = m_active && chatId == m_session.selectedChatId() && !m_lock.locked();
    if (onScreen) {
        if (m_store.prefBool(u"voiceAutoSpeak"_s))
            m_voice.speak(text);
        return;
    }
    if (!m_store.prefBool(u"notifications"_s))
        return;
    // Behind the lock a notification says that something arrived, never what.
    const bool preview = m_store.prefBool(u"notifyPreview"_s) && !m_lock.locked();
    QString body = text.simplified();
    if (body.size() > 180)
        body = body.left(179) + u'…';
    // Locked, a notification is neither a way in nor a way to answer.
    if (m_lock.locked()) {
        m_desktop.notify(title.isEmpty() ? m_session.botName() : title, u"New message"_s);
        return;
    }
    m_desktop.notifyChat(chatId, title.isEmpty() ? m_session.botName() : title,
                         preview && !body.isEmpty() ? body : u"New message"_s, true);
}

// ── Connection profiles ─────────────────────────────────────────────────────

QVariantMap App::newProfileForm() const
{
    return {
        {u"id"_s, QString()},
        {u"name"_s, QString()},
        {u"mode"_s, mobile() ? u"remote"_s : u"local"_s},
        {u"manageDaemon"_s, false},
        {u"launchCommand"_s, u"talon start"_s},
        {u"address"_s, QString()},
        {u"tls"_s, true},
        {u"token"_s, QString()},
        {u"hasToken"_s, false},
        {u"fingerprint"_s, QString()},
        {u"localUrl"_s, QString()},
        {u"clientP12"_s, QString()},
        {u"certPassword"_s, QString()},
        {u"certName"_s, QString()},
        {u"hasClientCert"_s, false},
        {u"clearClientCert"_s, false},
    };
}

QVariantMap App::profileForm(const QString &id) const
{
    QVariantMap form = newProfileForm();
    for (const Profile &p : m_store.profiles()) {
        if (p.id != id)
            continue;
        form[u"id"_s] = p.id;
        form[u"name"_s] = p.name;
        form[u"mode"_s] = p.local ? u"local"_s : u"remote"_s;
        form[u"manageDaemon"_s] = p.manageDaemon;
        form[u"launchCommand"_s] = (QStringList{p.launchCommand} + p.launchArgs).join(u' ');
        form[u"address"_s] = p.local ? QString() : p.baseUrl();
        form[u"tls"_s] = p.tls;
        form[u"fingerprint"_s] = formatFingerprint(p.fingerprint);
        form[u"localUrl"_s] = p.localUrl;
        form[u"hasClientCert"_s] = p.certStored;
        // Whether a token is set, never the token itself.
        const auto full = m_store.profileById(id);
        form[u"hasToken"_s] = full && !full->token.isEmpty();
        break;
    }
    return form;
}

QVariantMap App::formFromPairLink(const QString &text) const
{
    const QString link = findPairLink(text);
    if (link.isEmpty())
        return {{u"error"_s, u"No pairing link found. It starts with talon://pair"_s}};
    const auto p = profileFromPairLink(link);
    if (!p) {
        return {{u"error"_s, u"That pairing link can't be used: an https bridge must name its "
                             "certificate, and plain http is only accepted on a private "
                             "address."_s}};
    }
    QVariantMap form = newProfileForm();
    form[u"mode"_s] = u"remote"_s;
    form[u"name"_s] = p->name;
    form[u"address"_s] = p->baseUrl();
    form[u"tls"_s] = p->tls;
    form[u"token"_s] = p->token;
    form[u"fingerprint"_s] = formatFingerprint(p->fingerprint);
    form[u"fromLink"_s] = true;
    return form;
}

Profile App::profileFromForm(const QVariantMap &form, QString *error) const
{
    const QString id = form.value(u"id"_s).toString();
    const std::optional<Profile> existing = id.isEmpty() ? std::nullopt : m_store.profileById(id);
    Profile p = existing.value_or(Profile::defaults());
    p.name = form.value(u"name"_s).toString().trimmed();
    p.local = form.value(u"mode"_s).toString() == "local"_L1;

    if (p.local) {
        p.manageDaemon = form.value(u"manageDaemon"_s).toBool();
        const QStringList launch =
            QProcess::splitCommand(form.value(u"launchCommand"_s).toString());
        if (!launch.isEmpty()) {
            p.launchCommand = launch.first();
            p.launchArgs = launch.mid(1);
        }
        p.host = u"127.0.0.1"_s;
        p.tls = false;
        p.token.clear();
        p.fingerprint.clear();
        p.pinObserved = false;
        p.clientP12.clear();
        p.clientP12Password.clear();
        p.localUrl.clear();
        return p;
    }

    const HostInput in = parseHostInput(form.value(u"address"_s).toString());
    if (in.host.isEmpty()) {
        *error = u"Enter the server's address."_s;
        return p;
    }
    const QString oldKey = existing ? existing->bridgeKey() : QString();
    p.host = in.host.toLower();
    // A scheme typed into the address decides; otherwise the toggle does.
    p.tls = in.tls >= 0 ? in.tls == 1 : form.value(u"tls"_s).toBool();
    // No port written: a URL means its scheme's own port (a reverse proxy
    // lives there), a bare host means the bridge's.
    p.port = in.port > 0 ? in.port : in.tls >= 0 ? Profile::defaultPort(p.tls) : 19880;
    const bool sameBridge = existing && !existing->local && oldKey == p.bridgeKey();

    const QString token = form.value(u"token"_s).toString().trimmed();
    if (!token.isEmpty())
        p.token = token;
    else if (!sameBridge)
        p.token.clear(); // a token belongs to the bridge it was issued by

    const QString typed = form.value(u"fingerprint"_s).toString().trimmed();
    const QString fp = normalizeFingerprint(typed);
    if (!typed.isEmpty() && fp.isEmpty()) {
        *error = u"The certificate fingerprint should be 64 hex digits (SHA-256)."_s;
        return p;
    }
    const QString previous = sameBridge ? existing->fingerprint : QString();
    p.fingerprint = p.tls ? fp : QString();
    // An observation is of one certificate: a new or cleared pin starts over.
    p.pinObserved = !p.fingerprint.isEmpty() && p.fingerprint == previous
        && existing->pinObserved;

    if (form.value(u"clearClientCert"_s).toBool()) {
        p.clientP12.clear();
        p.clientP12Password.clear();
    }
    const QString p12 = form.value(u"clientP12"_s).toString();
    if (!p12.isEmpty()) {
        p.clientP12 = QByteArray::fromBase64(p12.toLatin1());
        p.clientP12Password = form.value(u"certPassword"_s).toString();
    }
    p.localUrl = form.value(u"localUrl"_s).toString().trimmed();
    if (!p.localUrl.isEmpty() && parseHostInput(p.localUrl).host.isEmpty()) {
        *error = u"The local network address doesn't look like an address."_s;
        return p;
    }
    if (p.name.isEmpty())
        p.name = p.host;
    return p;
}

QVariantMap App::checkForm(const QVariantMap &form) const
{
    QString error;
    const Profile p = profileFromForm(form, &error);
    if (!error.isEmpty())
        return {{u"ok"_s, false}, {u"error"_s, error}};
    QString warning;
    if (!p.local && !p.tls && !isPrivateAddress(p.host)) {
        warning = u"This address is not on a private network and the connection is not "
                  "encrypted: the token would travel in the clear."_s;
    }
    return {
        {u"ok"_s, true},
        // A TLS server we hold no pin for: look at its certificate first,
        // and let the person confirm it before any token is sent.
        {u"needsFingerprint"_s, !p.local && p.tls && p.fingerprint.isEmpty()},
        {u"address"_s, p.baseUrl()},
        {u"warning"_s, warning},
    };
}

bool App::isPairPage(const QString &text) const
{
    return isPairPageUrl(text);
}

void App::redeemPairPage(const QString &address, QJSValue done)
{
    const auto answer = [this, done](const QVariantMap &result) {
        if (!m_engine || !done.isCallable())
            return;
        QJSValue fn = done;
        fn.call({m_engine->toScriptValue(result)});
    };
    const QUrl url(address.trimmed());
    const bool tls = url.scheme().toLower() == "https"_L1;
    if (!isPairPageUrl(address)) {
        answer({{u"error"_s, u"That isn't a pairing page address."_s}});
        return;
    }
    // The page hands over a credential, so it is only fetched in the clear
    // from an address that can't be on the open internet.
    if (!tls && !isPrivateAddress(url.host())) {
        answer({{u"error"_s, u"That pairing page is plain http on a public address, where "
                             "anyone on the way could read the credential it hands over."_s}});
        return;
    }
    // No pin yet and no credential: the page is public by design, and the
    // payload names the certificate every later connection must present.
    Profile target = Profile::defaults();
    target.local = false;
    target.manageDaemon = false;
    target.host = url.host();
    target.tls = tls;
    target.port = url.port(Profile::defaultPort(tls));
    auto *client = new BridgeClient(target, this);
    QUrlQuery query;
    query.addQueryItem(u"grant"_s, QUrlQuery(url).queryItemValue(u"grant"_s, QUrl::FullyDecoded));
    query.addQueryItem(u"format"_s, u"json"_s);
    client->get(u"/pair"_s, query, [this, client, answer](const ApiResult &r) {
        client->deleteLater();
        if (r.kind == ApiResult::ClientCertRequired) {
            answer({{u"error"_s, u"This server asks for a client certificate before it shows "
                                 "anything, the pairing page included. Add the connection by "
                                 "hand and import the certificate there."_s}});
            return;
        }
        if (r.status == 404) {
            answer({{u"error"_s, u"That pairing link is unknown, expired or already used. Ask "
                                 "Talon for a new one with /mesh link."_s}});
            return;
        }
        if (!r.ok()) {
            answer({{u"error"_s, u"Couldn't open the pairing page: %1"_s.arg(r.error)}});
            return;
        }
        // The same link the page's own button carries, so both ways of
        // pairing pass through one set of checks.
        QUrlQuery q;
        q.addQueryItem(u"u"_s, QString::fromUtf8(QUrl::toPercentEncoding(r.json["url"_L1].toString())));
        q.addQueryItem(u"t"_s, QString::fromUtf8(QUrl::toPercentEncoding(r.json["token"_L1].toString())));
        if (r.json.contains("fingerprint"_L1))
            q.addQueryItem(u"f"_s, r.json["fingerprint"_L1].toString());
        if (r.json.contains("label"_L1))
            q.addQueryItem(u"n"_s, QString::fromUtf8(QUrl::toPercentEncoding(r.json["label"_L1].toString())));
        answer(formFromPairLink(u"talon://pair?"_s + q.toString(QUrl::FullyEncoded)));
    });
}

void App::probeForm(const QVariantMap &form, QJSValue done)
{
    QString error;
    const Profile p = profileFromForm(form, &error);
    const auto answer = [this, done](const QVariantMap &result) {
        if (!m_engine || !done.isCallable())
            return;
        QJSValue fn = done;
        fn.call({m_engine->toScriptValue(result)});
    };
    if (!error.isEmpty() || p.local || !p.tls) {
        answer({{u"reached"_s, false}, {u"error"_s, error}});
        return;
    }
    // The LAN address is the bridge itself, so that is the certificate the
    // pin is for.
    const Profile target = p.lanEndpoint().value_or(p);
    BridgeClient::probeCertificate(target, this, [answer, target](const QString &fp, bool reached) {
        answer({
            {u"reached"_s, reached},
            {u"fingerprint"_s, fp},
            {u"pretty"_s, formatFingerprint(fp)},
            {u"address"_s, target.baseUrl()},
            // No fingerprint from a server that answered: its certificate
            // chains to an authority this device already trusts.
            {u"trusted"_s, reached && fp.isEmpty()},
        });
    });
}

void App::connectWithForm(const QVariantMap &form)
{
    QString error;
    const Profile p = profileFromForm(form, &error);
    if (!error.isEmpty()) {
        emit toast(error);
        return;
    }
    m_session.applyProfile(p);
}

QVariantMap App::importCertificate(const QUrl &file, const QString &password) const
{
    const QString path = file.isLocalFile() ? file.toLocalFile() : file.toString();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {{u"ok"_s, false}, {u"error"_s, u"Couldn't read that file."_s}};
    if (f.size() > 1024 * 1024)
        return {{u"ok"_s, false}, {u"error"_s, u"That file is too large to be a certificate."_s}};
    const QByteArray bytes = f.readAll();
    QString error;
    if (!BridgeClient::checkPkcs12(bytes, password, &error))
        return {{u"ok"_s, false}, {u"error"_s, error}};
    return {
        {u"ok"_s, true},
        {u"p12"_s, QString::fromLatin1(bytes.toBase64())},
        {u"name"_s, QFileInfo(path).fileName()},
    };
}

void App::forgetEverything()
{
    m_session.forgetEverything();
    emit toast(u"The connection and everything cached from it were erased"_s);
}

void App::handleExternalLink(const QString &link)
{
    // `talos --quick` from a second launch: the small window, not the big one.
    if (link == "talos:quick"_L1) {
        emit quickAskRequested();
        return;
    }
    showWindow();
    if (!findPairLink(link).isEmpty())
        emit pairLinkReceived(findPairLink(link));
}

// ── Utilities ───────────────────────────────────────────────────────────────

void App::setLinkColor(const QColor &color)
{
    QPalette palette = QGuiApplication::palette();
    palette.setColor(QPalette::Link, color);
    palette.setColor(QPalette::LinkVisited, color);
    QGuiApplication::setPalette(palette);
}

void App::copy(const QString &text)
{
    QGuiApplication::clipboard()->setText(text);
}

QString App::clipboardText() const
{
    return QGuiApplication::clipboard()->text();
}

bool App::clipboardHasImage() const
{
    const QMimeData *mime = QGuiApplication::clipboard()->mimeData();
    return mime && mime->hasImage();
}

bool App::saveText(const QUrl &file, const QString &text)
{
    QSaveFile f(file.isLocalFile() ? file.toLocalFile() : file.toString());
    if (!f.open(QIODevice::WriteOnly))
        return false;
    f.write(text.toUtf8());
    return f.commit();
}

QString App::timeLabel(qint64 ms) const
{
    return QLocale().toString(QDateTime::fromMSecsSinceEpoch(ms).time(), QLocale::ShortFormat);
}

QString App::dateTimeLabel(qint64 ms) const
{
    return ms > 0 ? QLocale().toString(QDateTime::fromMSecsSinceEpoch(ms), QLocale::ShortFormat)
                  : QString();
}

QString App::relativeTime(qint64 ms) const
{
    if (ms <= 0)
        return u"never"_s;
    const qint64 s = (QDateTime::currentMSecsSinceEpoch() - ms) / 1000;
    if (s < 45)
        return u"just now"_s;
    if (s < 3600)
        return u"%1 min ago"_s.arg(qMax<qint64>(1, s / 60));
    if (s < 86'400)
        return u"%1 h ago"_s.arg(s / 3600);
    if (s < 7 * 86'400)
        return u"%1 d ago"_s.arg(s / 86'400);
    return QLocale().toString(QDateTime::fromMSecsSinceEpoch(ms).date(), QLocale::ShortFormat);
}

QString App::fileName(const QUrl &file) const
{
    return QFileInfo(file.isLocalFile() ? file.toLocalFile() : file.path()).fileName();
}

QString App::mapLink(double lat, double lon) const
{
    return u"https://www.openstreetmap.org/?mlat=%1&mlon=%2#map=16/%1/%2"_s
        .arg(lat, 0, 'f', 6).arg(lon, 0, 'f', 6);
}

void App::resolveApproval(int id, bool allow)
{
    for (int i = 0; i < m_pending.size(); ++i) {
        if (m_pending.at(i).id != id)
            continue;
        const auto decide = m_pending.at(i).decide;
        m_pending.removeAt(i);
        m_approvals.removeAt(i);
        emit approvalsChanged();
        decide(allow ? QString()
                     : u"Denied on the device: the command was not approved there."_s);
        return;
    }
}

void App::showWindow()
{
    if (!m_window)
        return;
    m_window->show();
    if (m_window->visibility() == QWindow::Minimized)
        m_window->showNormal();
    m_window->raise();
    m_window->requestActivate();
}

QVariantList App::languages() const
{
    return {
        QVariantMap{{u"code"_s, QString()}, {u"name"_s, tr("System")}},
        QVariantMap{{u"code"_s, u"en"_s}, {u"name"_s, u"English"_s}},
        QVariantMap{{u"code"_s, u"es"_s}, {u"name"_s, u"Español"_s}},
        QVariantMap{{u"code"_s, u"hi"_s}, {u"name"_s, u"हिन्दी"_s}},
    };
}

void App::applyLanguage()
{
    const QString chosen = m_store.prefString(u"language"_s);
    QCoreApplication::removeTranslator(&m_translator);
    const QLocale locale = chosen.isEmpty() ? QLocale() : QLocale(chosen);
    // English is what the source says; a missing file means the same.
    if (locale.language() != QLocale::English
        && m_translator.load(locale, u"talos"_s, u"_"_s, u":/i18n"_s))
        QCoreApplication::installTranslator(&m_translator);
    if (m_engine)
        m_engine->retranslate();
    m_session.refreshChatList();
}

void App::updateKeepAlive()
{
    platform::setKeepAlive(m_store.prefBool(u"meshSharing"_s) && m_store.prefBool(u"meshBackground"_s),
                           u"Staying reachable on the mesh"_s);
}

QUrl App::suggestedFile(const QString &title, const QString &suffix) const
{
    static const QRegularExpression unsafe(u"[\\x00-\\x1f<>:\"/\\\\|?*]+"_s);
    QString name = title.simplified().left(80);
    name.replace(unsafe, u" "_s);
    name = name.simplified();
    if (name.isEmpty() || name.startsWith(u'.'))
        name = u"chat"_s;
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    return QUrl::fromLocalFile(QDir(dir).filePath(name + u'.' + suffix));
}

QString App::localPath(const QUrl &url) const
{
    return url.isLocalFile() ? QDir::cleanPath(url.toLocalFile()) : QString();
}

bool App::moveToBackground()
{
    if (!platform::moveToBackground())
        return false;
    m_session.persist();
    return true;
}

void App::quit()
{
    m_quitting = true;
    m_session.persist();
    if (m_lock.enabled())
        m_media.clear();
    QCoreApplication::quit();
}

bool App::grab(const QString &path)
{
    return m_window && m_window->grabWindow().save(path);
}

} // namespace talos
