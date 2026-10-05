#include "app/AppController.h"

#include "app/AppDefaults.h"
#include "app/AppServices.h"
#include "app/AppUtil.h"
#include "app/FlashcardStudyWindow.h"
#include "app/NoteDocuments.h"
#include "app/NoteTitle.h"
#include "app/NoteWindow.h"
#include "app/PageOutline.h"
#include "app/ReferenceWindow.h"
#include "app/SettingsWindow.h"
#include "app/StudyPanel.h"
#include "app/WelcomeNote.h"
#include "bridge/BridgeServer.h"
#include "core/DSLReference.h"
#include "editor/NoteEditorView.h"

#include <QApplication>
#include <QCryptographicHash>
#include <QDesktopServices>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QLocalSocket>
#include <QMessageBox>
#include <QProcess>
#include <QUrl>
#include <QPointer>
#include <algorithm>
#include <memory>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace wp {

AppController &AppController::instance()
{
    static AppController *controller = new AppController;
    return *controller;
}

// MARK: Lifecycle

void AppController::start()
{
    if (m_started)
        return;
    m_started = true;
    auto &documents = NoteDocuments::instance();
    connect(&documents, &NoteDocuments::documentAdded, this, [this](NoteDocument *d) { documentAdded(d); });
    connect(&documents, &NoteDocuments::showRequested, this, [this](NoteDocument *d) {
        if (NoteWindow *w = window(d)) {
            if (w->isMinimized())
                w->showNormal();
            w->show();
            w->raise();
            w->activateWindow();
        }
    });
    connect(&documents, &NoteDocuments::errorOccurred, this, [this](const QString &message) {
        presentError(frontWindow(), message);
    });

    auto &services = AppServices::shared();
    m_server = std::make_unique<BridgeServer>(&services.bridge());
    try {
        m_server->start();
    } catch (const std::exception &error) {
        qWarning("Whiteprint: couldn't start the Claude bridge: %s", error.what());
        m_server.reset();
    }
}

bool AppController::isBridgeRunning() const
{
    return m_server && m_server->isListening();
}

void AppController::shutdown()
{
    if (m_server)
        m_server->stop();
    if (m_started)
        AppServices::shared().study().cancel();
}

void AppController::quit()
{
    QApplication::closeAllWindows();
    QApplication::quit();
}

void AppController::openPaths(const QStringList &paths)
{
    for (const QString &path : paths) {
        const QFileInfo info(path);
        if (info.suffix().compare(NotesFolder::fileExtension, Qt::CaseInsensitive) != 0)
            continue;
        NoteDocuments::instance().open(canonicalFile(info.absoluteFilePath()));
    }
}

void AppController::openStartupNote()
{
    auto &library = AppServices::shared().library();
    const QStringList urls = library.folder().noteURLs();
    if (urls.isEmpty()) {
        try {
            const QString url = library.folder().save(WelcomeNote::note(), WelcomeNote::title);
            library.reload();
            NoteDocuments::instance().open(url);
        } catch (...) {
            presentError(nullptr, currentErrorLine());
        }
        return;
    }
    QString newest;
    QDateTime newestTime;
    for (const QString &url : urls) {
        const QDateTime modified = QFileInfo(url).lastModified();
        if (newest.isEmpty() || modified > newestTime) {
            newest = url;
            newestTime = modified;
        }
    }
    NoteDocuments::instance().open(newest);
}

// MARK: Single instance

QString AppController::instanceServerName()
{
    QString name = QStringLiteral("whiteprint-instance-") + qEnvironmentVariable("USERNAME", QStringLiteral("user"));
    const QString extra = qEnvironmentVariable("WHITEPRINT_SOCKET") + QLatin1Char('|') +
                          qEnvironmentVariable(AppDefaults::suiteEnvironmentKey().toLatin1().constData());
    if (extra.size() > 1)
        name += QLatin1Char('-') + QString::fromLatin1(QCryptographicHash::hash(extra.toUtf8(), QCryptographicHash::Sha1).toHex().left(10));
    return name;
}

bool AppController::forwardToRunningInstance(const QStringList &paths)
{
    QLocalSocket socket;
    socket.connectToServer(instanceServerName());
    if (!socket.waitForConnected(400))
        return false;
#ifdef Q_OS_WIN
    AllowSetForegroundWindow(ASFW_ANY);
#endif
    QStringList absolute;
    for (const QString &p : paths)
        absolute.append(QFileInfo(p).absoluteFilePath());
    socket.write(absolute.join(QLatin1Char('\n')).toUtf8() + '\n');
    socket.waitForBytesWritten(1500);
    socket.disconnectFromServer();
    if (socket.state() != QLocalSocket::UnconnectedState)
        socket.waitForDisconnected(500);
    return true;
}

bool AppController::listenForInstances()
{
    QLocalServer::removeServer(instanceServerName());
    m_instanceServer.setSocketOptions(QLocalServer::UserAccessOption);
    if (!m_listening)
        connect(&m_instanceServer, &QLocalServer::newConnection, this, [this] { received(); });
    m_listening = true;
    return m_instanceServer.listen(instanceServerName());
}

void AppController::received()
{
    while (QLocalSocket *socket = m_instanceServer.nextPendingConnection()) {
        auto buffer = std::make_shared<QByteArray>();
        auto done = std::make_shared<bool>(false);
        auto process = [this, socket, buffer, done] {
            if (*done)
                return;
            *done = true;
            socket->deleteLater();
            QStringList paths;
            for (const QByteArray &line : buffer->split('\n'))
                if (!line.trimmed().isEmpty())
                    paths.append(QString::fromUtf8(line.trimmed()));
            if (paths.isEmpty())
                activateSomething();
            else
                openPaths(paths);
        };
        connect(socket, &QLocalSocket::readyRead, socket, [socket, buffer, process] {
            buffer->append(socket->readAll());
            if (buffer->endsWith('\n'))
                process();
        });
        connect(socket, &QLocalSocket::disconnected, this, [socket, buffer, process] {
            buffer->append(socket->readAll());
            process();
        });
        if (socket->bytesAvailable()) {
            buffer->append(socket->readAll());
            if (buffer->endsWith('\n'))
                process();
        }
    }
}

void AppController::activateSomething()
{
    if (NoteWindow *w = frontWindow()) {
        if (w->isMinimized())
            w->showNormal();
        w->raise();
        w->activateWindow();
    } else {
        openStartupNote();
    }
}

// MARK: Windows

NoteWindow *AppController::frontWindow() const
{
    for (QWidget *w = QApplication::activeWindow(); w; w = w->parentWidget())
        if (auto *note = qobject_cast<NoteWindow *>(w))
            return note;
    if (m_front && m_windows.contains(m_front))
        return m_front;
    return m_windows.isEmpty() ? nullptr : m_windows.last();
}

void AppController::registerWindow(NoteWindow *window)
{
    if (!m_windows.contains(window))
        m_windows.append(window);
    m_front = window;
}

void AppController::unregisterWindow(NoteWindow *window)
{
    m_windows.removeAll(window);
    if (m_front == window)
        m_front = nullptr;
}

void AppController::windowActivated(NoteWindow *window)
{
    m_front = window;
}

NoteWindow *AppController::window(NoteDocument *document) const
{
    for (NoteWindow *w : m_windows)
        if (w->noteDocument() == document)
            return w;
    return nullptr;
}

void AppController::documentAdded(NoteDocument *document)
{
    if (window(document))
        return;
    if (!document->filePath().isEmpty())
        addRecent(document->filePath());
    auto *w = new NoteWindow(document);
    w->show();
}

// MARK: Actions

void AppController::newNote()
{
    NoteWindow *front = frontWindow();
    newNote(front ? front->selectedFolder() : std::nullopt);
}

void AppController::newNote(const std::optional<QString> &folder)
{
    auto &library = AppServices::shared().library();
    try {
        const QString url = library.folder().save(Note(), NoteTitle::untitled, folder.value_or(QString()));
        library.reload();
        NoteDocuments::instance().open(url);
    } catch (...) {
        presentError(frontWindow(), currentErrorLine());
    }
}

void AppController::newFolder()
{
    if (NoteWindow *front = frontWindow())
        front->newFolder();
    else
        createFolder(std::nullopt);
}

std::optional<QString> AppController::createFolder(const std::optional<QString> &parent)
{
    auto &library = AppServices::shared().library();
    try {
        const QString url = library.folder().createFolder(QStringLiteral("New Folder"), parent.value_or(QString()));
        library.reload();
        return url;
    } catch (...) {
        presentError(frontWindow(), currentErrorLine());
        return std::nullopt;
    }
}

void AppController::openDialog(QWidget *parent)
{
    const QStringList files = QFileDialog::getOpenFileNames(parent, QStringLiteral("Open"), AppServices::shared().library().folder().url,
                                                            QStringLiteral("Whiteprint notes (*.wprint);;All files (*)"));
    openPaths(files);
}

void AppController::studyFlashcards()
{
    if (NoteWindow *front = frontWindow())
        front->studyFlashcards();
    else
        CommandPalette::presentDecks(nullptr);
}

void AppController::showCommandPalette(QWidget *over)
{
    NoteWindow *w = nullptr;
    for (QWidget *p = over; p && !w; p = p->parentWidget())
        w = qobject_cast<NoteWindow *>(p);
    CommandPalette::present(over ? over : frontWindow(), paletteCommands(w ? w : frontWindow()));
}

void AppController::showStudyPanel(QWidget *over, bool generating)
{
    StudyPanel::present(over ? over : frontWindow(), generating);
}

void AppController::showSettings()
{
    SettingsWindow::showShared();
}

void AppController::showDrawingReference()
{
    if (!m_reference)
        m_reference = ReferenceWindow::make(QStringLiteral("Drawing Language"), WhiteprintText::dslReference());
    m_reference->show();
    m_reference->raise();
    m_reference->activateWindow();
}

void AppController::showAbout(QWidget *parent)
{
    QMessageBox::about(parent, QStringLiteral("About Whiteprint"),
                       QStringLiteral("<b>Whiteprint</b> %1<br>Notes on blueprint paper, with drawings, flashcards and Claude.")
                           .arg(QApplication::applicationVersion()));
}

QList<PaletteCommand> AppController::paletteCommands(NoteWindow *window)
{
    QList<PaletteCommand> commands;
    QPointer<NoteWindow> w(window);
    if (w && w->noteDocument()) {
        const auto titles = PageOutline::titles(w->note());
        for (int i = 0; i < titles.size(); ++i)
            commands.append({titles[i], QStringLiteral("Page %1").arg(i + 1), [w, i] {
                                 if (w)
                                     w->scrollToPage(i + 1);
                             }});
    }
    commands.append({QStringLiteral("New note"), QStringLiteral("Ctrl+N"), [this] { newNote(); }});
    commands.append({QStringLiteral("New folder"), QStringLiteral("Ctrl+Shift+N"), [this] { newFolder(); }});
    commands.append({QStringLiteral("Study flashcards…"), QStringLiteral("Study"), [this] { studyFlashcards(); }});
    commands.append({QStringLiteral("Import for study plan"), QStringLiteral("Study"), [this, w] { showStudyPanel(w); }});
    commands.append({QStringLiteral("Generate study plan"), QStringLiteral("Study"), [this, w] { showStudyPanel(w, true); }});
    commands.append({QStringLiteral("Settings"), QStringLiteral("Ctrl+,"), [this] { showSettings(); }});
    if (w && w->noteDocument()) {
        commands.append({QStringLiteral("Add page"), QStringLiteral("Ctrl+Alt+N"), [w] { if (w) w->addPage(); }});
        commands.append({QStringLiteral("Insert drawing"), QStringLiteral("Ctrl+Shift+D"), [w] { if (w) w->insertDrawing(); }});
        commands.append({QStringLiteral("Insert flashcards"), QStringLiteral("Ctrl+Shift+F"), [w] { if (w) w->insertFlashcards(); }});
        commands.append({QStringLiteral("Export as Markdown…"), QStringLiteral("Export"), [w] { if (w) w->exportMarkdown(); }});
        commands.append({QStringLiteral("Export as PDF (Blueprint)…"), QStringLiteral("Export"), [w] { if (w) w->exportPDF(true); }});
        commands.append({QStringLiteral("Export as PDF (Print)…"), QStringLiteral("Export"), [w] { if (w) w->exportPDF(false); }});
        commands.append({QStringLiteral("Show in Explorer"), QStringLiteral("Ctrl+Alt+R"), [w] { if (w) w->showInExplorer(); }});
    }
    return commands;
}

void AppController::showInExplorer(const QString &path)
{
    if (path.isEmpty())
        return;
    if (QFileInfo(path).isDir())
        QDesktopServices::openUrl(QUrl::fromLocalFile(path));
    else
        QProcess::startDetached(QStringLiteral("explorer.exe"), {QStringLiteral("/select,") + QDir::toNativeSeparators(path)});
}

void AppController::presentError(QWidget *parent, const QString &message, const QString &title)
{
    QMessageBox::warning(parent ? parent : QApplication::activeWindow(), title, message);
}

// MARK: Recent notes

QStringList AppController::recentNotes()
{
    QStringList result;
    for (const QString &path : AppDefaults::store().value(QStringLiteral("RecentNotes")).toStringList())
        if (QFileInfo::exists(path))
            result.append(path);
    return result;
}

void AppController::addRecent(const QString &path)
{
    QStringList list = AppDefaults::store().value(QStringLiteral("RecentNotes")).toStringList();
    const QString key = pathKey(path);
    list.erase(std::remove_if(list.begin(), list.end(), [&](const QString &p) { return pathKey(p) == key; }), list.end());
    list.prepend(path);
    while (list.size() > maxRecent)
        list.removeLast();
    AppDefaults::store().setValue(QStringLiteral("RecentNotes"), list);
    emit instance().recentChanged();
}

void AppController::clearRecent()
{
    AppDefaults::store().remove(QStringLiteral("RecentNotes"));
    emit instance().recentChanged();
}

} // namespace wp
