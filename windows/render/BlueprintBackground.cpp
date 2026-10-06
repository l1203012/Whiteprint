#include "render/BlueprintBackground.h"

#include "render/Fonts.h"

#include <QFontMetricsF>
#include <QPaintDevice>
#include <QPaintEngine>
#include <QPainter>
#include <QPainterPath>
#include <QRadialGradient>
#include <algorithm>
#include <cmath>
#include <cstring>

namespace wp {

namespace {

constexpr double kPi = 3.14159265358979323846;

QColor blended(const QColor &base, double fraction, const QColor &other)
{
    auto mix = [&](double a, double b) { return a + (b - a) * fraction; };
    return QColor::fromRgbF(float(mix(base.redF(), other.redF())), float(mix(base.greenF(), other.greenF())),
                            float(mix(base.blueF(), other.blueF())), float(base.alphaF()));
}

bool isPDF(const QPainter &painter)
{
    const QPaintDevice *device = painter.device();
    QPaintEngine *engine = device ? device->paintEngine() : nullptr;
    return engine && engine->type() == QPaintEngine::Pdf;
}

QColor ruleColor(const BlueprintPalette &palette)
{
    return withAlpha(palette.text, palette.text.alphaF() * 0.7);
}

constexpr double minorWidth = 0.3;
constexpr double majorWidth = 0.6;

/// The two line weights of one 50 pt grid cell.
void drawGridTile(QPainter &p, const QColor &minor, const QColor &major)
{
    const double spacing = BlueprintBackground::minorSpacing;
    const double size = BlueprintBackground::majorSpacing;
    p.setPen(Qt::NoPen);
    p.setBrush(minor);
    for (double offset = spacing; offset < size; offset += spacing) {
        p.drawRect(QRectF(offset, 0, minorWidth, size));
        p.drawRect(QRectF(0, offset, size, minorWidth));
    }
    p.setBrush(major);
    p.drawRect(QRectF(0, 0, majorWidth, size));
    p.drawRect(QRectF(majorWidth, 0, size - majorWidth, majorWidth));
}

/// One opaque radial gradient around the page colour: lit a little above
/// and left of the centre, deepening towards the edges like a vignette.
void drawGradient(QPainter &painter, const QRectF &rect, const QColor &base)
{
    const QColor lit = blended(base, 0.08, Qt::white);
    const QColor deep = blended(base, 0.3, QColor::fromRgbF(0, 0.03f, 0.12f, 1));
    const QPointF center(rect.left() + rect.width() * 0.42, rect.top() + rect.height() * 0.38);
    const double reach = std::hypot(std::max(center.x() - rect.left(), rect.right() - center.x()),
                                    std::max(center.y() - rect.top(), rect.bottom() - center.y()));
    QRadialGradient gradient(center, reach);
    gradient.setColorAt(0, lit);
    gradient.setColorAt(0.5, base);
    gradient.setColorAt(1, deep);
    gradient.setSpread(QGradient::PadSpread);
    painter.fillRect(rect, QBrush(gradient));
}

void drawTexture(QPainter &painter, const QRectF &rect)
{
    const QImage &tile = PaperTexture::tile();
    if (tile.isNull())
        return;
    QBrush brush(tile);
    const double s = PaperTexture::points / PaperTexture::pixels;
    brush.setTransform(QTransform::fromScale(s, s));
    painter.save();
    painter.setBrushOrigin(rect.topLeft());
    painter.fillRect(rect, brush);
    painter.restore();
}

/// Minor and major lines anchored at `rect`'s top-left. On a PDF they are one
/// tiling pattern: hundreds of stroked lines make viewers split copied text into fragments.
void drawGrid(QPainter &painter, const QRectF &rect, const BlueprintPalette &palette)
{
    const QColor minor = palette.grid;
    const QColor major = withAlpha(palette.grid, std::min(1.0, palette.grid.alphaF() * 2.4));
    painter.save();
    painter.setClipRect(rect, Qt::IntersectClip);
    if (isPDF(painter)) {
        // Two compound paths (one fill each) rather than a stroke per line, which
        // PDF viewers handle as two objects. Their union also avoids double-blended crossings.
        QPainterPath minorLines, majorLines;
        minorLines.setFillRule(Qt::WindingFill);
        majorLines.setFillRule(Qt::WindingFill);
        const int columns = int(std::ceil(rect.width() / BlueprintBackground::minorSpacing));
        const int rows = int(std::ceil(rect.height() / BlueprintBackground::minorSpacing));
        const int perMajor = int(BlueprintBackground::majorSpacing / BlueprintBackground::minorSpacing);
        for (int i = 0; i <= columns; ++i) {
            const bool isMajor = i % perMajor == 0;
            (isMajor ? majorLines : minorLines)
                .addRect(QRectF(rect.left() + i * BlueprintBackground::minorSpacing, rect.top(), isMajor ? majorWidth : minorWidth, rect.height()));
        }
        for (int i = 0; i <= rows; ++i) {
            const bool isMajor = i % perMajor == 0;
            (isMajor ? majorLines : minorLines)
                .addRect(QRectF(rect.left(), rect.top() + i * BlueprintBackground::minorSpacing, rect.width(), isMajor ? majorWidth : minorWidth));
        }
        painter.setPen(Qt::NoPen);
        painter.fillPath(minorLines, minor);
        painter.fillPath(majorLines, major);
    } else {
        painter.setRenderHint(QPainter::Antialiasing);
        const double size = BlueprintBackground::majorSpacing;
        for (double y = 0; y < rect.height(); y += size) {
            for (double x = 0; x < rect.width(); x += size) {
                painter.save();
                painter.translate(rect.left() + x, rect.top() + y);
                drawGridTile(painter, minor, major);
                painter.restore();
            }
        }
    }
    painter.restore();
}

QFont titleFont(double size, QFont::Weight weight, double kern)
{
    QFont f = fonts::system(size, weight);
    f.setLetterSpacing(QFont::AbsoluteSpacing, kern);
    return f;
}

/// Draws one line of title block text with its baseline starting at `origin`,
/// truncated with an ellipsis to `width`.
void drawTitleText(QPainter &painter, const QString &string, const QFont &font, const QColor &color, double width, QPointF origin)
{
    const QFontMetricsF metrics(font);
    QString text = string;
    if (metrics.horizontalAdvance(text) > width)
        text = metrics.elidedText(text, Qt::ElideRight, width);
    painter.save();
    painter.setFont(font);
    painter.setPen(color);
    painter.drawText(origin, text);
    painter.restore();
}

} // namespace

// MARK: - SplitMix64

uint64_t SplitMix64::next()
{
    state += 0x9E3779B97F4A7C15ULL;
    uint64_t z = state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
    return z ^ (z >> 31);
}

double SplitMix64::unit()
{
    return double(next() >> 11) / double(uint64_t(1) << 53);
}

// MARK: - PaperTexture

namespace PaperTexture {

QByteArray coverage(uint64_t seed, int size)
{
    QImage mask(size, size, QImage::Format_Grayscale8);
    mask.fill(255);
    SplitMix64 random(seed);
    const double extent = size;
    {
        QPainter p(&mask);
        p.setRenderHint(QPainter::Antialiasing);
        // Fibres: short, gently curved strands, drawn at each wrap-around offset
        // so the tile is seamless.
        for (int i = 0; i < 140; ++i) {
            const double sx = random.unit() * extent;
            const double sy = random.unit() * extent;
            const double angle = random.unit() * 2 * kPi;
            const double length = 8 + random.unit() * 30;
            const double bend = (random.unit() - 0.5) * length * 0.35;
            const double ex = sx + std::cos(angle) * length;
            const double ey = sy + std::sin(angle) * length;
            const double cx = (sx + ex) / 2 - std::sin(angle) * bend;
            const double cy = (sy + ey) / 2 + std::cos(angle) * bend;
            QPen pen(QColor::fromRgbF(0, 0, 0, float(0.02 + random.unit() * 0.035)));
            pen.setWidthF(0.6 + random.unit() * 0.9);
            pen.setCapStyle(Qt::RoundCap);
            p.setPen(pen);
            for (double dx : {-extent, 0.0, extent}) {
                for (double dy : {-extent, 0.0, extent}) {
                    QPainterPath path(QPointF(sx + dx, sy + dy));
                    path.quadTo(QPointF(cx + dx, cy + dy), QPointF(ex + dx, ey + dy));
                    p.drawPath(path);
                }
            }
        }
    }

    // Speckle: sparse single pixels in a few strengths, so the tile compresses.
    QByteArray out(qsizetype(size) * size, char(255));
    for (int y = 0; y < size; ++y)
        std::memcpy(out.data() + qsizetype(y) * size, mask.constScanLine(y), size_t(size));
    auto *pixels = reinterpret_cast<uint8_t *>(out.data());
    const uint64_t count = uint64_t(size) * uint64_t(size);
    for (uint64_t i = 0; i < count / 10; ++i) {
        const uint64_t offset = random.next() % count;
        const uint8_t value = uint8_t(249 - 5 * (random.next() % 3));
        pixels[offset] = std::min(pixels[offset], value);
    }
    return out;
}

const QImage &tile()
{
    static const QImage image = [] {
        const QByteArray mask = coverage(0x5EEDB1E9ULL, pixels);
        // Not premultiplied, so a PDF stores constant white and the mask as its soft mask.
        QImage t(pixels, pixels, QImage::Format_ARGB32);
        for (int y = 0; y < pixels; ++y) {
            auto *line = reinterpret_cast<QRgb *>(t.scanLine(y));
            for (int x = 0; x < pixels; ++x)
                line[x] = qRgba(255, 255, 255, 255 - uint8_t(mask[qsizetype(y) * pixels + x]));
        }
        return t;
    }();
    return image;
}

} // namespace PaperTexture

// MARK: - BlueprintBackground

namespace BlueprintBackground {

void draw(QPainter &painter, const QRectF &rect, const BlueprintPalette &palette, const Options &options)
{
    if (rect.width() <= 0 || rect.height() <= 0)
        return;
    painter.save();
    painter.setClipRect(rect, Qt::IntersectClip);
    painter.fillRect(rect, palette.pageBackground);
    if (options.gradient)
        drawGradient(painter, rect, palette.pageBackground);
    if (options.texture)
        drawTexture(painter, rect);
    const bool framed = options.frame || options.titleBlock.has_value();
    const QRectF inner = framed ? rect.adjusted(frameInset, frameInset, -frameInset, -frameInset) : rect;
    if (options.grid)
        drawGrid(painter, inner, palette);
    if (framed) {
        QPen pen(ruleColor(palette));
        pen.setWidthF(0.75);
        painter.setPen(pen);
        painter.setBrush(Qt::NoBrush);
        painter.drawRect(inner);
    }
    painter.restore();
    if (options.titleBlock)
        drawTitleBlock(painter, *options.titleBlock, rect, palette);
}

void drawTitleBlock(QPainter &painter, const TitleBlock &block, const QRectF &rect, const BlueprintPalette &palette)
{
    const QRectF frame = rect.adjusted(frameInset, frameInset, -frameInset, -frameInset);
    const QSizeF size = titleBlockSize;
    const QRectF box(frame.right() - size.width(), frame.bottom() - size.height(), size.width(), size.height());
    const double widths[4] = {118, 62, 42, 68};
    double edges[5] = {box.left(), 0, 0, 0, 0};
    for (int i = 0; i < 4; ++i)
        edges[i + 1] = edges[i] + widths[i];

    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.fillRect(box, palette.pageBackground);
    QPen pen(ruleColor(palette));
    pen.setWidthF(0.5);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(box);
    for (int i = 1; i <= 3; ++i)
        painter.drawLine(QPointF(edges[i], box.top()), QPointF(edges[i], box.bottom()));

    const QFont caption = titleFont(5, QFont::Medium, 0.8);
    const QFont value = titleFont(8, QFont::DemiBold, 0);
    const QFont mark = titleFont(7, QFont::ExtraBold, 1);
    const QString cells[3][2] = {
        {QStringLiteral("TITLE"), block.title},
        {QStringLiteral("DATE"), block.date.toLocalTime().date().toString(QStringLiteral("yyyy-MM-dd"))},
        {QStringLiteral("SHEET"), QStringLiteral("%1 / %2").arg(block.page).arg(block.pageCount)},
    };
    const double pad = 5;
    for (int i = 0; i < 3; ++i) {
        const double width = widths[i] - 2 * pad;
        drawTitleText(painter, cells[i][0], caption, palette.muted, width, QPointF(edges[i] + pad, box.top() + 9));
        drawTitleText(painter, cells[i][1], value, palette.text, width, QPointF(edges[i] + pad, box.bottom() - 7));
    }
    const QString name = QStringLiteral("WHITEPRINT");
    const double markWidth = QFontMetricsF(mark).horizontalAdvance(name) - 1;
    const QRectF markCell(edges[3], box.top(), widths[3], box.height());
    drawTitleText(painter, name, mark, palette.text, markCell.width(),
                  QPointF(markCell.center().x() - markWidth / 2, markCell.center().y() + 2.6));
    painter.restore();
}

} // namespace BlueprintBackground

} // namespace wp
