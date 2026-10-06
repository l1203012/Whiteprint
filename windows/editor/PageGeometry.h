#pragma once
#include <QSizeF>
#include <QString>
#include <optional>
#include <vector>

namespace wp {

/// How the editor lays out note pages.
enum class PageLayoutMode {
    /// Wide sheets that grow with their content, in a centred column.
    slides,
    /// Sheets of printer paper (A4, or US Letter in the US and Canada) with
    /// the PDF export's margins, marking where printed pages will break.
    a4
};

/// Where pages and their text column go for a given document width.
///
/// Slides: a centred column of at most 720 pt, with padding that shrinks
/// with the window. A4: the paper scaled to fit the width (at most 1.3x),
/// with the text scaled along so lines wrap as on the printed page.
struct PageGeometry {
    static constexpr double maxTextWidth = 720;
    static constexpr double maxPadding = 72;
    static constexpr double minPadding = 28;
    static constexpr double deskMargin = 24;
    /// Space above the first page and between pages.
    static constexpr double pageGap = 36;
    static constexpr double cornerRadius = 8;
    /// Room around the sheet for its shadow.
    static constexpr double shadowInset = 16;
    static constexpr double bodyFontSize = 15;

    /// Paper sizes and typesetting of the PDF export, in points.
    static QSizeF a4Paper() { return QSizeF(595.28, 841.89); }
    static QSizeF letterPaper() { return QSizeF(612, 792); }
    static constexpr double paperMargin = 56;
    static constexpr double paperFontSize = 11;
    static constexpr double maxPaperScale = 1.3;
    /// The paper for this machine's region.
    static QSizeF localPaper();

    /// US Letter in the US and Canada, A4 elsewhere (as the PDF export does).
    static QSizeF paperSize(const std::optional<QString> &region);

    PageLayoutMode mode = PageLayoutMode::slides;
    double pageWidth = 0;
    double padding = 0;
    double pageX = 0;
    double topPadding = 0;
    double bottomPadding = 0;
    double blockSpacing = 0;
    double fontSize = 0;
    /// A4: how much of one printed sheet's height holds text, on screen.
    std::optional<double> printableHeight;

    PageGeometry() : PageGeometry(0) {}
    explicit PageGeometry(double documentWidth, PageLayoutMode mode = PageLayoutMode::slides, std::optional<QSizeF> paper = std::nullopt);

    double textWidth() const { return pageWidth - 2 * padding; }

    /// Empty and short pages still look like a sheet.
    double minPageHeight() const;

    struct Sheet {
        double height = 0;
        std::vector<double> breaks;
    };
    /// The height of a sheet holding `contentHeight` of blocks, and the
    /// offsets from its top where printed pages break (A4 only).
    Sheet sheet(double contentHeight) const;

    bool operator==(const PageGeometry &) const = default;

private:
    std::optional<double> m_paperHeight;
};

/// Rough page heights for pages whose views haven't been created yet, so
/// the scroller is about right before every page is laid out.
namespace HeightEstimate {

constexpr double bodyLineHeight = 24;
constexpr double averageCharacterWidth = 7.4;

double text(const QString &text, double width, double fontSize = PageGeometry::bodyFontSize);

} // namespace HeightEstimate

} // namespace wp
