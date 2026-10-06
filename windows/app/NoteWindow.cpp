#include "app/NoteWindow.h"

#include "app/AppController.h"
#include "app/AppDefaults.h"
#include "app/AppServices.h"
#include "app/AppUtil.h"
#include "app/FlashcardStudyWindow.h"
#include "app/MacStyle.h"
#include "app/MainMenu.h"
#include "app/NoteDocuments.h"
#include "app/NoteFileActions.h"
#include "app/NoteTitle.h"
#include "app/SidebarWidget.h"
#include "app/ViewPreferences.h"
#include "editor/NoteEditorView.h"
#include "render/Fonts.h"

#include <QActionGroup>
#include <QApplication>
#include <QButtonGroup>
#include <QCloseEvent>
#include <QFileDialog>
#include <QFrame>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QPainter>
#include <QPainterPath>
#include <QPushButton>
#include <QScreen>
#include <QSplitter>
#include <QStandardPaths>
#include <QToolButton>
#include <QVBoxLayout>
#include <exception>

namespace wp {

namespace {

QColor alpha(const QColor &c, double a)
{
    QColor r = c;
    r.setAlphaF(a);
    return r;
}

QIcon paintedIcon(const std::function<void(QPainter &, const QRectF &)> &draw, const QColor &color, int size = 18)
{
    QIcon icon;
    for (const double dpr : {1.0, 2.0}) {
        QPixmap pm(QSize(size, size) * dpr);
        pm.setDevicePixelRatio(dpr);
        pm.fill(Qt::transparent);
        QPainter p(&pm);
        p.setRenderHint(QPainter::Antialiasing);
        p.setPen(QPen(color, 1.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
        p.setBrush(Qt::NoBrush);
        draw(p, QRectF(0, 0, size, size));
        p.end();
        icon.addPixmap(pm);
    }
    return icon;
}

QIcon sidebarIcon(const QColor &c)
{
    return paintedIcon(
        [](QPainter &p, const QRectF &r) {
            const QRectF box = r.adjusted(2, 3.5, -2, -3.5);
            p.drawRoundedRect(box, 2.5, 2.5);
            p.drawLine(QPointF(box.left() + 5.5, box.top()), QPointF(box.left() + 5.5, box.bottom()));
        },
        c);
}

QIcon ellipsisIcon(const QColor &c)
{
    return paintedIcon(
        [c](QPainter &p, const QRectF &r) {
            p.drawEllipse(r.adjusted(2, 2, -2, -2));
            p.setPen(Qt::NoPen);
            p.setBrush(c);
            for (int i = -1; i <= 1; ++i)
                p.drawEllipse(r.center() + QPointF(i * 3.6, 0), 0.9, 0.9);
        },
        c);
}

QIcon layoutIcon(bool a4, const QColor &c)
{
    return paintedIcon(
        [a4](QPainter &p, const QRectF &r) {
            const QRectF box = a4 ? QRectF(r.center().x() - 3.8, 2.5, 7.6, 13) : QRectF(2, r.center().y() - 3.8, 14, 7.6);
            p.drawRoundedRect(box, 1.5, 1.5);
        },
        c);
}

} // namespace

/// `Notes / Folder / Title / Page 2`, elided at the left when the toolbar is narrow.
class Breadcrumb : public QWidget {
public:
    explicit Breadcrumb(QWidget *parent = nullptr) : QWidget(parent)
    {
        setMinimumWidth(80);
        setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Preferred);
    }

    void set(const QString &trail, const QString &title, const QString &page)
    {
        m_trail = trail;
        m_title = title;
        m_page = page;
        update();
    }
    QString text() const { return m_trail + QStringLiteral("  /  ") + m_title + (m_page.isEmpty() ? QString() : QStringLiteral("  /  ") + m_page); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        const auto c = mac::colors();
        const QFont muted = fonts::system(13);
        const QFont strong = fonts::system(13, QFont::Medium);
        const QString sep = QStringLiteral("  /  ");
        const QString tail = m_page.isEmpty() ? QString() : sep + m_page;
        const QFontMetrics mm(muted), ms(strong);
        const int tailWidth = mm.horizontalAdvance(tail);
        QString title = ms.elidedText(m_title, Qt::ElideRight, qMax(40, width() - tailWidth - 20));
        const int titleWidth = ms.horizontalAdvance(title);
        const int trailRoom = qMax(0, width() - titleWidth - tailWidth);
        const QString trail = mm.elidedText(m_trail + sep, Qt::ElideLeft, trailRoom);
        int x = 0;
        auto draw = [&](const QString &s, const QFont &f, const QColor &col) {
            p.setFont(f);
            p.setPen(col);
            const int w = QFontMetrics(f).horizontalAdvance(s);
            p.drawText(QRect(x, 0, w + 2, height()), Qt::AlignVCenter | Qt::AlignLeft, s);
            x += w;
        };
        draw(trail, muted, c.secondaryText);
        draw(title, strong, c.text);
        draw(tail, muted, c.secondaryText);
    }

private:
    QString m_trail, m_title, m_page;
};

namespace {

/// The toolbar strip above the editor: window colour with a hairline underneath.
class ToolbarStrip : public QWidget {
public:
    using QWidget::QWidget;

protected:
    void paintEvent(QPaintEvent *) override
    {
        QPainter p(this);
        const auto c = mac::colors();
        p.fillRect(rect(), c.window);
        p.fillRect(QRect(0, height() - 1, width(), 1), c.separator);
    }
};

} // namespace

NoteWindow::NoteWindow(NoteDocument *document, QWidget *parent) : QMainWindow(parent), m_document(document)
{
    setAttribute(Qt::WA_DeleteOnClose);
    setMinimumSize(560, 420);
    setObjectName(QStringLiteral("NoteWindow"));
    setWindowIcon(QApplication::windowIcon());

    m_editor = new NoteEditorView(document->note());
    m_sidebar = new SidebarWidget({
        [this] { return m_document.data(); },
        [this] { return note(); },
        [this] { return m_visiblePage; },
        [this](int page) { scrollToPage(page); },
    });

    auto *right = new QWidget;
    auto *rightLayout = new QVBoxLayout(right);
    rightLayout->setContentsMargins(0, 0, 0, 0);
    rightLayout->setSpacing(0);
    auto *strip = new ToolbarStrip;
    strip->setFixedHeight(44);
    buildToolbar(strip);
    rightLayout->addWidget(strip);
    rightLayout->addWidget(m_editor, 1);
    right->setMinimumWidth(360);

    m_split = new QSplitter(Qt::Horizontal);
    m_split->setHandleWidth(1);
    m_split->setChildrenCollapsible(false);
    m_split->addWidget(m_sidebar);
    m_split->addWidget(right);
    m_split->setStretchFactor(0, 0);
    m_split->setStretchFactor(1, 1);
    m_split->setStyleSheet(QStringLiteral("QSplitter::handle { background: %1; }").arg(mac::colors().separator.name()));
    setCentralWidget(m_split);
    m_sidebar->setMaximumWidth(340);

    // Geometry: the remembered one, cascaded from the front window.
    QSettings &defaults = AppDefaults::store();
    resize(1080, 760);
    if (defaults.contains(geometryKey))
        m_restoredGeometry = restoreGeometry(defaults.value(geometryKey).toByteArray());
    if (!m_restoredGeometry) {
        if (const QScreen *screen = QApplication::primaryScreen())
            move(screen->availableGeometry().center() - QPoint(width() / 2, height() / 2));
    }
    if (NoteWindow *front = AppController::instance().frontWindow(); front && front != this && front->isVisible() &&
                                                                       !front->isMaximized() && !front->isFullScreen()) {
        QPoint p = front->pos() + QPoint(28, 28);
        const QRect avail = front->screen() ? front->screen()->availableGeometry() : QRect(0, 0, 1920, 1080);
        if (p.x() + width() > avail.right() || p.y() + height() > avail.bottom())
            p = avail.topLeft() + QPoint(24, 24);
        move(p);
    }
    if (defaults.contains(splitKey))
        m_split->restoreState(defaults.value(splitKey).toByteArray());
    else
        m_split->setSizes({m_sidebarWidth, 1080 - m_sidebarWidth});

    MainMenu::install(this);

    // Document and editor wiring.
    connect(document, &NoteDocument::noteChanged, this, [this](NoteDocument::ChangeOrigin origin) { documentChanged(int(origin)); });
    connect(document, &NoteDocument::titleChanged, this, [this] { updateBreadcrumb(); });
    connect(document, &NoteDocument::moved, this, [this] { updateBreadcrumb(); });
    connect(document, &NoteDocument::dirtyChanged, this, [this](bool dirty) { setWindowModified(dirty); });
    connect(document, &NoteDocument::saveFailed, this, [this](const QString &message) {
        if (m_showingSaveError)
            return;
        m_showingSaveError = true;
        AppController::presentError(this, QStringLiteral("The note couldn't be saved. ") + message);
        m_showingSaveError = false;
    });
    connect(document, &NoteDocument::reloadFailed, this, [this](const QString &message) { AppController::presentError(this, message); });
    connect(document, &NoteDocument::externalChangeConflict, this, [this] {
        if (!m_document)
            return;
        QMessageBox box(QMessageBox::Warning, QStringLiteral("Whiteprint"),
                        QStringLiteral("“%1” changed on disk while it has unsaved edits.").arg(m_document->title()),
                        QMessageBox::NoButton, this);
        box.setInformativeText(QStringLiteral("Keep your version, or load the version from disk?"));
        QPushButton *keep = box.addButton(QStringLiteral("Keep My Version"), QMessageBox::AcceptRole);
        QPushButton *reload = box.addButton(QStringLiteral("Reload from Disk"), QMessageBox::DestructiveRole);
        box.exec();
        try {
            if (box.clickedButton() == static_cast<QAbstractButton *>(reload))
                m_document->reloadFromDisk();
            else if (box.clickedButton() == static_cast<QAbstractButton *>(keep))
                m_document->save();
        } catch (...) {
            AppController::presentError(this, currentErrorLine());
        }
    });
    connect(&NoteDocuments::instance(), &NoteDocuments::documentClosed, this, [this](NoteDocument *closed) {
        if (closed == m_document.data() && !m_closing) {
            m_closing = true;
            close();
        }
    });
    connect(m_editor, &NoteEditorView::changed, this, [this](const Note &note) {
        if (m_document)
            m_document->editorDidChange(note);
    });
    connect(m_editor, &NoteEditorView::visiblePageChanged, this, [this](int page) { visiblePageChanged(page); });
    connect(m_editor, &NoteEditorView::studyDeckRequested, this, [this](const CardDeck &deck) { study(deck); });
    connect(&ViewPreferences::shared(), &ViewPreferences::changed, this, [this] { applyViewPreferences(); });

    applyViewPreferences();
    updateBreadcrumb();
    m_sidebar->reloadAll();
    setWindowModified(document->isDirty());
    AppController::instance().registerWindow(this);
}

NoteWindow::~NoteWindow()
{
    AppController::instance().unregisterWindow(this);
}

Note NoteWindow::note() const
{
    return m_document ? m_document->note() : m_editor->note();
}

std::optional<QString> NoteWindow::selectedFolder() const
{
    return m_sidebar->selectedFolder();
}

bool NoteWindow::isSidebarVisible() const
{
    return !m_sidebar->isHidden();
}

bool NoteWindow::hasFile() const
{
    return m_document && !m_document->filePath().isEmpty();
}

// MARK: Toolbar

void NoteWindow::applyToolbarColors()
{
    const auto c = mac::colors();
    m_toolbar->setStyleSheet(QStringLiteral(
        "QToolButton { border: none; border-radius: 6px; padding: 4px 6px; background: transparent; color: %1; }"
        "QToolButton:hover { background: %2; }"
        "QToolButton:pressed { background: %3; }"
        "QToolButton::menu-indicator { image: none; width: 0px; }"
        "QToolButton#syntax { padding: 4px 9px; font-size: 12px; }"
        "QToolButton#syntax:checked { background: %4; color: %5; }"
        "QToolButton#segment { padding: 3px 11px; font-size: 12px; border-radius: 5px; }"
        "QToolButton#segment:checked { background: %6; border: 1px solid %7; }"
        "QFrame#track { background: %2; border-radius: 7px; }")
                                 .arg(c.text.name(), alpha(c.text, 0.08).name(QColor::HexArgb), alpha(c.text, 0.14).name(QColor::HexArgb),
                                      c.accent.name(), c.selectionText.name(), mac::isDark() ? c.control.name() : QStringLiteral("#ffffff"),
                                      c.controlBorder.name()));
    if (m_sidebarButton)
        m_sidebarButton->setIcon(sidebarIcon(c.secondaryText));
    if (m_moreButton)
        m_moreButton->setIcon(ellipsisIcon(c.secondaryText));
    if (m_split)
        m_split->setStyleSheet(QStringLiteral("QSplitter::handle { background: %1; }").arg(c.separator.name()));
}

void NoteWindow::buildToolbar(QWidget *container)
{
    const auto c = mac::colors();
    auto *layout = new QHBoxLayout(container);
    layout->setContentsMargins(10, 0, 12, 1);
    layout->setSpacing(6);

    m_toolbar = container;
    applyToolbarColors();


    m_sidebarButton = new QToolButton;
    m_sidebarButton->setIcon(sidebarIcon(c.secondaryText));
    m_sidebarButton->setToolTip(QStringLiteral("Show or hide the sidebar (Ctrl+\\)"));
    m_sidebarButton->setAutoRaise(true);
    connect(m_sidebarButton, &QToolButton::clicked, this, [this] { toggleSidebar(); });
    layout->addWidget(m_sidebarButton);

    m_breadcrumb = new Breadcrumb;
    layout->addWidget(m_breadcrumb, 1);

    auto *track = new QFrame;
    track->setObjectName(QStringLiteral("track"));
    auto *trackLayout = new QHBoxLayout(track);
    trackLayout->setContentsMargins(2, 2, 2, 2);
    trackLayout->setSpacing(2);
    auto makeSegment = [&](const QString &label, const QString &tip, bool a4) {
        auto *b = new QToolButton;
        b->setObjectName(QStringLiteral("segment"));
        b->setText(label);
        b->setToolTip(tip);
        b->setCheckable(true);
        b->setAutoExclusive(true);
        b->setFont(fonts::system(12));
        connect(b, &QToolButton::clicked, this, [a4] { ViewPreferences::shared().setLayoutMode(a4 ? ViewLayout::a4 : ViewLayout::slides); });
        trackLayout->addWidget(b);
        return b;
    };
    m_slidesButton = makeSegment(QStringLiteral("Slides"), QStringLiteral("Wide slide-style pages"), false);
    m_a4Button = makeSegment(QStringLiteral("A4"), QStringLiteral("Printer-paper pages showing where printed pages break"), true);
    layout->addWidget(track);

    m_syntaxButton = new QToolButton;
    m_syntaxButton->setObjectName(QStringLiteral("syntax"));
    m_syntaxButton->setText(QStringLiteral("Markdown"));
    m_syntaxButton->setToolTip(QStringLiteral("Show Markdown syntax (Ctrl+Shift+M)"));
    m_syntaxButton->setCheckable(true);
    m_syntaxButton->setFont(fonts::system(12));
    connect(m_syntaxButton, &QToolButton::clicked, this,
            [](bool on) { ViewPreferences::shared().setShowsMarkdownSyntax(on); });
    layout->addWidget(m_syntaxButton);

    m_moreButton = new QToolButton;
    m_moreButton->setIcon(ellipsisIcon(c.secondaryText));
    m_moreButton->setToolTip(QStringLiteral("Export, pages and more"));
    m_moreButton->setPopupMode(QToolButton::InstantPopup);
    m_moreButton->setMenu(moreMenu());
    layout->addWidget(m_moreButton);
}

QMenu *NoteWindow::moreMenu()
{
    auto *menu = new QMenu(this);
    QMenu *exportMenu = menu->addMenu(QStringLiteral("Export"));
    exportMenu->addAction(QStringLiteral("Markdown…"), this, [this] { exportMarkdown(); });
    exportMenu->addAction(QStringLiteral("PDF (Blueprint)…"), this, [this] { exportPDF(true); });
    exportMenu->addAction(QStringLiteral("PDF (Print)…"), this, [this] { exportPDF(false); });
    menu->addSeparator();
    menu->addAction(QStringLiteral("Add Page"), this, [this] { addPage(); });
    menu->addAction(QStringLiteral("Insert Drawing"), this, [this] { insertDrawing(); });
    menu->addAction(QStringLiteral("Insert Flashcards"), this, [this] { insertFlashcards(); });
    menu->addAction(QStringLiteral("Study Flashcards…"), this, [this] { studyFlashcards(); });
    menu->addSeparator();
    menu->addAction(QStringLiteral("Show in Explorer"), this, [this] { showInExplorer(); });
    return menu;
}

void NoteWindow::syncModeButtons()
{
    const ViewPreferences &prefs = ViewPreferences::shared();
    const bool slides = prefs.layoutMode() == ViewLayout::slides;
    m_slidesButton->setChecked(slides);
    m_a4Button->setChecked(!slides);
    m_syntaxButton->setChecked(prefs.showsMarkdownSyntax());
}

void NoteWindow::applyViewPreferences()
{
    const ViewPreferences &prefs = ViewPreferences::shared();
    const PageLayoutMode mode = prefs.layoutMode() == ViewLayout::a4 ? PageLayoutMode::a4 : PageLayoutMode::slides;
    if (m_editor->layoutMode() != mode)
        m_editor->setLayoutMode(mode);
    if (m_editor->showsMarkdownSyntax() != prefs.showsMarkdownSyntax())
        m_editor->setShowsMarkdownSyntax(prefs.showsMarkdownSyntax());
    syncModeButtons();
}

// MARK: Breadcrumb and title

QString NoteWindow::breadcrumbText() const
{
    return m_breadcrumb ? m_breadcrumb->text() : QString();
}

void NoteWindow::updateBreadcrumb()
{
    QString folders;
    if (m_document && !m_document->filePath().isEmpty())
        folders = AppServices::shared().library().folder().relativeFolder(m_document->filePath()).value_or(QString());
    QStringList trail{QStringLiteral("Notes")};
    trail += folders.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    const QString title = m_document ? m_document->title() : NoteTitle::untitled;
    m_breadcrumb->set(trail.join(QStringLiteral("  /  ")), title,
                      note().pages().size() > 1 ? QStringLiteral("Page %1").arg(m_visiblePage) : QString());
    setWindowTitle(title + QStringLiteral("[*]"));
}

// MARK: Document changes

void NoteWindow::documentChanged(int origin)
{
    if (!m_document)
        return;
    const Note current = m_document->note();
    if (NoteDocument::ChangeOrigin(origin) != NoteDocument::ChangeOrigin::editor)
        m_editor->setNote(current, true);
    m_visiblePage = qMin(m_visiblePage, int(current.pages().size()));
    updateBreadcrumb();
    m_sidebar->noteDidChange();
}

void NoteWindow::visiblePageChanged(int page)
{
    if (page == m_visiblePage)
        return;
    m_visiblePage = page;
    updateBreadcrumb();
    m_sidebar->visiblePageDidChange();
}

void NoteWindow::scrollToPage(int page)
{
    m_editor->scrollToPage(page);
    visiblePageChanged(page);
}

void NoteWindow::focusEditor()
{
    m_editor->setFocus();
}

void NoteWindow::study(const CardDeck &deck)
{
    if (!hasFile())
        return;
    FlashcardStudyWindow::open(m_document->filePath(), deck.id);
}

// MARK: Actions

void NoteWindow::addPage()
{
    m_editor->addPage();
}

void NoteWindow::insertDrawing()
{
    m_editor->insertDrawingAtSelection();
}

void NoteWindow::insertFlashcards()
{
    m_editor->insertDeckAtSelection();
}

void NoteWindow::performEditorCommand(EditorCommand command)
{
    m_editor->perform(command);
}

void NoteWindow::studyFlashcards()
{
    QList<CardDeck> decks;
    for (const CardDeck &deck : note().decks())
        if (!deck.cards.isEmpty())
            decks.append(deck);
    if (decks.size() == 1)
        study(decks.first());
    else
        CommandPalette::presentDecks(this);
}

void NoteWindow::newFolder()
{
    m_sidebar->newFolder(selectedFolder());
}

void NoteWindow::toggleSidebar()
{
    if (m_sidebar->isVisible()) {
        const auto sizes = m_split->sizes();
        if (!sizes.isEmpty() && sizes.first() > 0)
            m_sidebarWidth = sizes.first();
        m_sidebar->hide();
    } else {
        m_sidebar->show();
        m_split->setSizes({m_sidebarWidth, qMax(360, width() - m_sidebarWidth)});
    }
}

void NoteWindow::undo()
{
    if (m_editor->canUndo())
        m_editor->undo();
    else if (m_document && m_document->canUndo())
        m_document->undo();
}

void NoteWindow::redo()
{
    if (m_editor->canRedo())
        m_editor->redo();
    else if (m_document && m_document->canRedo())
        m_document->redo();
}

QString NoteWindow::undoText() const
{
    if (m_editor->canUndo())
        return m_editor->undoActionName();
    return m_document && m_document->canUndo() ? m_document->undoActionName() : QString();
}

QString NoteWindow::redoText() const
{
    if (m_editor->canRedo())
        return m_editor->redoActionName();
    return m_document && m_document->canRedo() ? m_document->redoActionName() : QString();
}

void NoteWindow::forwardToFocus(const char *slot)
{
    if (QWidget *focus = QApplication::focusWidget())
        QMetaObject::invokeMethod(focus, slot);
}

// MARK: Document actions

void NoteWindow::saveDocument()
{
    if (!m_document)
        return;
    m_editor->flushPendingChange();
    try {
        if (m_document->filePath().isEmpty()) {
            const QString path = QFileDialog::getSaveFileName(
                this, QStringLiteral("Save Note"),
                AppServices::shared().library().folder().url + QLatin1Char('/') + m_document->suggestedExportName(NotesFolder::fileExtension),
                QStringLiteral("Whiteprint notes (*.wprint)"));
            if (path.isEmpty())
                return;
            m_document->saveTo(path);
            AppServices::shared().library().reload();
        } else {
            m_document->save();
        }
    } catch (...) {
        AppController::presentError(this, currentErrorLine());
    }
}

void NoteWindow::duplicateDocument()
{
    if (!m_document)
        return;
    m_editor->flushPendingChange();
    try {
        auto &library = AppServices::shared().library();
        Note copy = m_document->note();
        const QString title = m_document->title() + QStringLiteral(" copy");
        copy.frontMatter.setTitle(title);
        const QString folder = hasFile() ? library.folder().relativeFolder(m_document->filePath()).value_or(QString()) : QString();
        const QString url = library.folder().save(copy, title, folder);
        library.reload();
        NoteDocuments::instance().open(url);
    } catch (...) {
        AppController::presentError(this, currentErrorLine());
    }
}

void NoteWindow::renameDocument()
{
    if (!hasFile())
        return;
    bool ok = false;
    const QString name = QInputDialog::getText(this, QStringLiteral("Rename"), QStringLiteral("Name:"), QLineEdit::Normal,
                                               NoteTitle::baseName(m_document->filePath()), &ok)
                             .trimmed();
    if (!ok || name.isEmpty())
        return;
    try {
        NoteFileActions::rename(m_document->filePath(), name);
    } catch (...) {
        AppController::presentError(this, currentErrorLine());
    }
}

void NoteWindow::moveDocument()
{
    if (!hasFile())
        return;
    const QString dir = QFileDialog::getExistingDirectory(this, QStringLiteral("Move To"), AppServices::shared().library().folder().url);
    if (dir.isEmpty())
        return;
    try {
        NoteFileActions::move({m_document->filePath()}, dir);
    } catch (...) {
        AppController::presentError(this, currentErrorLine());
    }
}

void NoteWindow::revertDocument()
{
    if (!hasFile())
        return;
    const auto answer = QMessageBox::question(this, QStringLiteral("Revert To Saved"),
                                              QStringLiteral("Discard the unsaved changes to “%1”?").arg(m_document->title()),
                                              QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
    if (answer != QMessageBox::Discard)
        return;
    try {
        m_document->reloadFromDisk();
    } catch (...) {
        AppController::presentError(this, currentErrorLine());
    }
}

void NoteWindow::exportMarkdown()
{
    if (!m_document)
        return;
    m_editor->flushPendingChange();
    const QString dir = hasFile() ? deletingLastPathComponent(m_document->filePath())
                                  : QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Export Markdown"),
                                                      dir + QLatin1Char('/') + m_document->suggestedExportName(QStringLiteral("md")),
                                                      QStringLiteral("Markdown (*.md)"));
    if (path.isEmpty())
        return;
    try {
        m_document->exportMarkdown(path);
    } catch (...) {
        AppController::presentError(this, currentErrorLine());
    }
}

void NoteWindow::exportPDF(bool blueprint)
{
    if (!m_document)
        return;
    m_editor->flushPendingChange();
    const QString dir = hasFile() ? deletingLastPathComponent(m_document->filePath())
                                  : QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation);
    const QString path = QFileDialog::getSaveFileName(this, blueprint ? QStringLiteral("Export PDF (Blueprint)") : QStringLiteral("Export PDF (Print)"),
                                                      dir + QLatin1Char('/') + m_document->suggestedExportName(QStringLiteral("pdf")),
                                                      QStringLiteral("PDF (*.pdf)"));
    if (path.isEmpty())
        return;
    try {
        m_document->exportPDF(path, blueprint ? PDFExportStyle::blueprint : PDFExportStyle::print);
    } catch (...) {
        AppController::presentError(this, currentErrorLine());
    }
}

void NoteWindow::showInExplorer()
{
    if (m_document)
        m_document->showInFileManager();
}

// MARK: Events

void NoteWindow::closeEvent(QCloseEvent *event)
{
    m_closing = true;
    m_editor->flushPendingChange();
    saveGeometryNow();
    if (m_document)
        NoteDocuments::instance().close(m_document);
    event->accept();
}

void NoteWindow::saveGeometryNow()
{
    QSettings &defaults = AppDefaults::store();
    defaults.setValue(geometryKey, saveGeometry());
    if (m_sidebar->isVisible())
        defaults.setValue(splitKey, m_split->saveState());
}

void NoteWindow::changeEvent(QEvent *event)
{
    QMainWindow::changeEvent(event);
    // A live Windows light/dark switch: the toolbar's stylesheet and icons hold the old colours.
    if (event->type() == QEvent::PaletteChange && m_toolbar)
        applyToolbarColors();
    if (event->type() == QEvent::ActivationChange) {
        m_sidebar->tree()->viewport()->update();
        if (isActiveWindow()) {
            AppController::instance().windowActivated(this);
            m_sidebar->reloadAll(); // windowDidBecomeMain
        }
    }
}

void NoteWindow::showEvent(QShowEvent *event)
{
    QMainWindow::showEvent(event);
    mac::styleTitleBar(this);
}

} // namespace wp
