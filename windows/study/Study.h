#pragma once
// Port of Study.swift: the study module's shared value and error types.
#include <QByteArray>
#include <QDateTime>
#include <QJsonObject>
#include <QString>
#include <exception>

namespace wp {

struct StudyImport {
    /// `i1`, `i2`, ... never reused.
    QString id;
    /// File name, e.g. `Lecture3.pptx`.
    QString name;
    /// Pages, slides or sections found.
    int unitCount = 0;
    int chunkCount = 0;
    /// How many chunks have saved points.
    int chunksDone = 0;
    QDateTime importedAt;

    StudyImport() = default;
    StudyImport(QString id, QString name, int unitCount, int chunkCount, int chunksDone, QDateTime importedAt)
        : id(std::move(id)), name(std::move(name)), unitCount(unitCount), chunkCount(chunkCount),
          chunksDone(chunksDone), importedAt(std::move(importedAt))
    {
    }
    bool operator==(const StudyImport &) const = default;

    /// Same shape as Swift's Codable output (`importedAt` = seconds since 2001-01-01 UTC).
    QJsonObject toJson() const;
    /// Throws DecodingError (core/JsonCoding.h).
    static StudyImport fromJson(const QJsonObject &object);
};

class StudyError : public std::exception {
public:
    enum class Kind { unknownImport, chunkOutOfRange, corrupt, alreadyRunning };

    Kind kind() const { return m_kind; }
    /// "no import 'i9'" etc., the same wording as the Swift app.
    QString description() const;
    const char *what() const noexcept override { return m_what.constData(); }

    static StudyError unknownImport(const QString &id) { return {Kind::unknownImport, id, 0, 0}; }
    static StudyError chunkOutOfRange(int n, int chunkCount) { return {Kind::chunkOutOfRange, {}, n, chunkCount}; }
    /// A study data file couldn't be read or decoded (path relative to the store).
    static StudyError corrupt(const QString &path) { return {Kind::corrupt, path, 0, 0}; }
    /// A runner's `start` was called while a run is in progress.
    static StudyError alreadyRunning() { return {Kind::alreadyRunning, {}, 0, 0}; }

    bool operator==(const StudyError &o) const
    {
        return m_kind == o.m_kind && m_text == o.m_text && m_n == o.m_n && m_count == o.m_count;
    }

private:
    StudyError(Kind kind, QString text, int n, int count);
    Kind m_kind;
    QString m_text;
    int m_n;
    int m_count;
    QByteArray m_what;
};

} // namespace wp
