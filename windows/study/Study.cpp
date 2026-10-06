#include "study/Study.h"

#include "core/JsonCoding.h"

#include <QTimeZone>

namespace wp {

namespace {
constexpr qint64 referenceDateOffsetMs = 978307200000; // 2001-01-01T00:00:00Z
}

QJsonObject StudyImport::toJson() const
{
    const double seconds = double(importedAt.toMSecsSinceEpoch() - referenceDateOffsetMs) / 1000.0;
    return QJsonObject{{"id", id},
                       {"name", name},
                       {"unitCount", unitCount},
                       {"chunkCount", chunkCount},
                       {"chunksDone", chunksDone},
                       {"importedAt", seconds}};
}

StudyImport StudyImport::fromJson(const QJsonObject &o)
{
    StudyImport s;
    s.id = json::requiredString(o, "id");
    s.name = json::requiredString(o, "name");
    s.unitCount = json::intValue(o.value("unitCount"), "unitCount");
    s.chunkCount = json::intValue(o.value("chunkCount"), "chunkCount");
    s.chunksDone = json::intValue(o.value("chunksDone"), "chunksDone");
    const QJsonValue at = o.value("importedAt");
    if (!at.isDouble())
        throw DecodingError{QStringLiteral("missing or invalid date 'importedAt'")};
    s.importedAt = QDateTime::fromMSecsSinceEpoch(qRound64(at.toDouble() * 1000.0) + referenceDateOffsetMs, QTimeZone::UTC);
    return s;
}

StudyError::StudyError(Kind kind, QString text, int n, int count)
    : m_kind(kind), m_text(std::move(text)), m_n(n), m_count(count)
{
    m_what = description().toUtf8();
}

QString StudyError::description() const
{
    switch (m_kind) {
    case Kind::unknownImport:
        return QStringLiteral("no import '%1'").arg(m_text);
    case Kind::chunkOutOfRange:
        return QStringLiteral("chunk %1 doesn't exist (import has %2)").arg(m_n).arg(m_count);
    case Kind::corrupt:
        return QStringLiteral("study data file '%1' is damaged").arg(m_text);
    case Kind::alreadyRunning:
        return QStringLiteral("a study plan is already being generated");
    }
    return {};
}

} // namespace wp
