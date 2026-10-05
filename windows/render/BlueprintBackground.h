#pragma once
#include "render/Palette.h"

#include <QByteArray>
#include <QDateTime>
#include <QImage>
#include <QRectF>
#include <QSizeF>
#include <QString>
#include <cstdint>
#include <optional>

class QPainter;

namespace wp {

/// A blueprint sheet: the page colour with a soft vignette gradient, a
/// fine paper-fibre texture, a 10 pt / 50 pt grid, and optionally a drawing
/// frame with a title block in the bottom-right corner.
///
/// On a PDF the grid and the texture are tiling patterns, so the sheet stays
/// small and does not break up the copied text of whatever is drawn on top.
namespace BlueprintBackground {

/// Points between minor grid lines.
inline constexpr double minorSpacing = 10;
/// Points between major grid lines.
inline constexpr double majorSpacing = 50;
/// Distance from the sheet's edge to the drawing frame.
inline constexpr double frameInset = 20;
/// Size of the title block in the frame's bottom-right corner.
inline const QSizeF titleBlockSize{290, 30};

struct TitleBlock {
    QString title;
    int page = 1;
    int pageCount = 1;
    QDateTime date;
};

struct Options {
    /// Lighter towards the centre, deeper towards the edges.
    bool gradient = true;
    bool texture = true;
    bool grid = true;
    /// A thin rule `frameInset` inside the edge; the grid stays inside it.
    bool frame = false;
    /// Drawn in the frame's bottom-right corner; implies `frame`. In a PDF,
    /// leave it out and call `drawTitleBlock` after the page's text, so
    /// copied text starts with the page's own.
    std::optional<TitleBlock> titleBlock;

    Options() = default;
    Options(bool gradient, bool texture, bool grid, bool frame = false, std::optional<TitleBlock> titleBlock = std::nullopt)
        : gradient(gradient), texture(texture), grid(grid), frame(frame), titleBlock(std::move(titleBlock))
    {
    }

    /// The page colour alone.
    static Options plain() { return Options(false, false, false); }
};

/// Fills `rect` of a y-down `painter` with the sheet.
void draw(QPainter &painter, const QRectF &rect, const BlueprintPalette &palette, const Options &options = Options());

/// Draws the title block of a sheet filling `rect` of a y-down `painter`:
/// title | date | sheet | WHITEPRINT, each cell a small caption over its value.
void drawTitleBlock(QPainter &painter, const TitleBlock &block, const QRectF &rect, const BlueprintPalette &palette);

} // namespace BlueprintBackground

/// A small, seamless tile of paper fibres and speckle: white through an
/// 8-bit mask generated from a fixed seed.
namespace PaperTexture {

/// Tile size in points; the image has two pixels per point.
inline constexpr double points = 96;
inline constexpr int pixels = 192;

/// White, with the mask as alpha. Built once.
const QImage &tile();
/// Mask samples, `size * size` bytes: 255 leaves the paper bare, lower values let white through.
QByteArray coverage(uint64_t seed, int size);

} // namespace PaperTexture

/// A small deterministic pseudo-random generator.
struct SplitMix64 {
    uint64_t state;

    explicit SplitMix64(uint64_t seed) : state(seed) {}
    uint64_t next();
    /// A value in 0..<1.
    double unit();
};

} // namespace wp
