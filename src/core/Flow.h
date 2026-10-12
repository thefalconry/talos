// SPDX-License-Identifier: GPL-2.0-only
#pragma once

#include <QList>
#include <QPointF>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <functional>

namespace talos::flow {

/// A Mermaid flowchart, read and laid out.
///
/// Only flowcharts (`graph` / `flowchart`) are understood: boxes, rounded
/// boxes, circles, diamonds, and the arrows between them with their labels.
/// Styling directives and subgraph boxes are read past. Any other kind of
/// diagram is not a chart as far as this is concerned, and is shown as the
/// source it is.
struct Node {
    enum Shape { Box, Round, Circle, Diamond };
    QString id;
    QString text;
    Shape shape = Box;
    int rank = 0;
    QRectF rect;
};

struct Edge {
    int from = -1;
    int to = -1;
    QString label;
    bool arrow = true;
    bool dotted = false;
    bool thick = false;
};

struct Chart {
    bool valid = false;
    /// Ranks run left to right instead of top to bottom.
    bool horizontal = false;
    /// Bottom-to-top or right-to-left.
    bool reversed = false;
    QList<Node> nodes;
    QList<Edge> edges;
    QSizeF size;
};

Chart parse(const QString &source);

/// Give every node its rectangle. @p measure returns the size a node's text
/// takes (it may contain line breaks), so the layout needs no font of its own.
void layout(Chart &chart, const std::function<QSizeF(const QString &)> &measure);

/// Where a line from a rectangle's centre towards @p towards leaves it.
QPointF edgePoint(const QRectF &rect, const QPointF &towards, Node::Shape shape);

} // namespace talos::flow
