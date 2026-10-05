#pragma once
// Port of FlashcardStudyWindowController.swift: "Study Flashcards", one card at a time on blueprint
// paper. Space or a click flips; Again / Hard / Good / Easy (keys 1-4) schedule and move on.
#include "app/FlashcardProgressStore.h"
#include "app/FlashcardSession.h"
#include "core/CardDeck.h"
#include "render/Palette.h"

#include <QList>
#include <QWidget>

class QCheckBox;
class QLabel;
class QPushButton;

namespace wp {

/// The big blueprint card: question, or question and answer with its source, or a message / the
/// end-of-run summary.
class FlashcardView : public QWidget {
    Q_OBJECT
public:
    struct Content {
        enum class Kind { card, message, finished } kind = Kind::message;
        Flashcard card;
        bool flipped = false;
        QString title, detail;
        FlashcardSession::Stats stats;
    };

    explicit FlashcardView(const BlueprintPalette &palette, QWidget *parent = nullptr);
    void setContent(const Content &content);
    const Content &content() const { return m_content; }

signals:
    void clicked();

protected:
    void paintEvent(QPaintEvent *) override;
    void mousePressEvent(QMouseEvent *) override;

private:
    BlueprintPalette m_palette;
    Content m_content;
};

class FlashcardStudyWindow : public QWidget {
    Q_OBJECT
public:
    /// Opens (or reuses) the study window on deck `deckID` of the note at `notePath` (a file path).
    /// Reads the note (the open document's, else the file); an unreadable note or a deck without cards
    /// shows a message box instead. Progress goes to AppServices::shared().flashcards().
    /// Use it for `NoteEditorView::studyDeckRequested(CardDeck)` with `deck.id`, for the sidebar and for
    /// the palette's deck list.
    static void open(const QString &notePath, const QString &deckID);

    FlashcardStudyWindow(const QString &notePath, const QString &noteTitle, const CardDeck &deck,
                         FlashcardProgressStore *store, QWidget *parent = nullptr);

    // State, for tests.
    const FlashcardSession &session() const { return m_session; }
    void flip();
    void rate(CardRating rating);
    void studyAll();
    void setShuffled(bool shuffled);
    /// Re-targets the window at another deck (like opening it again).
    void load(const QString &notePath, const QString &noteTitle, const CardDeck &deck);
    FlashcardView *cardView() const { return m_card; }
    QString summaryText() const;
    /// Space / Enter flip, 1-4 rate, Escape closes. True when handled.
    bool handleKey(int key, Qt::KeyboardModifiers modifiers);

protected:
    void keyPressEvent(QKeyEvent *event) override;
    void closeEvent(QCloseEvent *event) override;

private:
    void restart(bool everything);
    void refresh();

    FlashcardProgressStore *m_store;
    QString m_notePath, m_noteTitle;
    CardDeck m_deck;
    FlashcardSession m_session;
    bool m_shuffled = false;

    QLabel *m_title, *m_summary, *m_hint;
    QCheckBox *m_shuffleBox;
    FlashcardView *m_card;
    QPushButton *m_flipButton, *m_studyAllButton, *m_closeButton;
    QList<QPushButton *> m_ratingButtons;
    QWidget *m_ratingRow, *m_finishRow;
};

} // namespace wp
