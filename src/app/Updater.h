// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <QNetworkAccessManager>
#include <QObject>
#include <QPointer>
#include <QTimer>

class QNetworkReply;

namespace talos {

class Store;

/// Watches a GitHub repository's releases for a newer build of this client.
///
/// Nothing downloads until the user asks. A download is checked against the
/// release's published SHA-256 (a `SHA256SUMS` asset, or `<asset>.sha256`)
/// before it is offered for installing; a release that publishes no
/// checksum is reported as unverified rather than passed off as checked.
/// Installing is handed to the system: this class never overwrites the
/// running program.
class Updater : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString state READ state NOTIFY changed)
    Q_PROPERTY(QString currentVersion READ currentVersion CONSTANT)
    Q_PROPERTY(QString latestVersion READ latestVersion NOTIFY changed)
    Q_PROPERTY(QString notes READ notes NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)
    Q_PROPERTY(QString assetName READ assetName NOTIFY changed)
    Q_PROPERTY(QString assetSize READ assetSize NOTIFY changed)
    Q_PROPERTY(QString file READ file NOTIFY changed)
    Q_PROPERTY(bool verified READ verified NOTIFY changed)
    Q_PROPERTY(double progress READ progress NOTIFY progressChanged)
    Q_PROPERTY(bool available READ available NOTIFY changed)
    Q_PROPERTY(QString lastChecked READ lastChecked NOTIFY changed)
public:
    explicit Updater(Store *store, QObject *parent = nullptr);

    /// unconfigured | idle | checking | current | available | downloading |
    /// ready | error
    QString state() const { return m_state; }
    QString currentVersion() const { return QStringLiteral(TALOS_VERSION); }
    QString latestVersion() const { return m_latest; }
    QString notes() const { return m_notes; }
    QString error() const { return m_error; }
    QString assetName() const { return m_assetName; }
    QString assetSize() const;
    QString file() const { return m_file; }
    bool verified() const { return m_verified; }
    double progress() const { return m_progress; }
    bool available() const { return m_state == QLatin1StringView("available")
                                    || m_state == QLatin1StringView("ready"); }
    QString lastChecked() const;

    Q_INVOKABLE void check();
    Q_INVOKABLE void download();
    Q_INVOKABLE void cancel();
    /// Hand the downloaded file to the system (or show it in its folder).
    Q_INVOKABLE void install();
    Q_INVOKABLE void openReleasePage();

    /// -1, 0 or 1 comparing dotted versions; a leading "v" is ignored and a
    /// pre-release suffix sorts before its release.
    static int compareVersions(const QString &a, const QString &b);
    /// The release asset this platform installs, or -1.
    static int pickAsset(const QStringList &names, const QString &platform, const QString &arch);

signals:
    void changed();
    void progressChanged();

private:
    void set(const QString &state, const QString &error = {});
    void verify(const QString &path);
    QString repo() const;

    Store *m_store;
    QNetworkAccessManager m_nam;
    QPointer<QNetworkReply> m_active;
    QTimer m_timer;

    QString m_state;
    QString m_latest;
    QString m_notes;
    QString m_error;
    QString m_releaseUrl;
    QString m_assetName;
    QString m_assetUrl;
    qint64 m_assetBytes = 0;
    QString m_checksumUrl;
    QString m_file;
    bool m_verified = false;
    double m_progress = 0;
    qint64 m_lastCheckedMs = 0;
};

} // namespace talos
