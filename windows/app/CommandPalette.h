#pragma once
// Port of CommandPalette.swift: Ctrl+K, a centred floating panel to jump to notes, run commands and
// pick a flashcard deck to study. Arrow keys move, Enter runs, Escape (or clicking elsewhere) closes.
#include <QFrame>
#include <QList>
#include <QString>
#include <QStringList>
#include <functional>

class QLineEdit;
class QListWidget;

namespace wp {

/// A command row: `shortcut` is shown at the right (e.g. `Ctrl+N`), `run` is called after the panel closed.
struct PaletteCommand {
    QString title;
    QString shortcut;
    std::function<void()> run;
};

class CommandPalette : public QFrame {
    Q_OBJECT
public:
    enum class Kind { note, page, command, deck };

    struct Item {
        Kind kind = Kind::command;
        QString title;
        QString detail;
        std::function<void()> run;
    };

    /// Shows the shared palette centred over `window` (the screen when null), listing the notes of
    /// AppServices::shared().library() (opened with NoteDocuments::instance().open) and `commands`.
    /// Pass page-jump entries as commands too (their `shortcut` text is used as the detail).
    static void present(QWidget *window, QList<PaletteCommand> commands);

    /// Lists every flashcard deck (DeckCatalog::all()); choosing one opens FlashcardStudyWindow.
    static void presentDecks(QWidget *window);

    // Testable core -------------------------------------------------------------------------------
    explicit CommandPalette(QWidget *parent = nullptr);

    /// Replaces the items and clears the query.
    void setItems(QList<Item> items, const QString &placeholder);
    /// The notes (from the library) followed by `commands`.
    static QList<Item> itemsFor(const QList<PaletteCommand> &commands);
    static QList<Item> deckItems();

    void setQuery(const QString &query);
    QString query() const;
    /// Titles of the rows currently listed, best match first.
    QStringList shownTitles() const;
    int selectedRow() const;
    void moveSelection(int delta);
    /// Closes the panel and runs the selected item (asynchronously, once the panel is gone).
    void runSelected();

protected:
    bool eventFilter(QObject *watched, QEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void changeEvent(QEvent *event) override;

private:
    void filter();
    void positionOver(QWidget *window);

    QLineEdit *m_field;
    QListWidget *m_list;
    QList<Item> m_items;
    QList<Item> m_shown;
};

} // namespace wp
