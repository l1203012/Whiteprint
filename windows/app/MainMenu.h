#pragma once
// Port of MainMenu.swift: the menu bar. Windows has no global menu bar, so every note window gets
// its own QMenuBar with the same menus and shortcuts (Ctrl instead of Cmd, Alt instead of Option).
// Note actions go to the window, app actions to the AppController.
#include <QString>
#include <QStringList>

namespace wp {

class NoteWindow;

namespace MainMenu {

/// Builds the menus on `window->menuBar()`.
void install(NoteWindow *window);

/// Top-level menu titles in order, for tests.
QStringList menuTitles();

} // namespace MainMenu

} // namespace wp
