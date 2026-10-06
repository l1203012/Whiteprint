#pragma once
// Port of NoteFileActions.swift: sidebar file operations with their side effects (non-UI part).
#include <QString>
#include <QStringList>

namespace wp {

/// Open documents follow their files, Claude's note ids and flashcard progress move along, and the
/// library refreshes (all through AppServices::shared()). Every function throws NoteFiles::FileError,
/// FileIOError or a NotesLibrary read error; use `errorLine` for the message.
namespace NoteFileActions {

/// Moves notes and folders into `folder`.
void move(const QStringList &items, const QString &folder);

/// Renames a folder or a note and returns its new path. A note's title follows its new name; an open
/// note's document takes care of that itself.
QString rename(const QString &item, const QString &name);

/// Closes the notes open from `item`, then moves it to the Recycle Bin.
void trash(const QString &item);

} // namespace NoteFileActions

} // namespace wp
