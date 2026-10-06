#pragma once
// Port of ZipArchive.swift: a minimal read-only ZIP reader, enough for Office Open XML packages.
//
// Only the central directory is parsed up front; entries are inflated one at a time on request. Supports
// stored and deflated entries (inflate is a self-contained implementation, no zlib needed), not ZIP64,
// encryption or multi-disk archives.
#include <QByteArray>
#include <QHash>
#include <QString>
#include <cstdint>
#include <exception>
#include <optional>
#include <vector>

namespace wp {

class ZipArchive {
public:
    class Failure : public std::exception {
    public:
        enum class Kind { NotAZip, Corrupt, Unsupported, TooLarge };
        Failure(Kind kind, QString detail = {});
        Kind kind() const { return m_kind; }
        /// Corrupt/Unsupported: what is wrong; TooLarge: the entry name.
        const QString &detail() const { return m_detail; }
        const char *what() const noexcept override { return m_what.constData(); }

    private:
        Kind m_kind;
        QString m_detail;
        QByteArray m_what;
    };

    struct Entry {
        QString name;
        uint16_t method = 0;
        uint16_t flags = 0;
        uint32_t crc32 = 0;
        qint64 compressedSize = 0;
        qint64 uncompressedSize = 0;
        qint64 localHeaderOffset = 0;
    };

    /// Entries larger than this when inflated are refused (zip bombs, corrupt sizes).
    static constexpr qint64 maxEntrySize = 64ll << 20;

    /// Opens the file at `path`. Throws Failure (or Failure::NotAZip if it can't be read).
    explicit ZipArchive(const QString &path);
    explicit ZipArchive(const QByteArray &data);

    /// Entries in central directory order.
    const std::vector<Entry> &entries() const { return m_entries; }
    const Entry *entry(const QString &name) const;

    /// Inflates the named entry, or returns nullopt if the archive has no such entry.
    std::optional<QByteArray> contents(const QString &name) const;
    QByteArray contents(const Entry &entry) const;

private:
    void parse();

    QByteArray m_data;
    std::vector<Entry> m_entries;
    QHash<QString, int> m_index;
};

namespace CRC32 {
uint32_t checksum(const QByteArray &data);
}

/// Raw DEFLATE decoder. Returns false when the stream is malformed or doesn't produce exactly `size` bytes.
bool inflateRaw(const QByteArray &compressed, qint64 size, QByteArray &out);

} // namespace wp
