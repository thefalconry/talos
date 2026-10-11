// SPDX-License-Identifier: GPL-2.0-only
#include "core/AppLog.h"

#include "core/Profile.h"

#include <QDateTime>
#include <QTimer>
#include <cstdio>

namespace talos {

using namespace Qt::StringLiterals;

namespace {
constexpr size_t kMaxEntries = 2000;
const char *levelName(AppLog::Level l)
{
    switch (l) {
    case AppLog::Debug: return "debug";
    case AppLog::Info: return "info";
    case AppLog::Warn: return "warn";
    case AppLog::Error: return "error";
    }
    return "info";
}
} // namespace

AppLog *AppLog::instance()
{
    static AppLog log;
    return &log;
}

void AppLog::add(Level level, const char *tag, const QString &msg)
{
    const QString clean = redactSecrets(msg);
    m_entries.push_back({QDateTime::currentMSecsSinceEpoch(), level, QString::fromLatin1(tag), clean});
    if (m_entries.size() > kMaxEntries)
        m_entries.pop_front();
    if (level >= Info || qEnvironmentVariableIsSet("TALOS_DEBUG"))
        std::fprintf(stderr, "[%s] %s: %s\n", levelName(level), tag, qPrintable(clean));
    ++m_revision;
    // One notification per event-loop turn, however chatty the caller.
    if (!m_notifyQueued) {
        m_notifyQueued = true;
        QTimer::singleShot(0, this, [this] {
            m_notifyQueued = false;
            emit changed();
        });
    }
}

QVariantList AppLog::entries(int minLevel) const
{
    QVariantList out;
    for (const Entry &e : m_entries) {
        if (e.level < minLevel)
            continue;
        out.append(QVariantMap{
            {u"ts"_s, e.ts},
            {u"level"_s, QString::fromLatin1(levelName(e.level))},
            {u"component"_s, e.tag},
            {u"msg"_s, e.msg},
        });
    }
    return out;
}

QString AppLog::text() const
{
    QString out;
    for (const Entry &e : m_entries) {
        out += QDateTime::fromMSecsSinceEpoch(e.ts).toString(Qt::ISODateWithMs) + u' '
            + QString::fromLatin1(levelName(e.level)).toUpper() + u" ["_s + e.tag + u"] "_s
            + e.msg + u'\n';
    }
    return out;
}

void AppLog::clear()
{
    m_entries.clear();
    ++m_revision;
    emit changed();
}

} // namespace talos
