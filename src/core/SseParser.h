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
        while (true) {
            const qsizetype nl = m_buffer.indexOf('\n', start);
            if (nl < 0)
                break;
            qsizetype end = nl;
            if (end > start && m_buffer.at(end - 1) == '\r')
                --end;
            handleLine(QByteArrayView(m_buffer).sliced(start, end - start), events);
            start = nl + 1;
        }
        m_buffer.remove(0, start);
        return events;
    }

    void reset()
    {
        m_buffer.clear();
        m_data.clear();
        m_hasData = false;
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

    QByteArray m_buffer;
    QByteArray m_data;
    bool m_hasData = false;
};

} // namespace talos
