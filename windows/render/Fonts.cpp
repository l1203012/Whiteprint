#include "render/Fonts.h"

#include <cmath>

namespace wp::fonts {

QStringList uiFamilies()
{
    // The trailing families are glyph fallbacks (checkboxes, bullets, arrows, emoji).
    return {QStringLiteral("Segoe UI Variable Text"), QStringLiteral("Segoe UI"), QStringLiteral("Arial"),
            QStringLiteral("Segoe UI Symbol"), QStringLiteral("Segoe UI Emoji")};
}

QStringList monospaceFamilies()
{
    return {QStringLiteral("Cascadia Mono"), QStringLiteral("Consolas"), QStringLiteral("Courier New")};
}

static int pixels(double size)
{
    return std::max(1, int(std::lround(size)));
}

QFont system(double size, QFont::Weight weight, bool italic)
{
    QFont f;
    f.setFamilies(uiFamilies());
    f.setStyleHint(QFont::SansSerif);
    f.setPixelSize(pixels(size));
    f.setWeight(weight);
    f.setItalic(italic);
    return f;
}

QFont monospaced(double size, QFont::Weight weight)
{
    QFont f;
    f.setFamilies(monospaceFamilies());
    f.setStyleHint(QFont::Monospace);
    f.setFixedPitch(true);
    f.setPixelSize(pixels(size));
    f.setWeight(weight);
    return f;
}

bool isMonospaced(const QFont &font)
{
    return font.fixedPitch();
}

double sizeOf(const QFont &font)
{
    return font.pixelSize() > 0 ? font.pixelSize() : font.pointSizeF();
}

QFont withSize(const QFont &font, double size)
{
    QFont f = font;
    f.setPixelSize(pixels(size));
    return f;
}

} // namespace wp::fonts
