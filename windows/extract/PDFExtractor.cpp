#include "extract/PDFExtractor.h"

#include "extract/TextRecognizer.h"

#include <QFileInfo>
#include <QPainter>
#include <QPdfDocument>
#include <algorithm>

namespace wp::PDFExtractor {

int nonWhitespaceCount(const QString &text) {
    int n = 0;
    for (QChar c : text)
        if (!c.isSpace()) ++n;
    return n;
}

QImage render(QPdfDocument &document, int index) {
    // pagePointSize already accounts for the page rotation.
    QSizeF box = document.pagePointSize(index);
    if (box.width() < 1 || box.height() < 1) return {};
    const double scale = std::min(2.0, maxRenderSide / std::max(box.width(), box.height()));
    const QSize size(std::max(1, int(box.width() * scale)), std::max(1, int(box.height() * scale)));
    QImage page = document.render(index, size);
    if (page.isNull()) return {};
    QImage flat(page.size(), QImage::Format_Grayscale8);
    flat.fill(Qt::white);
    QPainter painter(&flat);
    painter.drawImage(0, 0, page);
    painter.end();
    return flat;
}

std::vector<ExtractedUnit> extract(const QString &path, bool ocr) {
    const QString name = QFileInfo(path).fileName();
    QPdfDocument document;
    if (document.load(path) != QPdfDocument::Error::None || document.status() != QPdfDocument::Status::Ready)
        throw ExtractionError::unreadable(name);

    const int pageCount = document.pageCount();
    std::vector<ExtractedUnit> units;
    units.reserve(size_t(pageCount));
    for (int index = 0; index < pageCount; ++index) {
        QString text = document.getAllText(index).text();
        // pdfium marks a line-ending hyphen with U+FFFE and drops the line break; put both back so
        // TextCleaner treats it like any other hyphenated line break.
        text.replace(QChar(0xFFFE), QStringLiteral("-\n"));
        if (ocr && nonWhitespaceCount(text) < ocrThreshold) {
            QImage image = render(document, index);
            if (!image.isNull()) {
                QString recognized = TextRecognizer::recognize(image);
                if (nonWhitespaceCount(recognized) > nonWhitespaceCount(text)) text = recognized;
            }
        }
        units.push_back({QStringLiteral("p. %1").arg(index + 1), text});
    }
    return units;
}

} // namespace wp::PDFExtractor
