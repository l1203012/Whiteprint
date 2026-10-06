#include "render/Palette.h"

namespace wp {

QColor withAlpha(const QColor &color, double alpha)
{
    QColor c = color;
    c.setAlphaF(qBound(0.0, alpha, 1.0));
    return c;
}

BlueprintPalette BlueprintPalette::blueprint()
{
    BlueprintPalette p;
    p.pageBackground = QColor(0x1E, 0x4D, 0x8C);
    p.grid = QColor::fromRgbF(1, 1, 1, 0.06);
    p.text = QColor(Qt::white);
    p.muted = QColor::fromRgbF(1, 1, 1, 0.65);
    p.accent = QColor(0x9F, 0xD3, 0xFF);
    return p;
}

BlueprintPalette BlueprintPalette::print()
{
    BlueprintPalette p;
    p.pageBackground = QColor(Qt::white);
    p.grid = QColor::fromRgbF(0, 0, 0, 0.05);
    p.text = QColor::fromRgbF(0.1, 0.1, 0.1, 1);
    p.muted = QColor::fromRgbF(0.4, 0.4, 0.4, 1);
    p.accent = QColor(0x1E, 0x4D, 0x8C);
    return p;
}

} // namespace wp
