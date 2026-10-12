// SPDX-License-Identifier: GPL-2.0-only
#include "mesh/ServiceInstall.h"

#include "core/Store.h"
#include "mesh/Platform.h"
#include "mesh/SelfUpdate.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>

#if defined(Q_OS_MACOS)
#  include <unistd.h>
#endif

namespace talos::service {

using namespace Qt::StringLiterals;

namespace {

const QString kUnit = u"talos-mesh.service"_s;
const QString kAgent = u"dev.talos.client.mesh"_s;

struct Ran {
    bool ok = false;
    QString out;
};

Ran run(const QString &program, const QStringList &args)
{
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(program, args);
    if (!p.waitForStarted(3000) || !p.waitForFinished(15'000)) {
        p.kill();
        return {false, u"%1 did not answer"_s.arg(program)};
    }
    return {p.exitStatus() == QProcess::NormalExit && p.exitCode() == 0,
            QString::fromUtf8(p.readAll()).trimmed()};
}

#if defined(Q_OS_MACOS)
/// launchd's name for this user's own services, and for ours among them.
QString agentDomain()
{
    return u"gui/%1"_s.arg(getuid());
}

QString agentTarget()
{
    return agentDomain() + u'/' + kAgent;
}
#endif

} // namespace

bool supported()
{
#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    return !platform::isSandboxed() && !QStandardPaths::findExecutable(u"systemctl"_s).isEmpty();
#elif defined(Q_OS_MACOS)
    return QFile::exists(u"/bin/launchctl"_s);
#else
    return false;
#endif
}

QString unitPath()
{
#if defined(Q_OS_MACOS)
    return QDir::home().filePath(u"Library/LaunchAgents/%1.plist"_s.arg(kAgent));
#else
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation))
        .filePath(u"systemd/user/"_s + kUnit);
#endif
}

QString unitText(const QString &exe)
{
    // systemd splits ExecStart on spaces unless the path is quoted, reads %
    // as a specifier and $ as the start of a variable.
    QString quoted = exe;
    quoted.replace(u'\\', u"\\\\"_s).replace(u'"', u"\\\""_s).replace(u'%', u"%%"_s)
        .replace(u'$', u"$$"_s);
    return u"[Unit]\n"
           "Description=Talos mesh device (background)\n"
           "After=network-online.target\n"
           "Wants=network-online.target\n"
           "\n"
           "[Service]\n"
           "ExecStart=\"%1\" --headless\n"
           "Restart=always\n"
           "RestartSec=5\n"
           "\n"
           "[Install]\n"
           "WantedBy=default.target\n"_s.arg(quoted);
}

QString agentText(const QString &exe, const QString &home, const QString &log)
{
    // A scratch profile's service serves that profile, not the real one.
    QString environment;
    if (!home.isEmpty()) {
        environment = u"  <key>EnvironmentVariables</key>\n"
                      "  <dict><key>TALOS_HOME</key><string>%1</string></dict>\n"_s
                          .arg(home.toHtmlEscaped());
    }
    QString output;
    if (!log.isEmpty()) {
        output = u"  <key>StandardErrorPath</key><string>%1</string>\n"_s
                     .arg(log.toHtmlEscaped());
    }
    return u"<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
           "<!DOCTYPE plist PUBLIC \"-//Apple//DTD PLIST 1.0//EN\" "
           "\"http://www.apple.com/DTDs/PropertyList-1.0.dtd\">\n"
           "<plist version=\"1.0\">\n"
           "<dict>\n"
           "  <key>Label</key><string>%1</string>\n"
           "  <key>ProgramArguments</key>\n"
           "  <array><string>%2</string><string>--headless</string></array>\n"
           "  <key>RunAtLoad</key><true/>\n"
           "  <key>KeepAlive</key><true/>\n"
           "  <key>ThrottleInterval</key><integer>5</integer>\n"
           "  <key>ProcessType</key><string>Background</string>\n"
           "%3%4"
           "</dict>\n"
           "</plist>\n"_s.arg(kAgent, exe.toHtmlEscaped(), environment, output);
}

bool installed()
{
    return QFile::exists(unitPath());
}

QString state()
{
    if (!supported() || !installed())
        return u"not installed"_s;
#if defined(Q_OS_MACOS)
    const Ran r = run(u"/bin/launchctl"_s, {u"print"_s, agentTarget()});
    if (!r.ok)
        return u"inactive"_s;
    // "state = running" among the lines launchd prints about a service.
    for (const QString &line : r.out.split(u'\n')) {
        const QString text = line.trimmed();
        if (text.startsWith("state = "_L1))
            return text.mid(8) == "running"_L1 ? u"active"_s : text.mid(8);
    }
    return u"unknown"_s;
#else
    const Ran r = run(u"systemctl"_s, {u"--user"_s, u"is-active"_s, kUnit});
    return r.out.isEmpty() ? u"unknown"_s : r.out;
#endif
}

bool install(QString *error)
{
    if (!supported()) {
        *error = u"A background service isn't available on this system."_s;
        return false;
    }
    const QString path = unitPath();
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        *error = u"Couldn't write %1: %2"_s.arg(path, f.errorString());
        return false;
    }
#if defined(Q_OS_MACOS)
    // What the service says goes where Console looks for an app's logs,
    // started afresh with each install so it cannot grow without end.
    const QString logDir = QDir::home().filePath(u"Library/Logs/Talos"_s);
    QDir().mkpath(logDir);
    const QString log = QDir(logDir).filePath(u"mesh.log"_s);
    QFile::remove(log);
    f.write(agentText(selfupdate::ownExecutable(), qEnvironmentVariable("TALOS_HOME"), log)
                .toUtf8());
    f.close();
    // Loading over a loaded service fails, so the old one goes first.
    run(u"/bin/launchctl"_s, {u"bootout"_s, agentTarget()});
    run(u"/bin/launchctl"_s, {u"enable"_s, agentTarget()});
    const Ran r = run(u"/bin/launchctl"_s, {u"bootstrap"_s, agentDomain(), path});
    if (!r.ok) {
        QFile::remove(path);
        *error = u"launchctl couldn't load the service: %1"_s.arg(r.out);
        return false;
    }
    return true;
#else
    f.write(unitText(selfupdate::ownExecutable()).toUtf8());
    f.close();
    for (const QStringList &args : {QStringList{u"--user"_s, u"daemon-reload"_s},
                                    QStringList{u"--user"_s, u"enable"_s, u"--now"_s, kUnit}}) {
        const Ran r = run(u"systemctl"_s, args);
        if (!r.ok) {
            *error = u"systemctl %1 failed: %2"_s.arg(args.join(u' '), r.out);
            return false;
        }
    }
    return true;
#endif
}

bool lingering()
{
#if defined(Q_OS_MACOS)
    // A LaunchAgent belongs to the signed-in session: it starts at sign-in
    // and ends at sign-out, and there is no switch that changes that.
    return false;
#else
    // Without lingering a user unit only runs while the user is signed in.
    const QString user = qEnvironmentVariable("USER");
    if (user.isEmpty())
        return false;
    return run(u"loginctl"_s, {u"show-user"_s, user, u"--property=Linger"_s, u"--value"_s}).out
        == "yes"_L1;
#endif
}

bool uninstall(QString *error)
{
    if (!supported()) {
        *error = u"A background service isn't available on this system."_s;
        return false;
    }
#if defined(Q_OS_MACOS)
    run(u"/bin/launchctl"_s, {u"bootout"_s, agentTarget()});
#else
    run(u"systemctl"_s, {u"--user"_s, u"disable"_s, u"--now"_s, kUnit});
#endif
    if (QFile::exists(unitPath()) && !QFile::remove(unitPath())) {
        *error = u"Couldn't remove %1"_s.arg(unitPath());
        return false;
    }
#if !defined(Q_OS_MACOS)
    run(u"systemctl"_s, {u"--user"_s, u"daemon-reload"_s});
#endif
    return true;
}

bool enable(Store *store, QString *error)
{
    // The service has no desktop session and so no unlocked keyring:
    // credentials move to the private file both processes can read.
    store->setPref(u"meshService"_s, true);
    store->setSecretsInFile(true);
    store->flush();
    if (install(error))
        return true;
    store->setPref(u"meshService"_s, false);
    store->setSecretsInFile(false);
    store->flush();
    return false;
}

bool disable(Store *store, QString *error)
{
    const bool ok = uninstall(error);
    store->setPref(u"meshService"_s, false);
    store->setSecretsInFile(false);
    store->flush();
    return ok;
}

} // namespace talos::service
