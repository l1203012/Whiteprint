#include "render/Palette.h"

namespace wp {

namespace {

QColor rgb(QRgb hex)
{
    return QColor::fromRgb(hex);
}

/// A theme without a sheet: the page is the canvas, with no grid, edge or shadow.
BlueprintPalette plain(const QColor &page, const QColor &text, const QColor &muted, const QColor &accent, const QColor &edge)
{
    BlueprintPalette p;
    p.pageBackground = page;
    p.grid = QColor(Qt::transparent);
    p.text = text;
    p.muted = muted;
    p.accent = accent;
    p.canvas = page;
    p.showsGrid = false;
    p.pageEdge = edge;
    p.shadowOpacity = 0;
    p.drawsSheet = false;
    p.checkbox = accent;
    return p;
}

} // namespace

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
    p.checkbox = p.accent;
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
    p.checkbox = p.accent;
    return p;
}

BlueprintPalette BlueprintPalette::paper(bool dark)
{
    if (dark)
        return plain(rgb(0x1E1E20), rgb(0xECECEE), rgb(0x98989D), rgb(0x4C9AFF), QColor::fromRgbF(1, 1, 1, 0.12));
    return plain(rgb(0xFCFBF9), rgb(0x1D1D1F), rgb(0x86868B), rgb(0x0A66D8), QColor::fromRgbF(0, 0, 0, 0.09));
}

BlueprintPalette BlueprintPalette::sepia()
{
    return plain(rgb(0xF8F1E3), rgb(0x3A3026), rgb(0x8A7A66), rgb(0x9C5B2E), QColor::fromRgbF(0, 0, 0, 0.1));
}

BlueprintPalette BlueprintPalette::night()
{
    return plain(rgb(0x1F1F22), rgb(0xE8E8EA), rgb(0x8E8E93), rgb(0x6CA8FF), QColor::fromRgbF(1, 1, 1, 0.12));
}

namespace PageThemes {

QList<PageTheme> all()
{
    return {PageTheme::paper, PageTheme::sepia, PageTheme::blueprint, PageTheme::night};
}

QString rawValue(PageTheme theme)
{
    switch (theme) {
    case PageTheme::paper: return QStringLiteral("paper");
    case PageTheme::sepia: return QStringLiteral("sepia");
    case PageTheme::blueprint: return QStringLiteral("blueprint");
    case PageTheme::night: return QStringLiteral("night");
    }
    return QString();
}

std::optional<PageTheme> fromRawValue(const QString &raw)
{
    for (PageTheme theme : all()) {
        if (rawValue(theme) == raw)
            return theme;
    }
    return std::nullopt;
}

QString title(PageTheme theme)
{
    switch (theme) {
    case PageTheme::paper: return QStringLiteral("Paper");
    case PageTheme::sepia: return QStringLiteral("Sepia");
    case PageTheme::blueprint: return QStringLiteral("Blueprint");
    case PageTheme::night: return QStringLiteral("Night");
    }
    return QString();
}

BlueprintPalette palette(PageTheme theme, bool dark)
{
    switch (theme) {
    case PageTheme::paper: return BlueprintPalette::paper(dark);
    case PageTheme::sepia: return BlueprintPalette::sepia();
    case PageTheme::blueprint: return BlueprintPalette::blueprint();
    case PageTheme::night: return BlueprintPalette::night();
    }
    return BlueprintPalette::paper(dark);
}

} // namespace PageThemes

} // namespace wp
