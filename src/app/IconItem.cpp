// SPDX-License-Identifier: GPL-2.0-only
#include "app/IconItem.h"

#include <QHash>
#include <QPainter>
#include <QtMath>

namespace talos {

namespace {

/// Reads numbers and command letters out of SVG path data.
class Scanner
{
public:
    explicit Scanner(const QString &s) : m_s(s) {}

    void skip()
    {
        while (m_i < m_s.size() && (m_s.at(m_i).isSpace() || m_s.at(m_i) == u','))
            ++m_i;
    }
    bool atEnd()
    {
        skip();
        return m_i >= m_s.size();
    }
    bool atCommand()
    {
        skip();
        return m_i < m_s.size() && m_s.at(m_i).isLetter() && m_s.at(m_i).toLower() != u'e';
    }
    QChar command() { return m_s.at(m_i++); }

    double number()
    {
        skip();
        const qsizetype start = m_i;
        if (m_i < m_s.size() && (m_s.at(m_i) == u'-' || m_s.at(m_i) == u'+'))
            ++m_i;
        bool dot = false;
        while (m_i < m_s.size()) {
            const QChar c = m_s.at(m_i);
            if (c.isDigit()) {
                ++m_i;
            } else if (c == u'.' && !dot) {
                dot = true;
                ++m_i;
            } else if ((c == u'e' || c == u'E') && m_i + 1 < m_s.size()) {
                m_i += 2; // exponent and its sign or first digit
            } else {
                break;
            }
        }
        if (m_i == start) {
            ++m_i; // never loop forever on junk
            return 0;
        }
        return QStringView(m_s).sliced(start, m_i - start).toDouble();
    }
    /// An arc flag is a single 0 or 1 and may run into the next number.
    bool flag()
    {
        skip();
        return m_i < m_s.size() && m_s.at(m_i++) == u'1';
    }

private:
    const QString &m_s;
    qsizetype m_i = 0;
};

/// An SVG elliptical arc from the current point to (x, y), as cubic curves
/// (the endpoint-to-centre conversion from the SVG implementation notes).
void arcTo(QPainterPath &path, double rx, double ry, double rotation, bool large, bool sweep,
           double x, double y)
{
    const QPointF from = path.currentPosition();
    if (qFuzzyCompare(from.x(), x) && qFuzzyCompare(from.y(), y))
        return;
    rx = qAbs(rx);
    ry = qAbs(ry);
    if (rx < 1e-9 || ry < 1e-9) {
        path.lineTo(x, y);
        return;
    }
    const double phi = qDegreesToRadians(rotation);
    const double cosPhi = qCos(phi), sinPhi = qSin(phi);
    const double dx = (from.x() - x) / 2, dy = (from.y() - y) / 2;
    const double x1 = cosPhi * dx + sinPhi * dy;
    const double y1 = -sinPhi * dx + cosPhi * dy;
    // Radii too small to span the endpoints are scaled up to just fit.
    const double lambda = (x1 * x1) / (rx * rx) + (y1 * y1) / (ry * ry);
    if (lambda > 1) {
        rx *= qSqrt(lambda);
        ry *= qSqrt(lambda);
    }
    const double num = rx * rx * ry * ry - rx * rx * y1 * y1 - ry * ry * x1 * x1;
    const double den = rx * rx * y1 * y1 + ry * ry * x1 * x1;
    double k = den > 0 ? qSqrt(qMax(0.0, num / den)) : 0;
    if (large == sweep)
        k = -k;
    const double cxp = k * rx * y1 / ry;
    const double cyp = -k * ry * x1 / rx;
    const double cx = cosPhi * cxp - sinPhi * cyp + (from.x() + x) / 2;
    const double cy = sinPhi * cxp + cosPhi * cyp + (from.y() + y) / 2;

    const auto angle = [](double ux, double uy, double vx, double vy) {
        const double a = qAtan2(ux * vy - uy * vx, ux * vx + uy * vy);
        return a;
    };
    const double theta = angle(1, 0, (x1 - cxp) / rx, (y1 - cyp) / ry);
    double delta = angle((x1 - cxp) / rx, (y1 - cyp) / ry, (-x1 - cxp) / rx, (-y1 - cyp) / ry);
    if (!sweep && delta > 0)
        delta -= 2 * M_PI;
    else if (sweep && delta < 0)
        delta += 2 * M_PI;

    const int segments = qMax(1, int(qCeil(qAbs(delta) / (M_PI / 2) - 1e-9)));
    const double step = delta / segments;
    const double t = 4.0 / 3.0 * qTan(step / 4);
    double a = theta;
    for (int i = 0; i < segments; ++i) {
        const double cosA = qCos(a), sinA = qSin(a);
        const double b = a + step;
        const double cosB = qCos(b), sinB = qSin(b);
        const auto map = [&](double ex, double ey) {
            return QPointF(cx + cosPhi * rx * ex - sinPhi * ry * ey,
                           cy + sinPhi * rx * ex + cosPhi * ry * ey);
        };
        path.cubicTo(map(cosA - t * sinA, sinA + t * cosA), map(cosB + t * sinB, sinB - t * cosB),
                     map(cosB, sinB));
        a = b;
    }
}

} // namespace

QPainterPath IconItem::parse(const QString &data)
{
    QPainterPath path;
    Scanner in(data);
    QChar cmd = u'M';
    QPointF start, lastControl;
    bool hadCurve = false;

    while (!in.atEnd()) {
        if (in.atCommand())
            cmd = in.command();
        const bool rel = cmd.isLower();
        const QPointF cur = path.currentPosition();
        const auto point = [&] {
            const double px = in.number();
            const double py = in.number();
            return rel ? QPointF(cur.x() + px, cur.y() + py) : QPointF(px, py);
        };
        bool curve = false;
        switch (cmd.toLower().unicode()) {
        case 'm': {
            const QPointF p = point();
            path.moveTo(p);
            start = p;
            // Further pairs after a moveto are implicit linetos.
            cmd = rel ? u'l' : u'L';
            break;
        }
        case 'l':
            path.lineTo(point());
            break;
        case 'h': {
            const double v = in.number();
            path.lineTo(rel ? cur.x() + v : v, cur.y());
            break;
        }
        case 'v': {
            const double v = in.number();
            path.lineTo(cur.x(), rel ? cur.y() + v : v);
            break;
        }
        case 'c': {
            const QPointF c1 = point(), c2 = point(), end = point();
            path.cubicTo(c1, c2, end);
            lastControl = c2;
            curve = true;
            break;
        }
        case 's': {
            // First control point mirrors the previous curve's second.
            const QPointF c1 = hadCurve ? 2 * cur - lastControl : cur;
            const QPointF c2 = point(), end = point();
            path.cubicTo(c1, c2, end);
            lastControl = c2;
            curve = true;
            break;
        }
        case 'a': {
            const double rx = in.number(), ry = in.number(), rotation = in.number();
            const bool large = in.flag(), sweep = in.flag();
            const QPointF end = point();
            arcTo(path, rx, ry, rotation, large, sweep, end.x(), end.y());
            break;
        }
        case 'z':
            path.closeSubpath();
            path.moveTo(start);
            break;
        default:
            in.number(); // unknown command: consume and move on
            break;
        }
        hadCurve = curve;
    }
    return path;
}

IconItem::IconItem(QQuickItem *parent) : QQuickPaintedItem(parent)
{
    setAntialiasing(true);
}

void IconItem::setPath(const QString &path)
{
    if (path == m_source)
        return;
    m_source = path;
    // A handful of icons are drawn hundreds of times: parse each once.
    static QHash<QString, QPainterPath> cache;
    auto it = cache.constFind(path);
    if (it == cache.constEnd())
        it = cache.insert(path, parse(path));
    m_path = *it;
    update();
    emit changed();
}

void IconItem::setColor(const QColor &color)
{
    if (color == m_color)
        return;
    m_color = color;
    update();
    emit changed();
}

void IconItem::setWeight(qreal weight)
{
    if (qFuzzyCompare(weight, m_weight))
        return;
    m_weight = weight;
    update();
    emit changed();
}

void IconItem::setFilled(bool filled)
{
    if (filled == m_filled)
        return;
    m_filled = filled;
    update();
    emit changed();
}

void IconItem::setViewBox(qreal viewBox)
{
    if (qFuzzyCompare(viewBox, m_viewBox) || viewBox <= 0)
        return;
    m_viewBox = viewBox;
    update();
    emit changed();
}

void IconItem::paint(QPainter *painter)
{
    if (m_path.isEmpty() || width() <= 0)
        return;
    painter->setRenderHint(QPainter::Antialiasing);
    const qreal scale = qMin(width(), height()) / m_viewBox;
    painter->scale(scale, scale);
    if (m_filled) {
        painter->setPen(Qt::NoPen);
        painter->setBrush(m_color);
    } else {
        // The pen is specified in screen pixels, so undo the scale.
        painter->setPen(QPen(m_color, m_weight / scale, Qt::SolidLine, Qt::RoundCap,
                             Qt::RoundJoin));
        painter->setBrush(Qt::NoBrush);
    }
    painter->drawPath(m_path);
}

} // namespace talos
