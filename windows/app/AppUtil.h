#pragma once
// Small path and text helpers shared by the app logic. Paths are plain QStrings with forward slashes
// (Qt style); the Swift `URL` becomes a path, and `URL.canonicalFile` becomes `canonicalFile`.
#include <QByteArray>
#include <QString>
#include <QStringList>
#include <exception>
#include <optional>

namespace wp {

/// One spelling per file, so paths from the document system and from folder listings compare equal
/// (`./`, `..`, symlinks, short 8.3 names, case on Windows). Works for paths that don't exist yet by
/// resolving their nearest existing ancestor. Empty stays empty.
QString canonicalFile(const QString &path);

/// A key for dictionaries: `canonicalFile`, lowercased on Windows (case-insensitive file system).
QString pathKey(const QString &path);

/// `/a/b/c` -> `a`, `b`, `c` (the root is dropped; `C:` drive letters are kept). Canonicalises first.
QStringList pathComponents(const QString &path);

/// Whether two path components are the same name (case-insensitive on Windows).
bool samePathPart(const QString &a, const QString &b);

/// `~` and `~/x` expanded to the home directory, like Foundation's `expandingTildeInPath`.
QString expandingTildeInPath(const QString &path);

/// `path` without its last component (`/a/b.wprint` -> `/a`).
QString deletingLastPathComponent(const QString &path);
/// `dir` + `/` + `name`, cleaned.
QString appendingPathComponent(const QString &dir, const QString &name);

/// I/O failure with a sentence for the user.
class FileIOError : public std::exception {
public:
    explicit FileIOError(QString message) : m_message(std::move(message)), m_what(m_message.toUtf8()) {}
    QString description() const { return m_message; }
    const char *what() const noexcept override { return m_what.constData(); }

private:
    QString m_message;
    QByteArray m_what;
};

/// Writes `data` to `path` through a temporary file and an atomic rename (like `.atomic`).
/// Creates no directories. Throws FileIOError.
void writeFileAtomically(const QString &path, const QByteArray &data);
inline void writeFileAtomically(const QString &path, const QString &text)
{
    writeFileAtomically(path, text.toUtf8());
}

/// The file's text, or nullopt when it can't be read or isn't valid UTF-8.
std::optional<QString> readUtf8File(const QString &path);
/// Decodes strictly; nullopt for invalid UTF-8.
std::optional<QString> decodeUtf8(const QByteArray &data);

/// The first `count` user-perceived characters (Swift's `prefix` on a String).
QString prefixGraphemes(const QString &text, int count);

/// `first` uppercased, the rest unchanged.
QString capitalizedFirst(const QString &text);

} // namespace wp
