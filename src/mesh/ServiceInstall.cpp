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

namespace talos::service {

using namespace Qt::StringLiterals;

namespace {

const QString kUnit = u"talos-mesh.service"_s;

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

} // namespace

bool supported()
{
#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    return !platform::isSandboxed() && !QStandardPaths::findExecutable(u"systemctl"_s).isEmpty();
#else
    return false;
#endif
}

QString unitPath()
{
    return QDir(QStandardPaths::writableLocation(QStandardPaths::GenericConfigLocation))
        .filePath(u"systemd/user/"_s + kUnit);
}

QString unitText(const QString &exe)
{
    // systemd splits ExecStart on spaces unless the path is quoted, and
    // reads % as a specifier.
    QString quoted = exe;
    quoted.replace(u'\\', u"\\\\"_s).replace(u'"', u"\\\""_s).replace(u'%', u"%%"_s);
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

bool installed()
{
    return QFile::exists(unitPath());
}

QString state()
{
    if (!supported() || !installed())
        return u"not installed"_s;
    const Ran r = run(u"systemctl"_s, {u"--user"_s, u"is-active"_s, kUnit});
    return r.out.isEmpty() ? u"unknown"_s : r.out;
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
}

bool lingering()
{
    // Without lingering a user unit only runs while the user is signed in.
    const QString user = qEnvironmentVariable("USER");
    if (user.isEmpty())
        return false;
    return run(u"loginctl"_s, {u"show-user"_s, user, u"--property=Linger"_s, u"--value"_s}).out
        == "yes"_L1;
}

bool uninstall(QString *error)
{
    if (!supported()) {
        *error = u"A background service isn't available on this system."_s;
        return false;
    }
    run(u"systemctl"_s, {u"--user"_s, u"disable"_s, u"--now"_s, kUnit});
    if (QFile::exists(unitPath()) && !QFile::remove(unitPath())) {
        *error = u"Couldn't remove %1"_s.arg(unitPath());
        return false;
    }
    run(u"systemctl"_s, {u"--user"_s, u"daemon-reload"_s});
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
