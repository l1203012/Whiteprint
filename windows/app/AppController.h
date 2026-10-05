#pragma once
// Port of AppDelegate.swift: the app lifecycle (bridge server, startup note, single instance,
// opening files) and the app-wide actions behind the menus (new note and folder, palette, study
// panel, settings, drawing reference). One window per open note document, created here.
#include "app/CommandPalette.h"
#include "app/NoteWindow.h"
#include "bridge/BridgeServer.h"

#include <QList>
#include <QLocalServer>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QStringList>
#include <memory>
#include <optional>

namespace wp {

class BridgeServer;
class NoteDocument;
class NoteWindow;

class AppController : public QObject {
    Q_OBJECT
public:
    static AppController &instance();

    // MARK: Lifecycle

    /// Wires the document windows and starts the bridge server (a failure is logged, not fatal).
    void start();
    /// Stops the bridge and cancels a study run (before the application quits).
    void shutdown();
    /// Opens each `.wprint` path (command line, file association); ignores anything else.
    void openPaths(const QStringList &paths);
    /// The most recently edited note, or a new Welcome note in an empty folder.
    void openStartupNote();
    bool isBridgeRunning() const;

    // MARK: Single instance

    /// Sends `paths` to an instance that is already running and returns true (then this process
    /// should exit); false when this is the first instance.
    static bool forwardToRunningInstance(const QStringList &paths);
    /// Starts listening for later instances (they send their paths, and the window is raised).
    bool listenForInstances();
    static QString instanceServerName();

    // MARK: Windows

    QList<NoteWindow *> windows() const { return m_windows; }
    /// The active note window, or the most recently active one.
    NoteWindow *frontWindow() const;
    void registerWindow(NoteWindow *window);
    void unregisterWindow(NoteWindow *window);
    void windowActivated(NoteWindow *window);
    NoteWindow *window(NoteDocument *document) const;

    // MARK: Actions

    /// A new note in the folder selected in the front window's sidebar.
    void newNote();
    void newNote(const std::optional<QString> &folder);
    void newFolder();
    std::optional<QString> createFolder(const std::optional<QString> &parent);
    void openDialog(QWidget *parent);
    void studyFlashcards();
    void showCommandPalette(QWidget *over);
    void showStudyPanel(QWidget *over, bool generating = false);
    void showSettings();
    void showDrawingReference();
    void showAbout(QWidget *parent);
    void quit();

    /// The palette's command list (menu commands, plus jumps to the pages of `window`'s note).
    QList<PaletteCommand> paletteCommands(NoteWindow *window);

    /// Opens Explorer with the file selected (or the folder opened).
    static void showInExplorer(const QString &path);

    static void presentError(QWidget *parent, const QString &message, const QString &title = QStringLiteral("Whiteprint"));

    // MARK: Recent notes

    static QStringList recentNotes();
    static void addRecent(const QString &path);
    static void clearRecent();
    static constexpr int maxRecent = 10;

signals:
    void recentChanged();

private:
    AppController() = default;
    void documentAdded(NoteDocument *document);
    void received();
    void activateSomething();

    std::unique_ptr<BridgeServer> m_server;
    QList<NoteWindow *> m_windows;
    QPointer<NoteWindow> m_front;
    QPointer<QWidget> m_reference;
    QLocalServer m_instanceServer;
    bool m_started = false;
    bool m_listening = false;
};

} // namespace wp
