#include "extract/Extraction.h"

#include "extract/PDFExtractor.h"
#include "extract/PresentationExtractor.h"
#include "extract/RichTextExtractor.h"
#include "extract/TextCleaner.h"

#include <QFileInfo>

namespace wp {

ExtractionError::ExtractionError(Kind kind, QString name) : m_kind(kind), m_name(std::move(name)) {
    m_what = description().toUtf8();
}

QString ExtractionError::description() const {
    switch (m_kind) {
    case Kind::UnsupportedType: return m_name + QStringLiteral(": unsupported file type (use PDF, DOCX, DOC or PPTX)");
    case Kind::Unreadable: return m_name + QStringLiteral(": can't be read");
    case Kind::Empty: return m_name + QStringLiteral(": no text found");
    }
    return m_name;
}

const QSet<QString> &DocumentExtractor::supportedExtensions() {
    static const QSet<QString> set{QStringLiteral("pdf"), QStringLiteral("docx"), QStringLiteral("doc"), QStringLiteral("pptx")};
    return set;
}

ExtractedDocument DocumentExtractor::extract(const QString &path, bool ocr) {
    QFileInfo info(path);
    const QString name = info.fileName();
    const QString type = info.suffix().toLower();
    if (!supportedExtensions().contains(type)) throw ExtractionError::unsupportedType(name);
    if (!info.isFile() || !info.isReadable()) throw ExtractionError::unreadable(name);

    std::vector<ExtractedUnit> raw;
    if (type == QLatin1String("pdf")) raw = PDFExtractor::extract(path, ocr);
    else if (type == QLatin1String("docx")) raw = RichTextExtractor::extract(path, RichTextExtractor::Type::OfficeOpenXML);
    else if (type == QLatin1String("doc")) raw = RichTextExtractor::extract(path, RichTextExtractor::Type::DocFormat);
    else raw = PresentationExtractor::extract(path);

    auto units = TextCleaner::clean(raw);
    if (units.empty()) throw ExtractionError::empty(name);
    return {name, std::move(units)};
}

} // namespace wp
