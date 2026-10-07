#pragma once
#include <QColor>
#include <QList>
#include <QString>
#include <optional>

namespace wp {

/// Page colours. On screen the palette comes from the chosen `PageTheme`;
/// `print()` is the white-paper variant for PDF export.
struct BlueprintPalette {
    QColor pageBackground;
    QColor grid;
    QColor text;
    QColor muted;
    QColor accent;
    /// Behind the pages, around them. Invalid means the window background.
    QColor canvas;
    /// Whether pages draw the blueprint grid.
    bool showsGrid = true;
    /// The hairline around each page.
    QColor pageEdge = QColor::fromRgbF(1, 1, 1, 0.08);
    /// The opacity of the page's drop shadow.
    double shadowOpacity = 0.22;
    /// Whether each page is drawn as a sheet with an edge and shadow. Without
    /// one, text sits straight on the canvas and pages are split by a short rule.
    bool drawsSheet = true;
    /// The fill of a ticked checkbox.
    QColor checkbox;

    /// `#1E4D8C` page, white at 6% grid, white text, white at 65% muted, `#9FD3FF` accent.
    static BlueprintPalette blueprint();
    /// White paper, dark text.
    static BlueprintPalette print();
    /// Warm off-white paper and near-black text, or charcoal and soft white
    /// when `dark`; no sheet.
    static BlueprintPalette paper(bool dark = false);
    /// Cream paper, brown ink.
    static BlueprintPalette sepia();
    /// Charcoal pages, soft white text.
    static BlueprintPalette night();

    bool operator==(const BlueprintPalette &) const = default;
};

/// The look of pages on screen, chosen in View > Page Theme or Settings.
enum class PageTheme { paper, sepia, blueprint, night };

namespace PageThemes {

constexpr PageTheme defaultTheme = PageTheme::paper;

/// Every theme, in menu order.
QList<PageTheme> all();
/// The stored identifier (`paper`, `sepia`, `blueprint`, `night`).
QString rawValue(PageTheme theme);
std::optional<PageTheme> fromRawValue(const QString &raw);
/// Menu title (`Paper`, `Sepia`, ...).
QString title(PageTheme theme);
/// The theme's colours; `dark` picks Paper's dark-mode variant.
BlueprintPalette palette(PageTheme theme, bool dark = false);

} // namespace PageThemes

enum class PDFExportStyle {
    /// Blue pages, white text.
    blueprint,
    /// White paper, dark text.
    print
};

/// A colour with its alpha replaced by `alpha` (0...1).
QColor withAlpha(const QColor &color, double alpha);

} // namespace wp
