#include "editor/PageGeometry.h"

#include "core/TextUtil.h"
#include "editor/MarkdownLine.h"

#include <QLocale>
#include <algorithm>
#include <cmath>

namespace wp {

QSizeF PageGeometry::localPaper()
{
    const auto territory = QLocale::system().territory();
    if (territory == QLocale::UnitedStates)
        return letterPaper();
    if (territory == QLocale::Canada)
        return letterPaper();
    return a4Paper();
}

QSizeF PageGeometry::paperSize(const std::optional<QString> &region)
{
    return (region == QString("US") || region == QString("CA")) ? letterPaper() : a4Paper();
}

PageGeometry::PageGeometry(double documentWidth, PageLayoutMode mode, std::optional<QSizeF> paperOpt) : mode(mode)
{
    const QSizeF paper = paperOpt.value_or(localPaper());
    const double available = std::max(documentWidth - 2 * deskMargin, 240.0);
    switch (mode) {
    case PageLayoutMode::slides:
        pageWidth = std::min(available, maxTextWidth + 2 * maxPadding);
        padding = std::min(maxPadding, std::max({minPadding, (pageWidth - maxTextWidth) / 2, pageWidth * 0.06}));
        topPadding = 60;
        bottomPadding = 72;
        blockSpacing = 12;
        fontSize = bodyFontSize;
        break;
    case PageLayoutMode::a4: {
        // Steps of 1/44 keep the font size on quarter points while resizing.
        const double scale = std::max(1.0, std::floor(std::min(available / paper.width(), maxPaperScale) * 44)) / 44;
        pageWidth = std::round(paper.width() * scale);
        padding = std::round(paperMargin * scale);
        topPadding = padding;
        bottomPadding = padding;
        fontSize = paperFontSize * scale;
        blockSpacing = std::round(12 * fontSize / bodyFontSize);
        printableHeight = (paper.height() - 2 * paperMargin) * scale;
        m_paperHeight = std::round(paper.height() * scale);
        break;
    }
    }
    pageX = std::round((documentWidth - pageWidth) / 2);
}

double PageGeometry::minPageHeight() const
{
    return m_paperHeight ? *m_paperHeight : std::round(pageWidth * 0.5);
}

PageGeometry::Sheet PageGeometry::sheet(double contentHeight) const
{
    if (!printableHeight || !m_paperHeight)
        return {std::max(topPadding + contentHeight + bottomPadding, minPageHeight()), {}};
    const double printable = *printableHeight;
    const int sheets = std::max(1, int(std::ceil(contentHeight / printable - 0.001)));
    Sheet result;
    for (int i = 1; i < sheets; ++i)
        result.breaks.push_back(topPadding + i * printable);
    result.height = std::round(*m_paperHeight + (sheets - 1) * printable);
    return result;
}

namespace HeightEstimate {

double text(const QString &text, double width, double fontSize)
{
    const double size = fontSize / PageGeometry::bodyFontSize;
    const int perLine = std::max(1, int(width / (averageCharacterWidth * size)));
    double height = 0;
    const QStringList lines = text.split(QLatin1Char('\n'), Qt::KeepEmptyParts);
    for (const QString &line : lines) {
        double scale = 1;
        const LineKind kind = MarkdownLinePrefix(line).kind;
        if (kind.type == LineKind::Type::heading) {
            if (kind.level == 1)
                scale = 1.9;
            else if (kind.level == 2)
                scale = 1.5;
            else if (kind.level == 3)
                scale = 1.25;
        }
        const int wrapped = std::max(1, (int(graphemeCount(line) * scale) + perLine - 1) / perLine);
        height += wrapped * bodyLineHeight * scale * size;
    }
    return std::max(height, bodyLineHeight * size);
}

} // namespace HeightEstimate

} // namespace wp
