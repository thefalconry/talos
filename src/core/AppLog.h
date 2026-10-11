// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <QObject>
#include <QVariantList>
#include <deque>

namespace talos {

/// The client's own in-memory log: a bounded ring the diagnostics screen
/// reads, mirrored to stderr. Messages are redacted before they are kept.
class AppLog : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int revision READ revision NOTIFY changed)
public:
    enum Level { Debug, Info, Warn, Error };

    static AppLog *instance();

    static void debug(const char *tag, const QString &msg) { instance()->add(Debug, tag, msg); }
    static void info(const char *tag, const QString &msg) { instance()->add(Info, tag, msg); }
    static void warn(const char *tag, const QString &msg) { instance()->add(Warn, tag, msg); }
    static void error(const char *tag, const QString &msg) { instance()->add(Error, tag, msg); }

    void add(Level level, const char *tag, const QString &msg);
    int revision() const { return m_revision; }

    Q_INVOKABLE QVariantList entries(int minLevel = 0) const;
    Q_INVOKABLE QString text() const;
    Q_INVOKABLE void clear();

signals:
    void changed();

private:
    struct Entry {
        qint64 ts;
        Level level;
        QString tag;
        QString msg;
    };
    std::deque<Entry> m_entries;
    int m_revision = 0;
    bool m_notifyQueued = false;
};

} // namespace talos
