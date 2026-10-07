#pragma once
// The pure logic behind SidebarViewController.swift: the rows of the source list (SidebarNode), how
// they are built from the notes tree, the pages and the study material, and the rules for drops,
// selection and remembered folder expansion. No widgets here, so it is unit-testable.
#include "app/DeckCatalog.h"
#include "app/NoteEntry.h"
#include "app/NoteTree.h"
#include "core/Note.h"
#include "study/Study.h"

#include <QList>
#include <QSet>
#include <QString>
#include <QStringList>
#include <functional>
#include <optional>

namespace wp {

/// An outline row. Sections and folders own their children.
struct SidebarNode {
    enum class Kind { section, folder, note, page, studyImport, extracting, addMaterial, generate, deck };

    Kind kind = Kind::section;
    /// Section title, folder name, note title, page title, import or extracting file name, deck title.
    QString title;
    /// Folder: path relative to the notes folder.
    QString path;
    /// Folder: its directory; note: its file; deck: the note's file.
    QString url;
    /// Page: 1-based number.
    int number = 0;
    NoteEntry entry;
    StudyImport import;
    DeckCatalog::Entry deck;
    QList<SidebarNode> children;

    bool isSection() const { return kind == Kind::section; }
    bool isFolder() const { return kind == Kind::folder; }
    bool isNote() const { return kind == Kind::note; }
    std::optional<QString> folderPath() const { return isFolder() ? std::optional<QString>(path) : std::nullopt; }
    std::optional<QString> noteURL() const { return isNote() ? std::optional<QString>(url) : std::nullopt; }
    /// The file or folder the row stands for (folders and notes only).
    std::optional<QString> fileURL() const { return isFolder() || isNote() ? std::optional<QString>(url) : std::nullopt; }
    /// Sections expand, and folders that hold notes or subfolders; an empty folder has no chevron.
    bool isExpandable() const { return isSection() || (isFolder() && !children.isEmpty()); }
    /// Notes and folders are the selectable rows.
    bool isSelectable() const { return isFolder() || isNote(); }
    /// The trailing text of the row (`12/40` for an import, the cards to study of a deck, ...).
    QString detail() const;
};

namespace SidebarModel {

inline const QString expandedFoldersKey = QStringLiteral("SidebarExpandedFolders");

/// Folders (with their children) first, then the notes, as in `NoteTree`.
QList<SidebarNode> nodes(const NoteTree &tree, const QString &root);
QList<SidebarNode> pageNodes(const Note &note);
/// Imports, files being extracted, the "add material"/"generate" row, then every deck.
QList<SidebarNode> studyNodes(const QList<StudyImport> &imports, const QStringList &extracting,
                              const QList<DeckCatalog::Entry> &decks);

/// Depth-first search through `nodes`.
const SidebarNode *find(const QList<SidebarNode> &nodes, const std::function<bool(const SidebarNode &)> &matches);

/// The folder new notes and folders go into: the selected folder, or the selected note's folder when
/// it is inside the notes folder (nullopt: the top level).
std::optional<QString> selectedFolder(const SidebarNode *selected);

/// Where a drop on `target` (a folder or the Notes section) puts the files; nullopt when the row
/// takes no drops. (A note target is first replaced by its parent folder by the caller.)
std::optional<QString> dropFolder(const SidebarNode *target, const QString &notesRoot);

/// Whether dragging `items` into `folder` is a real move: none is the folder (or contains it) and
/// none is already directly inside it.
bool canMove(const QStringList &items, const QString &folder);

/// Dragged paths from inside the notes folder, canonical.
QStringList draggable(const QStringList &paths, const QString &notesRoot);

/// A note outside the notes folder cannot be dragged; folders and notes of the folder can.
bool isDraggable(const SidebarNode &node);

/// `a/b/c` -> `a`, `a/b`, `a/b/c`.
QStringList ancestorPaths(const QString &folder);
/// The remembered expansion after `path` collapsed: it and everything below it forgotten.
QSet<QString> afterCollapse(const QSet<QString> &expanded, const QString &path);
/// `oldPath` replaced by `newPath` in the set when it was in it.
QSet<QString> afterRename(const QSet<QString> &expanded, const QString &oldPath, const QString &newPath);

/// Text of the "Move to the Recycle Bin?" alert.
struct TrashPrompt {
    QString message;
    QString detail;
};
TrashPrompt trashPrompt(const QString &item, bool isFolder, const QList<NoteEntry> &entries);

/// The context menu entries for a row (nullptr = empty space): title and action id.
struct MenuEntry {
    QString title;
    QString action;
    bool separator = false;
};
QList<MenuEntry> contextMenu(const SidebarNode *node);

} // namespace SidebarModel

} // namespace wp
