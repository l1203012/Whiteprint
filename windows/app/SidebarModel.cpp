#include "app/SidebarModel.h"

#include "app/AppUtil.h"
#include "app/NoteFiles.h"
#include "app/NoteTitle.h"
#include "app/PageOutline.h"

#include <QFileInfo>

namespace wp {

QString SidebarNode::detail() const
{
    switch (kind) {
    case Kind::page:
        return QString::number(number);
    case Kind::studyImport:
        return QStringLiteral("%1/%2").arg(import.chunksDone).arg(import.chunkCount);
    case Kind::extracting:
        return QStringLiteral("…");
    case Kind::deck: {
        const int toStudy = deck.summary.due + deck.summary.newCount;
        return toStudy > 0 ? QString::number(toStudy) : QStringLiteral("✓");
    }
    default:
        return {};
    }
}

namespace SidebarModel {

QList<SidebarNode> nodes(const NoteTree &tree, const QString &root)
{
    QList<SidebarNode> result;
    for (const NoteTree &folder : tree.folders) {
        SidebarNode node;
        node.kind = SidebarNode::Kind::folder;
        node.path = folder.path;
        node.title = folder.name;
        node.url = appendingPathComponent(root, folder.path);
        node.children = nodes(folder, root);
        result.append(node);
    }
    for (const NoteEntry &entry : tree.notes) {
        SidebarNode node;
        node.kind = SidebarNode::Kind::note;
        node.entry = entry;
        node.title = entry.title;
        node.url = entry.url;
        result.append(node);
    }
    return result;
}

QList<SidebarNode> pageNodes(const Note &note)
{
    QList<SidebarNode> result;
    const QStringList titles = PageOutline::titles(note);
    for (int i = 0; i < titles.size(); ++i) {
        SidebarNode node;
        node.kind = SidebarNode::Kind::page;
        node.number = i + 1;
        node.title = titles[i];
        result.append(node);
    }
    return result;
}

QList<SidebarNode> studyNodes(const QList<StudyImport> &imports, const QStringList &extracting,
                              const QList<DeckCatalog::Entry> &decks)
{
    QList<SidebarNode> result;
    for (const StudyImport &item : imports) {
        SidebarNode node;
        node.kind = SidebarNode::Kind::studyImport;
        node.import = item;
        node.title = item.name;
        result.append(node);
    }
    for (const QString &name : extracting) {
        SidebarNode node;
        node.kind = SidebarNode::Kind::extracting;
        node.title = name;
        result.append(node);
    }
    SidebarNode action;
    action.kind = imports.isEmpty() ? SidebarNode::Kind::addMaterial : SidebarNode::Kind::generate;
    action.title = imports.isEmpty() ? QStringLiteral("Add course material…") : QStringLiteral("Generate study plan");
    result.append(action);
    for (const DeckCatalog::Entry &entry : decks) {
        SidebarNode node;
        node.kind = SidebarNode::Kind::deck;
        node.deck = entry;
        node.title = entry.title();
        node.url = entry.note;
        result.append(node);
    }
    return result;
}

const SidebarNode *find(const QList<SidebarNode> &nodes, const std::function<bool(const SidebarNode &)> &matches)
{
    for (const SidebarNode &node : nodes) {
        if (matches(node))
            return &node;
        if (const SidebarNode *found = find(node.children, matches))
            return found;
    }
    return nullptr;
}

std::optional<QString> selectedFolder(const SidebarNode *selected)
{
    if (!selected)
        return std::nullopt;
    if (selected->isFolder())
        return selected->url;
    if (selected->isNote())
        return selected->entry.folder ? std::optional<QString>(deletingLastPathComponent(selected->entry.url))
                                      : std::nullopt;
    return std::nullopt;
}

std::optional<QString> dropFolder(const SidebarNode *target, const QString &notesRoot)
{
    if (!target)
        return std::nullopt;
    if (target->isFolder())
        return canonicalFile(target->url);
    if (target->isSection() && target->title == QLatin1String("Notes"))
        return canonicalFile(notesRoot);
    return std::nullopt;
}

bool canMove(const QStringList &items, const QString &folder)
{
    if (items.isEmpty())
        return false;
    const QString target = canonicalFile(folder);
    for (const QString &item : items) {
        if (NoteFiles::isInside(target, item))
            return false;
        if (pathKey(deletingLastPathComponent(item)) == pathKey(target))
            return false;
    }
    return true;
}

QStringList draggable(const QStringList &paths, const QString &notesRoot)
{
    const QString root = canonicalFile(notesRoot);
    QStringList result;
    for (const QString &path : paths) {
        const QString canonical = canonicalFile(path);
        if (canonical.isEmpty() || pathKey(canonical) == pathKey(root) || !NoteFiles::isInside(canonical, root))
            continue;
        result.append(canonical);
    }
    return result;
}

bool isDraggable(const SidebarNode &node)
{
    if (node.isNote())
        return node.entry.folder.has_value();
    return node.isFolder();
}

QStringList ancestorPaths(const QString &folder)
{
    QStringList result;
    const QStringList parts = folder.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (int i = 0; i < parts.size(); ++i)
        result.append(parts.mid(0, i + 1).join(QLatin1Char('/')));
    return result;
}

QSet<QString> afterCollapse(const QSet<QString> &expanded, const QString &path)
{
    QSet<QString> result;
    for (const QString &p : expanded)
        if (p != path && !p.startsWith(path + QLatin1Char('/')))
            result.insert(p);
    return result;
}

QSet<QString> afterRename(const QSet<QString> &expanded, const QString &oldPath, const QString &newPath)
{
    if (!expanded.contains(oldPath))
        return expanded;
    QSet<QString> result = expanded;
    result.remove(oldPath);
    result.insert(newPath);
    return result;
}

TrashPrompt trashPrompt(const QString &item, bool isFolder, const QList<NoteEntry> &entries)
{
    TrashPrompt prompt;
    const QString name = isFolder ? QFileInfo(item).fileName() : NoteTitle::baseName(item);
    prompt.message = QStringLiteral("Move “%1” to the Recycle Bin?").arg(name);
    if (isFolder) {
        int count = 0;
        for (const NoteEntry &entry : entries)
            if (NoteFiles::isInside(entry.url, item))
                ++count;
        prompt.detail = count == 0
            ? QStringLiteral("The folder is empty.")
            : QStringLiteral("The folder and the %1 note%2 in it go to the Recycle Bin. Open notes are closed.")
                  .arg(count)
                  .arg(count == 1 ? QString() : QStringLiteral("s"));
    } else {
        prompt.detail = QStringLiteral("You can put it back from the Recycle Bin.");
    }
    return prompt;
}

QList<MenuEntry> contextMenu(const SidebarNode *node)
{
    QList<MenuEntry> m;
    auto add = [&](const char *title, const char *action) { m.append({QString::fromUtf8(title), QString::fromUtf8(action), false}); };
    auto sep = [&] { m.append({{}, {}, true}); };
    if (node && node->isFolder()) {
        add("New Note in Folder", "newNote");
        add("New Folder", "newFolder");
        sep();
        add("Rename", "rename");
        add("Show in Explorer", "show");
        sep();
        add("Move to Recycle Bin…", "trash");
    } else if (node && node->isNote()) {
        add("New Note", "newNote");
        sep();
        if (node->entry.folder)
            add("Rename", "rename");
        add("Show in Explorer", "show");
        if (node->entry.folder) {
            sep();
            add("Move to Recycle Bin…", "trash");
        }
    } else if (node && node->kind == SidebarNode::Kind::deck) {
        add("Study", "study");
        add("Open Note", "open");
    } else {
        add("New Note", "newNote");
        add("New Folder", "newFolder");
    }
    return m;
}

} // namespace SidebarModel

} // namespace wp
