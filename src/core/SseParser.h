// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <QByteArray>
#include <QByteArrayList>

namespace talos {

/// Incremental Server-Sent-Events parser.
///
/// Feed it raw bytes as they arrive; it returns the `data:` payload of every
/// event completed by that chunk. Comment lines (the bridge's `: ping`
/// keep-alives) and fields other than `data` are dropped. A chunk may end
/// anywhere, including inside a UTF-8 sequence or between CR and LF.
class SseParser
{
public:
    QByteArrayList feed(const QByteArray &chunk)
    {
        QByteArrayList events;
        m_buffer += chunk;
        qsizetype start = 0;
        // What is already known to hold no newline isn't searched again, so
        // one large event arriving in many chunks stays linear.
        qsizetype from = m_searched;
        while (true) {
            const qsizetype nl = m_buffer.indexOf('\n', from);
            if (nl < 0)
                break;
            qsizetype end = nl;
            if (end > start && m_buffer.at(end - 1) == '\r')
                --end;
            handleLine(QByteArrayView(m_buffer).sliced(start, end - start), events);
            start = nl + 1;
            from = start;
        }
        m_buffer.remove(0, start);
        m_searched = m_buffer.size();
        // A stream that never ends a line, or an event, is not the bridge.
        // Drop what has piled up rather than hold it without limit.
        if (m_buffer.size() > kMaxEventBytes || m_data.size() > kMaxEventBytes)
            reset();
        return events;
    }

    void reset()
    {
        m_buffer.clear();
        m_data.clear();
        m_hasData = false;
        m_searched = 0;
    }

private:
    void handleLine(QByteArrayView line, QByteArrayList &events)
    {
        if (line.isEmpty()) {
            if (m_hasData)
                events.append(m_data);
            m_data.clear();
            m_hasData = false;
            return;
        }
        if (line.startsWith(':'))
            return;
        if (!line.startsWith("data"))
            return;
        QByteArrayView value;
        if (line.size() > 4) {
            if (line.at(4) != ':')
                return; // some other field that merely starts with "data"
            value = line.sliced(5);
            if (value.startsWith(' '))
                value = value.sliced(1);
        }
        if (m_hasData)
            m_data += '\n';
        m_data += value;
        m_hasData = true;
    }

    /// Far above any real event: a device command can carry a file chunk.
    static constexpr qsizetype kMaxEventBytes = 64 * 1024 * 1024;

    QByteArray m_buffer;
    qsizetype m_searched = 0;
    QByteArray m_data;
    bool m_hasData = false;
};

} // namespace talos
