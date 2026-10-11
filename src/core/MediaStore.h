// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <QHash>
#include <QObject>
#include <QPointer>
#include <QUrl>

class QNetworkReply;

namespace talos {

class Session;
class Store;

/// Fetches files the bridge serves (`/media?id=…`) into a local cache.
///
/// Media needs the bearer token, which belongs in a header and never in a
/// URL, so an `Image` cannot simply be pointed at the bridge. A view asks
/// for a path here and binds to the local file once it has arrived.
class MediaStore : public QObject
{
    Q_OBJECT
public:
    MediaStore(Session *session, Store *store, QObject *parent = nullptr);

    /// The cached copy as a `file:` URL, or empty when it isn't on disk yet.
    Q_INVOKABLE QUrl cached(const QString &path, const QString &name = {}) const;
    /// Download into the cache; ready() or failed() follows.
    Q_INVOKABLE void fetch(const QString &path, const QString &name = {});
    /// Download if needed, then hand the file to the system's default app.
    Q_INVOKABLE void open(const QString &path, const QString &name);
    /// Download if needed, then copy it to @p destination.
    Q_INVOKABLE void saveTo(const QString &path, const QString &name, const QUrl &destination);
    Q_INVOKABLE QString downloadsDir() const;
    /// Drop cached files not touched for a week.
    void prune();

signals:
    void ready(const QString &path, const QUrl &local);
    void failed(const QString &path, const QString &error);
    void notice(const QString &text);

private:
    QString localPath(const QString &path, const QString &name) const;
    void whenReady(const QString &path, const QString &name,
                   std::function<void(const QString &local)> then);

    Session *m_session;
    Store *m_store;
    QHash<QString, QPointer<QNetworkReply>> m_inFlight;
};

} // namespace talos
