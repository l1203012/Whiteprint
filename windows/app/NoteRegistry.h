#pragma once
// Port of NoteRegistry.swift.
#include <QHash>
#include <QString>
#include <optional>

namespace wp {

/// Session ids for notes (`n1`, `n2`, ...) as Claude sees them. An id stays bound to its file for the
/// whole session, and follows it when it's renamed.
class NoteRegistry {
public:
    /// The note's id, assigned on first use. `url` is a file path.
    QString id(const QString &forURL);

    /// The path bound to `id` (whitespace and case are ignored), or nullopt.
    std::optional<QString> url(const QString &forID) const;

    /// Keeps the ids of a file that was renamed or moved, or of every note in a folder that was.
    void move(const QString &from, const QString &to);

private:
    QHash<QString, QString> m_idsByPath;
    QHash<QString, QString> m_urlsByID;
    int m_next = 1;
};

} // namespace wp
