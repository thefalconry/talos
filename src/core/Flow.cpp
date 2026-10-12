// SPDX-License-Identifier: GPL-2.0-only
#include "core/Flow.h"

#include <QHash>
#include <QRegularExpression>
#include <QStringList>
#include <cmath>

namespace talos::flow {

using namespace Qt::StringLiterals;

namespace {

constexpr qreal kGapAlong = 46;  // between ranks
constexpr qreal kGapAcross = 26; // between nodes of a rank
constexpr qreal kPadX = 16;
constexpr qreal kPadY = 10;
constexpr qreal kMargin = 12;

QString unquote(QString text)
{
    text = text.trimmed();
    if (text.size() >= 2 && text.startsWith(u'"') && text.endsWith(u'"'))
        text = text.mid(1, text.size() - 2);
    text.replace(u"<br/>"_s, u"\n"_s).replace(u"<br>"_s, u"\n"_s).replace(u"<br />"_s, u"\n"_s);
    return text.trimmed();
}

class Reader
{
public:
    explicit Reader(Chart &chart) : m_chart(chart) {}

    void statement(const QString &line)
    {
        m_line = line;
        m_at = 0;
        QList<int> left = nodeGroup();
        if (left.isEmpty())
            return;
        while (true) {
            Edge edge;
            if (!link(&edge))
                return;
            const QList<int> right = nodeGroup();
            if (right.isEmpty())
                return;
            for (const int a : left) {
                for (const int b : right) {
                    Edge e = edge;
                    e.from = a;
                    e.to = b;
                    m_chart.edges << e;
                }
            }
            left = right;
        }
    }

private:
    void skipSpace()
    {
        while (m_at < m_line.size() && m_line.at(m_at).isSpace())
            ++m_at;
    }

    /// `A`, `A[text]`, or several joined with `&`.
    QList<int> nodeGroup()
    {
        QList<int> out;
        while (true) {
            const int n = node();
            if (n < 0)
                break;
            out << n;
            skipSpace();
            if (m_at < m_line.size() && m_line.at(m_at) == u'&') {
                ++m_at;
                continue;
            }
            break;
        }
        return out;
    }

    int node()
    {
        skipSpace();
        const qsizetype start = m_at;
        while (m_at < m_line.size()
               && (m_line.at(m_at).isLetterOrNumber() || m_line.at(m_at) == u'_'))
            ++m_at;
        if (m_at == start)
            return -1;
        const QString id = m_line.mid(start, m_at - start);
        int index = m_ids.value(id, -1);
        if (index < 0) {
            index = int(m_chart.nodes.size());
            m_ids.insert(id, index);
            m_chart.nodes << Node{id, id, Node::Box, 0, {}};
        }
        // A shape, if one follows directly.
        struct Form { QString open, close; Node::Shape shape; };
        static const QList<Form> forms{
            {u"((("_s, u")))"_s, Node::Circle}, {u"(("_s, u"))"_s, Node::Circle},
            {u"(["_s, u"])"_s, Node::Round}, {u"[["_s, u"]]"_s, Node::Box},
            {u"[("_s, u")]"_s, Node::Round}, {u"{{"_s, u"}}"_s, Node::Diamond},
            {u"["_s, u"]"_s, Node::Box}, {u"("_s, u")"_s, Node::Round},
            {u"{"_s, u"}"_s, Node::Diamond}, {u">"_s, u"]"_s, Node::Box},
        };
        for (const Form &f : forms) {
            if (!QStringView(m_line).sliced(m_at).startsWith(f.open))
                continue;
            const qsizetype from = m_at + f.open.size();
            const bool quoted = from < m_line.size() && m_line.at(from) == u'"';
            qsizetype close = -1;
            if (quoted) {
                const qsizetype endQuote = m_line.indexOf(u'"', from + 1);
                if (endQuote > 0)
                    close = m_line.indexOf(f.close, endQuote + 1);
            } else {
                close = m_line.indexOf(f.close, from);
            }
            if (close < 0)
                break;
            m_chart.nodes[index].text = unquote(m_line.mid(from, close - from));
            m_chart.nodes[index].shape = f.shape;
            m_at = close + f.close.size();
            break;
        }
        return index;
    }

    /// `-->`, `---`, `-.->`, `==>`, with `|label|` after or `-- label -->` inside.
    bool link(Edge *edge)
    {
        skipSpace();
        static const QRegularExpression arrow(
            u"\\G(<)?(--|==|-\\.)\\s*([^-=.>|\\s][^>|]*?)\\s*(-{2,3}|={2,3}|\\.{1,3}-{1,2})(>|o|x)?"_s);
        static const QRegularExpression simple(u"\\G(<)?(-{2,}|={2,}|-\\.+-)(>|o|x)?"_s);
        QString label;
        QString body;
        bool head = false;
        // The form with words inside first: read the other way, its opening
        // dashes would pass for a complete link and its label for a node.
        QRegularExpressionMatch m = arrow.match(m_line, m_at);
        if (m.hasMatch() && !m.captured(3).trimmed().isEmpty() && !m.captured(4).isEmpty()) {
            body = m.captured(2);
            label = m.captured(3);
            head = !m.captured(5).isEmpty();
        } else {
            m = simple.match(m_line, m_at);
            if (!m.hasMatch())
                return false;
            body = m.captured(2);
            head = !m.captured(3).isEmpty();
        }
        m_at = m.capturedEnd();
        skipSpace();
        if (m_at < m_line.size() && m_line.at(m_at) == u'|') {
            const qsizetype close = m_line.indexOf(u'|', m_at + 1);
            if (close > 0) {
                label = m_line.mid(m_at + 1, close - m_at - 1);
                m_at = close + 1;
            }
        }
        edge->label = unquote(label);
        edge->arrow = head;
        edge->dotted = body.contains(u'.');
        edge->thick = body.startsWith(u'=');
        return true;
    }

    Chart &m_chart;
    QHash<QString, int> m_ids;
    QString m_line;
    qsizetype m_at = 0;
};

} // namespace

Chart parse(const QString &source)
{
    Chart chart;
    Reader reader(chart);
    bool header = false;
    static const QRegularExpression head(
        u"^(?:graph|flowchart)(?:\\s+(TD|TB|BT|LR|RL))?\\s*$"_s,
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression skipped(
        u"^(?:subgraph\\b|end\\b|classDef\\b|class\\b|style\\b|linkStyle\\b|click\\b|direction\\b|%%)"_s);
    QString text = source;
    text.replace(u';', u'\n');
    for (const QString &raw : text.split(u'\n')) {
        const QString line = raw.trimmed();
        if (line.isEmpty())
            continue;
        if (!header) {
            if (line.startsWith(u"%%"))
                continue;
            const auto m = head.match(line);
            if (!m.hasMatch())
                return chart; // some other kind of diagram
            const QString dir = m.captured(1).toUpper();
            chart.horizontal = dir == "LR"_L1 || dir == "RL"_L1;
            chart.reversed = dir == "BT"_L1 || dir == "RL"_L1;
            header = true;
            continue;
        }
        if (skipped.match(line).hasMatch())
            continue;
        reader.statement(line);
    }
    chart.valid = header && !chart.nodes.isEmpty() && chart.nodes.size() <= 80;
    return chart;
}

void layout(Chart &chart, const std::function<QSizeF(const QString &)> &measure)
{
    if (!chart.valid)
        return;
    const int n = int(chart.nodes.size());
    // An arrow that leads back to something already on the way here closes
    // a loop. Such arrows are drawn but don't push anything down: found by
    // walking the chart from its nodes in the order they were written, so
    // the first node named stays at the top.
    QList<QList<int>> out(n);
    for (int e = 0; e < chart.edges.size(); ++e) {
        if (chart.edges.at(e).from != chart.edges.at(e).to)
            out[chart.edges.at(e).from] << e;
    }
    QList<bool> loops(chart.edges.size(), false);
    QList<int> state(n, 0); // 0 unseen, 1 on the current path, 2 done
    QList<int> order;       // nodes, each after everything it points to
    const std::function<void(int)> visit = [&](int node) {
        state[node] = 1;
        for (const int e : out[node]) {
            const int next = chart.edges.at(e).to;
            if (state[next] == 1)
                loops[e] = true;
            else if (state[next] == 0)
                visit(next);
        }
        state[node] = 2;
        order << node;
    };
    for (int i = 0; i < n; ++i) {
        if (state[i] == 0)
            visit(i);
    }
    // Rank: one more than the furthest thing pointing here.
    QList<int> rank(n, 0);
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
        for (const int e : out[*it]) {
            if (!loops[e])
                rank[chart.edges.at(e).to] = qMax(rank[chart.edges.at(e).to], rank[*it] + 1);
        }
    }
    int ranks = 0;
    for (int i = 0; i < n; ++i) {
        chart.nodes[i].rank = rank[i];
        ranks = qMax(ranks, rank[i] + 1);
    }

    QList<QSizeF> sizes;
    for (const Node &node : chart.nodes) {
        QSizeF s = measure(node.text);
        s += QSizeF(2 * kPadX, 2 * kPadY);
        if (node.shape == Node::Diamond)
            s = QSizeF(s.width() * 1.35 + 10, s.height() * 1.7);
        else if (node.shape == Node::Circle)
            s = QSizeF(qMax(s.width(), s.height()), qMax(s.width(), s.height()));
        sizes << s;
    }
    const auto along = [&](const QSizeF &s) { return chart.horizontal ? s.width() : s.height(); };
    const auto across = [&](const QSizeF &s) { return chart.horizontal ? s.height() : s.width(); };

    QList<QList<int>> rows(ranks);
    for (int i = 0; i < n; ++i)
        rows[rank[i]] << i;
    QList<qreal> thickness(ranks, 0), breadth(ranks, 0);
    qreal widest = 0;
    for (int r = 0; r < ranks; ++r) {
        for (const int i : rows[r]) {
            thickness[r] = qMax(thickness[r], along(sizes[i]));
            breadth[r] += across(sizes[i]);
        }
        breadth[r] += kGapAcross * qMax(0, int(rows[r].size()) - 1);
        widest = qMax(widest, breadth[r]);
    }
    qreal at = kMargin;
    for (int step = 0; step < ranks; ++step) {
        const int r = chart.reversed ? ranks - 1 - step : step;
        qreal pos = kMargin + (widest - breadth[r]) / 2;
        for (const int i : rows[r]) {
            const QSizeF s = sizes[i];
            const qreal a = at + (thickness[r] - along(s)) / 2;
            chart.nodes[i].rect = chart.horizontal ? QRectF(a, pos, s.width(), s.height())
                                                   : QRectF(pos, a, s.width(), s.height());
            pos += across(s) + kGapAcross;
        }
        at += thickness[r] + kGapAlong;
    }
    const qreal length = at - kGapAlong + kMargin;
    const qreal span = widest + 2 * kMargin;
    chart.size = chart.horizontal ? QSizeF(length, span) : QSizeF(span, length);
}

QPointF edgePoint(const QRectF &rect, const QPointF &towards, Node::Shape shape)
{
    const QPointF c = rect.center();
    const qreal dx = towards.x() - c.x(), dy = towards.y() - c.y();
    if (qFuzzyIsNull(dx) && qFuzzyIsNull(dy))
        return c;
    const qreal hw = rect.width() / 2, hh = rect.height() / 2;
    qreal t;
    if (shape == Node::Diamond) {
        t = 1.0 / (std::abs(dx) / hw + std::abs(dy) / hh);
    } else if (shape == Node::Circle) {
        t = hw / std::hypot(dx, dy);
    } else {
        const qreal tx = qFuzzyIsNull(dx) ? 1e9 : hw / std::abs(dx);
        const qreal ty = qFuzzyIsNull(dy) ? 1e9 : hh / std::abs(dy);
        t = qMin(tx, ty);
    }
    return {c.x() + dx * t, c.y() + dy * t};
}

} // namespace talos::flow
