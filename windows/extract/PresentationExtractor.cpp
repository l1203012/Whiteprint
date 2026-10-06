#include "extract/PresentationExtractor.h"

#include "extract/PresentationXML.h"

#include <QFileInfo>
#include <algorithm>

namespace wp {

namespace PackagePath {

QString resolve(const QString &target, const QString &source) {
    QStringList parts;
    if (!target.startsWith(QLatin1Char('/'))) {
        parts = source.split(QLatin1Char('/'), Qt::SkipEmptyParts);
        if (!parts.isEmpty()) parts.removeLast();
    }
    for (const QString &component : target.split(QLatin1Char('/'), Qt::SkipEmptyParts)) {
        if (component == QLatin1String(".")) continue;
        if (component == QLatin1String("..")) {
            if (!parts.isEmpty()) parts.removeLast();
        } else {
            parts << component;
        }
    }
    return parts.join(QLatin1Char('/'));
}

} // namespace PackagePath

namespace PresentationExtractor {

namespace {

QStringList orderedSlidePaths(const ZipArchive &archive) {
    auto presentation = archive.contents(QStringLiteral("ppt/presentation.xml"));
    auto rels = archive.contents(QStringLiteral("ppt/_rels/presentation.xml.rels"));
    if (!presentation || !rels) return {};
    QHash<QString, Relationship> targets;
    for (const auto &r : RelationshipParser::parse(*rels))
        if (!targets.contains(r.id)) targets.insert(r.id, r);
    QStringList out;
    for (const QString &id : SlideListParser::parse(*presentation)) {
        auto it = targets.constFind(id);
        if (it == targets.constEnd()) continue;
        QString path = PackagePath::resolve(it->target, QStringLiteral("ppt/presentation.xml"));
        if (archive.entry(path)) out << path;
    }
    return out;
}

QString notesText(const QString &slidePath, const ZipArchive &archive) {
    int slash = int(slidePath.lastIndexOf(QLatin1Char('/')));
    QString directory = slash >= 0 ? slidePath.left(slash) : QString();
    QString fileName = slidePath.mid(slash + 1);
    QString relsPath = directory + QStringLiteral("/_rels/") + fileName + QStringLiteral(".rels");
    auto rels = archive.contents(relsPath);
    if (!rels) return {};
    QString target;
    bool found = false;
    for (const auto &r : RelationshipParser::parse(*rels)) {
        if (r.type.endsWith(QLatin1String("/notesSlide"))) { target = r.target; found = true; break; }
    }
    if (!found) return {};
    auto xml = archive.contents(PackagePath::resolve(target, slidePath));
    if (!xml) return {};
    // A notes page also holds a picture of the slide and its own number.
    QStringList lines;
    for (const auto &shape : ShapeTextParser::parse(
             *xml, {QStringLiteral("sldImg"), QStringLiteral("sldNum"), QStringLiteral("ftr"), QStringLiteral("dt"), QStringLiteral("hdr")}))
        lines << shape.paragraphs;
    return lines.join(QLatin1Char('\n'));
}

} // namespace

QStringList slidePaths(const ZipArchive &archive) {
    try {
        QStringList ordered = orderedSlidePaths(archive);
        if (!ordered.isEmpty()) return ordered;
    } catch (...) {
    }
    const QString prefix = QStringLiteral("ppt/slides/slide");
    std::vector<std::pair<int, QString>> found;
    for (const auto &entry : archive.entries()) {
        const QString &name = entry.name;
        if (!name.startsWith(prefix) || !name.endsWith(QLatin1String(".xml"))) continue;
        bool ok = false;
        int number = name.mid(prefix.size(), name.size() - prefix.size() - 4).toInt(&ok);
        if (ok) found.emplace_back(number, name);
    }
    std::stable_sort(found.begin(), found.end(), [](const auto &a, const auto &b) { return a.first < b.first; });
    QStringList out;
    for (const auto &f : found) out << f.second;
    return out;
}

QString slideText(const QString &path, const ZipArchive &archive) {
    auto xml = archive.contents(path);
    if (!xml) return {};
    auto shapes = ShapeTextParser::parse(
        *xml, {QStringLiteral("sldNum"), QStringLiteral("ftr"), QStringLiteral("dt"), QStringLiteral("hdr")});
    auto isTitle = [](const ShapeText &s) {
        return s.placeholder && (*s.placeholder == QLatin1String("title") || *s.placeholder == QLatin1String("ctrTitle"));
    };
    QStringList lines;
    for (const auto &s : shapes)
        if (isTitle(s)) lines << s.paragraphs;
    for (const auto &s : shapes)
        if (!isTitle(s)) lines << s.paragraphs;
    QString text = lines.join(QLatin1Char('\n'));

    QString notes = notesText(path, archive);
    if (!notes.isEmpty()) text += (text.isEmpty() ? QString() : QStringLiteral("\n\n")) + QStringLiteral("Notes: ") + notes;
    return text;
}

std::vector<ExtractedUnit> extract(const QString &path) {
    const QString name = QFileInfo(path).fileName();
    std::optional<ZipArchive> archive;
    try {
        archive.emplace(path);
    } catch (const ZipArchive::Failure &) {
        throw ExtractionError::unreadable(name);
    }
    QStringList slides = slidePaths(*archive);
    if (slides.isEmpty()) throw ExtractionError::unreadable(name);
    std::vector<ExtractedUnit> units;
    for (int i = 0; i < slides.size(); ++i) {
        try {
            units.push_back({QStringLiteral("slide %1").arg(i + 1), slideText(slides[i], *archive)});
        } catch (const ZipArchive::Failure &) {
            throw ExtractionError::unreadable(name);
        } catch (const XMLParseFailure &) {
            throw ExtractionError::unreadable(name);
        }
    }
    return units;
}

} // namespace PresentationExtractor

} // namespace wp
