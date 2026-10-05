#include "extract/ZipArchive.h"

#include <QFile>
#include <algorithm>
#include <array>

namespace wp {

namespace {

QString failureText(ZipArchive::Failure::Kind kind, const QString &detail) {
    switch (kind) {
    case ZipArchive::Failure::Kind::NotAZip: return QStringLiteral("not a zip archive");
    case ZipArchive::Failure::Kind::Corrupt: return QStringLiteral("corrupt zip: ") + detail;
    case ZipArchive::Failure::Kind::Unsupported: return QStringLiteral("unsupported zip feature: ") + detail;
    case ZipArchive::Failure::Kind::TooLarge: return QStringLiteral("zip entry too large: ") + detail;
    }
    return detail;
}

using Kind = ZipArchive::Failure::Kind;

/// Bounds-checked little-endian reads at absolute offsets.
struct ByteReader {
    const QByteArray &data;

    void check(qint64 offset, qint64 length) const {
        if (offset < 0 || length < 0 || offset + length > data.size())
            throw ZipArchive::Failure(Kind::Corrupt, QStringLiteral("read past end of archive"));
    }
    uint16_t u16(qint64 offset) const {
        check(offset, 2);
        auto p = reinterpret_cast<const uchar *>(data.constData()) + offset;
        return uint16_t(p[0] | p[1] << 8);
    }
    uint32_t u32(qint64 offset) const {
        check(offset, 4);
        auto p = reinterpret_cast<const uchar *>(data.constData()) + offset;
        return uint32_t(p[0]) | uint32_t(p[1]) << 8 | uint32_t(p[2]) << 16 | uint32_t(p[3]) << 24;
    }
    QString string(qint64 offset, qint64 length) const {
        check(offset, length);
        return QString::fromUtf8(data.constData() + offset, qsizetype(length));
    }
};

// MARK: inflate (RFC 1951), after Mark Adler's puff.

constexpr int maxBits = 15;
constexpr int maxLCodes = 286;
constexpr int maxDCodes = 30;
constexpr int fixLCodes = 288;

struct InflateError {};

struct Huffman {
    short count[maxBits + 1];
    short symbol[fixLCodes];
};

struct Inflater {
    const uchar *in;
    size_t inLen;
    size_t inPos = 0;
    uint32_t bitBuf = 0;
    int bitCount = 0;
    uchar *out;
    size_t outCap;
    size_t outPos = 0;

    int bits(int need) {
        uint32_t val = bitBuf;
        while (bitCount < need) {
            if (inPos >= inLen) throw InflateError{};
            val |= uint32_t(in[inPos++]) << bitCount;
            bitCount += 8;
        }
        bitBuf = val >> need;
        bitCount -= need;
        return int(val & ((1u << need) - 1));
    }

    int decode(const Huffman &h) {
        int code = 0, first = 0, index = 0;
        for (int len = 1; len <= maxBits; ++len) {
            code |= bits(1);
            int count = h.count[len];
            if (code - count < first) return h.symbol[index + (code - first)];
            index += count;
            first += count;
            first <<= 1;
            code <<= 1;
        }
        throw InflateError{};
    }

    static int construct(Huffman &h, const short *length, int n) {
        for (int len = 0; len <= maxBits; ++len) h.count[len] = 0;
        for (int symbol = 0; symbol < n; ++symbol) h.count[length[symbol]]++;
        if (h.count[0] == n) return 0;
        int left = 1;
        for (int len = 1; len <= maxBits; ++len) {
            left <<= 1;
            left -= h.count[len];
            if (left < 0) return left;
        }
        short offs[maxBits + 1];
        offs[1] = 0;
        for (int len = 1; len < maxBits; ++len) offs[len + 1] = short(offs[len] + h.count[len]);
        for (int symbol = 0; symbol < n; ++symbol)
            if (length[symbol] != 0) h.symbol[offs[length[symbol]]++] = short(symbol);
        return left;
    }

    void stored() {
        bitBuf = 0;
        bitCount = 0;
        if (inPos + 4 > inLen) throw InflateError{};
        unsigned len = in[inPos] | in[inPos + 1] << 8;
        unsigned nlen = in[inPos + 2] | in[inPos + 3] << 8;
        inPos += 4;
        if (len != (~nlen & 0xFFFF)) throw InflateError{};
        if (inPos + len > inLen || outPos + len > outCap) throw InflateError{};
        std::copy(in + inPos, in + inPos + len, out + outPos);
        inPos += len;
        outPos += len;
    }

    void codes(const Huffman &lencode, const Huffman &distcode) {
        static const short lens[29] = {3, 4, 5, 6, 7, 8, 9, 10, 11, 13, 15, 17, 19, 23, 27, 31,
                                       35, 43, 51, 59, 67, 83, 99, 115, 131, 163, 195, 227, 258};
        static const short lext[29] = {0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 2, 2, 2, 2, 3, 3, 3, 3, 4, 4, 4, 4, 5, 5, 5, 5, 0};
        static const short dists[30] = {1, 2, 3, 4, 5, 7, 9, 13, 17, 25, 33, 49, 65, 97, 129, 193,
                                        257, 385, 513, 769, 1025, 1537, 2049, 3073, 4097, 6145, 8193, 12289, 16385, 24577};
        static const short dext[30] = {0, 0, 0, 0, 1, 1, 2, 2, 3, 3, 4, 4, 5, 5, 6, 6, 7, 7, 8, 8, 9, 9, 10, 10, 11, 11, 12, 12, 13, 13};
        for (;;) {
            int symbol = decode(lencode);
            if (symbol < 256) {
                if (outPos >= outCap) throw InflateError{};
                out[outPos++] = uchar(symbol);
            } else if (symbol == 256) {
                return;
            } else {
                symbol -= 257;
                if (symbol >= 29) throw InflateError{};
                size_t len = size_t(lens[symbol] + bits(lext[symbol]));
                int dsym = decode(distcode);
                if (dsym >= 30) throw InflateError{};
                size_t dist = size_t(dists[dsym] + bits(dext[dsym]));
                if (dist > outPos || outPos + len > outCap) throw InflateError{};
                for (size_t i = 0; i < len; ++i, ++outPos) out[outPos] = out[outPos - dist];
            }
        }
    }

    void fixed() {
        struct Tables {
            Huffman lencode, distcode;
            Tables() {
                short lengths[fixLCodes];
                int s = 0;
                for (; s < 144; ++s) lengths[s] = 8;
                for (; s < 256; ++s) lengths[s] = 9;
                for (; s < 280; ++s) lengths[s] = 7;
                for (; s < fixLCodes; ++s) lengths[s] = 8;
                construct(lencode, lengths, fixLCodes);
                for (s = 0; s < maxDCodes; ++s) lengths[s] = 5;
                construct(distcode, lengths, maxDCodes);
            }
        };
        static const Tables tables;
        codes(tables.lencode, tables.distcode);
    }

    void dynamic() {
        static const short order[19] = {16, 17, 18, 0, 8, 7, 9, 6, 10, 5, 11, 4, 12, 3, 13, 2, 14, 1, 15};
        int nlen = bits(5) + 257;
        int ndist = bits(5) + 1;
        int ncode = bits(4) + 4;
        if (nlen > maxLCodes || ndist > maxDCodes) throw InflateError{};
        short lengths[maxLCodes + maxDCodes];
        int index = 0;
        for (; index < ncode; ++index) lengths[order[index]] = short(bits(3));
        for (; index < 19; ++index) lengths[order[index]] = 0;
        Huffman lencode, distcode;
        if (construct(lencode, lengths, 19) != 0) throw InflateError{};
        index = 0;
        while (index < nlen + ndist) {
            int symbol = decode(lencode);
            if (symbol < 16) {
                lengths[index++] = short(symbol);
            } else {
                int len = 0, rep;
                if (symbol == 16) {
                    if (index == 0) throw InflateError{};
                    len = lengths[index - 1];
                    rep = 3 + bits(2);
                } else if (symbol == 17) {
                    rep = 3 + bits(3);
                } else {
                    rep = 11 + bits(7);
                }
                if (index + rep > nlen + ndist) throw InflateError{};
                while (rep--) lengths[index++] = short(len);
            }
        }
        if (lengths[256] == 0) throw InflateError{};
        int err = construct(lencode, lengths, nlen);
        if (err != 0 && (err < 0 || nlen != lencode.count[0] + lencode.count[1])) throw InflateError{};
        err = construct(distcode, lengths + nlen, ndist);
        if (err != 0 && (err < 0 || ndist != distcode.count[0] + distcode.count[1])) throw InflateError{};
        codes(lencode, distcode);
    }

    void run() {
        int last;
        do {
            last = bits(1);
            switch (bits(2)) {
            case 0: stored(); break;
            case 1: fixed(); break;
            case 2: dynamic(); break;
            default: throw InflateError{};
            }
        } while (!last);
    }
};

} // namespace

bool inflateRaw(const QByteArray &compressed, qint64 size, QByteArray &out) {
    out.clear();
    if (size < 0) return false;
    QByteArray buffer(qsizetype(size), Qt::Uninitialized);
    Inflater inflater{reinterpret_cast<const uchar *>(compressed.constData()), size_t(compressed.size()), 0, 0, 0,
                      reinterpret_cast<uchar *>(buffer.data()), size_t(size), 0};
    try {
        inflater.run();
    } catch (const InflateError &) {
        return false;
    }
    if (inflater.outPos != size_t(size)) return false;
    out = buffer;
    return true;
}

ZipArchive::Failure::Failure(Kind kind, QString detail) : m_kind(kind), m_detail(std::move(detail)) {
    m_what = failureText(m_kind, m_detail).toUtf8();
}

ZipArchive::ZipArchive(const QString &path) {
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) throw Failure(Kind::NotAZip);
    m_data = file.readAll();
    parse();
}

ZipArchive::ZipArchive(const QByteArray &data) : m_data(data) {
    parse();
}

void ZipArchive::parse() {
    ByteReader bytes{m_data};
    const qint64 total = m_data.size();
    if (total < 22) throw Failure(Kind::NotAZip);
    // The end record is 22 bytes plus a comment of at most 65535 bytes.
    const qint64 lowest = std::max<qint64>(0, total - 22 - 0xFFFF);
    qint64 eocd = -1;
    for (qint64 position = total - 22; position >= lowest; --position) {
        if (bytes.u32(position) == 0x06054B50u) { eocd = position; break; }
    }
    if (eocd < 0) throw Failure(Kind::NotAZip);

    const uint16_t count = bytes.u16(eocd + 10);
    const qint64 size = bytes.u32(eocd + 12);
    const qint64 offset = bytes.u32(eocd + 16);
    if (count == 0xFFFF || size == 0xFFFFFFFFll || offset == 0xFFFFFFFFll) throw Failure(Kind::Unsupported, QStringLiteral("ZIP64"));
    if (offset + size > eocd) throw Failure(Kind::Corrupt, QStringLiteral("central directory out of range"));

    qint64 position = offset;
    for (int i = 0; i < count; ++i) {
        if (bytes.u32(position) != 0x02014B50u) throw Failure(Kind::Corrupt, QStringLiteral("bad central directory entry"));
        const qint64 nameLength = bytes.u16(position + 28);
        const qint64 extraLength = bytes.u16(position + 30);
        const qint64 commentLength = bytes.u16(position + 32);
        Entry entry;
        entry.name = bytes.string(position + 46, nameLength);
        entry.method = bytes.u16(position + 10);
        entry.flags = bytes.u16(position + 8);
        entry.crc32 = bytes.u32(position + 16);
        entry.compressedSize = bytes.u32(position + 20);
        entry.uncompressedSize = bytes.u32(position + 24);
        entry.localHeaderOffset = bytes.u32(position + 42);
        if (!m_index.contains(entry.name)) m_index.insert(entry.name, int(m_entries.size()));
        m_entries.push_back(std::move(entry));
        position += 46 + nameLength + extraLength + commentLength;
    }
}

const ZipArchive::Entry *ZipArchive::entry(const QString &name) const {
    auto it = m_index.constFind(name);
    return it == m_index.constEnd() ? nullptr : &m_entries[size_t(it.value())];
}

std::optional<QByteArray> ZipArchive::contents(const QString &name) const {
    const Entry *e = entry(name);
    if (!e) return std::nullopt;
    return contents(*e);
}

QByteArray ZipArchive::contents(const Entry &entry) const {
    if (entry.flags & 1) throw Failure(Kind::Unsupported, QStringLiteral("encrypted entry ") + entry.name);
    if (entry.uncompressedSize > maxEntrySize) throw Failure(Kind::TooLarge, entry.name);
    ByteReader bytes{m_data};
    const qint64 header = entry.localHeaderOffset;
    if (bytes.u32(header) != 0x04034B50u) throw Failure(Kind::Corrupt, QStringLiteral("bad local header for ") + entry.name);
    const qint64 start = header + 30 + bytes.u16(header + 26) + bytes.u16(header + 28);
    if (start + entry.compressedSize > m_data.size()) throw Failure(Kind::Corrupt, entry.name + QStringLiteral(" out of range"));
    const QByteArray compressed = m_data.mid(qsizetype(start), qsizetype(entry.compressedSize));

    QByteArray output;
    switch (entry.method) {
    case 0:
        if (entry.compressedSize != entry.uncompressedSize)
            throw Failure(Kind::Corrupt, QStringLiteral("stored size mismatch for ") + entry.name);
        output = compressed;
        break;
    case 8:
        if (entry.uncompressedSize == 0) break;
        if (!inflateRaw(compressed, entry.uncompressedSize, output))
            throw Failure(Kind::Corrupt, QStringLiteral("can't inflate ") + entry.name);
        break;
    default:
        throw Failure(Kind::Unsupported, QStringLiteral("compression method %1 for %2").arg(entry.method).arg(entry.name));
    }
    if (CRC32::checksum(output) != entry.crc32)
        throw Failure(Kind::Corrupt, QStringLiteral("checksum mismatch for ") + entry.name);
    return output;
}

namespace CRC32 {

uint32_t checksum(const QByteArray &data) {
    static const std::array<uint32_t, 256> table = [] {
        std::array<uint32_t, 256> t{};
        for (uint32_t n = 0; n < 256; ++n) {
            uint32_t c = n;
            for (int k = 0; k < 8; ++k) c = (c & 1) ? 0xEDB88320u ^ (c >> 1) : c >> 1;
            t[n] = c;
        }
        return t;
    }();
    uint32_t crc = 0xFFFFFFFFu;
    for (char byte : data) crc = table[(crc ^ uchar(byte)) & 0xFF] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

} // namespace CRC32

} // namespace wp
