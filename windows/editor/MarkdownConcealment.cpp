#include "editor/MarkdownConcealment.h"

#include <QAbstractTextDocumentLayout>
#include <QFontMetricsF>
#include <QPainter>
#include <QPainterPath>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextLayout>
#include <cmath>
#include <vector>

namespace wp {

namespace ConcealedGlyphs {

using MarkdownStyler::Markup;

bool isZeroWidth(Markup markup)
{
    return markup == Markup::hidden || markup == Markup::rule;
}

QChar bulletCharacter()
{
    return QChar(0x2022);
}

void applyZeroWidth(QTextDocument *document)
{
    struct Run {
        int from, to;
        QTextCharFormat format;
    };
    std::vector<Run> runs;
    for (QTextBlock block = document->firstBlock(); block.isValid(); block = block.next()) {
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid())
                continue;
            const QTextCharFormat format = fragment.charFormat();
            const auto markup = MarkdownStyler::markupOf(format);
            if (!markup || *markup == Markup::checkbox || *markup == Markup::checkedBox)
                continue;
            const int start = fragment.position();
            if (*markup == Markup::bullet) {
                if (format.foreground().color().alpha() != 0) {
                    QTextCharFormat f = format;
                    f.setForeground(QBrush(Qt::transparent));
                    runs.push_back({start, start + fragment.length(), f});
                }
                continue;
            }
            if (format.fontLetterSpacingType() == QFont::AbsoluteSpacing && format.fontLetterSpacing() < 0)
                continue;
            const QFontMetricsF metrics(format.font());
            const QString text = fragment.text();
            for (int i = 0; i < text.size();) {
                const int units = text[i].isHighSurrogate() && i + 1 < text.size() ? 2 : 1;
                QTextCharFormat f = format;
                f.setFontLetterSpacingType(QFont::AbsoluteSpacing);
                f.setFontLetterSpacing(-metrics.horizontalAdvance(text.mid(i, units)));
                f.setForeground(QBrush(Qt::transparent));
                runs.push_back({start + i, start + i + units, f});
                i += units;
            }
        }
    }
    if (runs.empty())
        return;
    QTextCursor cursor(document);
    cursor.beginEditBlock();
    for (const Run &run : runs) {
        cursor.setPosition(run.from);
        cursor.setPosition(run.to, QTextCursor::KeepAnchor);
        cursor.setCharFormat(run.format);
    }
    cursor.endEditBlock();
}

QRectF characterRect(const QTextDocument *document, int index, double *ascent)
{
    const QTextBlock block = document->findBlock(index);
    if (!block.isValid())
        return {};
    const QTextLayout *layout = block.layout();
    if (!layout || layout->lineCount() == 0)
        return {};
    const int relative = index - block.position();
    const QTextLine line = layout->lineForTextPosition(relative);
    if (!line.isValid())
        return {};
    const double x1 = line.cursorToX(relative);
    const double x2 = line.cursorToX(relative + 1);
    if (ascent)
        *ascent = line.ascent();
    return QRectF(layout->position().x() + std::min(x1, x2), layout->position().y() + line.y(), std::abs(x2 - x1), line.height());
}

void draw(QPainter &painter, const QTextDocument *document, const BlueprintPalette &palette, const QRectF &exposed)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    const double textWidth = document->textWidth();
    for (QTextBlock block = document->firstBlock(); block.isValid(); block = block.next()) {
        const QRectF bounds = document->documentLayout()->blockBoundingRect(block);
        if (bounds.bottom() < exposed.top())
            continue;
        if (bounds.top() > exposed.bottom())
            break;
        bool ruleDrawn = false;
        for (auto it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            if (!fragment.isValid())
                continue;
            const QTextCharFormat format = fragment.charFormat();
            const auto markup = MarkdownStyler::markupOf(format);
            if (!markup || *markup == Markup::hidden)
                continue;
            double ascent = 0;
            const QRectF rect = characterRect(document, fragment.position(), &ascent);
            if (rect.isEmpty() && rect.height() == 0)
                continue;
            const double baseline = rect.top() + ascent;
            const QFont font = format.font();
            switch (*markup) {
            case Markup::bullet: {
                painter.setFont(font);
                painter.setPen(palette.text);
                const QString bullet(bulletCharacter());
                const QFontMetricsF metrics(font);
                const double width = metrics.horizontalAdvance(bullet);
                painter.drawText(QPointF(rect.left() + std::max(0.0, (rect.width() - width) / 2), baseline), bullet);
                break;
            }
            case Markup::checkbox:
            case Markup::checkedBox: {
                const bool checked = *markup == Markup::checkedBox;
                const QFontMetricsF metrics(font);
                const double capHeight = metrics.capHeight();
                const double side = std::round(capHeight * 1.3);
                const QRectF box(std::round(rect.left() + 1) + 0.5, std::round(baseline - capHeight / 2 - side / 2) + 0.5, side - 1, side - 1);
                if (checked) {
                    painter.setPen(Qt::NoPen);
                    painter.setBrush(palette.accent);
                    painter.drawRoundedRect(box, 3, 3);
                    QPainterPath tick;
                    tick.moveTo(box.left() + side * 0.24, box.center().y() + side * 0.02);
                    tick.lineTo(box.left() + side * 0.42, box.bottom() - side * 0.26);
                    tick.lineTo(box.right() - side * 0.22, box.top() + side * 0.26);
                    QPen pen(palette.pageBackground, 1.7);
                    pen.setCapStyle(Qt::RoundCap);
                    pen.setJoinStyle(Qt::RoundJoin);
                    painter.setPen(pen);
                    painter.setBrush(Qt::NoBrush);
                    painter.drawPath(tick);
                } else {
                    painter.setPen(QPen(withAlpha(palette.text, 0.75), 1.2));
                    painter.setBrush(Qt::NoBrush);
                    painter.drawRoundedRect(box, 3, 3);
                }
                break;
            }
            case Markup::rule: {
                if (ruleDrawn)
                    break;
                ruleDrawn = true;
                const double y = std::round(rect.top() + rect.height() / 2) + 0.5;
                painter.setPen(QPen(withAlpha(palette.text, 0.35), 1));
                painter.drawLine(QPointF(0, y), QPointF(textWidth, y));
                break;
            }
            case Markup::hidden: break;
            }
        }
    }
    painter.restore();
}

} // namespace ConcealedGlyphs

} // namespace wp
