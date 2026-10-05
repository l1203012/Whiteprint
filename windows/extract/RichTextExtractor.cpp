#include "extract/RichTextExtractor.h"

#include "extract/TextCleaner.h"
#include "extract/ZipArchive.h"

#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QXmlStreamReader>
#include <algorithm>
#include <map>
#include <optional>
#include <stdexcept>

namespace wp::RichTextExtractor {

namespace {

const QString wordNS = QStringLiteral("http://schemas.openxmlformats.org/wordprocessingml/2006/main");
const QString compatibilityNS = QStringLiteral("http://schemas.openxmlformats.org/markup-compatibility/2006");

bool isTitleLike(const QString &text) {
    if (text.size() > 120 || text.isEmpty()) return false;
    if (text.split(QLatin1Char(' '), Qt::SkipEmptyParts).size() > 14) return false;
    const QChar last = text.back();
    if (QStringLiteral(".,;:!?").contains(last) && !text.endsWith(QLatin1Char('?'))) return false;
    if (QStringLiteral("•‣◦▪-–*").contains(text.front())) return false;
    return !text.front().isLower();
}

QStringList splitLines(const QStringList &lines) {
    QStringList parts;
    QStringList current;
    int length = 0;
    for (const QString &line : lines) {
        if (!current.isEmpty() && length + 1 + line.size() > maxSectionLength) {
            parts << current.join(QLatin1Char('\n'));
            current.clear();
            length = 0;
        }
        length += (current.isEmpty() ? 0 : 1) + int(line.size());
        current << line;
    }
    if (!current.isEmpty()) parts << current.join(QLatin1Char('\n'));
    return parts;
}

QString truncated(const QString &heading) {
    if (heading.size() <= maxHeadingLength) return heading;
    return heading.left(maxHeadingLength - 1).trimmed() + QChar(0x2026);
}

// MARK: docx

struct StyleInfo {
    QString name;
    QString basedOn;
    std::optional<bool> bold;
    std::optional<double> size;
};

struct Styles {
    QHash<QString, StyleInfo> map;
    std::optional<double> defaultSize;

    template <typename F>
    auto resolve(const QString &id, F pick) const -> decltype(pick(StyleInfo{})) {
        QString current = id;
        for (int depth = 0; depth < 12 && !current.isEmpty(); ++depth) {
            auto it = map.constFind(current);
            if (it == map.constEnd()) break;
            if (auto v = pick(*it)) return v;
            current = it->basedOn;
        }
        return {};
    }

    bool isHeading(const QString &id) const {
        QString current = id;
        for (int depth = 0; depth < 12 && !current.isEmpty(); ++depth) {
            auto it = map.constFind(current);
            if (it == map.constEnd()) break;
            const QString n = it->name.toLower();
            if (n.startsWith(QLatin1String("heading")) || n == QLatin1String("title")) return true;
            current = it->basedOn;
        }
        const QString lowered = id.toLower();
        return !map.contains(id) && (lowered.startsWith(QLatin1String("heading")) || lowered == QLatin1String("title"));
    }
};

bool isOn(const QXmlStreamAttributes &attrs) {
    QString v = attrs.value(wordNS, QLatin1String("val")).toString().toLower();
    if (v.isEmpty()) v = attrs.value(QLatin1String("val")).toString().toLower();
    return v != QLatin1String("0") && v != QLatin1String("false") && v != QLatin1String("off") && v != QLatin1String("none");
}

QString attrVal(const QXmlStreamAttributes &attrs, const char *name = "val") {
    QString v = attrs.value(wordNS, QLatin1String(name)).toString();
    if (v.isEmpty()) v = attrs.value(QLatin1String(name)).toString();
    return v;
}

Styles parseStyles(const QByteArray &xml) {
    Styles styles;
    if (xml.isEmpty()) return styles;
    QXmlStreamReader reader(xml);
    bool inDefaults = false;
    int rPrDepth = 0;
    QString current;
    while (!reader.atEnd()) {
        auto token = reader.readNext();
        if (token == QXmlStreamReader::EndElement && reader.namespaceUri() == wordNS) {
            const auto name = reader.name();
            if (name == QLatin1String("docDefaults")) inDefaults = false;
            else if (name == QLatin1String("style")) current.clear();
            else if (name == QLatin1String("rPr")) rPrDepth = std::max(0, rPrDepth - 1);
            continue;
        }
        if (token != QXmlStreamReader::StartElement || reader.namespaceUri() != wordNS) continue;
        const auto name = reader.name();
        const auto attrs = reader.attributes();
        if (name == QLatin1String("docDefaults")) inDefaults = true;
        else if (name == QLatin1String("style")) {
            current = attrVal(attrs, "styleId");
            if (!current.isEmpty()) styles.map[current];
        } else if (name == QLatin1String("name") && !current.isEmpty()) styles.map[current].name = attrVal(attrs);
        else if (name == QLatin1String("basedOn") && !current.isEmpty()) styles.map[current].basedOn = attrVal(attrs);
        else if (name == QLatin1String("rPr")) ++rPrDepth;
        else if (name == QLatin1String("b") && rPrDepth > 0 && !current.isEmpty()) styles.map[current].bold = isOn(attrs);
        else if (name == QLatin1String("sz") && rPrDepth > 0) {
            bool ok = false;
            double half = attrVal(attrs).toDouble(&ok);
            if (!ok) continue;
            if (!current.isEmpty()) styles.map[current].size = half / 2;
            else if (inDefaults) styles.defaultSize = half / 2;
        }
    }
    return styles;
}

struct Run {
    QString text;
    std::optional<bool> bold;
    double size = 0;
};

struct ParagraphState {
    QString styleId;
    std::vector<Run> runs;
};

} // namespace

std::vector<RawParagraph> docxParagraphs(const QByteArray &documentXml, const QByteArray &stylesXml) {
    const Styles styles = parseStyles(stylesXml);
    std::vector<RawParagraph> out;
    std::vector<ParagraphState> stack;
    Run run;
    bool inRun = false, inRunProps = false, inText = false;
    int fallbackDepth = 0;

    auto finish = [&](ParagraphState &p) {
        QString joined;
        for (const Run &r : p.runs) joined += r.text;
        QString text = TextCleaner::collapseWhitespace(joined);
        if (text.isEmpty()) return;
        auto styleBold = styles.resolve(p.styleId, [](const StyleInfo &s) { return s.bold; });
        auto styleSize = styles.resolve(p.styleId, [](const StyleInfo &s) { return s.size; });
        const bool heading = styles.isHeading(p.styleId);
        bool allBold = true;
        double largest = 0;
        for (const Run &r : p.runs) {
            if (TextCleaner::collapseWhitespace(r.text).isEmpty()) continue;
            bool bold = r.bold ? *r.bold : styleBold.value_or(false);
            if (!bold) allBold = false;
            double size = r.size > 0 ? r.size : styleSize.value_or(styles.defaultSize.value_or(10));
            largest = std::max(largest, size);
        }
        out.push_back({text, allBold, largest, heading});
    };

    QXmlStreamReader reader(documentXml);
    while (!reader.atEnd()) {
        switch (reader.readNext()) {
        case QXmlStreamReader::StartElement: {
            const QString ns = reader.namespaceUri().toString();
            const auto name = reader.name();
            if (fallbackDepth > 0 || (ns == compatibilityNS && name == QLatin1String("Fallback"))) {
                ++fallbackDepth;
                break;
            }
            if (ns != wordNS) break;
            const auto attrs = reader.attributes();
            if (name == QLatin1String("p")) stack.emplace_back();
            else if (name == QLatin1String("pStyle")) { if (!stack.empty()) stack.back().styleId = attrVal(attrs); }
            else if (name == QLatin1String("r")) { run = Run{}; inRun = true; }
            else if (name == QLatin1String("rPr")) inRunProps = inRun;
            else if (name == QLatin1String("b") && inRunProps) run.bold = isOn(attrs);
            else if (name == QLatin1String("sz") && inRunProps) run.size = attrVal(attrs).toDouble() / 2;
            else if (name == QLatin1String("t")) inText = true;
            else if ((name == QLatin1String("tab") || name == QLatin1String("br") || name == QLatin1String("cr")) && inRun && !inRunProps)
                run.text += QLatin1Char(' ');
            else if (name == QLatin1String("noBreakHyphen") && inRun) run.text += QLatin1Char('-');
            break;
        }
        case QXmlStreamReader::EndElement: {
            if (fallbackDepth > 0) { --fallbackDepth; break; }
            if (reader.namespaceUri() != wordNS) break;
            const auto name = reader.name();
            if (name == QLatin1String("p")) {
                if (!stack.empty()) {
                    ParagraphState p = std::move(stack.back());
                    stack.pop_back();
                    finish(p);
                }
            } else if (name == QLatin1String("r")) {
                if (!stack.empty()) stack.back().runs.push_back(run);
                inRun = false;
                inRunProps = false;
            } else if (name == QLatin1String("rPr")) inRunProps = false;
            else if (name == QLatin1String("t")) inText = false;
            break;
        }
        case QXmlStreamReader::Characters:
            if (inText && inRun && fallbackDepth == 0) run.text += reader.text();
            break;
        default:
            break;
        }
    }
    if (reader.hasError()) throw std::runtime_error("malformed document.xml");
    return out;
}

std::vector<Paragraph> paragraphs(const std::vector<RawParagraph> &input) {
    std::vector<RawParagraph> raw;
    for (const auto &p : input)
        if (!p.text.isEmpty()) raw.push_back(p);

    std::map<double, int> sizeWeights;
    for (const auto &p : raw)
        if (p.size > 0) sizeWeights[p.size] += int(p.text.size());
    double bodySize = 12;
    int best = -1;
    for (const auto &[size, weight] : sizeWeights) {
        if (weight > best) { best = weight; bodySize = size; }
    }

    std::vector<Paragraph> out;
    for (size_t i = 0; i < raw.size(); ++i) {
        const RawParagraph &p = raw[i];
        const QString next = i + 1 < raw.size() ? raw[i + 1].text : QString();
        const bool titleLike = isTitleLike(p.text);
        const bool styled = titleLike && (p.bold || p.size >= bodySize + 1.5);
        const bool shortBeforeBody = titleLike && p.text.size() <= 60 && next.size() >= 150;
        const bool wordHeading = p.headingStyle && p.text.size() <= 200;
        out.push_back({p.text, styled || shortBeforeBody || wordHeading});
    }
    return out;
}

std::vector<ExtractedUnit> sections(const std::vector<Paragraph> &paragraphs) {
    struct Group {
        std::optional<QString> heading;
        QStringList lines;
        bool hasBody = false;
    };
    std::vector<Group> groups;
    for (const auto &paragraph : paragraphs) {
        if (paragraph.isHeading) {
            if (!groups.empty() && groups.back().heading && !groups.back().hasBody) {
                groups.back().lines << paragraph.text;
            } else {
                groups.push_back({paragraph.text, {paragraph.text}, false});
            }
        } else {
            if (groups.empty()) groups.push_back({});
            groups.back().lines << paragraph.text;
            groups.back().hasBody = true;
        }
    }

    std::vector<ExtractedUnit> units;
    for (const auto &group : groups) {
        const QStringList parts = splitLines(group.lines);
        for (int part = 0; part < parts.size(); ++part) {
            QString ref;
            if (group.heading) {
                ref = QStringLiteral("§ ") + truncated(*group.heading) + (part > 0 ? QStringLiteral(", part %1").arg(part + 1) : QString());
            } else {
                ref = QStringLiteral("section %1").arg(units.size() + 1);
            }
            units.push_back({ref, parts[part]});
        }
    }
    return units;
}

std::vector<ExtractedUnit> extract(const QString &path, Type type) {
    const QString name = QFileInfo(path).fileName();
    try {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) throw ExtractionError::unreadable(name);
        const QByteArray data = file.readAll();
        file.close();

        std::vector<RawParagraph> raw;
        const bool isZip = data.startsWith("PK");
        if (type == Type::OfficeOpenXML || isZip) {
            ZipArchive archive(data);
            auto document = archive.contents(QStringLiteral("word/document.xml"));
            if (!document) throw ExtractionError::unreadable(name);
            QByteArray styles;
            try {
                styles = archive.contents(QStringLiteral("word/styles.xml")).value_or(QByteArray());
            } catch (const ZipArchive::Failure &) {
            }
            raw = docxParagraphs(*document, styles);
        } else {
            const QByteArray head = data.left(512).trimmed();
            if (head.startsWith("{\\rtf")) raw = rtfParagraphs(data);
            else if (data.startsWith("\xD0\xCF\x11\xE0\xA1\xB1\x1A\xE1")) raw = binaryDocParagraphs(data);
            else throw ExtractionError::unreadable(name);
        }
        return sections(paragraphs(raw));
    } catch (const ExtractionError &) {
        throw ExtractionError::unreadable(name);
    } catch (const std::exception &) {
        throw ExtractionError::unreadable(name);
    }
}

} // namespace wp::RichTextExtractor
