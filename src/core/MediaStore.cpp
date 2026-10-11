// SPDX-License-Identifier: GPL-2.0-only
#include "core/MediaStore.h"

#include "core/AppLog.h"
#include "core/Session.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QSaveFile>
#include <QStandardPaths>

namespace talos {

using namespace Qt::StringLiterals;

MediaStore::MediaStore(Session *session, Store *store, QObject *parent)
    : QObject(parent), m_session(session), m_store(store)
{
}

QString MediaStore::localPath(const QString &path, const QString &name) const
{
    // Media ids are only unique within one daemon run, so the daemon's
    // start time is part of the key: a restarted daemon reusing `m1` must
    // not be served the old `m1` from disk.
    const QByteArray key = (m_session->activeEndpoint().baseUrl() + u'\n'
                            + m_session->status().value(u"startedAt"_s).toString() + u'\n' + path)
                               .toUtf8();
    const QString hash = QString::fromLatin1(
        QCryptographicHash::hash(key, QCryptographicHash::Sha1).toHex());
    // Keep the real name: the system picks the opening app from it.
    QString file = QFileInfo(name).fileName();
    if (file.isEmpty())
        file = u"file"_s;
    return QDir(m_store->cacheDir()).filePath(u"media/%1/%2"_s.arg(hash, file));
}

QUrl MediaStore::cached(const QString &path, const QString &name) const
{
    if (path.isEmpty())
        return {};
    const QString local = localPath(path, name);
    return QFileInfo::exists(local) ? QUrl::fromLocalFile(local) : QUrl();
}

void MediaStore::fetch(const QString &path, const QString &name)
{
    whenReady(path, name, {});
}

void MediaStore::whenReady(const QString &path, const QString &name,
                           std::function<void(const QString &)> then)
{
    if (path.isEmpty())
        return;
    const QString local = localPath(path, name);
    if (QFileInfo::exists(local)) {
        emit ready(path, QUrl::fromLocalFile(local));
        if (then)
            then(local);
        return;
    }
    BridgeClient *client = m_session->client();
    if (!client) {
        emit failed(path, u"Not connected"_s);
        return;
    }
    if (const auto running = m_inFlight.value(local)) {
        if (then) {
            connect(running, &QNetworkReply::finished, this, [local, then] {
                if (QFileInfo::exists(local))
                    then(local);
            });
        }
        return;
    }

    // `path` is a relative bridge path that already carries its query.
    const QUrl relative(path);
    QNetworkReply *reply = client->rawGet(relative.path(), QUrlQuery(relative.query()));
    m_inFlight.insert(local, reply);
    QDir().mkpath(QFileInfo(local).absolutePath());
    auto *out = new QSaveFile(local, reply);
    const bool opened = out->open(QIODevice::WriteOnly);
    connect(reply, &QNetworkReply::readyRead, this, [reply, out, opened] {
        if (opened && reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt() == 200)
            out->write(reply->readAll());
    });
    const QPointer<BridgeClient> owner = client;
    connect(reply, &QNetworkReply::finished, this,
            [this, reply, out, opened, path, local, then, owner] {
        reply->deleteLater();
        m_inFlight.remove(local);
        ApiResult r;
        if (owner) {
            r = owner->diagnose(reply, false);
        } else {
            r.kind = ApiResult::Network;
            r.error = u"Disconnected"_s;
        }
        if (!opened || !r.ok() || !out->commit()) {
            out->cancelWriting();
            const QString error = !r.ok() ? r.error : u"Couldn't write to the cache"_s;
            AppLog::warn("media", u"fetch failed: %1"_s.arg(error));
            emit failed(path, error);
            return;
        }
        emit ready(path, QUrl::fromLocalFile(local));
        if (then)
            then(local);
    });
}

void MediaStore::open(const QString &path, const QString &name)
{
    whenReady(path, name, [this, name](const QString &local) {
        if (!QDesktopServices::openUrl(QUrl::fromLocalFile(local)))
            emit notice(u"No app on this device opens %1"_s.arg(name));
    });
}

void MediaStore::saveTo(const QString &path, const QString &name, const QUrl &destination)
{
    whenReady(path, name, [this, name, destination](const QString &local) {
        QString dest = destination.isLocalFile() ? destination.toLocalFile()
                                                 : destination.toString();
        if (QFileInfo(dest).isDir())
            dest = QDir(dest).filePath(QFileInfo(name).fileName());
        QFile::remove(dest);
        if (QFile::copy(local, dest))
            emit notice(u"Saved %1"_s.arg(QFileInfo(dest).fileName()));
        else
            emit notice(u"Couldn't save to %1"_s.arg(dest));
    });
}

QString MediaStore::downloadsDir() const
{
    return QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
}

void MediaStore::prune()
{
    const QString root = QDir(m_store->cacheDir()).filePath(u"media"_s);
    const QDateTime cutoff = QDateTime::currentDateTime().addDays(-7);
    QDirIterator it(root, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        if (it.fileInfo().lastModified() < cutoff)
            QFile::remove(it.filePath());
    }
    // Leave no empty hash directories behind.
    for (const QString &dir : QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot))
        QDir(root).rmdir(dir);
}

} // namespace talos
