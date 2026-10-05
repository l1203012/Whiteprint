#pragma once
#include "core/DrawingScene.h"
#include "render/Palette.h"

#include <QColor>
#include <QList>
#include <QPainterPath>
#include <QPointF>
#include <QRectF>
#include <optional>

class QPainter;

namespace wp {

/// A filled triangular arrowhead.
struct ArrowHead {
    double length;
    double halfWidth;

    explicit ArrowHead(bool thick) : length(thick ? 11 : 9), halfWidth(thick ? 4.5 : 3.5) {}

    /// Where the shaft should stop so its end hides under the head.
    QPointF base(QPointF tip, QPointF tail) const;
    QPainterPath path(QPointF tip, QPointF tail) const;
};

/// Strokes and labels one scene into a y-down painter whose origin is the
/// scene's grid origin.
struct ScenePainter {
    static constexpr double lineWidth = 1.5;
    static constexpr double thickLineWidth = 2.5;
    static constexpr double fineLineWidth = 1;
    static constexpr double cornerRadius = 4;
    static constexpr double groupCornerRadius = 7;
    static constexpr double labelFontSize = 12;
    static constexpr double smallFontSize = 11;

    QPainter &painter;
    BlueprintPalette palette;

    ScenePainter(QPainter &painter, BlueprintPalette palette) : painter(painter), palette(std::move(palette)) {}

    void draw(const DrawingScene &scene) const;

    /// Height of half the cylinder's top ellipse.
    static double cylinderCapHeight(const QRectF &rect);
    /// The whole top ellipse, the sides, and the front half of the bottom ellipse.
    static QPainterPath cylinderPath(const QRectF &rect, double capHeight);
    /// The point halfway along the polyline.
    static QPointF midpoint(const QList<QPointF> &points);

private:
    void drawShape(const SceneShape &shape) const;
    void drawLine(const SceneLine &line) const;
    void drawDimension(const SceneDimension &dimension) const;
    void drawGroup(const SceneGroup &group) const;
    void stroke(const QPainterPath &path, DrawingStyle style, std::optional<QColor> color = std::nullopt,
                std::optional<double> width = std::nullopt) const;
    void strokePlain(const QPainterPath &path, double width) const;
    void drawLabel(const std::optional<QString> &label, const QRectF &rect, bool bold) const;
    void drawText(const std::optional<QString> &label, const QRectF &rect, bool bold) const;
    void drawKnockoutLabel(const QString &label, QPointF point) const;
};

/// Vector helpers for canvas points.
double distance(QPointF a, QPointF b);
/// The unit vector from `a` to `b`, or nullopt if they coincide.
std::optional<QPointF> direction(QPointF a, QPointF b);
QPointF offsetBy(QPointF p, QPointF direction, double distance);
QPointF interpolated(QPointF a, QPointF b, double fraction);

} // namespace wp
