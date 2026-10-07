#include "app/SidebarWidget.h"

#include "app/AppController.h"
#include "app/AppDefaults.h"
#include "app/AppEvents.h"
#include "app/AppServices.h"
#include "app/AppUtil.h"
#include "app/FlashcardStudyWindow.h"
#include "app/MacStyle.h"
#include "app/NoteDocuments.h"
#include "app/NoteFileActions.h"
#include "app/NoteTitle.h"
#include "app/UiKit.h"
#include "render/Fonts.h"
#include "render/PageThumbnail.h"

#include <QAbstractButton>
#include <QApplication>
#include <QContextMenuEvent>
#include <QDrag>
#include <QDragEnterEvent>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLineEdit>
#include <QMenu>
#include <QMessageBox>
#include <QMimeData>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QUrl>
#include <QVBoxLayout>
#include <exception>

namespace wp {

namespace {

constexpr int gutter = 14;
constexpr int rowInset = 8;

QColor withAlphaF(const QColor &c, double a)
{
    QColor r = c;
    r.setAlphaF(a);
    return r;
}

ui::Symbol symbolFor(const SidebarNode &node)
{
    switch (node.kind) {
    case SidebarNode::Kind::folder: return ui::Symbol::folder;
    case SidebarNode::Kind::note: return ui::Symbol::note;
    case SidebarNode::Kind::studyImport: return ui::Symbol::document;
    case SidebarNode::Kind::extracting: return ui::Symbol::hourglass;
    case SidebarNode::Kind::addMaterial: return ui::Symbol::tray;
    case SidebarNode::Kind::generate: return ui::Symbol::sparkles;
    case SidebarNode::Kind::deck: return ui::Symbol::deck;
    default: return ui::Symbol::note;
    }
}

/// A search-field look-alike that opens the command palette.
class SearchButton : public QAbstractButton {
public:
    explicit SearchButton(QWidget *parent = nullptr) : QAbstractButton(parent)
    {
        setFixedHeight(26);
        setCursor(Qt::ArrowCursor);
        setToolTip(QStringLiteral("Search notes and commands (Ctrl+K)"));
        setAccessibleName(QStringLiteral("Search notes and commands"));
        setFocusPolicy(Qt::TabFocus);
    }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const auto c = mac::colors();
        p.setPen(Qt::NoPen);
        p.setBrush(withAlphaF(c.text, underMouse() ? 0.10 : 0.07));
        p.drawRoundedRect(rect(), 6, 6);
        ui::drawSymbol(p, ui::Symbol::search, QRectF(8, (height() - 14) / 2.0, 14, 14), c.secondaryText);
        p.setFont(fonts::system(13));
        p.setPen(c.secondaryText);
        p.drawText(QRect(28, 0, width() - 70, height()), Qt::AlignVCenter | Qt::AlignLeft, QStringLiteral("Search"));
        p.setFont(fonts::system(11));
        p.setPen(withAlphaF(c.secondaryText, 0.75));
        p.drawText(QRect(0, 0, width() - 8, height()), Qt::AlignVCenter | Qt::AlignRight, QStringLiteral("Ctrl+K"));
    }
    void enterEvent(QEnterEvent *) override { update(); }
    void leaveEvent(QEvent *) override { update(); }
};

/// "+ New note", borderless.
class NewNoteButton : public QAbstractButton {
public:
    explicit NewNoteButton(QWidget *parent = nullptr) : QAbstractButton(parent)
    {
        setFixedHeight(26);
        setText(QStringLiteral("New note"));
        setToolTip(QStringLiteral("New note (Ctrl+N)"));
    }
    QSize sizeHint() const override { return QSize(110, 26); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        const auto c = mac::colors();
        if (underMouse()) {
            p.setPen(Qt::NoPen);
            p.setBrush(withAlphaF(c.text, 0.07));
            p.drawRoundedRect(rect(), 6, 6);
        }
        ui::drawSymbol(p, ui::Symbol::plus, QRectF(6, (height() - 14) / 2.0, 14, 14), c.secondaryText);
        p.setFont(fonts::system(13));
        p.setPen(c.secondaryText);
        p.drawText(QRect(26, 0, width() - 26, height()), Qt::AlignVCenter | Qt::AlignLeft, text());
    }
    void enterEvent(QEnterEvent *) override { update(); }
    void leaveEvent(QEvent *) override { update(); }
};

} // namespace

/// Paints the rows: pill selection in the accent colour, section headers, thumbnails.
class SidebarDelegate : public QStyledItemDelegate {
public:
    explicit SidebarDelegate(SidebarWidget *owner, SidebarTree *tree) : QStyledItemDelegate(tree), m_owner(owner), m_tree(tree) {}

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &index) const override
    {
        const SidebarNode *node = m_owner->node(m_tree->itemFromIndex(index));
        if (!node)
            return QSize(0, 26);
        if (node->kind == SidebarNode::Kind::page)
            return QSize(0, SidebarWidget::thumbnailSize().height() + 8);
        if (node->isSection())
            return QSize(0, 30);
        return QSize(0, 26);
    }

    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        QTreeWidgetItem *item = m_tree->itemFromIndex(index);
        const SidebarNode *node = m_owner->node(item);
        if (!node)
            return;
        const auto c = mac::colors();
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        const QRect row(0, option.rect.top(), m_tree->viewport()->width(), option.rect.height());

        if (node->isSection()) {
            p->setFont(fonts::system(11, QFont::DemiBold));
            p->setPen(withAlphaF(c.secondaryText, 0.85));
            p->drawText(row.adjusted(16, 4, -8, 0), Qt::AlignVCenter | Qt::AlignLeft, node->title);
            p->restore();
            return;
        }

        const bool selected = option.state & QStyle::State_Selected;
        const bool active = m_tree->window()->isActiveWindow();
        const QRect pill = row.adjusted(8, 1, -8, -1);
        const bool onAccent = selected && active;
        if (selected) {
            p->setPen(Qt::NoPen);
            p->setBrush(active ? c.accent : (mac::isDark() ? QColor(0x4a, 0x4a, 0x4e) : QColor(0xd0, 0xd0, 0xd6)));
            p->drawRoundedRect(pill, 6, 6);
        } else if ((option.state & QStyle::State_MouseOver) && node->isSelectable()) {
            p->setPen(Qt::NoPen);
            p->setBrush(withAlphaF(c.text, 0.06));
            p->drawRoundedRect(pill, 6, 6);
        }
        if (m_tree->dropTarget() == item) {
            p->setBrush(withAlphaF(c.accent, 0.16));
            p->setPen(QPen(c.accent, 2));
            p->drawRoundedRect(pill.adjusted(1, 1, -1, -1), 6, 6);
        }

        const QColor textColor = onAccent ? c.selectionText : c.text;
        const QColor mutedColor = onAccent ? withAlphaF(c.selectionText, 0.85) : c.secondaryText;
        const QColor detailColor = onAccent ? withAlphaF(c.selectionText, 0.75) : withAlphaF(c.secondaryText, 0.8);

        int x = option.rect.left() + rowInset - 2;
        const int cy = row.center().y() + 1;
        if (node->isFolder() && node->isExpandable()) {
            const bool expanded = item->isExpanded();
            QPen pen(mutedColor, 1.6, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin);
            p->setPen(pen);
            p->setBrush(Qt::NoBrush);
            const double ax = x + 5;
            QPainterPath chevron;
            if (expanded) {
                chevron.moveTo(ax - 3, cy - 1.5);
                chevron.lineTo(ax, cy + 1.5);
                chevron.lineTo(ax + 3, cy - 1.5);
            } else {
                chevron.moveTo(ax - 1.5, cy - 3.5);
                chevron.lineTo(ax + 1.5, cy);
                chevron.lineTo(ax - 1.5, cy + 3.5);
            }
            p->drawPath(chevron);
        }
        x += gutter;

        int right = pill.right() - 8;
        const QString detail = node->detail();
        if (!detail.isEmpty()) {
            p->setFont(fonts::monospaced(11));
            p->setPen(detailColor);
            const int w = p->fontMetrics().horizontalAdvance(detail);
            p->drawText(QRect(right - w, row.top(), w + 1, row.height()), Qt::AlignVCenter | Qt::AlignRight, detail);
            right -= w + 8;
        }

        if (node->kind == SidebarNode::Kind::page) {
            const QImage image = m_owner->thumbnail(node->number);
            if (!image.isNull()) {
                const QSize s = SidebarWidget::thumbnailSize();
                const QRect target(x, row.top() + (row.height() - s.height()) / 2, s.width(), s.height());
                p->drawImage(target, image);
                p->setPen(QPen(withAlphaF(c.text, 0.25), 1));
                p->setBrush(Qt::NoBrush);
                p->drawRoundedRect(QRectF(target).adjusted(0.5, 0.5, -0.5, -0.5), 2, 2);
                x += s.width() + 8;
            }
        } else {
            const bool muted = node->kind == SidebarNode::Kind::addMaterial || node->kind == SidebarNode::Kind::generate;
            ui::drawSymbol(*p, symbolFor(*node), QRectF(x, cy - 8, 16, 16), muted ? mutedColor : mutedColor);
            x += 16 + 7;
        }

        const bool emphasized = node->kind == SidebarNode::Kind::page && node->number == m_owner->currentVisiblePage();
        const bool muted = node->kind == SidebarNode::Kind::addMaterial || node->kind == SidebarNode::Kind::generate;
        p->setFont(fonts::system(13, emphasized ? QFont::Medium : QFont::Normal));
        p->setPen(muted ? mutedColor : textColor);
        const QRect textRect(x, row.top(), qMax(10, right - x), row.height());
        p->drawText(textRect, Qt::AlignVCenter | Qt::AlignLeft,
                    p->fontMetrics().elidedText(node->title, Qt::ElideRight, textRect.width()));
        p->restore();
    }

    QWidget *createEditor(QWidget *parent, const QStyleOptionViewItem &, const QModelIndex &) const override
    {
        auto *edit = new QLineEdit(parent);
        edit->setFont(fonts::system(13));
        edit->setFrame(true);
        return edit;
    }

    void setEditorData(QWidget *editor, const QModelIndex &index) const override
    {
        auto *edit = qobject_cast<QLineEdit *>(editor);
        const SidebarNode *node = m_owner->node(m_tree->itemFromIndex(index));
        if (!edit || !node)
            return;
        edit->setText(node->isNote() ? NoteTitle::baseName(node->url) : node->title);
        edit->selectAll();
    }

    void setModelData(QWidget *, QAbstractItemModel *, const QModelIndex &) const override {}

    void updateEditorGeometry(QWidget *editor, const QStyleOptionViewItem &option, const QModelIndex &) const override
    {
        const int x = option.rect.left() + rowInset - 2 + gutter + 16 + 3;
        editor->setGeometry(QRect(x, option.rect.top() + 1, m_tree->viewport()->width() - x - 10, option.rect.height() - 2));
    }

private:
    SidebarWidget *m_owner;
    SidebarTree *m_tree;
};

// MARK: SidebarTree

SidebarTree::SidebarTree(SidebarWidget *owner) : QTreeWidget(owner), m_owner(owner)
{
    setHeaderHidden(true);
    setColumnCount(1);
    setIndentation(12);
    setRootIsDecorated(false);
    setExpandsOnDoubleClick(false);
    setUniformRowHeights(false);
    setFrameShape(QFrame::NoFrame);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setSelectionMode(QAbstractItemView::SingleSelection);
    setFocusPolicy(Qt::StrongFocus);
    setAttribute(Qt::WA_MacShowFocusRect, false);
    setMouseTracking(true);
    viewport()->setAttribute(Qt::WA_Hover);
    viewport()->setAutoFillBackground(false);
    setAutoFillBackground(false);
    setStyleSheet(QStringLiteral("QTreeWidget { background: transparent; border: none; outline: none; }"
                                  "QTreeView::branch { image: none; border-image: none; background: transparent; }"
                                  "QTreeView::item:selected, QTreeView::item:hover { background: transparent; }"));
    setDragEnabled(true);
    setAcceptDrops(true);
    setDropIndicatorShown(false);
    setDragDropMode(QAbstractItemView::DragDrop);
    setDefaultDropAction(Qt::MoveAction);
    setAutoScroll(true);
    // The delegate paints the selection pill itself; keep the style from filling the row.
    QPalette pal = palette();
    for (auto group : {QPalette::Active, QPalette::Inactive, QPalette::Disabled})
        pal.setColor(group, QPalette::Highlight, Qt::transparent);
    setPalette(pal);
    setItemDelegate(new SidebarDelegate(owner, this));
}

void SidebarTree::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton) {
        if (QTreeWidgetItem *it = itemAt(event->position().toPoint())) {
            const SidebarNode *node = m_owner->node(it);
            if (node && node->isFolder() && node->isExpandable()) {
                const QRect r = visualItemRect(it);
                const int x = event->position().toPoint().x();
                if (x >= r.left() && x <= r.left() + rowInset + gutter + 2) {
                    m_owner->toggleExpansion(it);
                    event->accept();
                    return;
                }
            }
        }
    }
    QTreeWidget::mousePressEvent(event);
}

void SidebarTree::mouseDoubleClickEvent(QMouseEvent *event)
{
    QTreeWidgetItem *it = itemAt(event->position().toPoint());
    const SidebarNode *node = m_owner->node(it);
    if (node && node->isFolder()) {
        m_owner->toggleExpansion(it);
        event->accept();
        return;
    }
    QTreeWidget::mouseDoubleClickEvent(event);
}

void SidebarTree::contextMenuEvent(QContextMenuEvent *event)
{
    m_owner->showContextMenu(itemAt(event->pos()), event->globalPos());
    event->accept();
}

void SidebarTree::setDropTarget(QTreeWidgetItem *item)
{
    if (m_dropTarget == item)
        return;
    m_dropTarget = item;
    viewport()->update();
}

void SidebarTree::startDrag(Qt::DropActions)
{
    const QStringList paths = m_owner->draggedPaths(selectedItems());
    if (paths.isEmpty())
        return;
    auto *mime = new QMimeData;
    QList<QUrl> urls;
    for (const QString &p : paths)
        urls.append(QUrl::fromLocalFile(p));
    mime->setUrls(urls);
    QDrag drag(this);
    drag.setMimeData(mime);
    drag.exec(Qt::MoveAction | Qt::CopyAction, Qt::MoveAction);
    setDropTarget(nullptr);
}

static QStringList pathsOf(const QMimeData *mime)
{
    QStringList paths;
    if (!mime)
        return paths;
    for (const QUrl &url : mime->urls())
        if (url.isLocalFile())
            paths.append(url.toLocalFile());
    return paths;
}

void SidebarTree::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
    else
        event->ignore();
}

void SidebarTree::dragMoveEvent(QDragMoveEvent *event)
{
    QAbstractItemView::dragMoveEvent(event); // auto-scroll
    QTreeWidgetItem *target = itemAt(event->position().toPoint());
    if (target && m_owner->node(target) && m_owner->node(target)->isNote())
        target = target->parent();
    if (target && m_owner->canDrop(target, pathsOf(event->mimeData()))) {
        setDropTarget(target);
        event->setDropAction(Qt::MoveAction);
        event->accept();
    } else {
        setDropTarget(nullptr);
        event->ignore();
    }
}

void SidebarTree::dragLeaveEvent(QDragLeaveEvent *event)
{
    setDropTarget(nullptr);
    QTreeWidget::dragLeaveEvent(event);
}

void SidebarTree::dropEvent(QDropEvent *event)
{
    QTreeWidgetItem *target = itemAt(event->position().toPoint());
    if (target && m_owner->node(target) && m_owner->node(target)->isNote())
        target = target->parent();
    setDropTarget(nullptr);
    if (target && m_owner->canDrop(target, pathsOf(event->mimeData())) &&
        m_owner->acceptDrop(target, pathsOf(event->mimeData()))) {
        event->setDropAction(Qt::CopyAction); // the files moved already; never let the view remove rows
        event->accept();
    } else {
        event->ignore();
    }
}

// MARK: SidebarWidget

SidebarWidget::SidebarWidget(Host host, QWidget *parent) : QWidget(parent), m_host(std::move(host))
{
    setMinimumWidth(200);
    m_tree = new SidebarTree(this);
    auto *search = new SearchButton;
    auto *newNote = new NewNoteButton;
    connect(search, &QAbstractButton::clicked, this, [this] { AppController::instance().showCommandPalette(window()); });
    connect(newNote, &QAbstractButton::clicked, this, [] { AppController::instance().newNote(); });

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    auto *top = new QHBoxLayout;
    top->setContentsMargins(10, 8, 10, 6);
    top->addWidget(search);
    layout->addLayout(top);
    layout->addWidget(m_tree, 1);
    auto *bottom = new QHBoxLayout;
    bottom->setContentsMargins(10, 4, 10, 10);
    bottom->addWidget(newNote);
    bottom->addStretch();
    layout->addLayout(bottom);

    auto makeSection = [&](SidebarNode &section, const QString &title) {
        section.kind = SidebarNode::Kind::section;
        section.title = title;
        auto *item = new QTreeWidgetItem(m_tree);
        item->setText(0, title);
        item->setFlags(Qt::ItemIsEnabled);
        item->setData(0, Qt::UserRole, QVariant::fromValue<quintptr>(reinterpret_cast<quintptr>(&section)));
        return item;
    };
    m_notesItem = makeSection(m_notes, QStringLiteral("Notes"));
    m_pagesItem = makeSection(m_pages, QStringLiteral("Pages"));
    m_studyItem = makeSection(m_study, QStringLiteral("Study"));
    m_notesItem->setFlags(Qt::ItemIsEnabled | Qt::ItemIsDropEnabled);

    m_pageTimer.setSingleShot(true);
    m_pageTimer.setInterval(600);
    connect(&m_pageTimer, &QTimer::timeout, this, [this] {
        rebuildPages();
        selectCurrentNote();
    });
    connect(m_tree, &QTreeWidget::itemClicked, this, [this](QTreeWidgetItem *item, int) { rowClicked(item); });
    connect(m_tree, &QTreeWidget::itemActivated, this, [this](QTreeWidgetItem *item, int) {
        const SidebarNode *n = node(item);
        if (n && n->isFolder())
            toggleExpansion(item);
        else
            rowClicked(item);
    });
    connect(m_tree, &QTreeWidget::itemExpanded, this, [this](QTreeWidgetItem *item) { itemExpandedOrCollapsed(item, true); });
    connect(m_tree, &QTreeWidget::itemCollapsed, this, [this](QTreeWidgetItem *item) { itemExpandedOrCollapsed(item, false); });
    connect(m_tree->itemDelegate(), &QAbstractItemDelegate::closeEditor, this,
            [this](QWidget *editor, QAbstractItemDelegate::EndEditHint hint) {
                if (!m_renaming)
                    return;
                const auto *edit = qobject_cast<QLineEdit *>(editor);
                endRename(edit ? edit->text() : QString(), hint == QAbstractItemDelegate::RevertModelCache || !edit);
            });

    auto &services = AppServices::shared();
    connect(&services.library(), &NotesLibrary::changed, this, [this] { reloadNotes(); });
    connect(&AppEvents::instance(), &AppEvents::studyStoreDidChange, this, [this] { reloadStudy(); });
    connect(&AppEvents::instance(), &AppEvents::flashcardProgressDidChange, this, [this] { reloadStudy(); });
    connect(&services.study(), &StudySession::changed, this, [this] { reloadStudy(); });

    reloadAll();
    for (auto *s : {m_notesItem, m_pagesItem, m_studyItem})
        s->setExpanded(true);
    restoreExpansion(true);
    selectCurrentNote();
}

const SidebarNode *SidebarWidget::node(QTreeWidgetItem *item) const
{
    if (!item)
        return nullptr;
    return reinterpret_cast<const SidebarNode *>(item->data(0, Qt::UserRole).value<quintptr>());
}

QTreeWidgetItem *SidebarWidget::item(const std::function<bool(const SidebarNode &)> &matches) const
{
    std::function<QTreeWidgetItem *(QTreeWidgetItem *)> search = [&](QTreeWidgetItem *parent) -> QTreeWidgetItem * {
        for (int i = 0; i < parent->childCount(); ++i) {
            QTreeWidgetItem *child = parent->child(i);
            if (const SidebarNode *n = node(child); n && matches(*n))
                return child;
            if (QTreeWidgetItem *found = search(child))
                return found;
        }
        return nullptr;
    };
    return search(m_notesItem);
}

QString SidebarWidget::currentURL() const
{
    NoteDocument *doc = document();
    return doc ? doc->filePath() : QString();
}

int SidebarWidget::currentVisiblePage() const
{
    return m_host.visiblePage ? m_host.visiblePage() : 1;
}

// MARK: Content

void SidebarWidget::reloadAll()
{
    if (m_renaming)
        return;
    rebuildNotes();
    rebuildPages();
    rebuildStudy();
    restoreExpansion(true);
    selectCurrentNote();
}

void SidebarWidget::noteDidChange()
{
    // Typing reports changes every few hundred milliseconds; thumbnails can lag a little.
    if (!m_pageTimer.isActive())
        m_pageTimer.start();
}

void SidebarWidget::visiblePageDidChange()
{
    m_tree->viewport()->update();
}

void SidebarWidget::reloadNotes()
{
    if (m_renaming)
        return;
    rebuildNotes();
    restoreExpansion(false);
    selectCurrentNote();
    reloadStudy();
}

void SidebarWidget::reloadStudy()
{
    rebuildStudy();
}

void SidebarWidget::populate(QTreeWidgetItem *parent, const QList<SidebarNode> &nodes)
{
    for (const SidebarNode &n : nodes) {
        auto *item = new QTreeWidgetItem(parent);
        item->setText(0, n.title);
        Qt::ItemFlags flags = Qt::ItemIsEnabled;
        if (n.isSelectable())
            flags |= Qt::ItemIsSelectable;
        if (SidebarModel::isDraggable(n))
            flags |= Qt::ItemIsDragEnabled;
        if (n.isFolder())
            flags |= Qt::ItemIsDropEnabled;
        item->setFlags(flags);
        item->setData(0, Qt::UserRole, QVariant::fromValue<quintptr>(reinterpret_cast<quintptr>(&n)));
        if (n.kind == SidebarNode::Kind::deck)
            item->setToolTip(0, QStringLiteral("%1 · %2").arg(n.deck.noteTitle, n.deck.summary.description()));
        populate(item, n.children);
    }
}

void SidebarWidget::rebuildNotes()
{
    auto &library = AppServices::shared().library();
    m_restoring = true;
    qDeleteAll(m_notesItem->takeChildren());
    const NoteTree tree = NoteTree::build(library.entries(), library.folders());
    m_notes.children = SidebarModel::nodes(tree, library.folder().url);
    populate(m_notesItem, m_notes.children);
    m_restoring = false;
}

void SidebarWidget::rebuildPages()
{
    m_restoring = true;
    qDeleteAll(m_pagesItem->takeChildren());
    m_pages.children.clear();
    if (m_host.note) {
        const Note note = m_host.note();
        m_pages.children = SidebarModel::pageNodes(note);
        const int count = int(note.pages().size());
        for (auto it = m_thumbnails.begin(); it != m_thumbnails.end();)
            it = it.key() > count ? m_thumbnails.erase(it) : std::next(it);
    }
    populate(m_pagesItem, m_pages.children);
    m_restoring = false;
}

void SidebarWidget::rebuildStudy()
{
    auto &session = AppServices::shared().study();
    m_restoring = true;
    qDeleteAll(m_studyItem->takeChildren());
    m_study.children = SidebarModel::studyNodes(session.imports(), session.extracting(), DeckCatalog::all());
    populate(m_studyItem, m_study.children);
    m_restoring = false;
}

QImage SidebarWidget::thumbnail(int number)
{
    if (!m_host.note)
        return {};
    const Note note = m_host.note();
    if (number < 1 || number > int(note.pages().size()))
        return {};
    const NotePage &page = note.pages()[number - 1];
    const auto it = m_thumbnails.constFind(number);
    if (it != m_thumbnails.constEnd() && it->first == page)
        return it->second;
    const QImage image = PageThumbnail::image(page, thumbnailSize(), BlueprintPalette::blueprint(), devicePixelRatioF());
    m_thumbnails.insert(number, {page, image});
    return image;
}

void SidebarWidget::selectCurrentNote()
{
    QTreeWidgetItem *target = nullptr;
    if (m_selectedFolderPath)
        target = item([&](const SidebarNode &n) { return n.folderPath() == m_selectedFolderPath; });
    if (!target) {
        const QString url = currentURL();
        if (!url.isEmpty())
            target = item([&](const SidebarNode &n) { return n.noteURL() == url; });
    }
    if (target) {
        m_tree->setCurrentItem(target, 0, QItemSelectionModel::ClearAndSelect);
    } else {
        m_tree->clearSelection();
        m_tree->setCurrentItem(nullptr);
    }
}

std::optional<QString> SidebarWidget::selectedFolder() const
{
    const auto selected = m_tree->selectedItems();
    return SidebarModel::selectedFolder(selected.isEmpty() ? nullptr : node(selected.first()));
}

// MARK: Expansion

QSet<QString> SidebarWidget::expandedFolders() const
{
    const QStringList list = AppDefaults::store().value(SidebarModel::expandedFoldersKey).toStringList();
    return QSet<QString>(list.begin(), list.end());
}

void SidebarWidget::setExpandedFolders(const QSet<QString> &paths)
{
    QStringList list(paths.begin(), paths.end());
    list.sort();
    AppDefaults::store().setValue(SidebarModel::expandedFoldersKey, list);
}

void SidebarWidget::restoreExpansion(bool revealingCurrentNote)
{
    QSet<QString> paths = expandedFolders();
    if (revealingCurrentNote) {
        const QString url = currentURL();
        if (!url.isEmpty()) {
            const auto folder = AppServices::shared().library().folder().relativeFolder(url);
            if (folder && !folder->isEmpty()) {
                for (const QString &p : SidebarModel::ancestorPaths(*folder))
                    paths.insert(p);
                setExpandedFolders(paths);
            }
        }
    }
    m_restoring = true;
    std::function<void(QTreeWidgetItem *)> expand = [&](QTreeWidgetItem *parent) {
        for (int i = 0; i < parent->childCount(); ++i) {
            QTreeWidgetItem *child = parent->child(i);
            const SidebarNode *n = node(child);
            if (!n || !n->isFolder() || !paths.contains(n->path))
                continue;
            child->setExpanded(true);
            expand(child);
        }
    };
    expand(m_notesItem);
    m_restoring = false;
}

void SidebarWidget::itemExpandedOrCollapsed(QTreeWidgetItem *item, bool expanded)
{
    const SidebarNode *n = node(item);
    if (m_restoring || !n || !n->isFolder())
        return;
    QSet<QString> paths = expandedFolders();
    if (expanded)
        paths.insert(n->path);
    else
        paths = SidebarModel::afterCollapse(paths, n->path);
    setExpandedFolders(paths);
}

void SidebarWidget::toggleExpansion(QTreeWidgetItem *item)
{
    const SidebarNode *n = node(item);
    if (item && (!n || n->isExpandable()))
        item->setExpanded(!item->isExpanded());
}

// MARK: Actions

void SidebarWidget::rowClicked(QTreeWidgetItem *item)
{
    const SidebarNode *n = node(item);
    if (!n)
        return;
    switch (n->kind) {
    case SidebarNode::Kind::folder:
        m_selectedFolderPath = n->path;
        break;
    case SidebarNode::Kind::note:
        m_selectedFolderPath.reset();
        if (n->url != currentURL())
            NoteDocuments::instance().open(n->url);
        break;
    case SidebarNode::Kind::page:
        if (m_host.scrollToPage)
            m_host.scrollToPage(n->number);
        break;
    case SidebarNode::Kind::studyImport:
    case SidebarNode::Kind::extracting:
    case SidebarNode::Kind::addMaterial:
        AppController::instance().showStudyPanel(window());
        break;
    case SidebarNode::Kind::generate:
        AppController::instance().showStudyPanel(window(), true);
        break;
    case SidebarNode::Kind::deck:
        FlashcardStudyWindow::open(n->deck.note, n->deck.deck.id);
        break;
    case SidebarNode::Kind::section:
        toggleExpansion(item);
        return;
    }
    selectCurrentNote();
}

void SidebarWidget::newFolder(const std::optional<QString> &parent)
{
    auto &app = AppController::instance();
    const auto url = app.createFolder(parent);
    if (!url)
        return;
    const auto &folder = AppServices::shared().library().folder();
    if (parent) {
        if (const auto path = folder.relativePath(*parent); path && !path->isEmpty()) {
            QSet<QString> paths = expandedFolders();
            paths.insert(*path);
            setExpandedFolders(paths);
        }
    }
    reloadNotes();
    const auto path = folder.relativePath(*url);
    QTreeWidgetItem *it = item([&](const SidebarNode &n) { return n.folderPath() == path; });
    if (!it)
        return;
    m_selectedFolderPath = path;
    selectCurrentNote();
    beginRename(it);
}

// MARK: Context menu

void SidebarWidget::showContextMenu(QTreeWidgetItem *item, const QPoint &globalPos)
{
    const SidebarNode *n = node(item);
    if (n && n->isSection())
        n = nullptr;
    if (m_menu)
        m_menu->deleteLater();
    auto *menu = new QMenu(this);
    menu->setAttribute(Qt::WA_DeleteOnClose);
    m_menu = menu;
    for (const auto &entry : SidebarModel::contextMenu(n)) {
        if (entry.separator) {
            menu->addSeparator();
            continue;
        }
        QAction *action = menu->addAction(entry.title);
        const QString id = entry.action;
        connect(action, &QAction::triggered, this, [this, id, item = n ? item : nullptr] { runContextAction(id, item); });
    }
    menu->popup(globalPos);
}

void SidebarWidget::runContextAction(const QString &action, QTreeWidgetItem *item)
{
    const SidebarNode *n = node(item);
    const QString root = AppServices::shared().library().folder().url;
    auto &app = AppController::instance();
    if (action == QLatin1String("newNote")) {
        QString folder = root;
        if (n && n->isFolder())
            folder = n->url;
        else if (n && n->isNote() && n->entry.folder)
            folder = deletingLastPathComponent(n->url);
        app.newNote(std::optional<QString>(folder));
    } else if (action == QLatin1String("newFolder")) {
        newFolder(std::optional<QString>(n && n->isFolder() ? n->url : root));
    } else if (action == QLatin1String("rename")) {
        if (item)
            beginRename(item);
    } else if (action == QLatin1String("show")) {
        if (n && n->fileURL())
            AppController::showInExplorer(*n->fileURL());
    } else if (action == QLatin1String("open")) {
        if (n)
            NoteDocuments::instance().open(n->url);
    } else if (action == QLatin1String("study")) {
        if (n && n->kind == SidebarNode::Kind::deck)
            FlashcardStudyWindow::open(n->deck.note, n->deck.deck.id);
    } else if (action == QLatin1String("trash")) {
        if (!n || !n->fileURL())
            return;
        const QString url = *n->fileURL();
        const auto prompt = SidebarModel::trashPrompt(url, n->isFolder(), AppServices::shared().library().entries());
        QMessageBox box(QMessageBox::Question, QStringLiteral("Whiteprint"), prompt.message, QMessageBox::NoButton, window());
        box.setInformativeText(prompt.detail);
        QPushButton *go = box.addButton(QStringLiteral("Move to Recycle Bin"), QMessageBox::AcceptRole);
        box.addButton(QMessageBox::Cancel);
        box.setDefaultButton(go);
        box.exec();
        if (box.clickedButton() != static_cast<QAbstractButton *>(go))
            return;
        try {
            NoteFileActions::trash(url);
        } catch (...) {
            AppController::presentError(window(), currentErrorLine());
        }
    }
}

// MARK: Rename

void SidebarWidget::beginRename(QTreeWidgetItem *item)
{
    const SidebarNode *n = node(item);
    if (!item || !n || !n->fileURL())
        return;
    m_renaming = Renaming{*n->fileURL(), item};
    item->setFlags(item->flags() | Qt::ItemIsEditable);
    m_tree->scrollToItem(item);
    m_tree->editItem(item, 0);
}

void SidebarWidget::endRename(const QString &text, bool cancelled)
{
    if (!m_renaming)
        return;
    const Renaming r = *m_renaming;
    m_renaming.reset();
    if (r.item)
        r.item->setFlags(r.item->flags() & ~Qt::ItemIsEditable);
    const QString name = text.trimmed();
    if (!cancelled && !name.isEmpty()) {
        try {
            const auto &folder = AppServices::shared().library().folder();
            const bool isFolder = QFileInfo(r.url).isDir();
            const auto old = folder.relativePath(r.url);
            const QString renamed = NoteFileActions::rename(r.url, name);
            const auto fresh = folder.relativePath(renamed);
            if (isFolder && m_selectedFolderPath)
                m_selectedFolderPath = fresh;
            if (old && fresh)
                setExpandedFolders(SidebarModel::afterRename(expandedFolders(), *old, *fresh));
        } catch (...) {
            AppController::presentError(window(), currentErrorLine());
        }
    }
    reloadNotes();
    m_tree->setFocus();
}

// MARK: Drag and drop

QStringList SidebarWidget::draggedPaths(const QList<QTreeWidgetItem *> &items) const
{
    QStringList paths;
    for (QTreeWidgetItem *it : items) {
        const SidebarNode *n = node(it);
        if (m_renaming || !n || !SidebarModel::isDraggable(*n))
            continue;
        paths.append(n->url);
    }
    return paths;
}

std::optional<QString> SidebarWidget::dropTargetFolder(QTreeWidgetItem *target) const
{
    return SidebarModel::dropFolder(node(target), AppServices::shared().library().folder().url);
}

bool SidebarWidget::canDrop(QTreeWidgetItem *target, const QStringList &paths) const
{
    const auto folder = dropTargetFolder(target);
    if (!folder)
        return false;
    const QStringList items = SidebarModel::draggable(paths, AppServices::shared().library().folder().url);
    return SidebarModel::canMove(items, *folder);
}

bool SidebarWidget::acceptDrop(QTreeWidgetItem *target, const QStringList &paths)
{
    const auto folder = dropTargetFolder(target);
    if (!folder)
        return false;
    if (const SidebarNode *n = node(target); n && n->isFolder()) {
        QSet<QString> expanded = expandedFolders();
        expanded.insert(n->path);
        setExpandedFolders(expanded);
    }
    const QStringList items = SidebarModel::draggable(paths, AppServices::shared().library().folder().url);
    try {
        NoteFileActions::move(items, *folder);
        return true;
    } catch (...) {
        AppController::presentError(window(), currentErrorLine());
        return false;
    }
}

} // namespace wp
