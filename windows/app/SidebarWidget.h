#pragma once
// Port of SidebarViewController.swift: the Notion-style source list with search, the notes folder
// tree, the current note's pages, study material and flashcard decks, and "New note".
#include "app/SidebarModel.h"
#include "core/Note.h"

#include <QHash>
#include <QImage>
#include <QPointer>
#include <QSet>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QTreeWidget>
#include <QWidget>
#include <functional>
#include <optional>

class QLineEdit;
class QMenu;
class QPushButton;

namespace wp {

class NoteDocument;
class SidebarWidget;

/// The outline: drag and drop of notes and folders, and a few signals the sidebar reacts to.
class SidebarTree : public QTreeWidget {
    Q_OBJECT
public:
    explicit SidebarTree(SidebarWidget *owner);

    /// The row a drag currently hovers (drawn as a highlighted drop target).
    QTreeWidgetItem *dropTarget() const { return m_dropTarget; }

protected:
    void mousePressEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    void startDrag(Qt::DropActions supportedActions) override;
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dragLeaveEvent(QDragLeaveEvent *event) override;
    void dropEvent(QDropEvent *event) override;
    void contextMenuEvent(QContextMenuEvent *event) override;
    /// The delegate draws its own disclosure chevrons.
    void drawBranches(QPainter *, const QRect &, const QModelIndex &) const override {}

private:
    void setDropTarget(QTreeWidgetItem *item);

    SidebarWidget *m_owner;
    QTreeWidgetItem *m_dropTarget = nullptr;
};

class SidebarWidget : public QWidget {
    Q_OBJECT
public:
    /// What the sidebar needs from its window.
    struct Host {
        std::function<NoteDocument *()> document;
        std::function<Note()> note;
        std::function<int()> visiblePage;
        std::function<void(int)> scrollToPage;
    };

    explicit SidebarWidget(Host host, QWidget *parent = nullptr);

    /// Rebuilds every section (window became active, or a note was loaded).
    void reloadAll();
    /// The note's content changed: pages and thumbnails refresh after a short delay.
    void noteDidChange();
    /// The visible page changed.
    void visiblePageDidChange();
    /// Redraws the page thumbnails in the new theme's colours.
    void pageThemeDidChange();

    /// The folder new notes and folders go into (nullopt: the top level).
    std::optional<QString> selectedFolder() const;

    /// Creates a folder in `parent` (default: the top level) and starts naming it.
    void newFolder(const std::optional<QString> &parent);

    // Internals used by the tree and the tests.
    QTreeWidget *tree() const { return m_tree; }
    const SidebarNode *node(QTreeWidgetItem *item) const;
    QTreeWidgetItem *item(const std::function<bool(const SidebarNode &)> &matches) const;
    bool isRenaming() const { return m_renaming.has_value(); }
    void beginRename(QTreeWidgetItem *item);
    /// Finishes a rename with `text` (`cancelled` leaves the name alone).
    void endRename(const QString &text, bool cancelled);
    QImage thumbnail(int pageNumber);
    QSet<QString> expandedFolders() const;
    void setExpandedFolders(const QSet<QString> &paths);
    void rowClicked(QTreeWidgetItem *item);
    void toggleExpansion(QTreeWidgetItem *item);

    /// Drop handling (also used by the tests).
    std::optional<QString> dropTargetFolder(QTreeWidgetItem *target) const;
    bool canDrop(QTreeWidgetItem *target, const QStringList &paths) const;
    bool acceptDrop(QTreeWidgetItem *target, const QStringList &paths);
    QStringList draggedPaths(const QList<QTreeWidgetItem *> &items) const;

    void showContextMenu(QTreeWidgetItem *item, const QPoint &globalPos);
    void runContextAction(const QString &action, QTreeWidgetItem *item);

    static QSize thumbnailSize() { return QSize(22, 30); }
    int currentVisiblePage() const;

private:
    void reloadNotes();
    void reloadStudy();
    void rebuildNotes();
    void rebuildPages();
    void rebuildStudy();
    void populate(QTreeWidgetItem *parent, const QList<SidebarNode> &nodes);
    void selectCurrentNote();
    void restoreExpansion(bool revealingCurrentNote);
    void itemExpandedOrCollapsed(QTreeWidgetItem *item, bool expanded);
    QString currentURL() const;
    NoteDocument *document() const { return m_host.document ? m_host.document() : nullptr; }

    Host m_host;
    SidebarTree *m_tree;
    SidebarNode m_notes, m_pages, m_study;
    QTreeWidgetItem *m_notesItem = nullptr, *m_pagesItem = nullptr, *m_studyItem = nullptr;
    QHash<int, std::pair<NotePage, QImage>> m_thumbnails;
    QTimer m_pageTimer;
    std::optional<QString> m_selectedFolderPath;
    struct Renaming {
        QString url;
        QTreeWidgetItem *item = nullptr;
    };
    std::optional<Renaming> m_renaming;
    bool m_restoring = false;
    QPointer<QMenu> m_menu;

    friend class SidebarTree;
    friend class SidebarDelegate;
};

} // namespace wp
