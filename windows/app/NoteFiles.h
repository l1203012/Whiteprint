#pragma once
// Port of NoteFiles.swift: moves, renames and trashes notes and folders. (No NSFileCoordinator on
// Windows: open documents are told about moves by NoteDocuments::relocate, see NoteFileActions.)
#include <QString>
#include <optional>

namespace wp {

namespace NoteFiles {

struct FileError {
    enum class Kind { intoItself, nameTaken, badName, failed };
    Kind kind = Kind::failed;
    /// The item name, or the system's message for `failed`.
    QString name;

    static FileError intoItself(QString name) { return {Kind::intoItself, std::move(name)}; }
    static FileError nameTaken(QString name) { return {Kind::nameTaken, std::move(name)}; }
    static FileError badName(QString name) { return {Kind::badName, std::move(name)}; }
    static FileError failed(QString message) { return {Kind::failed, std::move(message)}; }

    QString description() const;
};

/// Moves a note or folder into `folder`, keeping its name (or counting up when it's taken). Returns
/// the new canonical path; unchanged when it's already there. Throws FileError.
QString move(const QString &item, const QString &folder);

/// Renames a note (keeping its extension) or a folder. Returns the new canonical path. Throws FileError.
QString rename(const QString &item, const QString &name);

/// Moves a note or folder to the Recycle Bin. Throws FileError.
void trash(const QString &item);

/// Whether `item` is `folder` or somewhere inside it.
bool isInside(const QString &item, const QString &folder);

/// `item` with its `old` prefix replaced by `newPath`, or nullopt when it isn't inside `old`.
std::optional<QString> relocated(const QString &item, const QString &old, const QString &newPath);

} // namespace NoteFiles

} // namespace wp
