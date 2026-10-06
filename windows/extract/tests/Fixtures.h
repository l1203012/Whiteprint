#pragma once
// Port of Fixtures.swift: generates test documents on the fly, so no binary fixtures are committed.
#include "extract/Extraction.h"
#include "extract/ZipArchive.h"

#include <QFile>
#include <QImage>
#include <QMap>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>
#include <QSet>
#include <QString>
#include <QtTest>
#include <vector>

namespace Fixtures {

inline QString show(const std::vector<wp::ExtractedUnit> &units) {
    QStringList lines;
    for (const auto &u : units) lines << QStringLiteral("[%1] %2").arg(u.ref, QString(u.text).replace(QLatin1Char('\n'), QStringLiteral("\\n")));
    return lines.join(QLatin1Char('\n'));
}

inline bool writeFile(const QString &path, const QByteArray &data) {
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly)) return false;
    f.write(data);
    return true;
}

// MARK: PDF

struct PDFPage {
    enum Kind { Text, Image, Blank } kind = Blank;
    /// Lines of real text, top to bottom (Text) or drawn as a picture without text layer (Image); an empty
    /// line leaves a wide gap.
    QStringList lines;
    static PDFPage text(const QStringList &lines) { return {Text, lines}; }
    static PDFPage image(const QStringList &lines) { return {Image, lines}; }
    static PDFPage blank() { return {Blank, {}}; }
};

inline void drawLines(QPainter &painter, const QStringList &lines, double fontSize, double scale, double pageHeight) {
    QFont font(QStringLiteral("Arial"));
    font.setPixelSize(int(fontSize * scale));
    painter.setFont(font);
    painter.setPen(Qt::black);
    double y = (50 + fontSize) * scale;
    Q_UNUSED(pageHeight);
    for (const QString &line : lines) {
        if (line.isEmpty()) {
            y += fontSize * 8 * scale;
            continue;
        }
        painter.drawText(QPointF(50 * scale, y), line);
        y += fontSize * 1.6 * scale;
    }
}

inline bool writePDF(const std::vector<PDFPage> &pages, const QString &path) {
    QPdfWriter writer(path);
    writer.setResolution(72);
    writer.setPageSize(QPageSize(QSizeF(612, 792), QPageSize::Point));
    writer.setPageMargins(QMarginsF(0, 0, 0, 0));
    QPainter painter(&writer);
    if (!painter.isActive()) return false;
    bool first = true;
    for (const auto &page : pages) {
        if (!first) writer.newPage();
        first = false;
        switch (page.kind) {
        case PDFPage::Text:
            drawLines(painter, page.lines, 12, 1, 792);
            break;
        case PDFPage::Image: {
            const int scale = 3;
            QImage image(612 * scale, 792 * scale, QImage::Format_RGB32);
            image.fill(Qt::white);
            QPainter ip(&image);
            ip.setRenderHint(QPainter::TextAntialiasing);
            drawLines(ip, page.lines, 22, scale, 792);
            ip.end();
            painter.drawImage(QRectF(0, 0, 612, 792), image);
            break;
        }
        case PDFPage::Blank:
            break;
        }
    }
    painter.end();
    return true;
}

// MARK: Word

struct WordPart {
    QString text;
    bool heading = false;
};
inline WordPart heading(const QString &text) { return {text, true}; }
inline WordPart body(const QString &text) { return {text, false}; }

inline QString xmlEscape(QString s) {
    return s.replace(QLatin1Char('&'), QStringLiteral("&amp;")).replace(QLatin1Char('<'), QStringLiteral("&lt;")).replace(QLatin1Char('>'), QStringLiteral("&gt;"));
}

// MARK: ZIP / PPTX

/// Zips `files` (path -> contents). Paths in `stored` are added uncompressed (as are empty files).
inline QByteArray makeZip(const QMap<QString, QByteArray> &files, const QSet<QString> &stored = {}) {
    QByteArray out, directory;
    auto u16 = [](QByteArray &d, int v) { d.append(char(v & 0xFF)).append(char((v >> 8) & 0xFF)); };
    auto u32 = [&](QByteArray &d, uint32_t v) { u16(d, int(v & 0xFFFF)); u16(d, int(v >> 16)); };
    int count = 0;
    for (auto it = files.cbegin(); it != files.cend(); ++it, ++count) {
        const QByteArray name = it.key().toUtf8();
        const QByteArray &data = it.value();
        const bool store = stored.contains(it.key()) || data.isEmpty();
        QByteArray payload = data;
        if (!store) {
            // qCompress = 4-byte length + zlib stream (2-byte header, deflate data, 4-byte adler32).
            QByteArray z = qCompress(data, 9);
            payload = z.mid(4 + 2, z.size() - 4 - 2 - 4);
        }
        const uint32_t crc = wp::CRC32::checksum(data);
        const int method = store ? 0 : 8;
        const uint32_t offset = uint32_t(out.size());
        u32(out, 0x04034B50); u16(out, 20); u16(out, 0); u16(out, method); u16(out, 0); u16(out, 0);
        u32(out, crc); u32(out, uint32_t(payload.size())); u32(out, uint32_t(data.size()));
        u16(out, int(name.size())); u16(out, 0);
        out += name; out += payload;
        u32(directory, 0x02014B50); u16(directory, 20); u16(directory, 20); u16(directory, 0); u16(directory, method);
        u16(directory, 0); u16(directory, 0);
        u32(directory, crc); u32(directory, uint32_t(payload.size())); u32(directory, uint32_t(data.size()));
        u16(directory, int(name.size())); u16(directory, 0); u16(directory, 0); u16(directory, 0); u16(directory, 0);
        u32(directory, 0); u32(directory, offset);
        directory += name;
    }
    const uint32_t dirOffset = uint32_t(out.size());
    out += directory;
    u32(out, 0x06054B50); u16(out, 0); u16(out, 0); u16(out, count); u16(out, count);
    u32(out, uint32_t(directory.size())); u32(out, dirOffset); u16(out, 0);
    return out;
}

inline bool writeZip(const QMap<QString, QString> &files, const QSet<QString> &stored, const QString &path) {
    QMap<QString, QByteArray> bytes;
    for (auto it = files.cbegin(); it != files.cend(); ++it) bytes.insert(it.key(), it.value().toUtf8());
    return writeFile(path, makeZip(bytes, stored));
}

/// A one-entry stored ZIP built by hand, for corrupting headers on purpose.
inline QByteArray storedZip(const QString &name, const QByteArray &contents) {
    return makeZip({{name, contents}}, {name});
}

inline bool writeDocx(const std::vector<WordPart> &parts, const QString &path) {
    QString paragraphs;
    for (const auto &p : parts) {
        const QString props = p.heading ? QStringLiteral("<w:rPr><w:b/><w:sz w:val=\"36\"/></w:rPr>") : QStringLiteral("<w:rPr><w:sz w:val=\"24\"/></w:rPr>");
        paragraphs += QStringLiteral("<w:p><w:r>%1<w:t xml:space=\"preserve\">%2</w:t></w:r></w:p>").arg(props, xmlEscape(p.text));
    }
    const QString document = QStringLiteral(
        "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>"
        "<w:document xmlns:w=\"http://schemas.openxmlformats.org/wordprocessingml/2006/main\"><w:body>%1</w:body></w:document>").arg(paragraphs);
    return writeZip({{QStringLiteral("[Content_Types].xml"),
                      QStringLiteral("<?xml version=\"1.0\"?><Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\">"
                                     "<Default Extension=\"xml\" ContentType=\"application/xml\"/></Types>")},
                     {QStringLiteral("word/document.xml"), document}},
                    {}, path);
}

/// An RTF document with bold 18 pt headings and 12 pt body paragraphs.
inline QByteArray rtfDocument(const std::vector<WordPart> &parts) {
    QByteArray rtf = "{\\rtf1\\ansi\\deff0{\\fonttbl{\\f0 Arial;}}{\\colortbl;\\red0\\green0\\blue0;}\\f0\n";
    for (const auto &p : parts) {
        rtf += p.heading ? "\\pard\\b\\fs36 " : "\\pard\\b0\\fs24 ";
        rtf += p.text.toLatin1();
        rtf += "\\par\n";
    }
    rtf += "}";
    return rtf;
}

inline void put16(QByteArray &d, int off, uint16_t v) { d[off] = char(v & 0xFF); d[off + 1] = char(v >> 8); }
inline void put32(QByteArray &d, int off, uint32_t v) { put16(d, off, v & 0xFFFF); put16(d, off + 2, uint16_t(v >> 16)); }

/// A minimal binary Word 97-2003 file: an OLE container holding a WordDocument stream (FIB + 8-bit text)
/// and a 1Table stream (a one-piece piece table). Text only, no formatting.
inline QByteArray binaryDoc(const std::vector<WordPart> &parts) {
    QByteArray text;
    for (const auto &p : parts) text += p.text.toLatin1() + '\r';
    const int textOffset = 0x800;
    QByteArray word(8192, '\0');
    put16(word, 0, 0xA5EC);
    put16(word, 0x0A, 0x0200); // fWhichTblStm: use 1Table
    put32(word, 0x4C, uint32_t(text.size()));
    put32(word, 0x1A2, 0); // fcClx
    put32(word, 0x1A6, 21); // lcbClx
    word.replace(textOffset, int(text.size()), text);

    QByteArray table(4096, '\0');
    table[0] = 2; // Pcdt
    put32(table, 1, 16);
    put32(table, 5, 0);
    put32(table, 9, uint32_t(text.size()));
    put16(table, 13, 0);
    put32(table, 15, uint32_t(textOffset * 2) | 0x40000000u); // fc, compressed (8-bit) text
    put16(table, 19, 0);

    const int wordSectors = word.size() / 512, tableSectors = table.size() / 512;
    const uint32_t ENDOFCHAIN = 0xFFFFFFFE, FREE = 0xFFFFFFFF;
    QList<uint32_t> fat(128, FREE);
    fat[0] = 0xFFFFFFFD; // the FAT sector itself
    fat[1] = ENDOFCHAIN; // directory
    for (int i = 0; i < wordSectors; ++i) fat[2 + i] = i + 1 < wordSectors ? uint32_t(3 + i) : ENDOFCHAIN;
    const int tableStart = 2 + wordSectors;
    for (int i = 0; i < tableSectors; ++i) fat[tableStart + i] = i + 1 < tableSectors ? uint32_t(tableStart + i + 1) : ENDOFCHAIN;

    QByteArray header(512, '\0');
    const char sig[] = "\xD0\xCF\x11\xE0\xA1\xB1\x1A\xE1";
    header.replace(0, 8, QByteArray(sig, 8));
    put16(header, 0x18, 0x3E); put16(header, 0x1A, 3); put16(header, 0x1C, 0xFFFE);
    put16(header, 0x1E, 9); put16(header, 0x20, 6);
    put32(header, 0x2C, 1); put32(header, 0x30, 1); put32(header, 0x38, 4096);
    put32(header, 0x3C, ENDOFCHAIN); put32(header, 0x44, ENDOFCHAIN);
    for (int i = 0; i < 109; ++i) put32(header, 0x4C + 4 * i, i == 0 ? 0 : FREE);

    QByteArray fatBytes(512, '\0');
    for (int i = 0; i < 128; ++i) put32(fatBytes, 4 * i, fat[i]);

    QByteArray directory(512, '\0');
    auto entry = [&](int index, const QString &name, int type, uint32_t start, uint32_t size, uint32_t child, uint32_t right) {
        const int off = index * 128;
        for (int i = 0; i < name.size(); ++i) put16(directory, off + 2 * i, name[i].unicode());
        put16(directory, off + 0x40, uint16_t(name.isEmpty() ? 0 : 2 * (name.size() + 1)));
        directory[off + 0x42] = char(type);
        directory[off + 0x43] = 1;
        put32(directory, off + 0x44, FREE);
        put32(directory, off + 0x48, right);
        put32(directory, off + 0x4C, child);
        put32(directory, off + 0x74, start);
        put32(directory, off + 0x78, size);
    };
    entry(0, QStringLiteral("Root Entry"), 5, ENDOFCHAIN, 0, 1, FREE);
    entry(1, QStringLiteral("WordDocument"), 2, 2, uint32_t(word.size()), FREE, 2);
    entry(2, QStringLiteral("1Table"), 2, uint32_t(tableStart), uint32_t(table.size()), FREE, FREE);
    entry(3, QString(), 0, 0, 0, FREE, FREE);
    for (int i = 3; i < 4; ++i) { put32(directory, i * 128 + 0x44, FREE); put32(directory, i * 128 + 0x48, FREE); put32(directory, i * 128 + 0x4C, FREE); }

    return header + fatBytes + directory + word + table;
}

// MARK: PresentationML

inline const QString drawingNS = QStringLiteral("http://schemas.openxmlformats.org/drawingml/2006/main");
inline const QString presentationNS = QStringLiteral("http://schemas.openxmlformats.org/presentationml/2006/main");
inline const QString relationshipsNS = QStringLiteral("http://schemas.openxmlformats.org/officeDocument/2006/relationships");

/// A shape with an optional placeholder type and one `<a:p>` per paragraph; a paragraph's runs are
/// separated by `|`.
inline QString shape(const QString &placeholder, const QStringList &paragraphs) {
    const QString ph = placeholder.isNull() ? QString() : QStringLiteral("<p:ph type=\"%1\"/>").arg(placeholder);
    QString body;
    for (const QString &paragraph : paragraphs) {
        body += QStringLiteral("<a:p>");
        for (const QString &run : paragraph.split(QLatin1Char('|'), Qt::KeepEmptyParts))
            body += QStringLiteral("<a:r><a:rPr lang=\"en-US\"/><a:t>%1</a:t></a:r>").arg(run);
        body += QStringLiteral("</a:p>");
    }
    return QStringLiteral("<p:sp><p:nvSpPr><p:cNvPr id=\"2\" name=\"Shape\"/><p:cNvSpPr/><p:nvPr>%1</p:nvPr></p:nvSpPr>"
                          "<p:spPr/><p:txBody><a:bodyPr/>%2</p:txBody></p:sp>").arg(ph, body);
}

inline QString slideXML(const QStringList &shapes, const QString &root = QStringLiteral("sld")) {
    return QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                          "<p:%1 xmlns:a=\"%2\" xmlns:p=\"%3\" xmlns:r=\"%4\">"
                          "<p:cSld><p:spTree><p:nvGrpSpPr><p:cNvPr id=\"1\" name=\"\"/><p:cNvGrpSpPr/><p:nvPr/></p:nvGrpSpPr>"
                          "<p:grpSpPr/>%5</p:spTree></p:cSld></p:%1>")
        .arg(root, drawingNS, presentationNS, relationshipsNS, shapes.join(QString()));
}

inline QString notesRels(const QString &target) {
    return QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
                          "<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">"
                          "<Relationship Id=\"rId1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/slideLayout\" Target=\"../slideLayouts/slideLayout1.xml\"/>"
                          "<Relationship Id=\"rId2\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/notesSlide\" Target=\"%1\"/>"
                          "</Relationships>").arg(target);
}

/// `presentation.xml` and its rels listing `slides` (part names under `ppt/slides/`) in order.
inline QMap<QString, QString> presentationParts(const QStringList &slides) {
    QString ids, rels;
    for (int i = 0; i < slides.size(); ++i) {
        ids += QStringLiteral("<p:sldId id=\"%1\" r:id=\"rId%2\"/>").arg(256 + i).arg(i + 10);
        rels += QStringLiteral("<Relationship Id=\"rId%1\" Type=\"http://schemas.openxmlformats.org/officeDocument/2006/relationships/slide\" Target=\"slides/%2\"/>")
                    .arg(i + 10).arg(slides[i]);
    }
    return {
        {QStringLiteral("ppt/presentation.xml"),
         QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<p:presentation xmlns:a=\"%1\" xmlns:p=\"%2\" xmlns:r=\"%3\"><p:sldIdLst>%4</p:sldIdLst></p:presentation>")
             .arg(drawingNS, presentationNS, relationshipsNS, ids)},
        {QStringLiteral("ppt/_rels/presentation.xml.rels"),
         QStringLiteral("<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n<Relationships xmlns=\"http://schemas.openxmlformats.org/package/2006/relationships\">%1</Relationships>").arg(rels)},
    };
}

inline const QString contentTypes = QStringLiteral(
    "<?xml version=\"1.0\" encoding=\"UTF-8\" standalone=\"yes\"?>\n"
    "<Types xmlns=\"http://schemas.openxmlformats.org/package/2006/content-types\"><Default Extension=\"xml\" ContentType=\"application/xml\"/></Types>");

} // namespace Fixtures
