#include "app/SettingsPages.h"

#include "app/AISettings.h"
#include "app/AppUtil.h"
#include "app/ClaudeSetup.h"
#include "app/NoteWorkspace.h"
#include "app/NotesLibrary.h"
#include "app/StudySession.h"
#include "app/UiKit.h"
#include "bridge/Bridge.h"
#include "render/Fonts.h"
#include "study/ClaudeCodeRunner.h"
#include "study/GrokRunner.h"

#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPointer>
#include <QPushButton>
#include <QThreadPool>
#include <QUrl>
#include <QVBoxLayout>

namespace wp {

namespace {

/// `C:\Users\me\x` shown as `~\x`, like abbreviatingWithTildeInPath.
QString abbreviated(const QString &path)
{
    const QString home = QDir::homePath();
    QString native = QDir::toNativeSeparators(path);
    const QString nativeHome = QDir::toNativeSeparators(home);
    if (native.startsWith(nativeHome, Qt::CaseInsensitive) &&
        (native.size() == nativeHome.size() || native[nativeHome.size()] == QLatin1Char('\\')))
        native = QStringLiteral("~") + native.mid(nativeHome.size());
    return native;
}

QString currentFailure()
{
    try {
        throw;
    } catch (const KeychainError &e) {
        return e.description();
    } catch (const FileIOError &e) {
        return e.description();
    } catch (const ClaudeDesktopConfig::ConfigError &e) {
        return e.description();
    } catch (...) {
        return currentErrorLine();
    }
}

QLabel *wrapping(const QString &text, double size, bool secondary)
{
    auto *l = ui::label(text, size, QFont::Normal, secondary, true);
    l->setTextInteractionFlags(Qt::TextSelectableByMouse);
    l->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    return l;
}

} // namespace

// MARK: SettingsPane

SettingsPane::SettingsPane(QWidget *parent) : QWidget(parent)
{
    m_form = new QVBoxLayout(this);
    m_form->setContentsMargins(24, 20, 24, 24);
    m_form->setSpacing(10);
    m_form->setAlignment(Qt::AlignTop);
    setFixedWidth(588);
}

void SettingsPane::header(const QString &text)
{
    if (m_form->count() > 0) {
        m_form->addSpacing(12);
    }
    m_form->addWidget(ui::label(text, 13, QFont::DemiBold));
}

QLabel *SettingsPane::note(const QString &text, bool secondary)
{
    QLabel *l = wrapping(text, 12, secondary);
    m_form->addWidget(l);
    return l;
}

QWidget *SettingsPane::row(const QList<QWidget *> &widgets)
{
    auto *w = new QWidget;
    auto *l = new QHBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(8);
    for (QWidget *child : widgets)
        l->addWidget(child);
    l->addStretch(1);
    m_form->addWidget(w);
    return w;
}

QLabel *SettingsPane::copyable(const QString &text)
{
    auto *field = new QLabel(text);
    field->setWordWrap(true);
    field->setFont(fonts::monospaced(11));
    field->setTextInteractionFlags(Qt::TextSelectableByMouse);
    field->setFixedWidth(420);
    field->setSizePolicy(QSizePolicy::Preferred, QSizePolicy::Minimum);
    auto *copy = new QPushButton(QStringLiteral("Copy"));
    copy->setFocusPolicy(Qt::NoFocus);
    copy->setStyleSheet(QStringLiteral("QPushButton { padding: 1px 10px; }"));
    connect(copy, &QPushButton::clicked, this, [field] { QApplication::clipboard()->setText(field->text()); });
    auto *w = new QWidget;
    auto *l = new QHBoxLayout(w);
    l->setContentsMargins(0, 0, 0, 0);
    l->setSpacing(8);
    l->setAlignment(Qt::AlignTop);
    l->addWidget(field, 0, Qt::AlignTop);
    l->addWidget(copy, 0, Qt::AlignTop);
    l->addStretch(1);
    m_form->addWidget(w);
    return field;
}

bool SettingsPane::confirm(const QString &message, const QString &detail, const QString &button)
{
    QMessageBox box(QMessageBox::Question, QStringLiteral("Whiteprint"), message, QMessageBox::NoButton, window());
    box.setInformativeText(detail);
    QPushButton *ok = box.addButton(button, QMessageBox::AcceptRole);
    box.addButton(QStringLiteral("Cancel"), QMessageBox::RejectRole);
    box.setDefaultButton(ok);
    box.exec();
    return box.clickedButton() == ok;
}

void SettingsPane::inform(const QString &message, const QString &detail, bool warning)
{
    QMessageBox box(warning ? QMessageBox::Warning : QMessageBox::Information, QStringLiteral("Whiteprint"), message,
                    QMessageBox::Ok, window());
    box.setInformativeText(detail);
    box.exec();
}

// MARK: AI

AISettingsPage::AISettingsPage(AISettings &settings, QWidget *parent)
    : SettingsPane(parent), m_settings(settings), m_helper(BridgePaths::helperExecutable())
{
    header(QStringLiteral("Study plans"));
    note(QStringLiteral("\u201cGenerate study plan\u201d reads your imported material with:"));
    m_provider = new QComboBox;
    for (const AIProvider p : allAIProviders)
        m_provider->addItem(aiProviderTitle(p));
    for (int i = 0; i < int(std::size(allAIProviders)); ++i)
        if (allAIProviders[i] == settings.provider())
            m_provider->setCurrentIndex(i);
    connect(m_provider, &QComboBox::activated, this, [this](int index) { providerChanged(index); });
    row({m_provider});

    header(QStringLiteral("Grok"));
    note(QStringLiteral("Uses xAI\u2019s API with your own key, billed by xAI. The key is kept in Windows Credential "
                        "Manager, never in preferences or logs."));
    m_apiKey = new QLineEdit;
    m_apiKey->setEchoMode(QLineEdit::Password);
    m_apiKey->setPlaceholderText(QStringLiteral("xai-\u2026"));
    m_apiKey->setFixedWidth(260);
    auto *keyLabel = new QLabel(QStringLiteral("API key"));
    keyLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    keyLabel->setFixedWidth(56);
    auto *save = new QPushButton(QStringLiteral("Save"));
    auto *remove = new QPushButton(QStringLiteral("Remove"));
    for (auto *b : {save, remove})
        b->setFocusPolicy(Qt::NoFocus);
    connect(save, &QPushButton::clicked, this, [this] { saveKey(); });
    connect(remove, &QPushButton::clicked, this, [this] { removeKey(); });
    connect(m_apiKey, &QLineEdit::returnPressed, this, [this] { saveKey(); });
    row({keyLabel, m_apiKey, save, remove});
    m_keyStatus = note({});

    m_model = new QLineEdit(settings.grokModel());
    m_model->setPlaceholderText(GrokRunner::Configuration::defaultModel());
    m_model->setFixedWidth(260);
    connect(m_model, &QLineEdit::editingFinished, this, [this] { modelChanged(); });
    auto *modelLabel = new QLabel(QStringLiteral("Model"));
    modelLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
    modelLabel->setFixedWidth(56);
    m_testButton = new QPushButton(QStringLiteral("Test Connection"));
    m_testButton->setFocusPolicy(Qt::NoFocus);
    connect(m_testButton, &QPushButton::clicked, this, [this] { testConnection(); });
    row({modelLabel, m_model, m_testButton});
    m_testStatus = note({});
    m_testStatus->hide();
    updateKeyStatus();

    header(QStringLiteral("Claude Code"));
    m_codeStatus = note(QStringLiteral("Looking for Claude Code\u2026"), false);
    note(QStringLiteral("Adds Whiteprint as an MCP server for your user, so every Claude Code session can read and write "
                        "your notes. One-click study plans use your logged-in Claude Code."));
    m_addButton = new QPushButton(QStringLiteral("Add Whiteprint to Claude Code"));
    m_addButton->setEnabled(false);
    m_addButton->setFocusPolicy(Qt::NoFocus);
    connect(m_addButton, &QPushButton::clicked, this, [this] { addToClaudeCode(); });
    row({m_addButton});
    note(QStringLiteral("Or run this in a terminal:"));
    m_command = copyable(ClaudeCodeSetup::commandLine(std::nullopt, m_helper));

    header(QStringLiteral("Claude Desktop"));
    m_desktopStatus = note({}, false);
    note(QStringLiteral("Adds Whiteprint to Claude Desktop\u2019s MCP servers. Your other servers are kept, and the old file "
                        "is saved as claude_desktop_config.json.backup. Restart Claude Desktop afterwards, then pick the "
                        "\u201cstudy_plan\u201d prompt to build a study plan."));
    auto *connect_ = new QPushButton(QStringLiteral("Connect Claude Desktop"));
    connect_->setFocusPolicy(Qt::NoFocus);
    connect(connect_, &QPushButton::clicked, this, [this] { connectDesktop(); });
    row({connect_});
    note(QStringLiteral("Or add this to claude_desktop_config.json yourself:"));
    copyable(ClaudeDesktopConfig::snippet(m_helper));
    updateDesktopStatus();

    QPointer<AISettingsPage> self(this);
    QThreadPool::globalInstance()->start([self] {
        const auto path = ClaudeCodeRunner::locateClaude();
        if (!self)
            return;
        QMetaObject::invokeMethod(self.data(), [self, path] {
            if (self)
                self->claudeFound(path);
        }, Qt::QueuedConnection);
    });
}

void AISettingsPage::updateKeyStatus()
{
    const bool saved = m_settings.apiKeyItem.read().has_value();
    m_keyStatus->setText(saved ? QStringLiteral("\u2713 A key is saved in Windows Credential Manager.")
                               : QStringLiteral("No key saved."));
    m_apiKey->setPlaceholderText(saved ? QStringLiteral("Saved \u2014 type a new key to replace it")
                                       : QStringLiteral("xai-\u2026"));
    m_testButton->setEnabled(saved);
}

void AISettingsPage::providerChanged(int index)
{
    if (index < 0 || index >= int(std::size(allAIProviders)))
        index = 0;
    {
        QSignalBlocker block(m_provider);
        m_provider->setCurrentIndex(index);
    }
    m_settings.setProvider(allAIProviders[index]);
    emit studyStateChanged();
}

bool AISettingsPage::saveKey()
{
    const QString key = m_apiKey->text().trimmed();
    if (key.isEmpty())
        return false;
    bool ok = true;
    try {
        m_settings.apiKeyItem.save(key);
        m_apiKey->clear();
        m_testStatus->clear();
        m_testStatus->hide();
    } catch (...) {
        ok = false;
        inform(QStringLiteral("Couldn\u2019t save the key in Windows Credential Manager."), currentFailure(), true);
    }
    updateKeyStatus();
    emit studyStateChanged();
    return ok;
}

void AISettingsPage::removeKey()
{
    try {
        m_settings.apiKeyItem.remove();
    } catch (...) {
        inform(QStringLiteral("Couldn\u2019t remove the key from Windows Credential Manager."), currentFailure(), true);
    }
    updateKeyStatus();
    emit studyStateChanged();
}

void AISettingsPage::modelChanged()
{
    m_settings.setGrokModel(m_model->text());
    m_model->setText(m_settings.grokModel());
}

void AISettingsPage::testConnection()
{
    modelChanged();
    if (!m_apiKey->text().trimmed().isEmpty())
        saveKey();
    const auto configuration = m_settings.grokConfiguration();
    if (!configuration)
        return;
    m_testButton->setEnabled(false);
    m_testStatus->setText(QStringLiteral("Testing %1\u2026").arg(configuration->model));
    m_testStatus->show();
    QPointer<AISettingsPage> self(this);
    GrokRunner::testConnection(*configuration, [self](const GrokRunner::ConnectionResult &result) {
        if (!self)
            return;
        self->m_testButton->setEnabled(true);
        self->m_testStatus->setText(result.ok ? QStringLiteral("\u2713 ") + (result.text.isEmpty() ? QStringLiteral("Connected.") : result.text)
                                              : QStringLiteral("\u2717 ") + result.text);
    });
}

void AISettingsPage::claudeFound(const std::optional<QString> &path)
{
    m_claude = path;
    m_addButton->setEnabled(path.has_value());
    m_command->setText(ClaudeCodeSetup::commandLine(path, m_helper));
    m_codeStatus->setText(path ? QStringLiteral("\u2713 Found at %1").arg(abbreviated(*path))
                               : QStringLiteral("Not found. Install Claude Code from claude.com/claude-code, then run "
                                                "\u201cclaude\u201d once in a terminal to log in."));
}

void AISettingsPage::updateDesktopStatus()
{
    m_desktopStatus->setText(ClaudeDesktopConfig::isConnected(m_helper) ? QStringLiteral("\u2713 Connected")
                                                                        : QStringLiteral("Not connected"));
}

void AISettingsPage::addToClaudeCode()
{
    if (!m_claude)
        return;
    if (!confirm(QStringLiteral("Add Whiteprint to Claude Code?"),
                 QStringLiteral("This runs:\n%1").arg(ClaudeCodeSetup::commandLine(m_claude, m_helper)),
                 QStringLiteral("Add")))
        return;
    m_addButton->setEnabled(false);
    ClaudeCodeSetup::registerHelper(*m_claude, m_helper, this, [this](const ClaudeCodeSetup::Result &result) {
        m_addButton->setEnabled(true);
        if (result.ok)
            inform(QStringLiteral("Whiteprint was added to Claude Code."), QStringLiteral("Start a new Claude Code session to use it."));
        else
            inform(QStringLiteral("Couldn\u2019t add Whiteprint to Claude Code."), result.text, true);
    });
}

void AISettingsPage::connectDesktop()
{
    const QString config = ClaudeDesktopConfig::defaultURL();
    if (!confirm(QStringLiteral("Connect Claude Desktop?"),
                 QStringLiteral("Whiteprint will add itself to %1 and keep a backup of the current file.").arg(abbreviated(config)),
                 QStringLiteral("Connect")))
        return;
    try {
        ClaudeDesktopConfig::connect(m_helper, config);
        inform(QStringLiteral("Claude Desktop is connected."), QStringLiteral("Quit and reopen Claude Desktop to load Whiteprint."));
    } catch (...) {
        inform(QStringLiteral("Couldn\u2019t update the Claude Desktop config."), currentFailure(), true);
    }
    updateDesktopStatus();
}

// MARK: Notes

NotesSettingsPage::NotesSettingsPage(NotesLibrary &library, QWidget *parent) : SettingsPane(parent), m_library(library)
{
    header(QStringLiteral("Notes folder"));
    note(QStringLiteral("New notes are saved here, and the sidebar lists the notes in it."));
    m_path = note({}, false);
    auto *choose = new QPushButton(QStringLiteral("Choose\u2026"));
    auto *reveal = new QPushButton(QStringLiteral("Show in Explorer"));
    for (auto *b : {choose, reveal})
        b->setFocusPolicy(Qt::NoFocus);
    connect(choose, &QPushButton::clicked, this, [this] {
        const QString dir = QFileDialog::getExistingDirectory(window(), QStringLiteral("Use Folder"), m_library.folder().url);
        if (!dir.isEmpty())
            useFolder(dir);
    });
    connect(reveal, &QPushButton::clicked, this, [this] { QDesktopServices::openUrl(QUrl::fromLocalFile(m_library.folder().url)); });
    row({choose, reveal});
    update();
}

void NotesSettingsPage::useFolder(const QString &path)
{
    m_library.changeFolder(path);
    update();
}

void NotesSettingsPage::update()
{
    m_path->setText(abbreviated(m_library.folder().url));
}

// MARK: Study

StudySettingsPage::StudySettingsPage(StudySession &study, QWidget *parent) : SettingsPane(parent), m_study(study)
{
    header(QStringLiteral("Extracted text"));
    note(QStringLiteral("Text extracted from imported files is cached on this PC with the points Claude saved, so study "
                        "plans can resume. Clearing it removes every import; your notes are not affected."));
    auto *clear = new QPushButton(QStringLiteral("Clear Extracted Text Cache\u2026"));
    clear->setFocusPolicy(Qt::NoFocus);
    connect(clear, &QPushButton::clicked, this, [this] {
        if (!confirm(QStringLiteral("Clear the extracted text cache?"),
                     QStringLiteral("All imports and saved points are removed. Your notes are kept."), QStringLiteral("Clear")))
            return;
        try {
            m_study.removeAll();
            inform(QStringLiteral("The cache was cleared."), {});
        } catch (...) {
            inform(QStringLiteral("Couldn\u2019t clear the cache."), currentFailure(), true);
        }
    });
    row({clear});
}

} // namespace wp
