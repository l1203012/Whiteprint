#include "render/PageThumbnail.h"

#include "core/DrawingCompiler.h"
#include "render/Fonts.h"
#include "render/MarkdownSyntax.h"
#include "render/SceneRenderer.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <algorithm>
#include <cmath>
#include <vector>

namespace wp {

namespace PageThumbnail {

QString plainText(const QString &line, int markerLength)
{
    const QString body = line.mid(std::min(markerLength, int(line.size())));
    std::vector<bool> keep(size_t(body.size()), true);
    for (const MarkdownSpan &span : MarkdownSyntax::spans(body))
        for (const TextRange &markup : span.markupRanges())
            for (int i = markup.location; i < markup.end(); ++i)
                keep[size_t(i)] = false;
    QString kept;
    for (int i = 0; i < int(body.size()); ++i)
        if (keep[size_t(i)])
            kept.append(body.at(i));
    return kept;
}

namespace {

/// Places a page's blocks top to bottom in page coordinates until it runs out of room.
class ThumbnailLayout
{
public:
    ThumbnailLayout(QPainter &painter, const BlueprintPalette &palette, double scale, double bottom)
        : painter(painter), palette(palette), scale(scale), bottom(bottom)
    {
    }

    bool isFull() const { return y >= bottom; }

    void addText(const QString &text)
    {
        std::optional<MarkdownFenceState> fence;
        const QStringList lines = text.split(QLatin1Char('\n'));
        for (const QString &line : lines) {
            if (isFull())
                break;
            const auto classified = MarkdownSyntax::classify(line, fence);
            fence = classified.openFence;
            addLine(line, classified.line);
        }
        y += fontSize * 0.6;
    }

    void addDrawing(const QString &source)
    {
        const DrawingScene scene = DrawingCompiler::compile(source).scene;
        const QSizeF canvas = SceneRenderer::canvasSize(scene);
        if (canvas.width() <= 2 * SceneRenderer::margin)
            return;
        const double fit = std::min(1.0, width / canvas.width());
        painter.save();
        painter.translate(padding - SceneRenderer::margin * fit, y);
        painter.scale(fit, fit);
        SceneRenderer::draw(scene, painter, palette);
        painter.restore();
        y += canvas.height() * fit + fontSize * 0.6;
    }

private:
    QPainter &painter;
    BlueprintPalette palette;
    double scale;
    double bottom;
    double y = padding;
    double width = pageWidth - 2 * padding;

    void addLine(const QString &line, const MarkdownLine &parsed)
    {
        double size = fontSize;
        QColor color = palette.text;
        QFont::Weight weight = QFont::Normal;
        switch (parsed.kind) {
        case MarkdownLineKind::blank:
            y += fontSize * 0.8;
            return;
        case MarkdownLineKind::fence:
            return;
        case MarkdownLineKind::heading:
            size = parsed.level == 1 ? 28 : parsed.level == 2 ? 22 : parsed.level == 3 ? 18 : fontSize;
            weight = QFont::DemiBold;
            break;
        case MarkdownLineKind::quote:
        case MarkdownLineKind::divider:
            color = palette.muted;
            break;
        case MarkdownLineKind::task:
            if (parsed.checked)
                color = palette.muted;
            break;
        case MarkdownLineKind::paragraph:
        case MarkdownLineKind::bullet:
        case MarkdownLineKind::ordered:
        case MarkdownLineKind::code:
            break;
        }
        const QString display = plainText(line, parsed.isHeading() ? parsed.markerLength : 0);
        const QFont font = parsed.kind == MarkdownLineKind::code ? fonts::monospaced(size * 0.87) : fonts::system(size, weight);
        const double lineHeight = std::ceil(size * 1.4);
        const double textWidth = QFontMetricsF(font).horizontalAdvance(display);
        const int rows = std::max(1, std::min(3, int(std::ceil(textWidth / width))));
        if (size * scale >= minimumTextSize) {
            painter.save();
            const QRectF box(padding, y, width, lineHeight * rows);
            painter.setClipRect(box, Qt::IntersectClip);
            painter.setFont(font);
            painter.setPen(color);
            painter.drawText(box, Qt::AlignLeft | Qt::AlignTop | Qt::TextWordWrap, display);
            painter.restore();
        } else {
            drawBars(textWidth, rows, size, lineHeight, color);
        }
        y += lineHeight * rows;
    }

    /// Rounded bars standing in for text too small to read.
    void drawBars(double textWidth, int rows, double size, double lineHeight, const QColor &color) const
    {
        const double height = size * 0.55;
        QPainterPath path;
        for (int row = 0; row < rows; ++row) {
            const double remaining = textWidth - row * width;
            const double barWidth = std::max(height, std::min(width, remaining));
            const QRectF rect(padding, y + row * lineHeight + (lineHeight - height) / 2, barWidth, height);
            path.addRoundedRect(rect, height / 2, height / 2);
        }
        painter.save();
        painter.setPen(Qt::NoPen);
        painter.fillPath(path, withAlpha(color, color.alphaF() * 0.6));
        painter.restore();
    }
};

/// The faint square grid, as hairlines `spacing` apart.
void strokeGrid(QPainter &painter, QSizeF size, double spacing, const QColor &color)
{
    QPainterPath path;
    for (double x = spacing; x < size.width(); x += spacing) {
        path.moveTo(x, 0);
        path.lineTo(x, size.height());
    }
    for (double y = spacing; y < size.height(); y += spacing) {
        path.moveTo(0, y);
        path.lineTo(size.width(), y);
    }
    painter.save();
    QPen pen(color);
    pen.setWidthF(0.5);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPath(path);
    painter.restore();
}

} // namespace

void draw(const NotePage &page, QPainter &painter, QSizeF size, const BlueprintPalette &palette)
{
    if (size.width() <= 0 || size.height() <= 0)
        return;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    painter.setClipRect(QRectF(QPointF(0, 0), size), Qt::IntersectClip);
    painter.fillRect(QRectF(QPointF(0, 0), size), palette.pageBackground);
    const double scale = size.width() / pageWidth;
    double spacing = SceneRenderer::unit * scale;
    while (spacing < 4)
        spacing *= 2;
    strokeGrid(painter, size, spacing, palette.grid);

    painter.scale(scale, scale);
    ThumbnailLayout layout(painter, palette, scale, size.height() / scale - padding / 2);
    for (const NoteBlock &block : page.blocks) {
        if (layout.isFull())
            break;
        switch (block.kind) {
        case BlockKind::text:
            layout.addText(block.text);
            break;
        case BlockKind::drawing:
            layout.addDrawing(block.drawing.source);
            break;
        case BlockKind::cards:
            layout.addText(QStringLiteral("\U0001F5C2 ") + block.deck.title.value_or(QStringLiteral("Flashcards"))
                           + QStringLiteral(" · %1 cards").arg(block.deck.cards.size()));
            break;
        }
    }
    painter.restore();
}

QImage image(const NotePage &page, QSize size, const BlueprintPalette &palette, double devicePixelRatio)
{
    QImage out(QSize(int(std::ceil(size.width() * devicePixelRatio)), int(std::ceil(size.height() * devicePixelRatio))),
               QImage::Format_ARGB32_Premultiplied);
    out.setDevicePixelRatio(devicePixelRatio);
    out.fill(Qt::transparent);
    QPainter painter(&out);
    draw(page, painter, QSizeF(size), palette);
    return out;
}

} // namespace PageThumbnail

} // namespace wp
