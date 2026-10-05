#pragma once
#include <QColor>

namespace wp {

/// Page colours. The blueprint palette is used on screen in both light and
/// dark mode; `print()` is the white-paper variant for PDF export.
struct BlueprintPalette {
    QColor pageBackground;
    QColor grid;
    QColor text;
    QColor muted;
    QColor accent;

    /// `#1E4D8C` page, white at 6% grid, white text, white at 65% muted, `#9FD3FF` accent.
    static BlueprintPalette blueprint();
    /// White paper, dark text.
    static BlueprintPalette print();

    bool operator==(const BlueprintPalette &) const = default;
};

enum class PDFExportStyle {
    /// Blue pages, white text.
    blueprint,
    /// White paper, dark text.
    print
};

/// A colour with its alpha replaced by `alpha` (0...1).
QColor withAlpha(const QColor &color, double alpha);

} // namespace wp
