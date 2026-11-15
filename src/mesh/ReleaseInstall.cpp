// SPDX-License-Identifier: GPL-2.0-only
#include "mesh/ReleaseInstall.h"

#include "mesh/Platform.h"
#include "mesh/SelfUpdate.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QScopeGuard>
#include <QSettings>
#include <QStandardPaths>
#include <cstdio>

#if QT_CONFIG(process)
#include <QProcess>
#endif

namespace talos::selfupdate {

using namespace Qt::StringLiterals;

namespace {

Applied failed(const QString &why)
{
    Applied a;
    a.error = why;
    return a;
}

#if defined(Q_OS_MACOS) || (defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID))
struct Ran {
    int code = -1;
    QString out;
};

Ran run(const QString &program, const QStringList &args, int timeoutMs = 180'000)
{
    QProcess p;
    p.setProcessChannelMode(QProcess::MergedChannels);
    p.start(program, args);
    if (!p.waitForStarted(5000))
        return {-1, p.errorString()};
    p.closeWriteChannel();
    if (!p.waitForFinished(timeoutMs)) {
        p.kill();
        p.waitForFinished(2000);
        return {-1, u"it took too long"_s};
    }
    return {p.exitStatus() == QProcess::NormalExit ? p.exitCode() : -1,
            QString::fromUtf8(p.readAll()).trimmed()};
}

/// The last few lines a tool printed: the part that says what went wrong.
QString tail(const QString &out)
{
    const QStringList lines = out.split(u'\n', Qt::SkipEmptyParts);
    return lines.mid(qMax(0, lines.size() - 3)).join(u' ').right(300);
}
#endif

[[maybe_unused]] bool suffixed(const QString &name, QLatin1StringView suffix)
{
    return name.endsWith(suffix, Qt::CaseInsensitive);
}

#if defined(Q_OS_MACOS)
QString plistValue(const QString &bundle, const QString &key)
{
    return QSettings(bundle + u"/Contents/Info.plist"_s, QSettings::NativeFormat)
        .value(key).toString();
}

/// What a build must be signed with to count as the same app as
/// @p bundle, or empty for an ad-hoc signature, which names no signer.
QString signerOf(const QString &bundle)
{
    const Ran r = run(u"/usr/bin/codesign"_s, {u"-d"_s, u"-r-"_s, bundle});
    for (const QString &line : r.out.split(u'\n')) {
        if (line.startsWith("designated => "_L1))
            return line.mid(14).trimmed();
    }
    return {};
}
#endif

#if defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
Applied applyPackage(const QString &file)
{
    const QString pkexec = QStandardPaths::findExecutable(u"pkexec"_s);
    QStringList args;
    if (suffixed(file, ".deb"_L1))
        args = {u"apt-get"_s, u"install"_s, u"-y"_s, file};
    else if (!QStandardPaths::findExecutable(u"dnf"_s).isEmpty())
        args = {u"dnf"_s, u"install"_s, u"-y"_s, file};
    else
        args = {u"rpm"_s, u"-U"_s, file};
    const Ran r = run(pkexec, args, 900'000);
    // pkexec's own two answers: the prompt was dismissed, or refused.
    if (r.code == 126 || r.code == 127)
        return failed(u"The update wasn't authorised, so nothing was installed."_s);
    if (r.code != 0)
        return failed(u"The package manager couldn't install it: %1"_s.arg(tail(r.out)));
    Applied a;
    a.ok = true;
    a.program = ownExecutable();
    return a;
}
#endif

} // namespace

QString ownBundle()
{
#if defined(Q_OS_MACOS)
    QDir dir(QCoreApplication::applicationDirPath());
    if (dir.dirName() != "MacOS"_L1 || !dir.cdUp() || dir.dirName() != "Contents"_L1 || !dir.cdUp())
        return {};
    const QString path = dir.canonicalPath();
    return path.endsWith(".app"_L1) ? path : QString();
#else
    return {};
#endif
}

QString releaseMode(const QString &asset)
{
    if (asset.isEmpty() || platform::isSandboxed())
        return {};
#if defined(Q_OS_MACOS)
    if (!suffixed(asset, ".dmg"_L1))
        return {};
    const QString bundle = ownBundle();
    // Started from the disk image, or from where macOS runs a quarantined
    // app out of: neither is the installed copy.
    if (bundle.isEmpty() || bundle.contains("/AppTranslocation/"_L1)
        || bundle.startsWith("/Volumes/"_L1))
        return {};
    const QFileInfo info(bundle);
    return info.isWritable() && QFileInfo(info.absolutePath()).isWritable() ? u"swap"_s : QString();
#elif defined(Q_OS_WIN)
    // Only a copy the installer put there is the installer's to update.
    if (!suffixed(asset, ".exe"_L1) || !asset.contains("setup"_L1, Qt::CaseInsensitive))
        return {};
    return QFile::exists(QCoreApplication::applicationDirPath() + u"/unins000.exe"_s)
        ? u"setup"_s : QString();
#elif defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    if (suffixed(asset, ".appimage"_L1))
        return qEnvironmentVariableIsSet("APPIMAGE") && canReplaceSelf() ? u"swap"_s : QString();
    if ((suffixed(asset, ".rpm"_L1) || suffixed(asset, ".deb"_L1)) && !canReplaceSelf()
        && !QStandardPaths::findExecutable(u"pkexec"_s).isEmpty())
        return u"package"_s;
    return {};
#else
    return {};
#endif
}

bool replaceFile(const QString &exe, const QString &file, QString *error)
{
    // Staged beside the target so the last step is a rename on one
    // filesystem: the program is the old one or the new one, never part
    // of each.
    const QString staged = QFileInfo(exe).absolutePath() + u"/.talos-update"_s;
    QFile::remove(staged);
    if (!QFile::copy(file, staged)) {
        *error = u"Couldn't write next to the installed program."_s;
        return false;
    }
    QFile::setPermissions(staged, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner
                                      | QFile::ReadGroup | QFile::ExeGroup | QFile::ReadOther
                                      | QFile::ExeOther);
    if (QFileInfo(staged).size() != QFileInfo(file).size()
        || ::rename(QFile::encodeName(staged).constData(), QFile::encodeName(exe).constData()) != 0) {
        QFile::remove(staged);
        *error = u"Couldn't replace the installed program."_s;
        return false;
    }
    return true;
}

Applied replaceBundle(const QString &bundle, const QString &dmg, const QString &version)
{
#if defined(Q_OS_MACOS)
    const QFileInfo target(bundle);
    const QString mount = QFileInfo(dmg).absolutePath() + u"/mount"_s;
    const QString hdiutil = u"/usr/bin/hdiutil"_s;
    run(hdiutil, {u"detach"_s, mount, u"-force"_s});
    QDir().mkpath(mount);
    Ran r = run(hdiutil, {u"attach"_s, u"-nobrowse"_s, u"-readonly"_s, u"-noautoopen"_s,
                          u"-mountpoint"_s, mount, dmg});
    if (r.code != 0)
        return failed(u"Couldn't open the disk image: %1"_s.arg(tail(r.out)));
    const auto detach = qScopeGuard([&] {
        run(hdiutil, {u"detach"_s, mount, u"-force"_s});
        QDir().rmdir(mount);
    });

    const QStringList apps = QDir(mount).entryList({u"*.app"_s}, QDir::Dirs | QDir::NoSymLinks);
    if (apps.isEmpty())
        return failed(u"The disk image holds no app."_s);
    const QString fresh = QDir(mount).filePath(apps.first());
    const QString id = u"CFBundleIdentifier"_s;
    if (plistValue(fresh, id).isEmpty() || plistValue(fresh, id) != plistValue(bundle, id))
        return failed(u"The app in the disk image isn't this one."_s);
    const QString inside = versionOf(fresh + u"/Contents/MacOS/"_s
                                     + plistValue(fresh, u"CFBundleExecutable"_s));
    if (inside.isEmpty() || compareVersions(inside, version) != 0)
        return failed(u"The disk image doesn't hold version %1."_s.arg(version));
    // A signed copy is only replaced by one from the same signer.
    QStringList verify{u"--verify"_s, u"--deep"_s, u"--strict"_s};
    const QString signer = signerOf(bundle);
    if (!signer.isEmpty())
        verify << u"-R="_s + signer;
    r = run(u"/usr/bin/codesign"_s, verify << fresh);
    if (r.code != 0)
        return failed(u"The new app's signature doesn't match this one's, so it was not "
                      "installed: %1"_s.arg(tail(r.out)));

    const QString staged = target.absolutePath() + u"/."_s + target.fileName() + u".new"_s;
    const QString old = target.absolutePath() + u"/."_s + target.fileName() + u".old"_s;
    QDir(staged).removeRecursively();
    QDir(old).removeRecursively();
    r = run(u"/usr/bin/ditto"_s, {fresh, staged});
    if (r.code != 0) {
        QDir(staged).removeRecursively();
        return failed(u"Couldn't copy the new app into place: %1"_s.arg(tail(r.out)));
    }
    if (!QDir().rename(bundle, old)) {
        QDir(staged).removeRecursively();
        return failed(u"Couldn't move the installed app aside."_s);
    }
    if (!QDir().rename(staged, bundle)) {
        QDir().rename(old, bundle);
        QDir(staged).removeRecursively();
        return failed(u"Couldn't put the new app in place."_s);
    }
    Applied a;
    a.ok = true;
    a.program = u"/usr/bin/open"_s;
    a.arguments = {u"-n"_s};
    // What `open` starts gets the session's environment, not this one's.
    if (qEnvironmentVariableIsSet("TALOS_HOME"))
        a.arguments << u"--env"_s << u"TALOS_HOME="_s + qEnvironmentVariable("TALOS_HOME");
    a.arguments << bundle;
    a.discard = old;
    return a;
#else
    Q_UNUSED(bundle)
    Q_UNUSED(dmg)
    Q_UNUSED(version)
    return failed(u"Not on this system."_s);
#endif
}

Applied applyRelease(const QString &file, const QString &version)
{
    const QString mode = releaseMode(file);
#if defined(Q_OS_MACOS)
    if (mode == "swap"_L1)
        return replaceBundle(ownBundle(), file, version);
#elif defined(Q_OS_WIN)
    Q_UNUSED(version)
    if (mode == "setup"_L1) {
        // The installer closes this app if it is still up, installs over
        // it where it already is, and starts it again when asked to.
        Applied a;
        a.ok = true;
        a.program = file;
        a.arguments = {u"/SILENT"_s, u"/SUPPRESSMSGBOXES"_s, u"/NORESTART"_s,
                       u"/CLOSEAPPLICATIONS"_s, u"/relaunch=1"_s};
        a.afterExit = false;
        return a;
    }
#elif defined(Q_OS_LINUX) && !defined(Q_OS_ANDROID)
    Q_UNUSED(version)
    if (mode == "swap"_L1) {
        Applied a;
        a.program = ownExecutable();
        a.ok = replaceFile(a.program, file, &a.error);
        return a;
    }
    if (mode == "package"_L1)
        return applyPackage(file);
#else
    Q_UNUSED(version)
#endif
    return failed(u"This install can't replace itself."_s);
}

bool startAfterExit(const QString &program, const QStringList &arguments, const QString &discard)
{
#if !QT_CONFIG(process)
    Q_UNUSED(program)
    Q_UNUSED(arguments)
    Q_UNUSED(discard)
    return false;
#elif defined(Q_OS_WIN)
    Q_UNUSED(discard)
    return QProcess::startDetached(program, arguments);
#else
    static const QString wait = u"while kill -0 \"$1\" 2>/dev/null; do sleep 0.2; done; "
                                "[ -n \"$2\" ] && rm -rf \"$2\"; shift 2; exec \"$@\""_s;
    QStringList args{u"-c"_s, wait, u"sh"_s,
                     QString::number(QCoreApplication::applicationPid()), discard, program};
    args += arguments;
    return QProcess::startDetached(u"/bin/sh"_s, args);
#endif
}

} // namespace talos::selfupdate
