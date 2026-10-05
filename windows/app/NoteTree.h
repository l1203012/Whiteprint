#pragma once
// Port of NoteTree.swift.
#include "app/NoteEntry.h"

#include <QList>
#include <QString>
#include <QStringList>

namespace wp {

/// The notes folder as the sidebar shows it: in each folder, subfolders first (alphabetical), then
/// notes in listing order.
struct NoteTree {
    QString name;
    /// Relative to the notes folder; `""` for the root.
    QString path;
    QList<NoteTree> folders;
    QList<NoteEntry> notes;

    bool operator==(const NoteTree &) const = default;

    /// Notes saved outside the notes folder (`folder == nullopt`) go last in the root.
    static NoteTree build(const QList<NoteEntry> &entries, const QStringList &folders);

private:
    void insertFolder(const QStringList &parts);
    void insert(const NoteEntry &entry, const QStringList &parts);
    int childIndex(const QString &name);
    void sortFolders();
};

} // namespace wp
