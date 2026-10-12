// SPDX-License-Identifier: GPL-2.0-only
#include "app/FlowChartItem.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <cmath>

namespace talos {

namespace {

constexpr qreal kWrapWidth = 190;

} // namespace

FlowChartItem::FlowChartItem(QQuickItem *parent) : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
}

void FlowChartItem::setSource(const QString &source)
{
    if (source == m_source)
        return;
    m_source = source;
    relayout();
}

void FlowChartItem::setFont(const QFont &font)
{
    if (font == m_font)
        return;
    m_font = font;
    relayout();
}

void FlowChartItem::restyle()
{
    update();
    emit changed();
}

void FlowChartItem::relayout()
{
    m_chart = flow::parse(m_source);
    const QFontMetricsF metrics(m_font);
    flow::layout(m_chart, [&metrics](const QString &text) {
        return metrics.boundingRect(QRectF(0, 0, kWrapWidth, 10'000),
                                    Qt::AlignCenter | Qt::TextWordWrap, text).size();
    });
    setImplicitSize(m_chart.size.width(), m_chart.size.height());
    update();
    emit changed();
}

void FlowChartItem::paint(QPainter *painter)
{
    if (!m_chart.valid || m_chart.size.isEmpty())
        return;
    // Drawn at its own size, or shrunk to the width there is.
    const qreal scale = qMin(1.0, width() / m_chart.size.width());
    painter->scale(scale, scale);
    painter->setRenderHint(QPainter::Antialiasing);
    painter->setFont(m_font);

    for (const flow::Edge &edge : m_chart.edges) {
        const flow::Node &a = m_chart.nodes.at(edge.from);
        const flow::Node &b = m_chart.nodes.at(edge.to);
        if (edge.from == edge.to)
            continue;
        const QPointF from = flow::edgePoint(a.rect, b.rect.center(), a.shape);
        const QPointF to = flow::edgePoint(b.rect, a.rect.center(), b.shape);
        QPen pen(m_line, edge.thick ? 2.4 : 1.4);
        if (edge.dotted)
            pen.setStyle(Qt::DashLine);
        painter->setPen(pen);
        painter->drawLine(from, to);
        if (edge.arrow) {
            const qreal angle = std::atan2(to.y() - from.y(), to.x() - from.x());
            const qreal size = 8;
            QPainterPath head;
            head.moveTo(to);
            head.lineTo(to.x() - size * std::cos(angle - 0.42), to.y() - size * std::sin(angle - 0.42));
            head.lineTo(to.x() - size * std::cos(angle + 0.42), to.y() - size * std::sin(angle + 0.42));
            head.closeSubpath();
            painter->setPen(Qt::NoPen);
            painter->setBrush(m_line);
            painter->drawPath(head);
        }
        if (!edge.label.isEmpty()) {
            const QFontMetricsF metrics(m_font);
            QRectF box = metrics.boundingRect(QRectF(0, 0, 160, 1000),
                                              Qt::AlignCenter | Qt::TextWordWrap, edge.label);
            box.adjust(-5, -2, 5, 2);
            box.moveCenter((from + to) / 2);
            painter->setPen(Qt::NoPen);
            painter->setBrush(m_paper);
            painter->drawRoundedRect(box, 4, 4);
            painter->setPen(m_ink);
            painter->drawText(box, Qt::AlignCenter | Qt::TextWordWrap, edge.label);
        }
    }

    for (const flow::Node &node : m_chart.nodes) {
        painter->setPen(QPen(m_line, 1.3));
        painter->setBrush(m_fill);
        const QRectF r = node.rect;
        switch (node.shape) {
        case flow::Node::Box:
            painter->drawRoundedRect(r, 5, 5);
            break;
        case flow::Node::Round:
            painter->drawRoundedRect(r, r.height() / 2, r.height() / 2);
            break;
        case flow::Node::Circle:
            painter->drawEllipse(r);
            break;
        case flow::Node::Diamond: {
            QPainterPath diamond;
            diamond.moveTo(r.center().x(), r.top());
            diamond.lineTo(r.right(), r.center().y());
            diamond.lineTo(r.center().x(), r.bottom());
            diamond.lineTo(r.left(), r.center().y());
            diamond.closeSubpath();
            painter->drawPath(diamond);
            break;
        }
        }
        painter->setPen(m_ink);
        const QRectF text = node.shape == flow::Node::Diamond
            ? r.adjusted(r.width() * 0.16, r.height() * 0.2, -r.width() * 0.16, -r.height() * 0.2)
            : r.adjusted(6, 4, -6, -4);
        painter->drawText(text, Qt::AlignCenter | Qt::TextWordWrap, node.text);
    }
}

} // namespace talos
