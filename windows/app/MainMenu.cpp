#include "app/MainMenu.h"

#include "app/AppController.h"
#include "app/NoteTitle.h"
#include "app/NoteWindow.h"
#include "app/ViewPreferences.h"
#include "editor/EditorCommand.h"

#include <QActionGroup>
#include <QApplication>
#include <QKeyEvent>
#include <QMenu>
#include <QMenuBar>
#include <QPointer>
#include <memory>
#include <utility>

namespace wp::MainMenu {

namespace {

QAction *add(QMenu *menu, const QString &title, const QKeySequence &shortcut, QObject *context, const std::function<void()> &run)
{
    QAction *action = menu->addAction(title);
    if (!shortcut.isEmpty())
        action->setShortcut(shortcut);
    QObject::connect(action, &QAction::triggered, context, [run] { run(); });
    return action;
}

QKeySequence keys(const char *text)
{
    return QKeySequence(QString::fromLatin1(text));
}

void fileMenu(QMenuBar *bar, NoteWindow *w)
{
    auto &app = AppController::instance();
    QMenu *menu = bar->addMenu(QStringLiteral("&File"));
    add(menu, QStringLiteral("New Note"), keys("Ctrl+N"), w, [&app] { app.newNote(); });
    add(menu, QStringLiteral("New Folder"), keys("Ctrl+Shift+N"), w, [&app] { app.newFolder(); });
    add(menu, QStringLiteral("Open…"), keys("Ctrl+O"), w, [&app, w] { app.openDialog(w); });
    QMenu *recent = menu->addMenu(QStringLiteral("Open Recent"));
    QObject::connect(recent, &QMenu::aboutToShow, recent, [recent] {
        recent->clear();
        const QStringList paths = AppController::recentNotes();
        for (const QString &path : paths) {
            QAction *a = recent->addAction(NoteTitle::baseName(path));
            QObject::connect(a, &QAction::triggered, recent, [path] { AppController::instance().openPaths({path}); });
        }
        if (paths.isEmpty())
            recent->addAction(QStringLiteral("No Recent Notes"))->setEnabled(false);
        recent->addSeparator();
        QAction *clear = recent->addAction(QStringLiteral("Clear Menu"));
        QObject::connect(clear, &QAction::triggered, recent, [] { AppController::clearRecent(); });
        clear->setEnabled(!paths.isEmpty());
    });
    menu->addSeparator();
    add(menu, QStringLiteral("Close"), keys("Ctrl+W"), w, [w] { w->close(); });
    add(menu, QStringLiteral("Save"), keys("Ctrl+S"), w, [w] { w->saveDocument(); });
    add(menu, QStringLiteral("Duplicate"), keys("Ctrl+Shift+S"), w, [w] { w->duplicateDocument(); });
    add(menu, QStringLiteral("Rename…"), {}, w, [w] { w->renameDocument(); });
    add(menu, QStringLiteral("Move To…"), {}, w, [w] { w->moveDocument(); });
    add(menu, QStringLiteral("Revert To Saved"), {}, w, [w] { w->revertDocument(); });
    menu->addSeparator();
    QMenu *exportMenu = menu->addMenu(QStringLiteral("Export"));
    add(exportMenu, QStringLiteral("Markdown…"), {}, w, [w] { w->exportMarkdown(); });
    add(exportMenu, QStringLiteral("PDF (Blueprint)…"), {}, w, [w] { w->exportPDF(true); });
    add(exportMenu, QStringLiteral("PDF (Print)…"), {}, w, [w] { w->exportPDF(false); });
    add(menu, QStringLiteral("Import for Study Plan…"), {}, w, [&app, w] { app.showStudyPanel(w); });
    menu->addSeparator();
    add(menu, QStringLiteral("Show in Explorer"), keys("Ctrl+Alt+R"), w, [w] { w->showInExplorer(); });
    menu->addSeparator();
    add(menu, QStringLiteral("Settings…"), keys("Ctrl+,"), w, [&app] { app.showSettings(); });
    menu->addSeparator();
    add(menu, QStringLiteral("Exit"), keys("Ctrl+Q"), w, [&app] { app.quit(); });
}

void editMenu(QMenuBar *bar, NoteWindow *w)
{
    QMenu *menu = bar->addMenu(QStringLiteral("&Edit"));
    QAction *undo = add(menu, QStringLiteral("Undo"), keys("Ctrl+Z"), w, [w] { w->undo(); });
    QAction *redo = add(menu, QStringLiteral("Redo"), keys("Ctrl+Shift+Z"), w, [w] { w->redo(); });
    redo->setShortcuts({keys("Ctrl+Shift+Z"), keys("Ctrl+Y")});
    QObject::connect(menu, &QMenu::aboutToShow, menu, [w, undo, redo] {
        const QString u = w->undoText(), r = w->redoText();
        undo->setText(u.isEmpty() ? QStringLiteral("Undo") : QStringLiteral("Undo ") + u);
        redo->setText(r.isEmpty() ? QStringLiteral("Redo") : QStringLiteral("Redo ") + r);
    });
    menu->addSeparator();
    add(menu, QStringLiteral("Cut"), keys("Ctrl+X"), w, [w] { w->forwardToFocus("cut"); });
    add(menu, QStringLiteral("Copy"), keys("Ctrl+C"), w, [w] { w->forwardToFocus("copy"); });
    add(menu, QStringLiteral("Paste"), keys("Ctrl+V"), w, [w] { w->forwardToFocus("paste"); });
    add(menu, QStringLiteral("Paste and Match Style"), keys("Ctrl+Alt+Shift+V"), w, [w] { w->forwardToFocus("paste"); });
    add(menu, QStringLiteral("Delete"), {}, w, [] {
        if (QWidget *focus = QApplication::focusWidget()) {
            QKeyEvent press(QEvent::KeyPress, Qt::Key_Delete, Qt::NoModifier);
            QApplication::sendEvent(focus, &press);
            QKeyEvent release(QEvent::KeyRelease, Qt::Key_Delete, Qt::NoModifier);
            QApplication::sendEvent(focus, &release);
        }
    });
    add(menu, QStringLiteral("Select All"), keys("Ctrl+A"), w, [w] { w->forwardToFocus("selectAll"); });
}

void viewMenu(QMenuBar *bar, NoteWindow *w)
{
    auto &app = AppController::instance();
    QMenu *menu = bar->addMenu(QStringLiteral("&View"));
    add(menu, QStringLiteral("Toggle Sidebar"), keys("Ctrl+\\"), w, [w] { w->toggleSidebar(); });
    add(menu, QStringLiteral("Command Palette"), keys("Ctrl+K"), w, [&app, w] { app.showCommandPalette(w); });
    menu->addSeparator();
    QMenu *layout = menu->addMenu(QStringLiteral("Page Layout"));
    auto *group = new QActionGroup(layout);
    QAction *slides = add(layout, QStringLiteral("Slides"), {}, w, [] { ViewPreferences::shared().setLayoutMode(ViewLayout::slides); });
    QAction *a4 = add(layout, QStringLiteral("A4"), {}, w, [] { ViewPreferences::shared().setLayoutMode(ViewLayout::a4); });
    for (QAction *a : {slides, a4}) {
        a->setCheckable(true);
        group->addAction(a);
    }
    QMenu *themeMenu = menu->addMenu(QStringLiteral("Page Theme"));
    auto *themeGroup = new QActionGroup(themeMenu);
    QList<std::pair<PageTheme, QAction *>> themes;
    for (PageTheme theme : PageThemes::all()) {
        QAction *a = add(themeMenu, PageThemes::title(theme), {}, w, [theme] { ViewPreferences::shared().setPageTheme(theme); });
        a->setCheckable(true);
        themeGroup->addAction(a);
        themes.append({theme, a});
    }
    QAction *syntax = add(menu, QStringLiteral("Show Markdown Syntax"), keys("Ctrl+Shift+M"), w,
                          [] { ViewPreferences::shared().setShowsMarkdownSyntax(!ViewPreferences::shared().showsMarkdownSyntax()); });
    syntax->setCheckable(true);
    auto refresh = [slides, a4, syntax, themes] {
        const auto &prefs = ViewPreferences::shared();
        slides->setChecked(prefs.layoutMode() == ViewLayout::slides);
        a4->setChecked(prefs.layoutMode() == ViewLayout::a4);
        for (const auto &[theme, action] : themes)
            action->setChecked(prefs.pageTheme() == theme);
        syntax->setChecked(prefs.showsMarkdownSyntax());
    };
    QObject::connect(menu, &QMenu::aboutToShow, menu, refresh);
    QObject::connect(layout, &QMenu::aboutToShow, layout, refresh);
    QObject::connect(themeMenu, &QMenu::aboutToShow, themeMenu, refresh);
    refresh();
    menu->addSeparator();
    add(menu, QStringLiteral("Enter Full Screen"), keys("F11"), w, [w] { w->isFullScreen() ? w->showNormal() : w->showFullScreen(); });
}

void formatMenu(QMenuBar *bar, NoteWindow *w)
{
    QMenu *menu = bar->addMenu(QStringLiteral("F&ormat"));
    struct Item {
        EditorCommand command;
        const char *shortcut;
    };
    const QList<QList<Item>> groups = {
        {{EditorCommand::heading1, "Ctrl+Alt+1"}, {EditorCommand::heading2, "Ctrl+Alt+2"},
         {EditorCommand::heading3, "Ctrl+Alt+3"}, {EditorCommand::body, "Ctrl+Alt+0"}},
        {{EditorCommand::bold, "Ctrl+B"}, {EditorCommand::italic, "Ctrl+I"}, {EditorCommand::inlineCode, ""}},
        {{EditorCommand::bulletList, "Ctrl+Alt+8"}, {EditorCommand::numberedList, "Ctrl+Alt+7"},
         {EditorCommand::checklist, "Ctrl+Alt+9"}, {EditorCommand::quote, ""}, {EditorCommand::codeBlock, ""},
         {EditorCommand::divider, ""}},
    };
    for (int i = 0; i < groups.size(); ++i) {
        if (i > 0)
            menu->addSeparator();
        for (const Item &item : groups[i]) {
            const EditorCommand command = item.command;
            add(menu, EditorCommands::title(command), QString::fromLatin1(item.shortcut).isEmpty() ? QKeySequence() : keys(item.shortcut), w,
                [w, command] { w->performEditorCommand(command); });
        }
    }
}

void noteMenu(QMenuBar *bar, NoteWindow *w)
{
    auto &app = AppController::instance();
    QMenu *menu = bar->addMenu(QStringLiteral("&Note"));
    add(menu, QStringLiteral("Add Page"), keys("Ctrl+Alt+N"), w, [w] { w->addPage(); });
    add(menu, QStringLiteral("Insert Drawing"), keys("Ctrl+Shift+D"), w, [w] { w->insertDrawing(); });
    add(menu, QStringLiteral("Insert Flashcards"), keys("Ctrl+Shift+F"), w, [w] { w->insertFlashcards(); });
    menu->addSeparator();
    add(menu, QStringLiteral("Study Flashcards…"), {}, w, [&app] { app.studyFlashcards(); });
    add(menu, QStringLiteral("Generate Study Plan…"), {}, w, [&app, w] { app.showStudyPanel(w, true); });
}

void windowMenu(QMenuBar *bar, NoteWindow *w)
{
    QMenu *menu = bar->addMenu(QStringLiteral("&Window"));
    add(menu, QStringLiteral("Minimize"), keys("Ctrl+M"), w, [w] { w->showMinimized(); });
    add(menu, QStringLiteral("Zoom"), {}, w, [w] { w->isMaximized() ? w->showNormal() : w->showMaximized(); });
    menu->addSeparator();
    add(menu, QStringLiteral("Bring All to Front"), {}, w, [w] {
        for (NoteWindow *other : AppController::instance().windows()) {
            other->show();
            other->raise();
        }
        w->raise();
        w->activateWindow();
    });
    auto tail = std::make_shared<QList<QPointer<QAction>>>();
    QObject::connect(menu, &QMenu::aboutToShow, menu, [menu, w, tail] {
        for (const auto &a : *tail)
            delete a.data();
        tail->clear();
        const auto windows = AppController::instance().windows();
        if (!windows.isEmpty())
            tail->append(menu->addSeparator());
        for (NoteWindow *other : windows) {
            QAction *a = menu->addAction(other->windowTitle().remove(QStringLiteral("[*]")));
            tail->append(a);
            a->setCheckable(true);
            a->setChecked(other == w);
            QObject::connect(a, &QAction::triggered, menu, [other] {
                other->show();
                other->raise();
                other->activateWindow();
            });
        }
    });
}

void helpMenu(QMenuBar *bar, NoteWindow *w)
{
    auto &app = AppController::instance();
    QMenu *menu = bar->addMenu(QStringLiteral("&Help"));
    add(menu, QStringLiteral("Drawing Language"), {}, w, [&app] { app.showDrawingReference(); });
    menu->addSeparator();
    add(menu, QStringLiteral("About Whiteprint"), {}, w, [&app, w] { app.showAbout(w); });
}

} // namespace

void install(NoteWindow *window)
{
    QMenuBar *bar = window->menuBar();
    bar->clear();
    fileMenu(bar, window);
    editMenu(bar, window);
    viewMenu(bar, window);
    formatMenu(bar, window);
    noteMenu(bar, window);
    windowMenu(bar, window);
    helpMenu(bar, window);
}

QStringList menuTitles()
{
    return {QStringLiteral("File"), QStringLiteral("Edit"), QStringLiteral("View"), QStringLiteral("Format"),
            QStringLiteral("Note"), QStringLiteral("Window"), QStringLiteral("Help")};
}

} // namespace wp::MainMenu
