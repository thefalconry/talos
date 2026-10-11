// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <QHash>
#include <QJSValue>
#include <QObject>
#include <QPointer>
#include <QUrl>
#include <QVariantMap>

class QJSEngine;
class QNetworkReply;

namespace talos {

class Session;
class Store;

/// Opens links the way each kind needs.
///
/// Talon hands out links on its own address: files (`/media`,
/// `/node/binary`), the secret-drop form (`/secret?grant=…`) and pairing
/// pages. When the bridge sits behind a reverse proxy that demands a client
/// certificate, a system browser cannot open any of them: it has no
/// certificate. So a link that points at the connected bridge is fetched
/// here, through the same connection the app itself uses: the imported
/// client certificate, the pinned server certificate and the bearer token.
/// The LAN and the public address of one profile are the same bridge, so a
/// link naming either is served over whichever is connected.
///
/// Everything else goes to the system browser.
class BridgeLinks : public QObject
{
    Q_OBJECT
public:
    BridgeLinks(Session *session, Store *store, QObject *parent = nullptr);
    void setScriptEngine(QJSEngine *engine) { m_engine = engine; }
    /// Where downloads land. Defaults to the user's Downloads folder.
    void setDownloadDir(const QString &dir) { m_downloadDir = dir; }
    Q_INVOKABLE QString downloadDir() const;

    /// What a link is: `pair` (a talon://pair link), `secret` (the bridge's
    /// secret-drop form), `bridge` (anything else the bridge serves) or
    /// `external`.
    Q_INVOKABLE QVariantMap inspect(const QString &link) const;
    /// True when @p url names the bridge this session is connected to.
    bool isBridgeUrl(const QUrl &url) const;

    /// Fetch a bridge link through the app's own connection. A file is
    /// saved to the Downloads folder; a web page comes back as `html` for
    /// the caller to show. The callback gets `{ok, error, file, name, html}`.
    Q_INVOKABLE void download(const QString &link, QJSValue done);
    Q_INVOKABLE void cancel(const QString &link);

    /// Whether a secret-drop link is still live (asking does not spend it).
    Q_INVOKABLE void checkSecretDrop(const QString &link, QJSValue done);
    /// Submit the value for a secret-drop link straight to the bridge. It
    /// never passes through a chat. The callback gets `{ok, error}`.
    Q_INVOKABLE void submitSecretDrop(const QString &link, const QString &value, QJSValue done);

    Q_INVOKABLE void openExternally(const QString &link);
    Q_INVOKABLE void openFile(const QString &file);
    Q_INVOKABLE void revealFile(const QString &file);

signals:
    void progress(const QString &link, double fraction, const QString &label);

private:
    void reply(const QJSValue &cb, const QVariant &value) const;

    Session *m_session;
    Store *m_store;
    QJSEngine *m_engine = nullptr;
    QString m_downloadDir;
    QHash<QString, QPointer<QNetworkReply>> m_active;
};

} // namespace talos
