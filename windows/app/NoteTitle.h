#pragma once
// Port of NoteTitle.swift. A missing file path is the empty string.
#include "core/Note.h"

#include <QString>
#include <optional>

namespace wp {

/// How a note is named in the window title, sidebar and `list_notes`.
namespace NoteTitle {

inline const QString untitled = QStringLiteral("Untitled");

/// The front matter `title`, falling back to the file name (or `Untitled` without a file).
QString display(const Note &note, const QString &filePath);

/// The new `title` after the file moved from `oldPath` to `newPath`, or nullopt when the file kept
/// its name (a save, a move to another folder) or either path is empty.
std::optional<QString> titleAfterRename(const QString &oldPath, const QString &newPath);

/// The file name without its extension (`/notes/A.wprint` -> `A`).
QString baseName(const QString &path);

} // namespace NoteTitle

} // namespace wp
