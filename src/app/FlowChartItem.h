// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include "core/Flow.h"

#include <QColor>
#include <QFont>
#include <QQuickPaintedItem>

namespace talos {

/// Draws a Mermaid flowchart (see core/Flow.h for what is understood).
/// `valid` is false for anything else, and the caller shows the source.
class FlowChartItem : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(QString source READ source WRITE setSource NOTIFY changed)
    Q_PROPERTY(bool valid READ valid NOTIFY changed)
    Q_PROPERTY(QColor ink READ ink WRITE setInk NOTIFY changed)
    Q_PROPERTY(QColor line READ line WRITE setLine NOTIFY changed)
    Q_PROPERTY(QColor fill READ fill WRITE setFill NOTIFY changed)
    Q_PROPERTY(QColor paper READ paper WRITE setPaper NOTIFY changed)
    Q_PROPERTY(QFont font READ font WRITE setFont NOTIFY changed)
public:
    explicit FlowChartItem(QQuickItem *parent = nullptr);

    QString source() const { return m_source; }
    void setSource(const QString &source);
    bool valid() const { return m_chart.valid; }
    QColor ink() const { return m_ink; }
    void setInk(const QColor &c) { m_ink = c; restyle(); }
    QColor line() const { return m_line; }
    void setLine(const QColor &c) { m_line = c; restyle(); }
    QColor fill() const { return m_fill; }
    void setFill(const QColor &c) { m_fill = c; restyle(); }
    QColor paper() const { return m_paper; }
    void setPaper(const QColor &c) { m_paper = c; restyle(); }
    QFont font() const { return m_font; }
    void setFont(const QFont &font);

    void paint(QPainter *painter) override;

signals:
    void changed();

private:
    void relayout();
    void restyle();

    QString m_source;
    flow::Chart m_chart;
    QColor m_ink{Qt::black};
    QColor m_line{Qt::gray};
    QColor m_fill{Qt::white};
    QColor m_paper{Qt::white};
    QFont m_font;
};

} // namespace talos
