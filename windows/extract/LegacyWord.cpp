// Legacy Word readers behind RichTextExtractor: RTF and the binary Word 97-2003 (.doc) format.
#include "extract/RichTextExtractor.h"
#include "extract/TextCleaner.h"

#include <QHash>
#include <QSet>
#include <algorithm>
#include <stdexcept>

namespace wp::RichTextExtractor {

namespace {

QChar cp1252(uchar b) {
    static const ushort table[32] = {0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021, 0x02C6, 0x2030, 0x0160,
                                     0x2039, 0x0152, 0x008D, 0x017D, 0x008F, 0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022,
                                     0x2013, 0x2014, 0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178};
    if (b >= 0x80 && b < 0xA0) return QChar(table[b - 0x80]);
    return QChar(ushort(b));
}

/// Accumulates paragraphs and the formatting of their visible characters.
struct ParagraphBuilder {
    QString text;
    bool allBold = true;
    double largest = 0;
    std::vector<RawParagraph> out;

    void add(QChar c, bool bold, double size) {
        text += c;
        if (!c.isSpace()) {
            if (!bold) allBold = false;
            largest = std::max(largest, size);
        }
    }
    void flush() {
        QString collapsed = TextCleaner::collapseWhitespace(text);
        if (!collapsed.isEmpty()) out.push_back({collapsed, allBold, largest, false});
        text.clear();
        allBold = true;
        largest = 0;
    }
};

} // namespace

// MARK: RTF

std::vector<RawParagraph> rtfParagraphs(const QByteArray &rtf) {
    struct State {
        bool bold = false;
        int halfPoints = 24;
        bool skip = false;
        int uc = 1;
    };
    static const QSet<QString> skippedDestinations{
        "fonttbl", "colortbl", "stylesheet", "info", "pict", "header", "footer", "headerl", "headerr", "headerf", "footerl",
        "footerr", "footerf", "footnote", "listtable", "listoverridetable", "rsidtbl", "generator", "themedata", "datastore",
        "latentstyles", "fldinst", "xmlnstbl", "filetbl", "revtbl", "object", "shppict", "nonshppict", "private"};

    ParagraphBuilder builder;
    std::vector<State> stack{State{}};
    int skipChars = 0;
    const int n = int(rtf.size());

    auto put = [&](QChar c) {
        State &s = stack.back();
        if (s.skip) return;
        if (skipChars > 0) { --skipChars; return; }
        builder.add(c, s.bold, s.halfPoints / 2.0);
    };

    for (int i = 0; i < n;) {
        const char ch = rtf[i];
        if (ch == '{') {
            stack.push_back(stack.back());
            ++i;
        } else if (ch == '}') {
            if (stack.size() > 1) stack.pop_back();
            ++i;
        } else if (ch == '\\') {
            if (i + 1 >= n) break;
            const char c = rtf[i + 1];
            if (c == '\\' || c == '{' || c == '}') {
                put(QChar(c));
                i += 2;
            } else if (c == '\'') {
                bool ok = false;
                int value = rtf.mid(i + 2, 2).toInt(&ok, 16);
                if (ok) put(cp1252(uchar(value)));
                i += 4;
            } else if (c == '*') {
                stack.back().skip = true;
                i += 2;
            } else if (c == '~') {
                put(QChar(' '));
                i += 2;
            } else if (c == '_') {
                put(QChar('-'));
                i += 2;
            } else if (c == '\n' || c == '\r') {
                if (!stack.back().skip) builder.flush();
                i += 2;
            } else if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z')) {
                int j = i + 1;
                while (j < n && ((rtf[j] >= 'a' && rtf[j] <= 'z') || (rtf[j] >= 'A' && rtf[j] <= 'Z'))) ++j;
                const QString word = QString::fromLatin1(rtf.mid(i + 1, j - i - 1));
                bool hasParam = false;
                int param = 0;
                int k = j;
                if (k < n && (rtf[k] == '-' || (rtf[k] >= '0' && rtf[k] <= '9'))) {
                    int sign = 1;
                    if (rtf[k] == '-') { sign = -1; ++k; }
                    int start = k;
                    while (k < n && rtf[k] >= '0' && rtf[k] <= '9') ++k;
                    param = sign * rtf.mid(start, k - start).toInt();
                    hasParam = k > start;
                }
                if (k < n && rtf[k] == ' ') ++k;
                i = k;

                State &s = stack.back();
                if (skippedDestinations.contains(word)) s.skip = true;
                else if (word == QLatin1String("par") || word == QLatin1String("sect") || word == QLatin1String("row")) {
                    if (!s.skip) builder.flush();
                } else if (word == QLatin1String("line") || word == QLatin1String("tab") || word == QLatin1String("cell")) put(QChar(' '));
                else if (word == QLatin1String("b")) s.bold = !hasParam || param != 0;
                else if (word == QLatin1String("fs") && hasParam) s.halfPoints = param;
                else if (word == QLatin1String("plain")) { s.bold = false; s.halfPoints = 24; }
                else if (word == QLatin1String("uc") && hasParam) s.uc = param;
                else if (word == QLatin1String("u") && hasParam) {
                    put(QChar(ushort(param < 0 ? param + 65536 : param)));
                    if (!s.skip) skipChars = s.uc;
                } else if (word == QLatin1String("emdash")) put(QChar(0x2014));
                else if (word == QLatin1String("endash")) put(QChar(0x2013));
                else if (word == QLatin1String("bullet")) put(QChar(0x2022));
                else if (word == QLatin1String("lquote")) put(QChar(0x2018));
                else if (word == QLatin1String("rquote")) put(QChar(0x2019));
                else if (word == QLatin1String("ldblquote")) put(QChar(0x201C));
                else if (word == QLatin1String("rdblquote")) put(QChar(0x201D));
            } else {
                i += 2; // unknown control symbol
            }
        } else if (ch == '\r' || ch == '\n') {
            ++i;
        } else {
            put(cp1252(uchar(ch)));
            ++i;
        }
    }
    builder.flush();
    return std::move(builder.out);
}

// MARK: binary .doc

namespace {

/// Minimal reader for OLE compound files ([MS-CFB]), enough to pull named streams out of a .doc.
class OleFile {
public:
    explicit OleFile(const QByteArray &data) : m_data(data) {
        if (data.size() < 512 || !data.startsWith("\xD0\xCF\x11\xE0\xA1\xB1\x1A\xE1")) fail();
        const int sectorShift = u16(0x1E);
        if (sectorShift != 9 && sectorShift != 12) fail();
        m_sectorSize = 1 << sectorShift;
        m_miniSectorSize = 1 << u16(0x20);
        m_miniCutoff = u32(0x38);
        const uint32_t firstDir = u32(0x30);
        const uint32_t firstMiniFat = u32(0x3C);
        const uint32_t firstDifat = u32(0x44);
        const uint32_t difatCount = u32(0x48);

        QList<uint32_t> fatSectors;
        for (int i = 0; i < 109; ++i) {
            uint32_t s = u32(0x4C + 4 * i);
            if (s < 0xFFFFFFFAu) fatSectors << s;
        }
        uint32_t difat = firstDifat;
        for (uint32_t i = 0; i < difatCount && difat < 0xFFFFFFFAu; ++i) {
            const QByteArray sector = sectorData(difat);
            const int entries = m_sectorSize / 4 - 1;
            for (int e = 0; e < entries; ++e) {
                uint32_t s = u32(sector, 4 * e);
                if (s < 0xFFFFFFFAu) fatSectors << s;
            }
            difat = u32(sector, 4 * entries);
        }
        for (uint32_t s : fatSectors) {
            const QByteArray sector = sectorData(s);
            for (int e = 0; e < m_sectorSize / 4; ++e) m_fat << u32(sector, 4 * e);
        }

        const QByteArray directory = chain(firstDir);
        for (int off = 0; off + 128 <= directory.size(); off += 128) {
            Entry entry;
            const int nameBytes = std::min<int>(u16(directory, off + 0x40), 64);
            entry.name = QString::fromUtf16(reinterpret_cast<const char16_t *>(directory.constData() + off), std::max(0, nameBytes / 2 - 1));
            entry.type = uchar(directory[off + 0x42]);
            entry.start = u32(directory, off + 0x74);
            entry.size = u32(directory, off + 0x78);
            m_entries << entry;
        }
        if (m_entries.isEmpty() || m_entries[0].type != 5) fail();
        if (firstMiniFat < 0xFFFFFFFAu) {
            const QByteArray mf = chain(firstMiniFat);
            for (int e = 0; e + 4 <= mf.size(); e += 4) m_miniFat << u32(mf, e);
        }
        m_miniStream = chain(m_entries[0].start).left(qsizetype(m_entries[0].size));
    }

    QByteArray stream(const QString &name) const {
        for (const Entry &e : m_entries) {
            if (e.type != 2 || e.name != name) continue;
            if (e.size < m_miniCutoff) return miniChain(e.start).left(qsizetype(e.size));
            return chain(e.start).left(qsizetype(e.size));
        }
        return {};
    }

private:
    struct Entry {
        QString name;
        int type = 0;
        uint32_t start = 0;
        uint32_t size = 0;
    };

    [[noreturn]] static void fail() { throw std::runtime_error("not a readable OLE file"); }

    uint16_t u16(int off) const { return u16(m_data, off); }
    uint32_t u32(int off) const { return u32(m_data, off); }
    static uint16_t u16(const QByteArray &d, int off) {
        if (off < 0 || off + 2 > d.size()) fail();
        auto p = reinterpret_cast<const uchar *>(d.constData()) + off;
        return uint16_t(p[0] | p[1] << 8);
    }
    static uint32_t u32(const QByteArray &d, int off) {
        if (off < 0 || off + 4 > d.size()) fail();
        auto p = reinterpret_cast<const uchar *>(d.constData()) + off;
        return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
    }

    QByteArray sectorData(uint32_t id) const {
        const qint64 offset = (qint64(id) + 1) * m_sectorSize;
        if (offset + m_sectorSize > m_data.size()) fail();
        return m_data.mid(qsizetype(offset), m_sectorSize);
    }

    QByteArray chain(uint32_t start) const {
        QByteArray out;
        uint32_t id = start;
        for (int guard = 0; id < 0xFFFFFFFAu; ++guard) {
            if (guard > m_fat.size() || id >= uint32_t(m_fat.size())) fail();
            out += sectorData(id);
            id = m_fat[int(id)];
        }
        return out;
    }

    QByteArray miniChain(uint32_t start) const {
        QByteArray out;
        uint32_t id = start;
        for (int guard = 0; id < 0xFFFFFFFAu; ++guard) {
            if (guard > m_miniFat.size() || id >= uint32_t(m_miniFat.size())) fail();
            const qint64 offset = qint64(id) * m_miniSectorSize;
            if (offset + m_miniSectorSize > m_miniStream.size()) fail();
            out += m_miniStream.mid(qsizetype(offset), m_miniSectorSize);
            id = m_miniFat[int(id)];
        }
        return out;
    }

    QByteArray m_data;
    int m_sectorSize = 512;
    int m_miniSectorSize = 64;
    uint32_t m_miniCutoff = 4096;
    QList<uint32_t> m_fat;
    QList<uint32_t> m_miniFat;
    QList<Entry> m_entries;
    QByteArray m_miniStream;
};

uint16_t le16(const QByteArray &d, qint64 off) {
    if (off < 0 || off + 2 > d.size()) throw std::runtime_error("truncated .doc");
    auto p = reinterpret_cast<const uchar *>(d.constData()) + off;
    return uint16_t(p[0] | p[1] << 8);
}
uint32_t le32(const QByteArray &d, qint64 off) {
    if (off < 0 || off + 4 > d.size()) throw std::runtime_error("truncated .doc");
    auto p = reinterpret_cast<const uchar *>(d.constData()) + off;
    return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
}

} // namespace

std::vector<RawParagraph> binaryDocParagraphs(const QByteArray &doc) {
    const OleFile ole(doc);
    const QByteArray word = ole.stream(QStringLiteral("WordDocument"));
    if (word.size() < 0x1AA || le16(word, 0) != 0xA5EC) throw std::runtime_error("not a Word 97-2003 document");
    const bool table1 = (le16(word, 0x0A) & 0x0200) != 0;
    const QByteArray table = ole.stream(table1 ? QStringLiteral("1Table") : QStringLiteral("0Table"));
    const qint64 ccpText = le32(word, 0x4C);
    const qint64 fcClx = le32(word, 0x1A2);
    const qint64 lcbClx = le32(word, 0x1A6);
    if (fcClx + lcbClx > table.size() || lcbClx == 0) throw std::runtime_error("missing piece table");

    // Skip the Prc entries (type 1) up to the Pcdt (type 2).
    qint64 pos = fcClx;
    const qint64 end = fcClx + lcbClx;
    qint64 plc = -1, plcLength = 0;
    while (pos < end) {
        const uchar type = uchar(table[qsizetype(pos)]);
        if (type == 1) {
            pos += 3 + le16(table, pos + 1);
        } else if (type == 2) {
            plcLength = le32(table, pos + 1);
            plc = pos + 5;
            break;
        } else {
            throw std::runtime_error("bad CLX");
        }
    }
    if (plc < 0 || plcLength < 4 + 12) throw std::runtime_error("missing PlcPcd");
    const qint64 pieces = (plcLength - 4) / 12;

    QString text;
    for (qint64 i = 0; i < pieces; ++i) {
        const qint64 cpStart = le32(table, plc + 4 * i);
        qint64 cpEnd = le32(table, plc + 4 * (i + 1));
        if (cpStart >= ccpText) break;
        cpEnd = std::min(cpEnd, ccpText);
        const qint64 count = cpEnd - cpStart;
        if (count <= 0) continue;
        uint32_t fc = le32(table, plc + 4 * (pieces + 1) + 8 * i + 2);
        if (fc & 0x40000000u) {
            const qint64 offset = (fc & ~0x40000000u) / 2;
            if (offset + count > word.size()) throw std::runtime_error("truncated text");
            for (qint64 k = 0; k < count; ++k) text += cp1252(uchar(word[qsizetype(offset + k)]));
        } else {
            const qint64 offset = fc;
            if (offset + 2 * count > word.size()) throw std::runtime_error("truncated text");
            for (qint64 k = 0; k < count; ++k) text += QChar(le16(word, offset + 2 * k));
        }
    }

    ParagraphBuilder builder;
    std::vector<bool> fieldCode; // one entry per open field: true while inside its instruction part
    for (QChar c : text) {
        const ushort u = c.unicode();
        if (u == 0x13) { fieldCode.push_back(true); continue; }
        if (u == 0x14) { if (!fieldCode.empty()) fieldCode.back() = false; continue; }
        if (u == 0x15) { if (!fieldCode.empty()) fieldCode.pop_back(); continue; }
        if (std::find(fieldCode.begin(), fieldCode.end(), true) != fieldCode.end()) continue;
        if (u == 0x0D || u == 0x07 || u == 0x0C) builder.flush();
        else if (u == 0x0B || u == 0x09) builder.add(QChar(' '), false, 0);
        else if (u == 0x1E) builder.add(QChar('-'), false, 0);
        else if (u >= 0x20) builder.add(c, false, 0);
    }
    builder.flush();
    return std::move(builder.out);
}

} // namespace wp::RichTextExtractor
