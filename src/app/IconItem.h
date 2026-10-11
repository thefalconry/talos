// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <QColor>
#include <QPainterPath>
#include <QQuickPaintedItem>

namespace talos {

/// Draws an SVG path as a stroked (or filled) vector shape.
///
/// Icons are painted with QPainter into a texture sized for the screen's
/// pixel ratio. That keeps them sharp on every backend, including ones
/// without multisampling, and they clip like any other item.
class IconItem : public QQuickPaintedItem
{
    Q_OBJECT
    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY changed)
    Q_PROPERTY(QColor color READ color WRITE setColor NOTIFY changed)
    Q_PROPERTY(qreal weight READ weight WRITE setWeight NOTIFY changed)
    Q_PROPERTY(bool filled READ filled WRITE setFilled NOTIFY changed)
    Q_PROPERTY(qreal viewBox READ viewBox WRITE setViewBox NOTIFY changed)
public:
    explicit IconItem(QQuickItem *parent = nullptr);

    QString path() const { return m_source; }
    void setPath(const QString &path);
    QColor color() const { return m_color; }
    void setColor(const QColor &color);
    /// Stroke width in screen pixels, whatever the item's size.
    qreal weight() const { return m_weight; }
    void setWeight(qreal weight);
    bool filled() const { return m_filled; }
    void setFilled(bool filled);
    /// Side of the square the path's coordinates live in.
    qreal viewBox() const { return m_viewBox; }
    void setViewBox(qreal viewBox);

    void paint(QPainter *painter) override;

    /// Parse SVG path data (M L H V C S A Z, absolute and relative).
    static QPainterPath parse(const QString &data);

signals:
    void changed();

private:
    QString m_source;
    QPainterPath m_path;
    QColor m_color{Qt::black};
    qreal m_weight = 1.7;
    qreal m_viewBox = 24;
    bool m_filled = false;
};

} // namespace talos
