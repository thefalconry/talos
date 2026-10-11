// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <QJsonObject>
#include <QObject>
#include <QStringList>
#include <functional>

namespace talos {

/// What a device command produced, as the daemon's tools read it back.
struct CommandOutcome {
    bool ok = false;
    QString message;
    QJsonObject data;

    static CommandOutcome fail(const QString &message) { return {false, message, {}}; }
    static CommandOutcome done(const QJsonObject &data = {}) { return {true, {}, data}; }
};

/// Runs the shell and filesystem commands the daemon sends when the user has
/// granted it control of this device: `exec`, `read_file`, `write_file`,
/// `list_dir`, `stat`, `delete`, `mkdir` and `move`.
///
/// Commands run as the user running the app, in the user's own shell
/// environment. Nothing here asks for more privilege than that.
class DeviceExec : public QObject
{
    Q_OBJECT
public:
    using Done = std::function<void(const CommandOutcome &)>;

    static constexpr int kMaxChunkBytes = 256 * 1024;
    static constexpr qint64 kDefaultMaxWriteBytes = 4LL * 1024 * 1024 * 1024;
    /// Output beyond this is elided from the middle: the head says what the
    /// command started doing, the tail how it ended.
    static constexpr int kOutputHeadBytes = 192 * 1024;
    static constexpr int kOutputTailBytes = 64 * 1024;

    explicit DeviceExec(QObject *parent = nullptr);

    static QStringList capabilities();
    /// The shell a command line is handed to on @p os (defaults to this one).
    static QStringList shellInvocation(const QString &cmd, const QString &os = {});

    /// Run @p name. Returns false, without calling @p done, for a command
    /// this executor does not implement.
    bool handle(const QString &name, const QJsonObject &params, Done done);

    /// The largest file a mesh write may produce, in bytes.
    std::function<qint64()> writeLimit = [] { return kDefaultMaxWriteBytes; };

    CommandOutcome readFile(const QString &path, qint64 offset, qint64 len) const;
    CommandOutcome writeFile(const QString &path, const QByteArray &bytes, qint64 offset,
                             bool truncate) const;
    CommandOutcome listDir(const QString &path) const;
    CommandOutcome statPath(const QString &path) const;
    CommandOutcome deletePath(const QString &path) const;
    CommandOutcome makeDir(const QString &path) const;
    CommandOutcome movePath(const QString &from, const QString &to) const;
    void exec(const QString &cmd, const QString &cwd, int timeoutMs, Done done);
};

} // namespace talos
