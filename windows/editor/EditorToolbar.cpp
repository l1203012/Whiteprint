#include "editor/EditorToolbar.h"

#include <QAction>
#include <QToolBar>

namespace wp {

QList<EditorCommand> EditorToolbar::buttonCommands()
{
    return {EditorCommand::bold,       EditorCommand::italic,   EditorCommand::inlineCode, EditorCommand::checklist,
            EditorCommand::bulletList, EditorCommand::drawing,  EditorCommand::flashcards, EditorCommand::newPage};
}

QList<EditorCommand> EditorToolbar::styleCommands()
{
    return {EditorCommand::heading1, EditorCommand::heading2, EditorCommand::heading3, EditorCommand::body};
}

QList<EditorCommand> EditorToolbar::defaultCommands()
{
    return styleCommands() + buttonCommands();
}

QKeySequence EditorToolbar::shortcut(EditorCommand command)
{
    switch (command) {
    case EditorCommand::bold: return QKeySequence(Qt::CTRL | Qt::Key_B);
    case EditorCommand::italic: return QKeySequence(Qt::CTRL | Qt::Key_I);
    case EditorCommand::inlineCode: return QKeySequence(Qt::CTRL | Qt::Key_E);
    case EditorCommand::heading1: return QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_1);
    case EditorCommand::heading2: return QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_2);
    case EditorCommand::heading3: return QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_3);
    case EditorCommand::body: return QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_0);
    default: return QKeySequence();
    }
}

EditorToolbar::EditorToolbar(std::function<void(EditorCommand)> onCommand, QObject *parent)
    : QObject(parent), m_onCommand(std::move(onCommand))
{
}

QAction *EditorToolbar::action(EditorCommand command)
{
    if (QAction *existing = m_actions.value(int(command)))
        return existing;
    auto *action = new QAction(EditorCommands::glyph(command), this);
    action->setToolTip(EditorCommands::title(command));
    action->setStatusTip(EditorCommands::title(command));
    action->setData(EditorCommands::rawValue(command));
    connect(action, &QAction::triggered, this, [this, command] {
        if (m_onCommand)
            m_onCommand(command);
    });
    m_actions.insert(int(command), action);
    return action;
}

QList<QAction *> EditorToolbar::actions()
{
    QList<QAction *> result;
    for (EditorCommand command : defaultCommands())
        result.append(action(command));
    return result;
}

QToolBar *EditorToolbar::makeToolBar(QWidget *parent)
{
    auto *bar = new QToolBar(QStringLiteral("Formatting"), parent);
    bar->setObjectName(QStringLiteral("io.github.l1203012.whiteprint.editor"));
    for (EditorCommand command : styleCommands())
        bar->addAction(action(command));
    bar->addSeparator();
    for (EditorCommand command : buttonCommands())
        bar->addAction(action(command));
    return bar;
}

} // namespace wp
