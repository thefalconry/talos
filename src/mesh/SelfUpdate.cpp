// SPDX-License-Identifier: GPL-2.0-only
#include "mesh/SelfUpdate.h"

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QList>
#include <QRegularExpression>
#include <cstdio>

namespace talos::selfupdate {

using namespace Qt::StringLiterals;

namespace {

// Built from two halves so the only complete marker in the binary is the
// one below with a version behind it.
// The marker itself. `used` keeps the linker from discarding it.
#if defined(__GNUC__)
__attribute__((used))
#elif defined(_MSC_VER)
#pragma comment(linker, "/include:kBuildMarker")
#endif
extern const char kBuildMarker[] = "TALOS-BUILD-" "VERSION:" TALOS_VERSION ";";

const QByteArray &markerPrefix()
{
    static const QByteArray prefix = QByteArray("TALOS-BUILD-") + QByteArray("VERSION:");
    (void)kBuildMarker;
    return prefix;
}

} // namespace

int compareVersions(const QString &a, const QString &b)
{
    const auto parse = [](QString v) {
        static const QRegularExpression lead(u"^[^0-9]*"_s);
        v.remove(lead);
        const QString core = v.section(u'-', 0, 0).section(u'+', 0, 0);
        QList<int> parts;
        for (const QString &p : core.split(u'.'))
            parts << p.toInt();
        while (parts.size() < 3)
            parts << 0;
        return std::pair(parts, v.contains(u'-'));
    };
    const auto [pa, preA] = parse(a);
    const auto [pb, preB] = parse(b);
    for (int i = 0; i < qMax(pa.size(), pb.size()); ++i) {
        const int x = pa.value(i), y = pb.value(i);
        if (x != y)
            return x < y ? -1 : 1;
    }
    if (preA != preB)
        return preA ? -1 : 1;
    return 0;
}

QString versionOf(const QString &file)
{
    QFile f(file);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QByteArray &prefix = markerPrefix();
    QByteArray tail;
    static const QRegularExpression shape(u"^[0-9]+(\\.[0-9]+){1,3}([-+][0-9A-Za-z.]+)?$"_s);
    while (!f.atEnd()) {
        // Carry the end of the previous block so a marker that straddles
        // two reads is still found.
        const QByteArray block = tail + f.read(1 << 20);
        qsizetype at = 0;
        while ((at = block.indexOf(prefix, at)) >= 0) {
            const qsizetype start = at + prefix.size();
            const qsizetype end = block.indexOf(';', start);
            if (end > start && end - start <= 40) {
                const QString version = QString::fromLatin1(block.mid(start, end - start));
                if (shape.match(version).hasMatch())
                    return version;
            }
            at = start;
        }
        tail = block.right(prefix.size() + 48);
    }
    return {};
}

QString ownExecutable()
{
    const QString image = qEnvironmentVariable("APPIMAGE");
    if (!image.isEmpty())
        return image;
    const QFileInfo info(QCoreApplication::applicationFilePath());
    const QString resolved = info.canonicalFilePath();
    return resolved.isEmpty() ? info.absoluteFilePath() : resolved;
}

bool canReplaceSelf()
{
    const QFileInfo exe(ownExecutable());
    return exe.isWritable() && QFileInfo(exe.absolutePath()).isWritable();
}

QString install(const QString &exe, const Request &req, QString *error)
{
    const auto fail = [error](const QString &why) {
        *error = why;
        return QString();
    };
    static const QRegularExpression hex64(u"^[0-9a-fA-F]{64}$"_s);
    if (!hex64.match(req.sha256).hasMatch())
        return fail(u"no valid sha256 given, refusing to install unverified bytes"_s);

    QFile src(req.source);
    if (!src.open(QIODevice::ReadOnly))
        return fail(u"cannot open %1: %2"_s.arg(req.source, src.errorString()));

    // Staged beside the target so the final step is a rename on one
    // filesystem: the program is either the old one or the new one, never
    // half of each.
    const QString staged = QFileInfo(exe).absolutePath() + u"/.talos-update"_s;
    QFile::remove(staged);
    QFile dst(staged);
    if (!dst.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return fail(u"cannot write next to the installed program (%1). An install owned by "
                    "the system is updated through its package manager."_s.arg(dst.errorString()));
    }
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!src.atEnd()) {
        const QByteArray block = src.read(1 << 20);
        hash.addData(block);
        if (dst.write(block) != block.size()) {
            dst.close();
            QFile::remove(staged);
            return fail(u"staging copy failed: %1"_s.arg(dst.errorString()));
        }
    }
    if (!dst.flush()) {
        dst.close();
        QFile::remove(staged);
        return fail(u"staging copy failed: %1"_s.arg(dst.errorString()));
    }
    dst.close();
    const QString got = QString::fromLatin1(hash.result().toHex());
    if (got.compare(req.sha256, Qt::CaseInsensitive) != 0) {
        QFile::remove(staged);
        return fail(u"integrity check failed (expected %1, got %2), aborting"_s
                        .arg(req.sha256.toLower(), got));
    }

    const QString pushed = versionOf(staged);
    if (pushed.isEmpty()) {
        QFile::remove(staged);
        return fail(u"that file is not a Talos build (a talon-node binary cannot replace "
                    "this app). Push a Talos build for this platform."_s);
    }
    if (!req.allowDowngrade && compareVersions(pushed, req.running) < 0) {
        QFile::remove(staged);
        return fail(u"refusing to downgrade from %1 to %2 (pass allow_downgrade to force it)"_s
                        .arg(req.running, pushed));
    }
    QFile::setPermissions(staged, QFile::ReadOwner | QFile::WriteOwner | QFile::ExeOwner
                                      | QFile::ReadGroup | QFile::ExeGroup | QFile::ReadOther
                                      | QFile::ExeOther);
#if defined(Q_OS_WIN)
    // A running program can be renamed on Windows, not overwritten.
    QFile::remove(exe + u".old"_s);
    QFile::rename(exe, exe + u".old"_s);
#endif
    if (::rename(QFile::encodeName(staged).constData(), QFile::encodeName(exe).constData()) != 0) {
        QFile::remove(staged);
#if defined(Q_OS_WIN)
        QFile::rename(exe + u".old"_s, exe);
#endif
        return fail(u"could not replace the running program"_s);
    }
    return pushed;
}

} // namespace talos::selfupdate
