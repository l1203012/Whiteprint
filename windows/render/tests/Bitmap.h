#pragma once
// Shared helpers for the render tests (header only, so it isn't built as a test itself).
#include <QColor>
#include <QFileInfo>
#include <QImage>
#include <QPainter>
#include <QtTest>
#include <cstdlib>
#include <optional>

namespace wp::testing {

/// The offscreen platform has no fonts of its own on Windows: point it at the system's.
inline const bool systemFontsConfigured = [] {
    if (qEnvironmentVariableIsEmpty("QT_QPA_FONTDIR") && QFileInfo::exists(QStringLiteral("C:/Windows/Fonts")))
        qputenv("QT_QPA_FONTDIR", "C:/Windows/Fonts");
    return true;
}();

struct Pixel {
    int r = 0, g = 0, b = 0, a = 0;
};

/// An sRGB, y-down offscreen canvas at 1 px per point, for sampling rendered pixels.
class Bitmap
{
public:
    Bitmap(int width, int height, std::optional<QColor> background = std::nullopt)
        : image(width, height, QImage::Format_ARGB32_Premultiplied)
    {
        image.fill(background ? *background : QColor(Qt::transparent));
    }

    /// Wraps an image rendered elsewhere.
    explicit Bitmap(const QImage &source) : image(source.convertToFormat(QImage::Format_ARGB32_Premultiplied)) {}

    int width() const { return image.width(); }
    int height() const { return image.height(); }

    /// The pixel at (x, y) in y-down coordinates, as 0-255 RGBA.
    Pixel pixel(int x, int y) const
    {
        const QRgb p = image.pixel(x, y);
        return {qRed(p), qGreen(p), qBlue(p), qAlpha(p)};
    }

    /// Whether the pixel is close to `color` (within `tolerance` per channel).
    bool pixelMatches(int x, int y, const QColor &color, int tolerance = 12) const
    {
        const Pixel p = pixel(x, y);
        return std::abs(p.r - color.red()) <= tolerance && std::abs(p.g - color.green()) <= tolerance
               && std::abs(p.b - color.blue()) <= tolerance;
    }

    /// How many pixels in the bitmap are close to `color`.
    int count(const QColor &color, int tolerance = 40) const
    {
        int n = 0;
        for (int y = 0; y < height(); ++y)
            for (int x = 0; x < width(); ++x)
                if (pixelMatches(x, y, color, tolerance))
                    ++n;
        return n;
    }

    QImage image;
};

inline int brightness(const Pixel &p)
{
    return p.r + p.g + p.b;
}

} // namespace wp::testing
