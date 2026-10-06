#include "render/PDFExporter.h"

#include "render/BlueprintBackground.h"
#include "render/Fonts.h"
#include "render/MarkdownStyler.h"

#include <QAbstractTextDocumentLayout>
#include <QBuffer>
#include <QFontMetricsF>
#include <QLocale>
#include <QMarginsF>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFragment>
#include <cmath>
#include <memory>
#include <vector>

namespace wp {

namespace PDFExporter {

namespace {

std::optional<QString> currentRegion()
{
    const QLocale locale;
    if (locale.territory() == QLocale::AnyTerritory)
        return std::nullopt;
    return QLocale::territoryToCode(locale.territory());
}

/// The text of one note page as a laid-out document.
std::unique_ptr<QTextDocument> typeset(const QString &text, const std::optional<QString> &title, bool first,
                                       const BlueprintPalette &palette, const QSizeF &pageSize, QPaintDevice *device)
{
    std::unique_ptr<QTextDocument> doc = MarkdownStyler::presentation(text, palette, fontSize);
    if (first && title && !text.startsWith(QStringLiteral("# ") + *title)) {
        const bool followedByText = !text.isEmpty();
        QTextBlockFormat originalBlock = doc->firstBlock().blockFormat();
        QTextCharFormat originalChar = doc->firstBlock().charFormat();
        QFont titleFont = fonts::system(26, QFont::Bold);
        QTextCharFormat titleChar;
        titleChar.setFont(titleFont);
        titleChar.setForeground(QBrush(palette.text));
        QTextBlockFormat titleBlock;
        titleBlock.setBottomMargin(fontSize * 1.6);
        QTextCursor cursor(doc.get());
        cursor.beginEditBlock();
        cursor.setPosition(0);
        cursor.insertText(*title, titleChar);
        if (followedByText)
            cursor.insertBlock(originalBlock, originalChar);
        cursor.setPosition(0);
        cursor.setBlockFormat(titleBlock);
        cursor.setBlockCharFormat(titleChar);
        cursor.endEditBlock();
    }
    doc->documentLayout()->setPaintDevice(device);
    doc->setPageSize(QSizeF(pageSize.width() - 2 * margin, pageSize.height() - 2 * margin));
    return doc;
}

void drawRules(QPainter &painter, QTextDocument &doc, const BlueprintPalette &palette, double pageTop, double pageHeight, double width)
{
    QAbstractTextDocumentLayout *layout = doc.documentLayout();
    for (QTextBlock block = doc.firstBlock(); block.isValid(); block = block.next()) {
        const auto it = block.begin();
        if (it.atEnd() || !it.fragment().charFormat().boolProperty(MarkdownStyler::ruleProperty))
            continue;
        const QTextLayout *textLayout = block.layout();
        if (!textLayout || textLayout->lineCount() == 0)
            continue;
        const QTextLine line = textLayout->lineAt(0);
        const double mid = textLayout->position().y() + line.y() + line.height() / 2;
        if (mid < pageTop || mid >= pageTop + pageHeight)
            continue;
        const double y = std::round(mid - pageTop + margin) + 0.25;
        QPen pen(withAlpha(palette.muted, palette.muted.alphaF() * 0.5));
        pen.setWidthF(0.5);
        painter.setPen(pen);
        painter.drawLine(QPointF(margin, y), QPointF(margin + width, y));
    }
    Q_UNUSED(layout)
}

} // namespace

QSizeF paperSize(const std::optional<QString> &region)
{
    return region && (*region == QLatin1String("US") || *region == QLatin1String("CA")) ? letter : a4;
}

QStringList pageTexts(const Note &note)
{
    QStringList texts;
    for (const NotePage &page : note.pages()) {
        QStringList parts;
        for (const NoteBlock &block : page.blocks)
            if (block.kind == BlockKind::text)
                parts.append(block.text);
        texts.append(parts.join(QLatin1String("\n\n")));
    }
    QStringList kept;
    for (qsizetype i = 0; i < texts.size(); ++i) {
        const QString &t = texts[i];
        const bool blank = std::all_of(t.begin(), t.end(), [](QChar c) { return c.isSpace(); });
        if (i == 0 || !blank)
            kept.append(t);
    }
    return kept;
}

QByteArray data(const Note &note, PDFExportStyle style, const QDateTime &date)
{
    const BlueprintPalette palette = style == PDFExportStyle::blueprint ? BlueprintPalette::blueprint() : BlueprintPalette::print();
    const QSizeF pageSize = paperSize(currentRegion());
    std::optional<QString> title = note.frontMatter.title();
    if (title && title->isEmpty())
        title.reset();

    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    QPdfWriter writer(&buffer);
    writer.setResolution(72);
    writer.setPageSize(QPageSize(pageSize, QPageSize::Point));
    writer.setPageMargins(QMarginsF(0, 0, 0, 0), QPageLayout::Point);
    writer.setCreator(QStringLiteral("Whiteprint"));
    if (title)
        writer.setTitle(*title);

    const double textWidth = pageSize.width() - 2 * margin;
    const double textHeight = pageSize.height() - 2 * margin;

    // Lay out every note page first, so each PDF page knows the page count.
    struct Sheet {
        QTextDocument *doc;
        int page;
    };
    std::vector<std::unique_ptr<QTextDocument>> docs;
    std::vector<Sheet> sheets;
    const QStringList texts = pageTexts(note);
    for (qsizetype i = 0; i < texts.size(); ++i) {
        docs.push_back(typeset(texts[i], title, i == 0, palette, pageSize, &writer));
        const int pages = std::max(1, docs.back()->pageCount());
        for (int p = 0; p < pages; ++p)
            sheets.push_back({docs.back().get(), p});
    }

    QPainter painter(&writer);
    painter.setRenderHint(QPainter::Antialiasing);
    painter.setRenderHint(QPainter::TextAntialiasing);
    const QRectF paper(QPointF(0, 0), pageSize);
    for (size_t index = 0; index < sheets.size(); ++index) {
        if (index > 0)
            writer.newPage();
        const Sheet &sheet = sheets[index];
        BlueprintBackground::TitleBlock block;
        block.title = title.value_or(QStringLiteral("Untitled"));
        block.page = int(index) + 1;
        block.pageCount = int(sheets.size());
        block.date = date;

        if (style == PDFExportStyle::blueprint)
            BlueprintBackground::draw(painter, paper, palette, BlueprintBackground::Options(true, true, true, true));
        else
            BlueprintBackground::draw(painter, paper, palette, BlueprintBackground::Options::plain());

        const double top = sheet.page * textHeight;
        painter.save();
        painter.setClipRect(QRectF(margin, margin, textWidth, textHeight));
        painter.translate(margin, margin - top);
        QAbstractTextDocumentLayout::PaintContext context;
        context.clip = QRectF(0, top, textWidth, textHeight);
        context.palette.setColor(QPalette::Text, palette.text);
        sheet.doc->documentLayout()->draw(&painter, context);
        painter.restore();
        painter.save();
        drawRules(painter, *sheet.doc, palette, top, textHeight, textWidth);
        painter.restore();

        if (style == PDFExportStyle::blueprint) {
            BlueprintBackground::drawTitleBlock(painter, block, paper, palette);
        } else {
            const QFont font = fonts::system(9);
            const double h = QFontMetricsF(font).height();
            painter.save();
            painter.setFont(font);
            painter.setPen(palette.muted);
            painter.drawText(QRectF(0, pageSize.height() - margin / 2 - h / 2, pageSize.width(), h),
                             Qt::AlignHCenter | Qt::AlignTop, QString::number(block.page));
            painter.restore();
        }
    }
    painter.end();
    buffer.close();
    return bytes;
}

} // namespace PDFExporter

} // namespace wp
