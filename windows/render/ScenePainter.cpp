#include "render/ScenePainter.h"

#include "render/Fonts.h"
#include "render/SceneRenderer.h"

#include <QFontMetricsF>
#include <QPainter>
#include <cmath>

namespace wp {

// MARK: Vector helpers

double distance(QPointF a, QPointF b)
{
    return std::hypot(b.x() - a.x(), b.y() - a.y());
}

std::optional<QPointF> direction(QPointF a, QPointF b)
{
    const double length = distance(a, b);
    if (length <= 0.0001)
        return std::nullopt;
    return QPointF((b.x() - a.x()) / length, (b.y() - a.y()) / length);
}

QPointF offsetBy(QPointF p, QPointF direction, double distance)
{
    return QPointF(p.x() + direction.x() * distance, p.y() + direction.y() * distance);
}

QPointF interpolated(QPointF a, QPointF b, double fraction)
{
    return QPointF(a.x() + (b.x() - a.x()) * fraction, a.y() + (b.y() - a.y()) * fraction);
}

// MARK: ArrowHead

QPointF ArrowHead::base(QPointF tip, QPointF tail) const
{
    const auto dir = direction(tail, tip);
    if (!dir || distance(tail, tip) <= length)
        return tip;
    return offsetBy(tip, *dir, -(length - 1));
}

QPainterPath ArrowHead::path(QPointF tip, QPointF tail) const
{
    QPainterPath p;
    const auto dir = direction(tail, tip);
    if (!dir)
        return p;
    const QPointF b = offsetBy(tip, *dir, -length);
    const QPointF normal(-dir->y(), dir->x());
    p.moveTo(tip);
    p.lineTo(offsetBy(b, normal, halfWidth));
    p.lineTo(offsetBy(b, normal, -halfWidth));
    p.closeSubpath();
    return p;
}

namespace {

QFont labelFont(bool bold, double size)
{
    return fonts::system(size, bold ? QFont::DemiBold : QFont::Normal);
}

/// Size of `text` laid out in `width` (or unbounded when `width` is 0).
QSizeF measure(const QFont &font, const QString &text, double width)
{
    const QFontMetricsF metrics(font);
    const double w = width > 0 ? width : 100000;
    return metrics.boundingRect(QRectF(0, 0, w, 100000), Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, text).size();
}

} // namespace

// MARK: ScenePainter

void ScenePainter::draw(const DrawingScene &scene) const
{
    for (const SceneGroup &group : scene.groups)
        drawGroup(group);
    for (const SceneLine &line : scene.lines)
        drawLine(line);
    for (const SceneShape &shape : scene.shapes)
        drawShape(shape);
    for (const SceneDimension &dimension : scene.dimensions)
        drawDimension(dimension);
}

// MARK: Shapes

void ScenePainter::drawShape(const SceneShape &shape) const
{
    const QRectF rect = canvasRect(shape.frame);
    const bool bold = shape.style.contains(DrawingStyle::bold);
    switch (shape.kind) {
    case ShapeKind::box: {
        const double radius = std::min({cornerRadius, rect.width() / 2, rect.height() / 2});
        QPainterPath path;
        path.addRoundedRect(rect, radius, radius);
        stroke(path, shape.style);
        drawLabel(shape.label, rect, bold);
        break;
    }
    case ShapeKind::circle: {
        QPainterPath path;
        path.addEllipse(rect);
        stroke(path, shape.style);
        drawLabel(shape.label, rect, bold);
        break;
    }
    case ShapeKind::db: {
        const double cap = cylinderCapHeight(rect);
        stroke(cylinderPath(rect, cap), shape.style);
        const QRectF body(rect.left(), rect.top() + 2 * cap, rect.width(), rect.height() - 2 * cap);
        drawLabel(shape.label, body, bold);
        break;
    }
    case ShapeKind::text:
        drawText(shape.label, rect, bold);
        break;
    }
}

double ScenePainter::cylinderCapHeight(const QRectF &rect)
{
    return std::max(2.0, std::min({rect.height() * 0.15, rect.width() * 0.2, 8.0}));
}

QPainterPath ScenePainter::cylinderPath(const QRectF &rect, double cap)
{
    QPainterPath path;
    path.addEllipse(QRectF(rect.left(), rect.top(), rect.width(), 2 * cap));
    const double bottomY = rect.bottom() - cap;
    const double k = 0.5523;
    const double rx = rect.width() / 2;
    const double midX = rect.center().x();
    path.moveTo(rect.left(), rect.top() + cap);
    path.lineTo(rect.left(), bottomY);
    path.cubicTo(QPointF(rect.left(), bottomY + k * cap), QPointF(midX - k * rx, rect.bottom()), QPointF(midX, rect.bottom()));
    path.cubicTo(QPointF(midX + k * rx, rect.bottom()), QPointF(rect.right(), bottomY + k * cap), QPointF(rect.right(), bottomY));
    path.lineTo(rect.right(), rect.top() + cap);
    return path;
}

// MARK: Lines

void ScenePainter::drawLine(const SceneLine &line) const
{
    QList<QPointF> points;
    for (const GridPoint &p : line.points)
        points.append(canvasPoint(p));
    if (points.size() < 2)
        return;
    const ArrowHead arrow(line.style.contains(DrawingStyle::thick));
    QList<QPointF> shaft = points;
    if (line.startArrow)
        shaft[0] = arrow.base(points[0], points[1]);
    if (line.endArrow)
        shaft[shaft.size() - 1] = arrow.base(points[points.size() - 1], points[points.size() - 2]);
    QPainterPath path;
    path.moveTo(shaft[0]);
    for (qsizetype i = 1; i < shaft.size(); ++i)
        path.lineTo(shaft[i]);
    if (line.closed)
        path.closeSubpath();
    stroke(path, line.style);
    painter.save();
    painter.setPen(Qt::NoPen);
    painter.setBrush(palette.text);
    if (line.startArrow)
        painter.drawPath(arrow.path(points[0], points[1]));
    if (line.endArrow)
        painter.drawPath(arrow.path(points[points.size() - 1], points[points.size() - 2]));
    painter.restore();
    if (line.label && !line.label->isEmpty())
        drawKnockoutLabel(*line.label, midpoint(points));
}

QPointF ScenePainter::midpoint(const QList<QPointF> &points)
{
    double total = 0;
    for (qsizetype i = 1; i < points.size(); ++i)
        total += distance(points[i - 1], points[i]);
    double remaining = total / 2;
    for (qsizetype i = 1; i < points.size(); ++i) {
        const double length = distance(points[i - 1], points[i]);
        if (remaining <= length && length > 0)
            return interpolated(points[i - 1], points[i], remaining / length);
        remaining -= length;
    }
    return points[0];
}

// MARK: Dimensions

void ScenePainter::drawDimension(const SceneDimension &dimension) const
{
    const QPointF from = canvasPoint(dimension.from), to = canvasPoint(dimension.to);
    const auto offset = dimension.offsetLine();
    const QPointF start = canvasPoint(offset.first), end = canvasPoint(offset.second);
    QPainterPath path;
    path.moveTo(start);
    path.lineTo(end);
    if (const auto normal = direction(from, start)) {
        const double gap = 3, overshoot = 4;
        const std::pair<QPointF, QPointF> pairs[2] = {{from, start}, {to, end}};
        for (const auto &[measured, mark] : pairs) {
            path.moveTo(offsetBy(measured, *normal, gap));
            path.lineTo(offsetBy(mark, *normal, overshoot));
        }
    }
    strokePlain(path, fineLineWidth);
    if (const auto along = direction(start, end)) {
        const QPointF slash(along->x() - along->y(), along->y() + along->x());
        QPainterPath ticks;
        for (const QPointF &mark : {start, end}) {
            ticks.moveTo(offsetBy(mark, slash, -3.5));
            ticks.lineTo(offsetBy(mark, slash, 3.5));
        }
        strokePlain(ticks, lineWidth);
    }
    drawKnockoutLabel(dimension.label, interpolated(start, end, 0.5));
}

// MARK: Groups

void ScenePainter::drawGroup(const SceneGroup &group) const
{
    const QRectF rect = canvasRect(group.frame);
    const double radius = std::min({groupCornerRadius, rect.width() / 2, rect.height() / 2});
    QPainterPath path;
    path.addRoundedRect(rect, radius, radius);
    stroke(path, group.style, palette.muted, group.style.contains(DrawingStyle::thick) ? lineWidth * 1.4 : fineLineWidth * 1.2);
    if (!group.label || group.label->isEmpty())
        return;
    const QFont font = fonts::system(smallFontSize, QFont::Medium);
    painter.save();
    painter.setFont(font);
    painter.setPen(palette.muted);
    painter.drawText(QPointF(rect.left() + 8, rect.top() + 4 + QFontMetricsF(font).ascent()), *group.label);
    painter.restore();
}

// MARK: Stroking

void ScenePainter::stroke(const QPainterPath &path, DrawingStyle style, std::optional<QColor> color, std::optional<double> width) const
{
    const double w = width.value_or(style.contains(DrawingStyle::thick) ? thickLineWidth : lineWidth);
    QPen pen(color.value_or(palette.text));
    pen.setWidthF(w);
    pen.setJoinStyle(Qt::RoundJoin);
    if (style.contains(DrawingStyle::dashed)) {
        pen.setCapStyle(Qt::FlatCap);
        pen.setDashPattern({5 / w, 3.5 / w}); // in pen widths
    } else {
        pen.setCapStyle(Qt::RoundCap);
    }
    painter.save();
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(path);
    painter.restore();
}

void ScenePainter::strokePlain(const QPainterPath &path, double width) const
{
    stroke(path, DrawingStyle(), std::nullopt, width);
}

// MARK: Text

void ScenePainter::drawLabel(const std::optional<QString> &label, const QRectF &rect, bool bold) const
{
    if (!label || label->isEmpty())
        return;
    const QFont font = labelFont(bold, labelFontSize);
    const double width = std::max(rect.width() - 8, 1.0);
    const double height = std::ceil(measure(font, *label, width).height());
    const QRectF box(rect.center().x() - width / 2, rect.center().y() - height / 2, width, height);
    painter.save();
    painter.setFont(font);
    painter.setPen(palette.text);
    painter.drawText(box, Qt::AlignHCenter | Qt::AlignTop | Qt::TextWordWrap, *label);
    painter.restore();
}

void ScenePainter::drawText(const std::optional<QString> &label, const QRectF &rect, bool bold) const
{
    if (!label || label->isEmpty())
        return;
    const QFont font = labelFont(bold, labelFontSize);
    const QSizeF size = measure(font, *label, 0);
    const double y = rect.top() + std::max(0.0, (rect.height() - size.height()) / 2);
    const QRectF box(rect.left(), y, std::ceil(size.width()) + 1, std::ceil(size.height()));
    painter.save();
    painter.setFont(font);
    painter.setPen(palette.text);
    painter.drawText(box, Qt::AlignLeft | Qt::AlignTop | Qt::TextDontClip, *label);
    painter.restore();
}

void ScenePainter::drawKnockoutLabel(const QString &label, QPointF point) const
{
    const QFont font = labelFont(false, smallFontSize);
    const QSizeF size = measure(font, label, 0);
    const QRectF textRect(point.x() - std::ceil(size.width()) / 2, point.y() - std::ceil(size.height()) / 2, std::ceil(size.width()),
                          std::ceil(size.height()));
    const QRectF plate = textRect.adjusted(-4, -1, 4, 1);
    painter.save();
    QPainterPath path;
    path.addRoundedRect(plate, 3, 3);
    painter.setPen(Qt::NoPen);
    painter.fillPath(path, palette.pageBackground);
    painter.setFont(font);
    painter.setPen(palette.text);
    painter.drawText(textRect, Qt::AlignHCenter | Qt::AlignTop | Qt::TextDontClip, label);
    painter.restore();
}

} // namespace wp
