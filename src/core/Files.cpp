// SPDX-License-Identifier: GPL-2.0-only
#include "core/Files.h"

#include <QFile>
#include <QFileInfo>

#if defined(Q_OS_UNIX)
#  include <cerrno>
#  include <cstdio>
#endif

namespace talos {

using namespace Qt::StringLiterals;

bool moveOver(const QString &from, const QString &to)
{
#if defined(Q_OS_UNIX)
    if (std::rename(QFile::encodeName(from).constData(), QFile::encodeName(to).constData()) == 0)
        return true;
    // Anything but "different filesystem" would fail the slow way too.
    if (errno != EXDEV)
        return false;
#endif
    // Copy-and-delete, or a platform whose rename won't replace: keep the
    // old file within reach until the new one is in place.
    const QString aside = to + u".talos-old"_s;
    const bool had = QFileInfo(to).isFile();
    if (had) {
        QFile::remove(aside);
        if (!QFile::rename(to, aside))
            return false;
    }
    if (QFile::rename(from, to)) {
        if (had)
            QFile::remove(aside);
        return true;
    }
    if (had)
        QFile::rename(aside, to);
    return false;
}

} // namespace talos
