#pragma once
// Port of StudyStore.swift.
#include "core/StudyModel.h"
#include "extract/Extraction.h"
#include "study/Study.h"

#include <QJsonDocument>
#include <QList>
#include <QString>
#include <functional>
#include <mutex>
#include <optional>
#include <vector>

namespace wp {

/// Collapses runs of whitespace, including newlines, into single spaces.
QString oneLine(const QString &text);
inline QString oneLine(const std::optional<QString> &text) { return text ? oneLine(*text) : QString(); }

/// `★`, `○` or `✕`.
QString importanceSymbol(Importance importance);

/// Imported documents, their chunks and the points Claude saved, persisted as small JSON files under
/// `directory`. Re-importing an identical file (same content hash) returns the existing import.
/// Thread-safe.
///
/// Layout: `index.json` (id counter, imports, hashes, finished chunks), `<id>/chunks/<n>.json` and
/// `<id>/points/<n>.json` (the same files as the macOS app). Only the index is kept in memory; chunk
/// text and points are read from disk when asked for.
///
/// Methods throw StudyError, ExtractionError (core/extract) as the Swift ones throw.
class StudyStore {
public:
    using Extract = std::function<ExtractedDocument(const QString &path)>;
    using Chunk = std::function<std::vector<ExtractedChunk>(const ExtractedDocument &)>;

    /// `%APPDATA%\Whiteprint\Study` (the Windows counterpart of Application Support/Whiteprint/Study).
    static QString defaultDirectory();

    /// Opens (or creates) the store. `extract` and `chunk` default to the real extractor and
    /// chunker (empty function = default); tests inject fakes.
    explicit StudyStore(const QString &directory = defaultDirectory(), Extract extract = {}, Chunk chunk = {});

    StudyStore(const StudyStore &) = delete;
    StudyStore &operator=(const StudyStore &) = delete;

    const QString &directory() const { return m_directory; }

    /// Extracts and chunks the file. Slow for big files: call off the GUI thread.
    StudyImport importFile(const QString &path);

    QList<StudyImport> imports() const;
    std::optional<StudyImport> importInfo(const QString &id) const;

    /// Chunk text for `read_chunk`, headed with `name · chunk n/N · refs`.
    QString chunk(const QString &importID, int n) const;

    /// Replaces any points saved earlier for that chunk. An empty list still marks the chunk as done.
    /// Whitespace is tidied and refs are prefixed with the file name (`slide 4` -> `Lecture3.pptx · slide 4`).
    void savePoints(const QList<StudyPoint> &points, const QString &importID, int chunk);

    /// All saved points, of one import or of all (nullopt), in document order.
    QList<StudyPoint> points(const std::optional<QString> &importID = std::nullopt) const;

    /// Compact text listing of saved points for `get_points`: one line each, `★|○|✕ text (ref) [topic]`,
    /// grouped by import.
    QString pointsSummary(const std::optional<QString> &importID = std::nullopt) const;

    void remove(const QString &importID);
    /// Removes every import. Ids stay used, so a new import never gets an old id.
    void removeAll();

    /// `[1, 2, 3, 7]` -> `1–3, 7`.
    static QString ranges(const QList<int> &numbers);

private:
    struct Entry {
        StudyImport info;
        /// SHA-256 of the file's bytes.
        QString hash;
        /// Chunk numbers with saved points, ascending.
        QList<int> done;
    };
    struct Index {
        int lastID = 0;
        QList<Entry> entries;
    };

    static QJsonObject toJson(const Index &index);
    static Index indexFromJson(const QJsonObject &object);

    static QString chunkPath(const QString &id, int n);
    static QString pointsPath(const QString &id, int n);
    const Entry &entry(const QString &id) const;
    static void check(int n, const Entry &entry);
    QList<Entry> selectedEntries(const std::optional<QString> &importID) const;
    QList<StudyPoint> loadPoints(const Entry &entry) const;
    void save(const Index &updated);
    void write(const QByteArray &json, const QString &path) const;
    QJsonDocument read(const QString &path) const;
    static QString contentHash(const QString &path);

    QString m_directory;
    Extract m_extract;
    Chunk m_chunk;
    mutable std::mutex m_lock;
    Index m_index;
};

} // namespace wp
