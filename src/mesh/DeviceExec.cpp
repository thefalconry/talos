// SPDX-License-Identifier: GPL-2.0-only
#include "mesh/DeviceExec.h"

#include "mesh/Platform.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QProcess>
#include <QTimer>
#include <memory>

namespace talos {

using namespace Qt::StringLiterals;

namespace {

/// Collects a stream's output without letting a chatty command balloon
/// memory or the result payload: the first and last parts are kept, the
/// middle is counted and dropped.
class CappedOutput
{
public:
    void add(const QByteArray &chunk)
    {
        m_total += chunk.size();
        qsizetype taken = 0;
        if (m_head.size() < DeviceExec::kOutputHeadBytes) {
            taken = qMin(chunk.size(), DeviceExec::kOutputHeadBytes - m_head.size());
            m_head += chunk.left(taken);
        }
        if (taken < chunk.size()) {
            m_tail += chunk.mid(taken);
            if (m_tail.size() > DeviceExec::kOutputTailBytes)
                m_tail.remove(0, m_tail.size() - DeviceExec::kOutputTailBytes);
        }
    }

    QString value() const
    {
        const qint64 dropped = m_total - m_head.size() - m_tail.size();
        if (dropped <= 0)
            return QString::fromUtf8(m_head + m_tail);
        return QString::fromUtf8(m_head) + u"\n… [%1 bytes omitted] …\n"_s.arg(dropped)
            + QString::fromUtf8(m_tail);
    }

private:
    QByteArray m_head;
    QByteArray m_tail;
    qint64 m_total = 0;
};

QString modeString(QFile::Permissions p)
{
    QString s;
    s += p & QFile::ReadOwner ? u'r' : u'-';
    s += p & QFile::WriteOwner ? u'w' : u'-';
    s += p & QFile::ExeOwner ? u'x' : u'-';
    s += p & QFile::ReadGroup ? u'r' : u'-';
    s += p & QFile::WriteGroup ? u'w' : u'-';
    s += p & QFile::ExeGroup ? u'x' : u'-';
    s += p & QFile::ReadOther ? u'r' : u'-';
    s += p & QFile::WriteOther ? u'w' : u'-';
    s += p & QFile::ExeOther ? u'x' : u'-';
    return s;
}

qint64 num(const QJsonValue &v, qint64 fallback)
{
    return v.isDouble() ? qint64(v.toDouble()) : fallback;
}

} // namespace

DeviceExec::DeviceExec(QObject *parent) : QObject(parent) {}

QStringList DeviceExec::capabilities()
{
    return {u"exec"_s, u"read_file"_s, u"write_file"_s, u"list_dir"_s,
            u"stat"_s, u"delete"_s, u"mkdir"_s, u"move"_s};
}

QStringList DeviceExec::shellInvocation(const QString &cmd, const QString &os)
{
    const QString p = os.isEmpty() ? platform::name() : os;
    if (p == "windows"_L1)
        return {u"cmd"_s, u"/c"_s, cmd};
    // A login shell on macOS, so the PATH a GUI app inherits from launchd
    // (which has no Homebrew, no user bins) is replaced by the user's own.
    if (p == "macos"_L1)
        return {u"/bin/zsh"_s, u"-l"_s, u"-c"_s, cmd};
    return {u"sh"_s, u"-c"_s, cmd};
}

bool DeviceExec::handle(const QString &name, const QJsonObject &params, Done done)
{
    const auto str = [&params](QLatin1StringView key) { return params[key].toString(); };
    if (name == "exec"_L1) {
        exec(str("cmd"_L1), str("cwd"_L1), int(num(params["timeoutMs"_L1], 60'000)),
             std::move(done));
    } else if (name == "read_file"_L1) {
        done(readFile(str("path"_L1), num(params["offset"_L1], 0),
                      num(params["len"_L1], kMaxChunkBytes)));
    } else if (name == "write_file"_L1) {
        done(writeFile(str("path"_L1), QByteArray::fromBase64(str("base64"_L1).toLatin1()),
                       num(params["offset"_L1], 0), params["truncate"_L1].toBool()));
    } else if (name == "list_dir"_L1) {
        done(listDir(str("path"_L1)));
    } else if (name == "stat"_L1) {
        done(statPath(str("path"_L1)));
    } else if (name == "delete"_L1) {
        done(deletePath(str("path"_L1)));
    } else if (name == "mkdir"_L1) {
        done(makeDir(str("path"_L1)));
    } else if (name == "move"_L1) {
        done(movePath(str("from"_L1), str("to"_L1)));
    } else {
        return false;
    }
    return true;
}

void DeviceExec::exec(const QString &cmd, const QString &cwd, int timeoutMs, Done done)
{
    if (cmd.trimmed().isEmpty()) {
        done(CommandOutcome::fail(u"No command."_s));
        return;
    }
    const int budget = qBound(1000, timeoutMs, 300'000);
    const QStringList shell = shellInvocation(cmd);

    struct Run {
        CappedOutput out;
        CappedOutput err;
        bool timedOut = false;
        bool answered = false;
    };
    auto run = std::make_shared<Run>();
    auto *proc = new QProcess(this);
    if (!cwd.isEmpty())
        proc->setWorkingDirectory(cwd);
    proc->setProgram(shell.first());
#if defined(Q_OS_WIN)
    // cmd.exe has its own quoting rules; hand it the line untouched.
    proc->setNativeArguments(u"/c "_s + cmd);
#else
    proc->setArguments(shell.mid(1));
#endif
    proc->setStandardInputFile(QProcess::nullDevice());

    connect(proc, &QProcess::readyReadStandardOutput, proc,
            [proc, run] { run->out.add(proc->readAllStandardOutput()); });
    connect(proc, &QProcess::readyReadStandardError, proc,
            [proc, run] { run->err.add(proc->readAllStandardError()); });

    auto *timer = new QTimer(proc);
    timer->setSingleShot(true);
    connect(timer, &QTimer::timeout, proc, [proc, run] {
        run->timedOut = true;
        proc->kill();
    });

    const auto answer = [proc, run, done](int code) {
        if (run->answered)
            return;
        run->answered = true;
        run->out.add(proc->readAllStandardOutput());
        run->err.add(proc->readAllStandardError());
        QString err = run->err.value();
        if (run->timedOut)
            err += (err.isEmpty() ? u""_s : u"\n"_s) + u"[killed: timeout]"_s;
        CommandOutcome o;
        // "ok" means the command itself succeeded; a non-zero exit still
        // carries its output back for the model to read.
        o.ok = !run->timedOut && code == 0;
        if (run->timedOut)
            o.message = u"Command timed out."_s;
        o.data = {{"stdout"_L1, run->out.value()}, {"stderr"_L1, err}, {"exitCode"_L1, code}};
        done(o);
        proc->deleteLater();
    };
    connect(proc, &QProcess::finished, proc,
            [answer](int code, QProcess::ExitStatus status) {
        answer(status == QProcess::NormalExit ? code : -1);
    });
    connect(proc, &QProcess::errorOccurred, proc,
            [proc, run, done](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart || run->answered)
            return;
        run->answered = true;
        done(CommandOutcome::fail(u"Failed to run command: %1"_s.arg(proc->errorString())));
        proc->deleteLater();
    });

    timer->start(budget);
    proc->start();
}

CommandOutcome DeviceExec::readFile(const QString &path, qint64 offset, qint64 len) const
{
    if (path.isEmpty())
        return CommandOutcome::fail(u"No path."_s);
    QFile f(path);
    if (!f.exists())
        return CommandOutcome::fail(u"No such file: %1"_s.arg(path));
    if (!f.open(QIODevice::ReadOnly))
        return CommandOutcome::fail(u"read_file failed: %1"_s.arg(f.errorString()));
    const qint64 size = f.size();
    const qint64 at = qBound<qint64>(0, offset, size);
    f.seek(at);
    const QByteArray bytes = f.read(qBound<qint64>(0, len, qint64(kMaxChunkBytes)));
    return CommandOutcome::done({
        {"base64"_L1, QString::fromLatin1(bytes.toBase64())},
        {"size"_L1, size},
        {"eof"_L1, at + bytes.size() >= size},
    });
}

CommandOutcome DeviceExec::writeFile(const QString &path, const QByteArray &bytes,
                                     qint64 offset, bool truncate) const
{
    if (path.isEmpty())
        return CommandOutcome::fail(u"No path."_s);
    const qint64 cap = writeLimit();
    if ((truncate ? 0 : offset) + bytes.size() > cap) {
        return CommandOutcome::fail(
            u"write_file refused: the file would exceed the %1-byte limit for mesh "
            "writes (Settings → Mesh)."_s.arg(cap));
    }
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(truncate ? QIODevice::WriteOnly | QIODevice::Truncate
                         : QIODevice::ReadWrite)) {
        return CommandOutcome::fail(u"write_file failed: %1"_s.arg(f.errorString()));
    }
    if (!truncate) {
        // Chunks must arrive in order: a duplicate or a gap would silently
        // corrupt the file.
        if (offset != f.size()) {
            return CommandOutcome::fail(
                u"write_file chunk offset %1 does not match current size %2 for %3 "
                "(out-of-order or duplicate chunk)."_s.arg(offset).arg(f.size()).arg(path));
        }
        f.seek(offset);
    }
    if (f.write(bytes) != bytes.size() || !f.flush())
        return CommandOutcome::fail(u"write_file failed: %1"_s.arg(f.errorString()));
    return CommandOutcome::done({{"bytesWritten"_L1, bytes.size()}});
}

CommandOutcome DeviceExec::listDir(const QString &path) const
{
    if (path.isEmpty())
        return CommandOutcome::fail(u"No path."_s);
    const QDir dir(path);
    if (!dir.exists())
        return CommandOutcome::fail(u"No such directory: %1"_s.arg(path));
    QJsonArray entries;
    const QFileInfoList infos = dir.entryInfoList(
        QDir::AllEntries | QDir::Hidden | QDir::System | QDir::NoDotAndDotDot, QDir::Name);
    for (const QFileInfo &info : infos) {
        entries << QJsonObject{
            {"name"_L1, info.fileName()},
            {"type"_L1, info.isDir() && !info.isSymLink() ? u"dir"_s : u"file"_s},
            {"size"_L1, info.size()},
            {"mtime"_L1, info.lastModified().toMSecsSinceEpoch()},
        };
    }
    return CommandOutcome::done({{"entries"_L1, entries}});
}

CommandOutcome DeviceExec::statPath(const QString &path) const
{
    if (path.isEmpty())
        return CommandOutcome::fail(u"No path."_s);
    const QFileInfo info(path);
    if (!info.exists() && !info.isSymLink())
        return CommandOutcome::fail(u"No such path: %1"_s.arg(path));
    return CommandOutcome::done({
        {"type"_L1, info.isSymLink() ? u"link"_s : info.isDir() ? u"directory"_s : u"file"_s},
        {"size"_L1, info.size()},
        {"mtime"_L1, info.lastModified().toMSecsSinceEpoch()},
        {"mode"_L1, modeString(info.permissions())},
    });
}

CommandOutcome DeviceExec::deletePath(const QString &path) const
{
    if (path.isEmpty())
        return CommandOutcome::fail(u"No path."_s);
    const QFileInfo info(path);
    if (!info.exists() && !info.isSymLink())
        return CommandOutcome::fail(u"No such path: %1"_s.arg(path));
    // A link is removed as a link, never followed into what it points at.
    const bool ok = info.isDir() && !info.isSymLink() ? QDir(path).removeRecursively()
                                                      : QFile::remove(path);
    return ok ? CommandOutcome::done() : CommandOutcome::fail(u"delete failed: %1"_s.arg(path));
}

CommandOutcome DeviceExec::makeDir(const QString &path) const
{
    if (path.isEmpty())
        return CommandOutcome::fail(u"No path."_s);
    return QDir().mkpath(path) ? CommandOutcome::done()
                               : CommandOutcome::fail(u"mkdir failed: %1"_s.arg(path));
}

CommandOutcome DeviceExec::movePath(const QString &from, const QString &to) const
{
    if (from.isEmpty() || to.isEmpty())
        return CommandOutcome::fail(u"move needs from and to."_s);
    if (!QFileInfo::exists(from))
        return CommandOutcome::fail(u"No such path: %1"_s.arg(from));
    QDir().mkpath(QFileInfo(to).absolutePath());
    if (QFileInfo(to).isFile())
        QFile::remove(to);
    if (QFile::rename(from, to))
        return CommandOutcome::done();
    return CommandOutcome::fail(u"move failed: %1 → %2"_s.arg(from, to));
}

} // namespace talos
