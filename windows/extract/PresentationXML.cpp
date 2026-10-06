#include "extract/PresentationXML.h"

#include "extract/TextCleaner.h"

#include <QXmlStreamReader>

namespace wp {

namespace {

const QString drawingNS = QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/main");
const QString presentationNS = QStringLiteral("http://schemas.openxmlformats.org/presentationml/2006/main");
const QString compatibilityNS = QStringLiteral("http://schemas.openxmlformats.org/markup-compatibility/2006");
const QString relationshipsNS = QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships");

} // namespace

std::vector<ShapeText> ShapeTextParser::parse(const QByteArray &data, const QSet<QString> &skipped) {
    std::vector<ShapeText> shapes;
    std::vector<ShapeText> stack;
    std::optional<QString> paragraph;
    bool inText = false;
    int fallbackDepth = 0;

    auto isContainer = [](const QString &name) { return name == QLatin1String("sp") || name == QLatin1String("graphicFrame"); };

    QXmlStreamReader reader(data);
    while (!reader.atEnd()) {
        switch (reader.readNext()) {
        case QXmlStreamReader::StartElement: {
            const QString ns = reader.namespaceUri().toString();
            const QString name = reader.name().toString();
            if (fallbackDepth > 0 || (ns == compatibilityNS && name == QLatin1String("Fallback"))) {
                ++fallbackDepth;
                break;
            }
            if (ns == presentationNS && isContainer(name)) {
                stack.push_back({});
            } else if (ns == presentationNS && name == QLatin1String("ph")) {
                if (!stack.empty()) {
                    QString type = reader.attributes().value(QLatin1String("type")).toString();
                    stack.back().placeholder = reader.attributes().hasAttribute(QLatin1String("type")) ? type : QStringLiteral("body");
                }
            } else if (ns == drawingNS && name == QLatin1String("p")) {
                paragraph = QString();
            } else if (ns == drawingNS && name == QLatin1String("t")) {
                inText = true;
            } else if (ns == drawingNS && name == QLatin1String("br")) {
                if (paragraph) *paragraph += QLatin1Char('\n');
            } else if (ns == drawingNS && name == QLatin1String("tab")) {
                if (paragraph) *paragraph += QLatin1Char(' ');
            }
            break;
        }
        case QXmlStreamReader::EndElement: {
            if (fallbackDepth > 0) {
                --fallbackDepth;
                break;
            }
            const QString ns = reader.namespaceUri().toString();
            const QString name = reader.name().toString();
            if (ns == presentationNS && isContainer(name)) {
                if (stack.empty()) break;
                ShapeText shape = std::move(stack.back());
                stack.pop_back();
                bool skip = shape.placeholder && skipped.contains(*shape.placeholder);
                if (!skip && !shape.paragraphs.isEmpty()) shapes.push_back(std::move(shape));
            } else if (ns == drawingNS && name == QLatin1String("p")) {
                QString text = paragraph ? paragraph->trimmed() : QString();
                paragraph.reset();
                if (text.isEmpty()) break;
                if (stack.empty()) shapes.push_back({std::nullopt, {text}});
                else stack.back().paragraphs << text;
            } else if (ns == drawingNS && name == QLatin1String("t")) {
                inText = false;
            }
            break;
        }
        case QXmlStreamReader::Characters:
            if (inText && fallbackDepth == 0 && paragraph) *paragraph += reader.text();
            break;
        default:
            break;
        }
    }
    if (reader.hasError()) throw XMLParseFailure();
    return shapes;
}

std::vector<Relationship> RelationshipParser::parse(const QByteArray &data) {
    std::vector<Relationship> out;
    QXmlStreamReader reader(data);
    while (!reader.atEnd()) {
        if (reader.readNext() != QXmlStreamReader::StartElement) continue;
        if (reader.name() != QLatin1String("Relationship")) continue;
        const auto attrs = reader.attributes();
        if (attrs.value(QLatin1String("TargetMode")) == QLatin1String("External")) continue;
        if (!attrs.hasAttribute(QLatin1String("Id")) || !attrs.hasAttribute(QLatin1String("Type")) || !attrs.hasAttribute(QLatin1String("Target")))
            continue;
        out.push_back({attrs.value(QLatin1String("Id")).toString(), attrs.value(QLatin1String("Type")).toString(),
                       attrs.value(QLatin1String("Target")).toString()});
    }
    return out;
}

QStringList SlideListParser::parse(const QByteArray &data) {
    QStringList ids;
    QXmlStreamReader reader(data);
    while (!reader.atEnd()) {
        if (reader.readNext() != QXmlStreamReader::StartElement) continue;
        if (reader.namespaceUri() != presentationNS || reader.name() != QLatin1String("sldId")) continue;
        // The relationship id is the namespaced `r:id`, not the plain numeric `id`.
        auto id = reader.attributes().value(relationshipsNS, QLatin1String("id"));
        if (!id.isNull()) ids << id.toString();
    }
    return ids;
}

} // namespace wp
