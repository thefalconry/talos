// SPDX-License-Identifier: GPL-2.0-only
#include "app/Updater.h"

#include "core/AppLog.h"
#include "core/Models.h"
#include "core/Store.h"
#include "mesh/Platform.h"
#include "mesh/SelfUpdate.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkReply>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QSysInfo>

namespace talos {

using namespace Qt::StringLiterals;

Updater::Updater(Store *store, QObject *parent) : QObject(parent), m_store(store)
{
    m_nam.setRedirectPolicy(QNetworkRequest::NoLessSafeRedirectPolicy);
    m_state = repo().isEmpty() ? u"unconfigured"_s : u"idle"_s;

    // On launch (after things settle) and every six hours.
    m_timer.setInterval(6 * 3600 * 1000);
    connect(&m_timer, &QTimer::timeout, this, [this] {
        if (m_store->prefBool(u"updateAutoCheck"_s) && m_state != "downloading"_L1)
            check();
    });
    m_timer.start();
    QTimer::singleShot(8000, this, [this] {
        if (m_store->prefBool(u"updateAutoCheck"_s) && m_state == "idle"_L1)
            check();
    });
    connect(m_store, &Store::prefChanged, this, [this](const QString &key) {
        if (key != "updateRepo"_L1 || m_state == "downloading"_L1)
            return;
        m_latest.clear();
        set(repo().isEmpty() ? u"unconfigured"_s : u"idle"_s);
    });
}

QString Updater::repo() const
{
    static const QRegularExpression shape(u"^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$"_s);
    const QString r = m_store->prefString(u"updateRepo"_s).trimmed();
    return shape.match(r).hasMatch() ? r : QString();
}

QString Updater::assetSize() const
{
    return formatBytes(m_assetBytes);
}

QString Updater::lastChecked() const
{
    return m_lastCheckedMs
        ? QLocale().toString(QDateTime::fromMSecsSinceEpoch(m_lastCheckedMs), QLocale::ShortFormat)
        : QString();
}

void Updater::set(const QString &state, const QString &error)
{
    m_state = state;
    m_error = error;
    emit changed();
}

int Updater::compareVersions(const QString &a, const QString &b)
{
    return selfupdate::compareVersions(a, b);
}

int Updater::pickAsset(const QStringList &names, const QString &platform, const QString &arch)
{
    QStringList suffixes;
    if (platform == "linux"_L1)
        suffixes = {u".appimage"_s, u".tar.gz"_s};
    else if (platform == "windows"_L1)
        suffixes = {u"setup.exe"_s, u".msi"_s, u".exe"_s, u".zip"_s};
    else if (platform == "macos"_L1)
        suffixes = {u".dmg"_s};
    else if (platform == "android"_L1)
        suffixes = {u".apk"_s};
    const bool arm = arch.contains("arm"_L1) || arch.contains("aarch"_L1);
    static const QStringList armMarks{u"arm64"_s, u"aarch64"_s};
    static const QStringList x86Marks{u"x86_64"_s, u"amd64"_s, u"x64"_s};

    for (const QString &suffix : suffixes) {
        int fallback = -1;
        for (int i = 0; i < names.size(); ++i) {
            const QString n = names.at(i).toLower();
            if (!n.endsWith(suffix))
                continue;
            // The archive for another OS can share a suffix.
            if (suffix == ".tar.gz"_L1 && !n.contains("linux"_L1))
                continue;
            if (suffix == ".zip"_L1 && !n.contains("win"_L1))
                continue;
            const auto has = [&n](const QStringList &marks) {
                return std::any_of(marks.begin(), marks.end(),
                                   [&n](const QString &m) { return n.contains(m); });
            };
            const bool namesArm = has(armMarks), namesX86 = has(x86Marks);
            if ((arm && namesArm) || (!arm && namesX86))
                return i;
            // An asset that names no architecture suits either.
            if (!namesArm && !namesX86 && fallback < 0)
                fallback = i;
        }
        if (fallback >= 0)
            return fallback;
    }
    return -1;
}

void Updater::check()
{
    const QString r = repo();
    if (r.isEmpty()) {
        set(u"unconfigured"_s);
        return;
    }
    if (m_state == "checking"_L1 || m_state == "downloading"_L1)
        return;
    set(u"checking"_s);
    QNetworkRequest req(QUrl(u"https://api.github.com/repos/%1/releases/latest"_s.arg(r)));
    req.setRawHeader("Accept", "application/vnd.github+json");
    req.setHeader(QNetworkRequest::UserAgentHeader, u"talos/" TALOS_VERSION ""_s);
    req.setTransferTimeout(15'000);
    QNetworkReply *reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply] {
        reply->deleteLater();
        m_lastCheckedMs = QDateTime::currentMSecsSinceEpoch();
        const int status = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
        if (status == 404) {
            set(u"current"_s); // a repository with no releases yet
            return;
        }
        if (reply->error() != QNetworkReply::NoError) {
            set(u"error"_s, u"Couldn't check for updates: %1"_s.arg(reply->errorString()));
            return;
        }
        const QJsonObject rel = QJsonDocument::fromJson(reply->readAll()).object();
        m_latest = rel["tag_name"_L1].toString();
        m_notes = rel["body"_L1].toString().trimmed();
        m_releaseUrl = rel["html_url"_L1].toString();
        if (m_latest.isEmpty() || compareVersions(m_latest, currentVersion()) <= 0) {
            set(u"current"_s);
            return;
        }
        QStringList names;
        const QJsonArray assets = rel["assets"_L1].toArray();
        for (const auto &a : assets)
            names << a.toObject()["name"_L1].toString();
        const int pick = pickAsset(names, platform::name(), QSysInfo::currentCpuArchitecture());
        m_assetName.clear();
        m_assetUrl.clear();
        m_checksumUrl.clear();
        m_assetBytes = 0;
        if (pick >= 0) {
            const QJsonObject asset = assets.at(pick).toObject();
            m_assetName = names.at(pick);
            m_assetUrl = asset["browser_download_url"_L1].toString();
            m_assetBytes = qint64(asset["size"_L1].toDouble());
            for (const auto &a : assets) {
                const QString n = a.toObject()["name"_L1].toString();
                if (n.compare("SHA256SUMS"_L1, Qt::CaseInsensitive) == 0
                    || n.compare(m_assetName + ".sha256"_L1, Qt::CaseInsensitive) == 0) {
                    m_checksumUrl = a.toObject()["browser_download_url"_L1].toString();
                }
            }
        }
        m_file.clear();
        m_verified = false;
        AppLog::info("update", u"version %1 is available"_s.arg(m_latest));
        set(u"available"_s);
    });
}

void Updater::download()
{
    if (m_state != "available"_L1)
        return;
    // Android installs through its own package installer, which needs the
    // file handed over by the browser's download manager.
    if (m_assetUrl.isEmpty() || platform::name() == "android"_L1) {
        openReleasePage();
        return;
    }
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation);
    QDir().mkpath(dir);
    const QString path = QDir(dir).filePath(QFileInfo(m_assetName).fileName());
    auto *out = new QSaveFile(path);
    if (!out->open(QIODevice::WriteOnly)) {
        delete out;
        set(u"error"_s, u"Couldn't write to %1"_s.arg(dir));
        return;
    }
    m_progress = 0;
    emit progressChanged();
    set(u"downloading"_s);

    QNetworkRequest req{QUrl(m_assetUrl)};
    req.setHeader(QNetworkRequest::UserAgentHeader, u"talos/" TALOS_VERSION ""_s);
    req.setTransferTimeout(45'000);
    QNetworkReply *reply = m_nam.get(req);
    m_active = reply;
    out->setParent(reply);
    connect(reply, &QNetworkReply::readyRead, this, [reply, out] { out->write(reply->readAll()); });
    connect(reply, &QNetworkReply::downloadProgress, this, [this](qint64 got, qint64 total) {
        m_progress = total > 0 ? double(got) / double(total) : 0;
        emit progressChanged();
    });
    connect(reply, &QNetworkReply::finished, this, [this, reply, out, path] {
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            out->cancelWriting();
            if (reply->error() == QNetworkReply::OperationCanceledError)
                set(u"available"_s);
            else
                set(u"error"_s, u"Download failed: %1"_s.arg(reply->errorString()));
            return;
        }
        if (!out->commit()) {
            set(u"error"_s, u"Couldn't finish writing the download"_s);
            return;
        }
        verify(path);
    });
}

void Updater::verify(const QString &path)
{
    const auto finish = [this, path](bool verified) {
        m_file = path;
        m_verified = verified;
#if defined(Q_OS_UNIX)
        QFile::setPermissions(path, QFile::permissions(path) | QFile::ExeOwner);
#endif
        set(u"ready"_s);
    };
    if (m_checksumUrl.isEmpty()) {
        finish(false);
        return;
    }
    QNetworkRequest req{QUrl(m_checksumUrl)};
    req.setHeader(QNetworkRequest::UserAgentHeader, u"talos/" TALOS_VERSION ""_s);
    req.setTransferTimeout(15'000);
    QNetworkReply *reply = m_nam.get(req);
    connect(reply, &QNetworkReply::finished, this, [this, reply, path, finish] {
        reply->deleteLater();
        const QString sums = QString::fromUtf8(reply->readAll());
        if (reply->error() != QNetworkReply::NoError || sums.isEmpty()) {
            QFile::remove(path);
            set(u"error"_s, u"Couldn't fetch the release's checksums, so the download "
                            "was discarded."_s);
            return;
        }
        QFile f(path);
        QCryptographicHash hash(QCryptographicHash::Sha256);
        if (!f.open(QIODevice::ReadOnly) || !hash.addData(&f)) {
            set(u"error"_s, u"Couldn't read the download back"_s);
            return;
        }
        const QString digest = QString::fromLatin1(hash.result().toHex());
        // SHA256SUMS lines are "<hex>  <name>"; a .sha256 file may be the
        // bare digest.
        bool match = false;
        const QString name = QFileInfo(path).fileName();
        for (const QString &line : sums.split(u'\n', Qt::SkipEmptyParts)) {
            const QStringList parts = line.simplified().split(u' ');
            if (parts.first().toLower() != digest)
                continue;
            QString listed = parts.last();
            listed.remove(u'*'); // binary-mode marker
            if (parts.size() == 1 || listed == name)
                match = true;
        }
        if (!match) {
            QFile::remove(path);
            set(u"error"_s, u"The download didn't match the release's published checksum "
                            "and was deleted."_s);
            return;
        }
        finish(true);
    });
}

void Updater::cancel()
{
    if (m_active)
        m_active->abort();
}

void Updater::install()
{
    if (m_file.isEmpty())
        return;
#if defined(Q_OS_LINUX)
    // An AppImage or archive replaces the old one by hand: show where it is.
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(m_file).absolutePath()));
#else
    QDesktopServices::openUrl(QUrl::fromLocalFile(m_file));
#endif
}

void Updater::openReleasePage()
{
    if (!m_releaseUrl.isEmpty())
        QDesktopServices::openUrl(QUrl(m_releaseUrl));
    else if (!repo().isEmpty())
        QDesktopServices::openUrl(QUrl(u"https://github.com/%1/releases"_s.arg(repo())));
}

} // namespace talos
