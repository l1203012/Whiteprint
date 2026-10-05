#include "study/StudyStore.h"

#include "core/JsonCoding.h"

#include <QCryptographicHash>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QSaveFile>
#include <QStandardPaths>
#include <algorithm>
#include <set>

namespace wp {

QString oneLine(const QString &text)
{
    return text.simplified();
}

QString importanceSymbol(Importance importance)
{
    switch (importance) {
    case Importance::must: return QStringLiteral("★");
    case Importance::good: return QStringLiteral("○");
    case Importance::skip: return QStringLiteral("✕");
    }
    return {};
}

namespace {

const QString indexFile = QStringLiteral("index.json");

/// Keeps the Swift behaviour: tidy a point's text, give it a file-name ref, drop it if empty.
std::optional<StudyPoint> tidy(const StudyPoint &point, const QString &fileName)
{
    const QString text = oneLine(point.text);
    if (text.isEmpty())
        return std::nullopt;
    QString ref = oneLine(point.ref);
    if (ref.isEmpty())
        ref = fileName;
    else if (!ref.startsWith(fileName))
        ref = fileName + QStringLiteral(" · ") + ref;
    std::optional<QString> topic;
    if (point.topic) {
        const QString t = oneLine(*point.topic);
        if (!t.isEmpty())
            topic = t;
    }
    return StudyPoint(text, point.importance, ref, topic);
}

QString summaryLine(const StudyPoint &point)
{
    QString line = QStringLiteral("%1 %2 (%3)").arg(importanceSymbol(point.importance), point.text, point.ref);
    if (point.topic)
        line += QStringLiteral(" [%1]").arg(*point.topic);
    return line;
}

QJsonObject chunkToJson(const ExtractedChunk &c)
{
    return QJsonObject{{"index", c.index}, {"firstRef", c.firstRef}, {"lastRef", c.lastRef}, {"text", c.text}};
}

ExtractedChunk chunkFromJson(const QJsonObject &o)
{
    ExtractedChunk c;
    c.index = json::intValue(o.value("index"), "index");
    c.firstRef = json::requiredString(o, "firstRef");
    c.lastRef = json::requiredString(o, "lastRef");
    c.text = json::requiredString(o, "text");
    return c;
}

} // namespace

QString StudyStore::defaultDirectory()
{
    QString base = qEnvironmentVariable("APPDATA");
    if (base.isEmpty())
        base = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation);
    return QDir(base).filePath(QStringLiteral("Whiteprint/Study"));
}

StudyStore::StudyStore(const QString &directory, Extract extract, Chunk chunk)
    : m_directory(directory), m_extract(std::move(extract)), m_chunk(std::move(chunk))
{
    if (!m_extract)
        m_extract = [](const QString &path) { return DocumentExtractor::extract(path); };
    if (!m_chunk)
        m_chunk = [](const ExtractedDocument &d) { return Chunker::chunks(d); };
    QDir().mkpath(m_directory);
    if (QFileInfo::exists(QDir(m_directory).filePath(indexFile))) {
        const QJsonDocument doc = read(indexFile);
        try {
            if (!doc.isObject())
                throw DecodingError{"not an object"};
            m_index = indexFromJson(doc.object());
        } catch (const DecodingError &) {
            throw StudyError::corrupt(indexFile);
        }
    }
}

// MARK: - Storage

QJsonObject StudyStore::toJson(const Index &index)
{
    QJsonArray entries;
    for (const Entry &e : index.entries) {
        QJsonArray done;
        for (int n : e.done)
            done.append(n);
        entries.append(QJsonObject{{"info", e.info.toJson()}, {"hash", e.hash}, {"done", done}});
    }
    return QJsonObject{{"lastID", index.lastID}, {"entries", entries}};
}

StudyStore::Index StudyStore::indexFromJson(const QJsonObject &o)
{
    Index index;
    index.lastID = json::intValue(o.value("lastID"), "lastID");
    for (const QJsonValue &v : json::requiredArray(o, "entries")) {
        const QJsonObject eo = json::objectValue(v, "entry");
        Entry e;
        e.info = StudyImport::fromJson(json::objectValue(eo.value("info"), "info"));
        e.hash = json::requiredString(eo, "hash");
        for (const QJsonValue &n : json::requiredArray(eo, "done"))
            e.done.append(json::intValue(n, "done"));
        index.entries.append(std::move(e));
    }
    return index;
}

QString StudyStore::chunkPath(const QString &id, int n) { return QStringLiteral("%1/chunks/%2.json").arg(id).arg(n); }
QString StudyStore::pointsPath(const QString &id, int n) { return QStringLiteral("%1/points/%2.json").arg(id).arg(n); }

const StudyStore::Entry &StudyStore::entry(const QString &id) const
{
    for (const Entry &e : m_index.entries) {
        if (e.info.id == id)
            return e;
    }
    throw StudyError::unknownImport(id);
}

void StudyStore::check(int n, const Entry &entry)
{
    if (n < 1 || n > entry.info.chunkCount)
        throw StudyError::chunkOutOfRange(n, entry.info.chunkCount);
}

QList<StudyStore::Entry> StudyStore::selectedEntries(const std::optional<QString> &importID) const
{
    std::lock_guard lock(m_lock);
    if (importID)
        return {entry(*importID)};
    return m_index.entries;
}

QList<StudyPoint> StudyStore::loadPoints(const Entry &entry) const
{
    QList<StudyPoint> result;
    for (int n : entry.done) {
        const QString path = pointsPath(entry.info.id, n);
        try {
            const QJsonDocument doc = read(path);
            if (!doc.isArray())
                throw DecodingError{"not an array"};
            for (const QJsonValue &v : doc.array())
                result.append(StudyPoint::fromJson(json::objectValue(v, "point")));
        } catch (const DecodingError &) {
            throw StudyError::corrupt(path);
        }
    }
    return result;
}

/// Writes the index, then adopts it, so a failed write leaves memory and disk in step.
void StudyStore::save(const Index &updated)
{
    write(QJsonDocument(toJson(updated)).toJson(QJsonDocument::Compact), indexFile);
    m_index = updated;
}

void StudyStore::write(const QByteArray &json, const QString &path) const
{
    const QString full = QDir(m_directory).filePath(path);
    QDir().mkpath(QFileInfo(full).absolutePath());
    QSaveFile file(full);
    if (!file.open(QIODevice::WriteOnly) || file.write(json) != json.size() || !file.commit())
        throw StudyError::corrupt(path);
}

QJsonDocument StudyStore::read(const QString &path) const
{
    QFile file(QDir(m_directory).filePath(path));
    if (!file.open(QIODevice::ReadOnly))
        throw StudyError::corrupt(path);
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(file.readAll(), &error);
    if (error.error != QJsonParseError::NoError)
        throw StudyError::corrupt(path);
    return doc;
}

/// Hashes the file in 1 MB blocks so big files don't sit in memory.
QString StudyStore::contentHash(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        throw ExtractionError::unreadable(QFileInfo(path).fileName());
    QCryptographicHash hash(QCryptographicHash::Sha256);
    while (!file.atEnd()) {
        const QByteArray block = file.read(1 << 20);
        if (block.isEmpty())
            break;
        hash.addData(block);
    }
    return QString::fromLatin1(hash.result().toHex());
}

// MARK: - API

StudyImport StudyStore::importFile(const QString &path)
{
    const QString hash = contentHash(path);
    {
        std::lock_guard lock(m_lock);
        for (const Entry &e : m_index.entries) {
            if (e.hash == hash)
                return e.info;
        }
    }

    const QString fileName = QFileInfo(path).fileName();
    const ExtractedDocument document = m_extract(path);
    const int unitCount = int(document.units.size());
    const std::vector<ExtractedChunk> chunks = m_chunk(document);
    if (chunks.empty())
        throw ExtractionError::empty(fileName);

    std::lock_guard lock(m_lock);
    for (const Entry &e : m_index.entries) {
        if (e.hash == hash)
            return e.info;
    }
    Index updated = m_index;
    updated.lastID += 1;
    const QString id = QStringLiteral("i%1").arg(updated.lastID);
    QDir(QDir(m_directory).filePath(id)).removeRecursively();
    for (const ExtractedChunk &c : chunks)
        write(QJsonDocument(chunkToJson(c)).toJson(QJsonDocument::Compact), chunkPath(id, c.index));
    const StudyImport info(id, fileName, unitCount, int(chunks.size()), 0, QDateTime::currentDateTimeUtc());
    updated.entries.append(Entry{info, hash, {}});
    save(updated);
    return info;
}

QList<StudyImport> StudyStore::imports() const
{
    std::lock_guard lock(m_lock);
    QList<StudyImport> result;
    for (const Entry &e : m_index.entries)
        result.append(e.info);
    return result;
}

std::optional<StudyImport> StudyStore::importInfo(const QString &id) const
{
    std::lock_guard lock(m_lock);
    for (const Entry &e : m_index.entries) {
        if (e.info.id == id)
            return e.info;
    }
    return std::nullopt;
}

QString StudyStore::chunk(const QString &importID, int n) const
{
    StudyImport info;
    {
        std::lock_guard lock(m_lock);
        const Entry &e = entry(importID);
        check(n, e);
        info = e.info;
    }
    const QString path = chunkPath(importID, n);
    ExtractedChunk c;
    try {
        const QJsonDocument doc = read(path);
        if (!doc.isObject())
            throw DecodingError{"not an object"};
        c = chunkFromJson(doc.object());
    } catch (const DecodingError &) {
        throw StudyError::corrupt(path);
    }
    const QString refs = c.firstRef == c.lastRef ? c.firstRef : c.firstRef + QStringLiteral("–") + c.lastRef;
    return QStringLiteral("%1 · chunk %2/%3 · %4\n\n%5").arg(info.name).arg(n).arg(info.chunkCount).arg(refs, c.text);
}

void StudyStore::savePoints(const QList<StudyPoint> &points, const QString &importID, int chunk)
{
    std::lock_guard lock(m_lock);
    qsizetype position = -1;
    for (qsizetype i = 0; i < m_index.entries.size(); ++i) {
        if (m_index.entries[i].info.id == importID)
            position = i;
    }
    if (position < 0)
        throw StudyError::unknownImport(importID);
    check(chunk, m_index.entries[position]);
    const QString name = m_index.entries[position].info.name;
    QJsonArray array;
    for (const StudyPoint &p : points) {
        if (const auto tidied = tidy(p, name))
            array.append(tidied->toJson());
    }
    write(QJsonDocument(array).toJson(QJsonDocument::Compact), pointsPath(importID, chunk));

    Index updated = m_index;
    Entry &e = updated.entries[position];
    std::set<int> done(e.done.begin(), e.done.end());
    done.insert(chunk);
    e.done = QList<int>(done.begin(), done.end());
    e.info.chunksDone = int(done.size());
    save(updated);
}

QList<StudyPoint> StudyStore::points(const std::optional<QString> &importID) const
{
    QList<StudyPoint> result;
    for (const Entry &e : selectedEntries(importID))
        result += loadPoints(e);
    return result;
}

QString StudyStore::pointsSummary(const std::optional<QString> &importID) const
{
    const QList<Entry> entries = selectedEntries(importID);
    if (entries.isEmpty())
        return QStringLiteral("no imports");
    QStringList blocks;
    for (const Entry &e : entries) {
        QString header = QStringLiteral("%1 %2 · chunks %3/%4").arg(e.info.id, e.info.name).arg(e.done.size()).arg(e.info.chunkCount);
        QList<int> missing;
        for (int n = 1; n <= e.info.chunkCount; ++n) {
            if (!e.done.contains(n))
                missing.append(n);
        }
        if (!missing.isEmpty())
            header += QStringLiteral(" · not done: ") + ranges(missing);
        QStringList lines{header};
        const QList<StudyPoint> loaded = loadPoints(e);
        if (loaded.isEmpty()) {
            lines.append(QStringLiteral("(no points)"));
        } else {
            for (const StudyPoint &p : loaded)
                lines.append(summaryLine(p));
        }
        blocks.append(lines.join('\n'));
    }
    return blocks.join(QStringLiteral("\n\n"));
}

void StudyStore::remove(const QString &importID)
{
    std::lock_guard lock(m_lock);
    entry(importID);
    Index updated = m_index;
    updated.entries.removeIf([&](const Entry &e) { return e.info.id == importID; });
    save(updated);
    QDir(QDir(m_directory).filePath(importID)).removeRecursively();
}

void StudyStore::removeAll()
{
    std::lock_guard lock(m_lock);
    Index updated = m_index;
    QStringList ids;
    for (const Entry &e : updated.entries)
        ids.append(e.info.id);
    updated.entries.clear();
    save(updated);
    for (const QString &id : ids)
        QDir(QDir(m_directory).filePath(id)).removeRecursively();
}

QString StudyStore::ranges(const QList<int> &numbers)
{
    if (numbers.isEmpty())
        return {};
    QStringList parts;
    int start = numbers[0], end = numbers[0];
    for (qsizetype i = 1; i <= numbers.size(); ++i) {
        if (i < numbers.size() && numbers[i] == end + 1) {
            end = numbers[i];
            continue;
        }
        parts.append(start == end ? QString::number(start) : QStringLiteral("%1–%2").arg(start).arg(end));
        if (i < numbers.size()) {
            start = numbers[i];
            end = numbers[i];
        }
    }
    return parts.join(QStringLiteral(", "));
}

} // namespace wp
