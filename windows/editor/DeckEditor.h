#pragma once
#include "editor/DeckDraft.h"
#include "editor/EditorDocument.h"

#include <QFrame>
#include <functional>

class QLabel;
class QLineEdit;
class QVBoxLayout;
class QWidget;
class QPlainTextEdit;

namespace wp {

/// The deck popup's content: the title and a list of cards with question,
/// answer and source fields, which can be added, removed and reordered.
/// Done (Ctrl+Enter) commits; so does any other close (clicking outside, Esc).
class DeckEditor : public QFrame
{
    Q_OBJECT

public:
    static constexpr int editorWidth = 440;

    DeckEditor(BlockID blockID, const CardDeck &deck, std::function<void(const CardDeck &)> onCommit, QWidget *parent = nullptr);

    BlockID blockID() const { return m_blockID; }
    const DeckDraft &draft() const { return m_draft; }

    /// Shows the editor as a popup below `anchor`.
    void present(QWidget *anchor);

    // MARK: Cards

    void addCard();
    void removeCard(int index);
    void moveCard(int index, int delta);

    // MARK: Committing

    /// Closes the popup (which commits), or commits directly when not shown.
    void close();
    /// Closes without committing (the deck went away).
    void discard();
    void commit();

protected:
    void hideEvent(QHideEvent *) override;
    void keyPressEvent(QKeyEvent *) override;

private:
    void rebuildCards();
    void fieldChanged(int index, int field, const QString &text);
    QPlainTextEdit *makeField(const QString &placeholder, double size, int weight, const QString &text);

    BlockID m_blockID;
    DeckDraft m_draft;
    std::function<void(const CardDeck &)> m_onCommit;
    QLineEdit *m_title;
    QVBoxLayout *m_cards;
    QWidget *m_cardsHost;
    QLabel *m_count;
    QList<QPlainTextEdit *> m_questions;
    bool m_committed = false;
};

} // namespace wp
