#pragma once
#include "editor/EditorCommand.h"

#include <QHash>
#include <QKeySequence>
#include <QList>
#include <QObject>
#include <functional>

class QAction;
class QToolBar;
class QWidget;

namespace wp {

/// The editor's command set as `QAction`s, the Windows counterpart of the macOS
/// Touch Bar: a text style group (H1/H2/H3/Body), Bold, Italic, Code, Checklist,
/// Bulleted list, Insert drawing, Insert flashcards and New page. Every action
/// runs an `EditorCommand` through `onCommand`. Optional: use `makeToolBar()`
/// for a ready toolbar, or add `actions()` to your own menus and toolbars.
class EditorToolbar : public QObject
{
public:
    /// The commands with a button of their own, in default order.
    static QList<EditorCommand> buttonCommands();
    /// The commands in the style group.
    static QList<EditorCommand> styleCommands();
    /// Style commands, then button commands.
    static QList<EditorCommand> defaultCommands();
    /// Suggested shortcut (Ctrl instead of Cmd); empty when the command has none.
    static QKeySequence shortcut(EditorCommand command);

    explicit EditorToolbar(std::function<void(EditorCommand)> onCommand, QObject *parent = nullptr);

    /// The action for a command, created on first use.
    QAction *action(EditorCommand command);
    /// Actions for `defaultCommands()`.
    QList<QAction *> actions();

    /// A toolbar with the style actions, a separator, then the button actions.
    QToolBar *makeToolBar(QWidget *parent = nullptr);

private:
    std::function<void(EditorCommand)> m_onCommand;
    QHash<int, QAction *> m_actions;
};

} // namespace wp
