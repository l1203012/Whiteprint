#include "app/CommandPalette.h"

#include "app/AppServices.h"
#include "app/DeckCatalog.h"
#include "app/FlashcardStudyWindow.h"
#include "app/MacStyle.h"
#include "app/NoteDocuments.h"
#include "app/PaletteSearch.h"
#include "app/UiKit.h"
#include "render/Fonts.h"

#include <QApplication>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLineEdit>
#include <QListWidget>
#include <QPainter>
#include <QPointer>
#include <QScreen>
#include <QStyledItemDelegate>
#include <QTimer>
#include <QVBoxLayout>

namespace wp {

namespace {

constexpr int kindRole = Qt::UserRole + 1;
constexpr int detailRole = Qt::UserRole + 2;

ui::Symbol symbolFor(CommandPalette::Kind kind)
{
    switch (kind) {
    case CommandPalette::Kind::note: return ui::Symbol::note;
    case CommandPalette::Kind::page: return ui::Symbol::page;
    case CommandPalette::Kind::command: return ui::Symbol::command;
    case CommandPalette::Kind::deck: return ui::Symbol::deck;
    }
    return ui::Symbol::command;
}

/// One row: icon, title, spacer, detail; the selected row is an accent-coloured rounded rectangle.
class RowDelegate : public QStyledItemDelegate {
public:
    using QStyledItemDelegate::QStyledItemDelegate;

    QSize sizeHint(const QStyleOptionViewItem &, const QModelIndex &) const override { return {0, 32}; }

    void paint(QPainter *p, const QStyleOptionViewItem &option, const QModelIndex &index) const override
    {
        const auto c = mac::colors();
        const bool selected = option.state & QStyle::State_Selected;
        p->save();
        p->setRenderHint(QPainter::Antialiasing);
        const QRectF row = QRectF(option.rect).adjusted(0, 1, 0, -1);
        if (selected) {
            p->setPen(Qt::NoPen);
            p->setBrush(c.selection);
            p->drawRoundedRect(row, 6, 6);
        }
        const QColor primary = selected ? c.selectionText : c.text;
        const QColor secondary = selected ? QColor(255, 255, 255, 190) : c.secondaryText;
        const auto kind = static_cast<CommandPalette::Kind>(index.data(kindRole).toInt());
        ui::drawSymbol(*p, symbolFor(kind), QRectF(row.left() + 10, row.center().y() - 9, 18, 18), secondary);

        const QString detail = index.data(detailRole).toString();
        const QFont detailFont = fonts::system(12);
        const QFontMetrics dm(detailFont);
        const double detailWidth = detail.isEmpty() ? 0 : qMin(dm.horizontalAdvance(detail), int(row.width() * 0.4));
        if (!detail.isEmpty()) {
            p->setFont(detailFont);
            p->setPen(selected ? secondary : c.secondaryText.darker(mac::isDark() ? 100 : 100));
            p->setOpacity(selected ? 1.0 : 0.8);
            p->drawText(QRectF(row.right() - 12 - detailWidth, row.top(), detailWidth, row.height()),
                        Qt::AlignVCenter | Qt::AlignRight, dm.elidedText(detail, Qt::ElideRight, int(detailWidth)));
            p->setOpacity(1.0);
        }
        const QFont titleFont = fonts::system(14);
        const QFontMetrics tm(titleFont);
        const double titleLeft = row.left() + 38;
        const int titleWidth = int(row.right() - 12 - detailWidth - 10 - titleLeft);
        p->setFont(titleFont);
        p->setPen(primary);
        p->drawText(QRectF(titleLeft, row.top(), titleWidth, row.height()), Qt::AlignVCenter | Qt::AlignLeft,
                    tm.elidedText(index.data(Qt::DisplayRole).toString(), Qt::ElideRight, titleWidth));
        p->restore();
    }
};

QPointer<CommandPalette> sharedPalette;

CommandPalette &shared()
{
    if (!sharedPalette)
        sharedPalette = new CommandPalette;
    return *sharedPalette;
}

} // namespace

CommandPalette::CommandPalette(QWidget *parent) : QFrame(parent, Qt::Popup | Qt::FramelessWindowHint)
{
    setAttribute(Qt::WA_TranslucentBackground);
    setFixedSize(560, 380);

    m_field = new QLineEdit;
    m_field->setFont(fonts::system(17));
    m_field->setFrame(false);
    m_field->setStyleSheet(QStringLiteral("QLineEdit, QLineEdit:focus { background: transparent; border: none; padding: 0; }"));
    m_field->installEventFilter(this);

    m_list = new QListWidget;
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setItemDelegate(new RowDelegate(m_list));
    m_list->setFocusPolicy(Qt::NoFocus);
    m_list->setSpacing(1);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setStyleSheet(QStringLiteral("QListWidget { background: transparent; }"));
    m_list->viewport()->setAutoFillBackground(false);

    auto *glass = new ui::SymbolWidget(ui::Symbol::search, 18);
    auto *top = new QHBoxLayout;
    top->setContentsMargins(17, 14, 14, 10);
    top->setSpacing(8);
    top->addWidget(glass);
    top->addWidget(m_field);

    auto *divider = new QFrame;
    divider->setFixedHeight(1);
    divider->setObjectName("divider");

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(1, 1, 1, 1);
    layout->setSpacing(0);
    layout->addLayout(top);
    layout->addWidget(divider);
    auto *listBox = new QVBoxLayout;
    listBox->setContentsMargins(7, 6, 7, 6);
    listBox->addWidget(m_list);
    layout->addLayout(listBox, 1);

    connect(m_field, &QLineEdit::textChanged, this, [this] { filter(); });
    connect(m_list, &QListWidget::itemClicked, this, [this](QListWidgetItem *) { runSelected(); });
    changeEvent(nullptr);
}

void CommandPalette::changeEvent(QEvent *event)
{
    if (event)
        QFrame::changeEvent(event);
    if (auto *divider = findChild<QFrame *>("divider"))
        divider->setStyleSheet(QStringLiteral("background: %1;").arg(mac::colors().separator.name()));
}

void CommandPalette::paintEvent(QPaintEvent *)
{
    const auto c = mac::colors();
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    p.setPen(QPen(mac::isDark() ? QColor(255, 255, 255, 40) : QColor(0, 0, 0, 50), 1));
    p.setBrush(mac::isDark() ? c.sidebar : c.window);
    p.drawRoundedRect(QRectF(rect()).adjusted(0.5, 0.5, -0.5, -0.5), 12, 12);
}

void CommandPalette::setItems(QList<Item> items, const QString &placeholder)
{
    m_items = std::move(items);
    m_field->setPlaceholderText(placeholder);
    QSignalBlocker block(m_field);
    m_field->clear();
    filter();
}

void CommandPalette::setQuery(const QString &query)
{
    m_field->setText(query);
}

QString CommandPalette::query() const
{
    return m_field->text();
}

QStringList CommandPalette::shownTitles() const
{
    QStringList titles;
    for (const Item &item : m_shown)
        titles << item.title;
    return titles;
}

int CommandPalette::selectedRow() const
{
    return m_list->currentRow();
}

void CommandPalette::filter()
{
    m_shown = PaletteSearch::filter(m_items, m_field->text(), [](const Item &item) { return item.title; });
    m_list->clear();
    for (const Item &item : m_shown) {
        auto *row = new QListWidgetItem(item.title);
        row->setData(kindRole, int(item.kind));
        row->setData(detailRole, item.detail);
        m_list->addItem(row);
    }
    if (!m_shown.isEmpty()) {
        m_list->setCurrentRow(0);
        m_list->scrollToTop();
    }
}

void CommandPalette::moveSelection(int delta)
{
    if (m_shown.isEmpty())
        return;
    const int row = qBound(0, m_list->currentRow() + delta, int(m_shown.size()) - 1);
    m_list->setCurrentRow(row);
    m_list->scrollToItem(m_list->item(row));
}

void CommandPalette::runSelected()
{
    const int row = m_list->currentRow();
    if (row < 0 || row >= m_shown.size())
        return;
    const auto run = m_shown[row].run;
    close();
    // After the panel is gone, so dialogs and focus go to the note window.
    if (run)
        QTimer::singleShot(0, qApp, run);
}

bool CommandPalette::eventFilter(QObject *watched, QEvent *event)
{
    if (watched == m_field && event->type() == QEvent::KeyPress) {
        const auto *key = static_cast<QKeyEvent *>(event);
        switch (key->key()) {
        case Qt::Key_Up: moveSelection(-1); return true;
        case Qt::Key_Down: moveSelection(1); return true;
        case Qt::Key_PageUp: moveSelection(-8); return true;
        case Qt::Key_PageDown: moveSelection(8); return true;
        case Qt::Key_Return:
        case Qt::Key_Enter: runSelected(); return true;
        case Qt::Key_Escape: close(); return true;
        default: break;
        }
    }
    return QFrame::eventFilter(watched, event);
}

void CommandPalette::positionOver(QWidget *window)
{
    QRect area;
    if (window && window->isVisible())
        area = window->frameGeometry();
    else if (QScreen *screen = QGuiApplication::primaryScreen())
        area = screen->availableGeometry();
    move(area.center().x() - width() / 2, int(area.top() + area.height() * 0.18));
}

QList<CommandPalette::Item> CommandPalette::itemsFor(const QList<PaletteCommand> &commands)
{
    QList<Item> items;
    for (const NoteEntry &entry : AppServices::shared().library().entries()) {
        const QString url = entry.url;
        const QString detail = entry.folder && !entry.folder->isEmpty() ? *entry.folder : QStringLiteral("Note");
        items.append({Kind::note, entry.title, detail, [url] { NoteDocuments::instance().open(url); }});
    }
    for (const PaletteCommand &command : commands)
        items.append({Kind::command, command.title, command.shortcut, command.run});
    return items;
}

QList<CommandPalette::Item> CommandPalette::deckItems()
{
    QList<Item> items;
    for (const auto &entry : DeckCatalog::all()) {
        const QString note = entry.note, id = entry.deck.id;
        items.append({Kind::deck, entry.title(), entry.noteTitle + QStringLiteral(" · ") + entry.summary.description(),
                      [note, id] { FlashcardStudyWindow::open(note, id); }});
    }
    if (items.isEmpty())
        items.append({Kind::command, QStringLiteral("No flashcards yet — insert a deck with Ctrl+Shift+F"), {}, {}});
    return items;
}

void CommandPalette::present(QWidget *window, QList<PaletteCommand> commands)
{
    CommandPalette &palette = shared();
    palette.setItems(itemsFor(commands), QStringLiteral("Search notes, pages and commands"));
    palette.positionOver(window);
    palette.show();
    palette.raise();
    palette.m_field->setFocus();
}

void CommandPalette::presentDecks(QWidget *window)
{
    CommandPalette &palette = shared();
    palette.setItems(deckItems(), QStringLiteral("Study flashcards: pick a deck"));
    palette.positionOver(window);
    palette.show();
    palette.raise();
    palette.m_field->setFocus();
}

} // namespace wp
