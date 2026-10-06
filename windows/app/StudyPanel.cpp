#include "app/StudyPanel.h"

#include "app/AISettings.h"
#include "app/AppServices.h"
#include "app/BridgeService.h"
#include "app/MacStyle.h"
#include "app/StudySession.h"
#include "app/UiKit.h"
#include "extract/Extraction.h"
#include "render/Fonts.h"

#include <QDialog>
#include <QDragEnterEvent>
#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QLabel>
#include <QListWidget>
#include <QMimeData>
#include <QPainter>
#include <QPlainTextEdit>
#include <QPointer>
#include <QProgressBar>
#include <QPushButton>
#include <QScrollBar>
#include <QToolButton>
#include <QVBoxLayout>

namespace wp {

/// A dashed drop target for course files.
class DropZone : public QWidget {
public:
    explicit DropZone(std::function<void(const QStringList &)> onDrop) : m_onDrop(std::move(onDrop))
    {
        setAcceptDrops(true);
        setFixedHeight(96);
        m_column = new QVBoxLayout(this);
        m_column->setAlignment(Qt::AlignCenter);
        m_column->setSpacing(6);
        auto *icon = new ui::SymbolWidget(ui::Symbol::tray, 24);
        auto *text = ui::label(QStringLiteral("Drop PDF, Word or PowerPoint files here"), 13, QFont::Normal, true);
        m_column->addWidget(icon, 0, Qt::AlignHCenter);
        m_column->addWidget(text, 0, Qt::AlignHCenter);
    }

    void addButton(QPushButton *button) { m_column->addWidget(button, 0, Qt::AlignHCenter); }

    void drop(const QStringList &paths) { m_onDrop(paths); }

protected:
    void paintEvent(QPaintEvent *) override
    {
        const auto c = mac::colors();
        QPainter p(this);
        p.setRenderHint(QPainter::Antialiasing);
        QPen pen(m_targeted ? c.accent : c.separator, 1.5, Qt::CustomDashLine);
        pen.setDashPattern({4, 3});
        p.setPen(pen);
        if (m_targeted) {
            QColor fill = c.accent;
            fill.setAlphaF(0.08);
            p.setBrush(fill);
        }
        p.drawRoundedRect(QRectF(rect()).adjusted(1, 1, -1, -1), 8, 8);
    }

    void dragEnterEvent(QDragEnterEvent *event) override
    {
        m_targeted = !files(event->mimeData()).isEmpty();
        if (m_targeted)
            event->acceptProposedAction();
        update();
    }

    void dragLeaveEvent(QDragLeaveEvent *) override
    {
        m_targeted = false;
        update();
    }

    void dropEvent(QDropEvent *event) override
    {
        m_targeted = false;
        update();
        const QStringList paths = files(event->mimeData());
        if (paths.isEmpty())
            return;
        event->acceptProposedAction();
        m_onDrop(paths);
    }

private:
    static QStringList files(const QMimeData *data)
    {
        QStringList result;
        for (const QUrl &url : data->urls())
            if (url.isLocalFile() &&
                DocumentExtractor::supportedExtensions().contains(QFileInfo(url.toLocalFile()).suffix().toLower()))
                result << url.toLocalFile();
        return result;
    }

    std::function<void(const QStringList &)> m_onDrop;
    QVBoxLayout *m_column;
    bool m_targeted = false;
};

namespace {
QPointer<QDialog> openDialog;
QPointer<StudyPanel> openPanel;
} // namespace

void StudyPanel::present(QWidget *over, bool generating)
{
    if (openDialog && openPanel) {
        openDialog->show();
        openDialog->raise();
        openDialog->activateWindow();
        if (generating)
            openPanel->generateWhenReady();
        return;
    }
    auto *dialog = new QDialog(over);
    dialog->setAttribute(Qt::WA_DeleteOnClose);
    dialog->setWindowTitle(QStringLiteral("Study Material"));
    dialog->setSizeGripEnabled(true);
    auto *layout = new QVBoxLayout(dialog);
    layout->setContentsMargins(0, 0, 0, 0);
    auto *panel = new StudyPanel(dialog);
    layout->addWidget(panel);
    connect(panel, &StudyPanel::closeRequested, dialog, &QDialog::close);
    openDialog = dialog;
    openPanel = panel;
    if (generating)
        panel->generateWhenReady();
    mac::styleTitleBar(dialog);
    dialog->show();
    dialog->raise();
    dialog->activateWindow();
}

StudyPanel::StudyPanel(QWidget *parent, StudySession *session)
    : QWidget(parent), m_session(session ? session : &AppServices::shared().study())
{
    setMinimumWidth(520);
    auto *title = ui::label(QStringLiteral("Study plan"), 17, QFont::DemiBold);
    auto *subtitle = ui::label(
        QStringLiteral("Add lecture slides, PDFs or Word files. Claude (or Grok, see Settings ▸ AI) reads them through "
                       "Whiteprint and writes a study plan note with must-know topics, a learning path and a to-do list."),
        13, QFont::Normal, true, true);

    m_drop = new DropZone([this](const QStringList &paths) { m_session->importFiles(paths); });
    auto *importButton = new QPushButton(QStringLiteral("Import…"));
    importButton->setFocusPolicy(Qt::NoFocus);
    connect(importButton, &QPushButton::clicked, this, [this] { chooseFiles(); });
    m_drop->addButton(importButton);

    m_list = new QListWidget;
    m_list->setFixedHeight(120);
    m_list->setFrameShape(QFrame::NoFrame);
    m_list->setSelectionMode(QAbstractItemView::NoSelection);
    m_list->setFocusPolicy(Qt::NoFocus);
    m_list->setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    m_list->setStyleSheet(QStringLiteral("QListWidget { background: transparent; } QListWidget::item { border: none; }"));

    m_errors = ui::label({}, 12, QFont::Normal, false, true);
    m_errors->setStyleSheet(QStringLiteral("color: %1;").arg(mac::isDark() ? "#ff453a" : "#ff3b30"));
    m_errors->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_privacy = ui::label({}, 11, QFont::Normal, true, true);
    auto *lock = new ui::SymbolWidget(ui::Symbol::lock, 14);
    auto *privacyRow = new QHBoxLayout;
    privacyRow->setSpacing(6);
    privacyRow->addWidget(lock, 0, Qt::AlignTop);
    privacyRow->addWidget(m_privacy, 1);

    m_help = ui::label({}, 12, QFont::Normal, false, true);
    m_help->setTextInteractionFlags(Qt::TextSelectableByMouse);

    m_log = new QPlainTextEdit;
    m_log->setReadOnly(true);
    m_log->setFont(fonts::monospaced(11));
    m_log->setFixedHeight(110);
    m_log->setLineWrapMode(QPlainTextEdit::WidgetWidth);
    m_log->setForegroundRole(QPalette::PlaceholderText);
    {
        QPalette p = m_log->palette();
        p.setColor(QPalette::Text, mac::colors().secondaryText);
        m_log->setPalette(p);
    }
    m_log->setStyleSheet(QStringLiteral("QPlainTextEdit { background: transparent; border: 1px solid %1; border-radius: 4px; padding: 2px; }")
                             .arg(mac::colors().separator.name()));

    auto *close = new QPushButton(QStringLiteral("Close"));
    close->setFocusPolicy(Qt::NoFocus);
    connect(close, &QPushButton::clicked, this, &StudyPanel::closeRequested);
    m_cancel = new QPushButton(QStringLiteral("Cancel"));
    m_cancel->setFocusPolicy(Qt::NoFocus);
    connect(m_cancel, &QPushButton::clicked, this, [this] { m_session->cancel(); });
    m_generate = new QPushButton(QStringLiteral("Generate study plan"));
    m_generate->setIcon(ui::icon(ui::Symbol::sparkles, QColor(255, 255, 255), 16));
    m_generate->setDefault(true);
    m_generate->setFocusPolicy(Qt::NoFocus);
    connect(m_generate, &QPushButton::clicked, this, [this] { generate(); });
    auto *buttons = new QHBoxLayout;
    buttons->addWidget(close);
    buttons->addStretch(1);
    buttons->addWidget(m_cancel);
    buttons->addWidget(m_generate);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(20, 20, 20, 20);
    layout->setSpacing(12);
    layout->addWidget(title);
    layout->addSpacing(-8);
    layout->addWidget(subtitle);
    layout->addWidget(m_drop);
    layout->addWidget(m_list);
    layout->addWidget(m_errors);
    layout->addLayout(privacyRow);
    layout->addWidget(m_help);
    layout->addWidget(m_log);
    layout->addLayout(buttons);

    connect(m_session, &StudySession::changed, this, [this] { update(); });
    m_session->locateClaude();
    update();
}

QString StudyPanel::providerHelp() const
{
    switch (m_session->provider()) {
    case AIProvider::grok:
        return m_session->isProviderReady() ? QString()
                                            : QStringLiteral("Add your xAI API key in Settings ▸ AI to build study plans with Grok.");
    case AIProvider::claudeCode:
        if (!m_session->claudeLookupDone())
            return QStringLiteral("Looking for Claude Code…");
        if (!m_session->claudePath())
            return QStringLiteral(
                "Claude Code wasn’t found. For the one-click button, install it from claude.com/claude-code, then run "
                "“claude” once in a terminal and log in with your Claude account.\n"
                "Or use Claude Desktop: connect Whiteprint in Settings (Ctrl+,), then pick the “study_plan” prompt. "
                "Or switch to Grok in Settings ▸ AI.");
        return {};
    }
    return {};
}

void StudyPanel::update()
{
    // Import rows.
    m_list->clear();
    const auto &imports = m_session->imports();
    const auto &extracting = m_session->extracting();
    const auto c = mac::colors();
    if (imports.isEmpty() && extracting.isEmpty()) {
        auto *item = new QListWidgetItem;
        item->setSizeHint({0, 30});
        m_list->addItem(item);
        auto *empty = ui::label(QStringLiteral("No material yet."), 13, QFont::Normal, true);
        empty->setContentsMargins(6, 0, 0, 0);
        m_list->setItemWidget(item, empty);
    }
    for (const StudyImport &import : imports) {
        auto *item = new QListWidgetItem;
        item->setSizeHint({0, 30});
        m_list->addItem(item);
        auto *w = new QWidget;
        auto *l = new QHBoxLayout(w);
        l->setContentsMargins(6, 0, 6, 0);
        l->setSpacing(8);
        l->addWidget(new ui::SymbolWidget(ui::Symbol::document, 16));
        auto *name = ui::label({}, 13);
        name->setSizePolicy(QSizePolicy::Ignored, QSizePolicy::Preferred);
        name->setText(QFontMetrics(name->font()).elidedText(import.name, Qt::ElideMiddle, 220));
        name->setToolTip(import.name);
        name->setMinimumWidth(0);
        l->addWidget(name, 1);
        auto *detail = ui::label(QStringLiteral("%1 · %2/%3 read")
                                     .arg(BridgeService::summary(import, false)).arg(import.chunksDone).arg(import.chunkCount),
                                 11, QFont::Normal, true);
        l->addWidget(detail);
        auto *bar = new QProgressBar;
        bar->setRange(0, qMax(1, import.chunkCount));
        bar->setValue(import.chunksDone);
        bar->setTextVisible(false);
        bar->setFixedSize(70, 5);
        bar->setStyleSheet(QStringLiteral("QProgressBar { background: %1; border: none; border-radius: 2px; } "
                                          "QProgressBar::chunk { background: %2; border-radius: 2px; }")
                               .arg(c.separator.name(), c.accent.name()));
        l->addWidget(bar);
        auto *remove = new QToolButton;
        remove->setIcon(ui::icon(ui::Symbol::xmark, c.secondaryText, 16));
        remove->setAutoRaise(true);
        remove->setCursor(Qt::PointingHandCursor);
        remove->setToolTip(QStringLiteral("Remove %1").arg(import.name));
        remove->setEnabled(!m_session->isRunning());
        const QString id = import.id;
        connect(remove, &QToolButton::clicked, this, [this, id] { m_session->remove(id); });
        l->addWidget(remove);
        m_list->setItemWidget(item, w);
    }
    for (const QString &file : extracting) {
        auto *item = new QListWidgetItem;
        item->setSizeHint({0, 30});
        m_list->addItem(item);
        auto *w = new QWidget;
        auto *l = new QHBoxLayout(w);
        l->setContentsMargins(6, 0, 6, 0);
        l->setSpacing(8);
        l->addWidget(new ui::SymbolWidget(ui::Symbol::hourglass, 16));
        auto *name = ui::label({}, 13);
        name->setText(QFontMetrics(name->font()).elidedText(file, Qt::ElideMiddle, 260));
        l->addWidget(name, 1);
        l->addWidget(ui::label(QStringLiteral("Extracting text…"), 11, QFont::Normal, true));
        m_list->setItemWidget(item, w);
    }

    const QStringList &errors = m_session->importErrors();
    m_errors->setText(errors.join(QLatin1Char('\n')));
    m_errors->setVisible(!errors.isEmpty());

    m_privacy->setText(m_session->provider() == AIProvider::grok
                           ? QStringLiteral("Text is extracted on your PC. Only extracted text is sent to xAI, with your API key.")
                           : QStringLiteral("Text is extracted on your PC. Only extracted text is shared with your own Claude client."));
    const QString help = providerHelp();
    m_help->setText(help);
    m_help->setVisible(!help.isEmpty());

    const QStringList &lines = m_session->log();
    if (m_log->toPlainText() != lines.join(QLatin1Char('\n'))) {
        m_log->setPlainText(lines.join(QLatin1Char('\n')));
        m_log->verticalScrollBar()->setValue(m_log->verticalScrollBar()->maximum());
    }
    m_log->setVisible(!lines.isEmpty());

    m_generate->setEnabled(m_session->isProviderReady() && !m_session->isRunning() && !imports.isEmpty() && extracting.isEmpty());
    m_generate->setToolTip(QStringLiteral("Uses %1 (change it in Settings ▸ AI)").arg(aiProviderName(m_session->provider())));
    m_cancel->setVisible(m_session->isRunning());

    if (m_startWhenReady && (m_session->provider() == AIProvider::grok || m_session->claudeLookupDone())) {
        m_startWhenReady = false;
        if (m_generate->isEnabled())
            generate();
    }
}

void StudyPanel::generate()
{
    if (!m_generate->isEnabled())
        return;
    m_session->generate();
}

void StudyPanel::generateWhenReady()
{
    m_startWhenReady = true;
    update();
}

void StudyPanel::chooseFiles()
{
    QStringList patterns;
    for (const QString &ext : DocumentExtractor::supportedExtensions())
        patterns << QStringLiteral("*.") + ext;
    patterns.sort();
    const QStringList paths = QFileDialog::getOpenFileNames(
        window(), QStringLiteral("Choose PDF, Word or PowerPoint files"), {},
        QStringLiteral("PDF, Word or PowerPoint (%1)").arg(patterns.join(QLatin1Char(' '))));
    if (!paths.isEmpty())
        m_session->importFiles(paths);
}

} // namespace wp
