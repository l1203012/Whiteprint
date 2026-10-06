#include "editor/DeckEditor.h"

#include "editor/DeckBlockView.h"
#include "render/Fonts.h"

#include <QAbstractTextDocumentLayout>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QToolButton>
#include <QVBoxLayout>
#include <cmath>

namespace wp {

DeckEditor::DeckEditor(BlockID blockID, const CardDeck &deck, std::function<void(const CardDeck &)> onCommit, QWidget *parent)
    : QFrame(parent, Qt::Popup), m_blockID(blockID), m_draft(deck), m_onCommit(std::move(onCommit))
{
    setFrameShape(QFrame::StyledPanel);
    setFixedWidth(editorWidth);

    auto *heading = new QLabel(QStringLiteral("Flashcards"));
    heading->setFont(fonts::system(13, QFont::DemiBold));
    m_count = new QLabel;
    m_count->setFont(fonts::system(11));
    m_count->setEnabled(false);
    auto *header = new QHBoxLayout;
    header->addWidget(heading);
    header->addWidget(m_count);
    header->addStretch();

    m_title = new QLineEdit(m_draft.title);
    m_title->setPlaceholderText(QStringLiteral("Deck title"));
    m_title->setFont(fonts::system(13));
    m_title->setAccessibleName(QStringLiteral("Deck title"));
    connect(m_title, &QLineEdit::textChanged, this, [this](const QString &t) { m_draft.title = t; });

    m_cardsHost = new QWidget;
    m_cards = new QVBoxLayout(m_cardsHost);
    m_cards->setContentsMargins(8, 8, 8, 8);
    m_cards->setSpacing(10);
    auto *scroll = new QScrollArea;
    scroll->setWidgetResizable(true);
    scroll->setWidget(m_cardsHost);
    scroll->setFixedHeight(320);
    scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);

    auto *add = new QPushButton(QStringLiteral("+ Add card"));
    connect(add, &QPushButton::clicked, this, [this] { addCard(); });
    auto *done = new QPushButton(QStringLiteral("Done"));
    done->setDefault(true);
    connect(done, &QPushButton::clicked, this, [this] { close(); });
    auto *hint = new QLabel(QStringLiteral("Ctrl+Enter"));
    hint->setFont(fonts::system(11));
    hint->setEnabled(false);
    auto *footer = new QHBoxLayout;
    footer->addWidget(add);
    footer->addStretch();
    footer->addWidget(hint);
    footer->addWidget(done);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(12, 12, 12, 12);
    layout->setSpacing(10);
    layout->addLayout(header);
    layout->addWidget(m_title);
    layout->addWidget(scroll);
    layout->addLayout(footer);
    rebuildCards();
}

QPlainTextEdit *DeckEditor::makeField(const QString &placeholder, double size, int weight, const QString &text)
{
    auto *field = new QPlainTextEdit(text);
    field->setPlaceholderText(placeholder);
    field->setFont(fonts::system(size, QFont::Weight(weight)));
    field->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    field->setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    auto fit = [field] {
        const int lines = std::max(1, int(std::ceil(field->document()->size().height())));
        field->setFixedHeight(lines + 10);
    };
    connect(field, &QPlainTextEdit::textChanged, field, fit);
    connect(field->document()->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged, field, fit);
    fit();
    return field;
}

void DeckEditor::rebuildCards()
{
    while (QLayoutItem *item = m_cards->takeAt(0)) {
        if (QWidget *w = item->widget())
            w->deleteLater();
        delete item;
    }
    m_questions.clear();
    const int count = int(m_draft.cards.size());
    for (int index = 0; index < count; ++index) {
        const Flashcard &card = m_draft.cards[index];
        auto *row = new QWidget;
        auto *rowLayout = new QHBoxLayout(row);
        rowLayout->setContentsMargins(0, 0, 0, 0);
        rowLayout->setAlignment(Qt::AlignTop);
        auto *number = new QLabel(QString::number(index + 1));
        number->setFixedWidth(18);
        number->setAlignment(Qt::AlignTop);
        auto *fields = new QVBoxLayout;
        fields->setSpacing(4);
        QPlainTextEdit *question = makeField(QStringLiteral("Question"), 13, QFont::Medium, card.question);
        QPlainTextEdit *answer = makeField(QStringLiteral("Answer"), 13, QFont::Normal, card.answer);
        QPlainTextEdit *ref =
            makeField(QStringLiteral("Source (optional), e.g. Lecture 3 · slide 14"), 11, QFont::Normal, card.ref.value_or(QString()));
        question->setAccessibleName(QStringLiteral("Question %1").arg(index + 1));
        answer->setAccessibleName(QStringLiteral("Answer %1").arg(index + 1));
        ref->setAccessibleName(QStringLiteral("Source %1").arg(index + 1));
        connect(question, &QPlainTextEdit::textChanged, this, [this, index, question] { fieldChanged(index, 0, question->toPlainText()); });
        connect(answer, &QPlainTextEdit::textChanged, this, [this, index, answer] { fieldChanged(index, 1, answer->toPlainText()); });
        connect(ref, &QPlainTextEdit::textChanged, this, [this, index, ref] { fieldChanged(index, 2, ref->toPlainText()); });
        fields->addWidget(question);
        fields->addWidget(answer);
        fields->addWidget(ref);
        auto *buttons = new QVBoxLayout;
        buttons->setSpacing(2);
        auto button = [&](const QString &text, const QString &tip, bool enabled, auto action) {
            auto *b = new QToolButton;
            b->setText(text);
            b->setToolTip(tip);
            b->setAccessibleName(tip);
            b->setAutoRaise(true);
            b->setEnabled(enabled);
            connect(b, &QToolButton::clicked, this, action);
            buttons->addWidget(b);
        };
        button(QString(QChar(0x25B2)), QStringLiteral("Move card up"), index > 0, [this, index] { moveCard(index, -1); });
        button(QString(QChar(0x25BC)), QStringLiteral("Move card down"), index < count - 1, [this, index] { moveCard(index, 1); });
        button(QString(QChar(0x2715)), QStringLiteral("Remove card"), true, [this, index] { removeCard(index); });
        buttons->addStretch();
        rowLayout->addWidget(number);
        rowLayout->addLayout(fields, 1);
        rowLayout->addLayout(buttons);
        m_cards->addWidget(row);
        m_questions.append(question);
    }
    if (count == 0)
        m_cards->addWidget(new QLabel(QStringLiteral("No cards yet.")));
    m_cards->addStretch();
    m_count->setText(DeckLayout::countText(count));
}

void DeckEditor::fieldChanged(int index, int field, const QString &text)
{
    if (index < 0 || index >= m_draft.cards.size())
        return;
    Flashcard &card = m_draft.cards[index];
    switch (field) {
    case 0: card.question = text; break;
    case 1: card.answer = text; break;
    default: card.ref = text; break;
    }
}

void DeckEditor::present(QWidget *anchor)
{
    adjustSize();
    move(anchor->mapToGlobal(QPoint(std::max(0, (anchor->width() - width()) / 2), anchor->height() + 4)));
    show();
    (m_questions.isEmpty() ? static_cast<QWidget *>(m_title) : static_cast<QWidget *>(m_questions.first()))->setFocus();
}

void DeckEditor::addCard()
{
    const int index = m_draft.addCard();
    rebuildCards();
    if (index >= 0 && index < m_questions.size())
        m_questions[index]->setFocus();
}

void DeckEditor::removeCard(int index)
{
    m_draft.removeCard(index);
    rebuildCards();
}

void DeckEditor::moveCard(int index, int delta)
{
    if (!m_draft.moveCard(index, delta))
        return;
    rebuildCards();
}

void DeckEditor::close()
{
    if (isVisible())
        hide(); // hideEvent commits
    else
        commit();
}

void DeckEditor::discard()
{
    m_committed = true;
    hide();
}

void DeckEditor::commit()
{
    if (m_committed)
        return;
    m_committed = true;
    if (m_onCommit)
        m_onCommit(m_draft.deck());
}

void DeckEditor::hideEvent(QHideEvent *event)
{
    QFrame::hideEvent(event);
    commit();
}

void DeckEditor::keyPressEvent(QKeyEvent *event)
{
    if ((event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter) && (event->modifiers() & Qt::ControlModifier)) {
        close();
        return;
    }
    QFrame::keyPressEvent(event);
}

} // namespace wp
