#include "app/NoteTree.h"

#include "app/NotesFolder.h"

#include <algorithm>

namespace wp {

NoteTree NoteTree::build(const QList<NoteEntry> &entries, const QStringList &folders)
{
    NoteTree root;
    for (const QString &path : folders)
        root.insertFolder(path.split(QLatin1Char('/'), Qt::SkipEmptyParts));
    QList<NoteEntry> elsewhere;
    for (const NoteEntry &entry : entries) {
        if (!entry.folder) {
            elsewhere.append(entry);
            continue;
        }
        root.insert(entry, entry.folder->split(QLatin1Char('/'), Qt::SkipEmptyParts));
    }
    root.notes += elsewhere;
    root.sortFolders();
    return root;
}

void NoteTree::insertFolder(const QStringList &parts)
{
    if (parts.isEmpty())
        return;
    const int index = childIndex(parts.first());
    folders[index].insertFolder(parts.mid(1));
}

void NoteTree::insert(const NoteEntry &entry, const QStringList &parts)
{
    if (parts.isEmpty()) {
        notes.append(entry);
        return;
    }
    const int index = childIndex(parts.first());
    folders[index].insert(entry, parts.mid(1));
}

int NoteTree::childIndex(const QString &childName)
{
    for (int i = 0; i < folders.size(); ++i)
        if (folders[i].name == childName)
            return i;
    NoteTree child;
    child.name = childName;
    child.path = path.isEmpty() ? childName : path + QLatin1Char('/') + childName;
    folders.append(child);
    return int(folders.size()) - 1;
}

void NoteTree::sortFolders()
{
    std::stable_sort(folders.begin(), folders.end(),
                     [](const NoteTree &a, const NoteTree &b) { return NotesFolder::precedes(a.name, b.name); });
    for (NoteTree &folder : folders)
        folder.sortFolders();
}

} // namespace wp
