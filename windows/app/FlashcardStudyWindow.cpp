#include "app/FlashcardStudyWindow.h"

#include "app/AppEvents.h"
#include "app/AppServices.h"
#include "app/MacStyle.h"
#include "app/NoteDocuments.h"
#include "app/NoteTitle.h"
#include "app/NoteWorkspace.h"
#include "app/UiKit.h"
#include "app/ViewPreferences.h"
#include "render/Fonts.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QHBoxLayout>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QMouseEvent>
#include <QPainter>
#include <QPainterPath>
#include <QPointer>
#include <QPushButton>
#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextOption>
#include <QVBoxLayout>

namespace wp {

namespace {
QPointer<FlashcardStudyWindow> sharedWindow;

struct Part {
    QString text;
    double size;
    QFont::Weight weight;
    QColor color;
};
} // namespace

// MARK: FlashcardView

FlashcardView::FlashcardView(const BlueprintPalette &palette, QWidget *parent) : QWidget(parent), m_palette(palette)
{
    setMinimumHeight(260);
    setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Expanding);
}

void FlashcardView::setContent(const Content &content)
{
    m_content = content;
    setCursor(content.kind == Content::Kind::card ? Qt::PointingHandCursor : Qt::ArrowCursor);
    update();
}

void FlashcardView::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_content.kind == Content::Kind::card)
        emit clicked();
}

void FlashcardView::paintEvent(QPaintEvent *)
{
    QPainter p(this);
    p.setRenderHint(QPainter::Antialiasing);
    const QRectF sheet = QRectF(rect()).adjusted(2, 2, -2, -2);
    QPainterPath shape;
    shape.addRoundedRect(sheet, 10, 10);
    p.fillPath(shape, m_palette.pageBackground);
    if (m_palette.showsGrid) {
        p.save();
        p.setClipPath(shape);
        p.setPen(QPen(m_palette.grid, 1));
        p.setRenderHint(QPainter::Antialiasing, false);
        for (double x = sheet.left() + 20; x < sheet.right(); x += 20)
            p.drawLine(QPointF(x, sheet.top()), QPointF(x, sheet.bottom()));
        for (double y = sheet.top() + 20; y < sheet.bottom(); y += 20)
            p.drawLine(QPointF(sheet.left(), y), QPointF(sheet.right(), y));
        p.restore();
    }
    p.setRenderHint(QPainter::Antialiasing);

    const QRectF text = sheet.adjusted(40, 32, -40, -32);
    QList<Part> parts;
    const auto &pal = m_palette;
    switch (m_content.kind) {
    case Content::Kind::card:
        if (!m_content.flipped) {
            parts = {{m_content.card.question, 26, QFont::DemiBold, pal.text}};
        } else {
            parts = {{m_content.card.question, 15, QFont::Medium, pal.muted},
                     {QString(), 10, QFont::Normal, pal.muted},
                     {m_content.card.answer, 22, QFont::Normal, pal.text}};
            if (m_content.card.ref && !m_content.card.ref->isEmpty())
                parts += {{QString(), 10, QFont::Normal, pal.muted},
                          {QStringLiteral("↳ ") + *m_content.card.ref, 12, QFont::Normal, pal.accent}};
        }
        break;
    case Content::Kind::message:
        parts = {{m_content.title, 22, QFont::DemiBold, pal.text},
                 {QString(), 8, QFont::Normal, pal.muted},
                 {m_content.detail, 14, QFont::Normal, pal.muted}};
        break;
    case Content::Kind::finished: {
        const auto &stats = m_content.stats;
        QStringList counts;
        for (const CardRating rating : allCardRatings) {
            const auto it = stats.ratings.find(rating);
            counts << QStringLiteral("%1 %2").arg(cardRatingTitle(rating)).arg(it == stats.ratings.end() ? 0 : it->second);
        }
        parts = {{QStringLiteral("Session complete"), 26, QFont::DemiBold, pal.text},
                 {QString(), 8, QFont::Normal, pal.muted},
                 {QStringLiteral("%1 card%2 studied, %3 answer%4")
                      .arg(stats.cards).arg(stats.cards == 1 ? "" : "s").arg(stats.answers).arg(stats.answers == 1 ? "" : "s"),
                  15, QFont::Normal, pal.text},
                 {counts.join(QStringLiteral("   ·   ")), 13, QFont::Normal, pal.muted}};
        break;
    }
    }

    // Lines of text, each centred, the block centred vertically.
    QTextDocument doc;
    doc.setDocumentMargin(0);
    doc.setTextWidth(text.width());
    QTextOption option;
    option.setAlignment(Qt::AlignHCenter);
    option.setWrapMode(QTextOption::WrapAtWordBoundaryOrAnywhere);
    doc.setDefaultTextOption(option);
    QTextCursor cursor(&doc);
    for (int i = 0; i < parts.size(); ++i) {
        QTextBlockFormat block;
        block.setAlignment(Qt::AlignHCenter);
        block.setLineHeight(3, QTextBlockFormat::LineDistanceHeight);
        QTextCharFormat format;
        format.setFont(fonts::system(parts[i].size, parts[i].weight));
        format.setForeground(parts[i].color);
        if (i == 0)
            cursor.setBlockFormat(block);
        else
            cursor.insertBlock(block);
        cursor.setCharFormat(format);
        cursor.insertText(parts[i].text.isEmpty() ? QStringLiteral(" ") : parts[i].text, format);
    }
    const double height = qMin(doc.size().height(), text.height());
    p.save();
    p.setClipRect(text);
    p.translate(text.left(), text.center().y() - height / 2);
    doc.drawContents(&p, QRectF(0, 0, text.width(), height));
    p.restore();
}

// MARK: FlashcardStudyWindow

void FlashcardStudyWindow::open(const QString &notePath, const QString &deckID)
{
    Note loaded;
    try {
        if (NoteDocument *document = NoteDocuments::instance().document(notePath))
            loaded = document->note();
        else
            loaded = NotesLibrary::read(notePath);
    } catch (...) {
        QMessageBox::warning(nullptr, QStringLiteral("Whiteprint"), currentErrorLine());
        return;
    }
    const auto location = loaded.deck(deckID);
    if (!location || location->deck.cards.isEmpty()) {
        QMessageBox box(QMessageBox::Information, QStringLiteral("Whiteprint"),
                        QStringLiteral("This deck has no cards yet."));
        box.setInformativeText(QStringLiteral("Add questions and answers to the deck, then study it."));
        box.exec();
        return;
    }
    const QString title = NoteTitle::display(loaded, notePath);
    if (sharedWindow) {
        sharedWindow->load(notePath, title, location->deck);
    } else {
        sharedWindow = new FlashcardStudyWindow(notePath, title, location->deck, &AppServices::shared().flashcards());
        sharedWindow->setAttribute(Qt::WA_DeleteOnClose);
    }
    sharedWindow->show();
    sharedWindow->raise();
    sharedWindow->activateWindow();
}

FlashcardStudyWindow::FlashcardStudyWindow(const QString &notePath, const QString &noteTitle, const CardDeck &deck,
                                           FlashcardProgressStore *store, QWidget *parent)
    : QWidget(parent, Qt::Window), m_store(store), m_notePath(notePath), m_noteTitle(noteTitle), m_deck(deck),
      m_session(deck, store->progress(notePath, deck.id), QDateTime::currentDateTimeUtc())
{
    setWindowTitle(QStringLiteral("Study Flashcards"));
    setMinimumSize(480, 440);
    resize(680, 560);
    setAutoFillBackground(true);
    setFocusPolicy(Qt::StrongFocus);

    m_title = ui::label({}, 17, QFont::DemiBold);
    m_title->setMinimumWidth(40);
    m_summary = ui::label({}, 12, QFont::Normal, true);
    m_shuffleBox = new QCheckBox(QStringLiteral("Shuffle"));
    m_shuffleBox->setFocusPolicy(Qt::NoFocus);
    connect(m_shuffleBox, &QCheckBox::toggled, this, [this](bool on) { setShuffled(on); });

    auto *header = new QHBoxLayout;
    header->setSpacing(10);
    header->addWidget(m_title);
    header->addWidget(m_summary);
    header->addStretch(1);
    header->addWidget(m_shuffleBox);

    m_card = new FlashcardView(ViewPreferences::shared().pagePalette());
    connect(m_card, &FlashcardView::clicked, this, [this] { flip(); });

    m_hint = ui::label({}, 11, QFont::Normal, true);
    m_hint->setAlignment(Qt::AlignCenter);

    auto button = [this](const QString &title) {
        auto *b = new QPushButton(title);
        b->setFocusPolicy(Qt::NoFocus);
        b->setMinimumHeight(32);
        return b;
    };
    m_flipButton = button(QStringLiteral("Show Answer"));
    m_flipButton->setMinimumWidth(140);
    connect(m_flipButton, &QPushButton::clicked, this, [this] { flip(); });
    auto *flipRow = new QHBoxLayout;
    flipRow->addStretch(1);
    flipRow->addWidget(m_flipButton);
    flipRow->addStretch(1);
    auto *flipWrap = new QWidget;
    flipWrap->setLayout(flipRow);
    flipRow->setContentsMargins(0, 0, 0, 0);
    m_flipButton->setProperty("wrap", QVariant::fromValue(flipWrap));

    m_ratingRow = new QWidget;
    auto *ratings = new QHBoxLayout(m_ratingRow);
    ratings->setContentsMargins(0, 0, 0, 0);
    ratings->setSpacing(10);
    for (const CardRating rating : allCardRatings) {
        auto *b = button(cardRatingTitle(rating));
        b->setProperty("rating", int(rating));
        connect(b, &QPushButton::clicked, this, [this, rating] { this->rate(rating); });
        ratings->addWidget(b, 1);
        m_ratingButtons << b;
    }

    m_finishRow = new QWidget;
    auto *finish = new QHBoxLayout(m_finishRow);
    finish->setContentsMargins(0, 0, 0, 0);
    finish->setSpacing(10);
    m_studyAllButton = button(QStringLiteral("Study All Cards"));
    m_closeButton = button(QStringLiteral("Close"));
    connect(m_studyAllButton, &QPushButton::clicked, this, [this] { studyAll(); });
    connect(m_closeButton, &QPushButton::clicked, this, [this] { close(); });
    finish->addStretch(1);
    finish->addWidget(m_studyAllButton);
    finish->addWidget(m_closeButton);
    finish->addStretch(1);

    auto *controls = new QVBoxLayout;
    controls->setSpacing(0);
    controls->addWidget(flipWrap);
    controls->addWidget(m_ratingRow);
    controls->addWidget(m_finishRow);

    auto *layout = new QVBoxLayout(this);
    layout->setContentsMargins(24, 18, 24, 16);
    layout->setSpacing(14);
    layout->addLayout(header);
    layout->addWidget(m_card, 1);
    layout->addLayout(controls);
    layout->addWidget(m_hint);

    refresh();
    mac::styleTitleBar(this);
}

void FlashcardStudyWindow::load(const QString &notePath, const QString &noteTitle, const CardDeck &deck)
{
    m_notePath = notePath;
    m_noteTitle = noteTitle;
    m_deck = deck;
    restart(false);
}

void FlashcardStudyWindow::restart(bool everything)
{
    m_session = FlashcardSession(m_deck, m_store->progress(m_notePath, m_deck.id), QDateTime::currentDateTimeUtc(),
                                 m_shuffled, everything);
    refresh();
}

QString FlashcardStudyWindow::summaryText() const
{
    return m_summary->text();
}

void FlashcardStudyWindow::refresh()
{
    const QDateTime now = QDateTime::currentDateTimeUtc();
    m_title->setText(m_deck.title && !m_deck.title->isEmpty() ? *m_deck.title : m_noteTitle);
    const auto total = FlashcardSession::summary(m_deck, m_session.progress(), now);
    const int left = int(m_session.queue().size());
    m_summary->setText(m_session.isFinished() ? total.description()
                                              : QStringLiteral("%1 · %2 left").arg(total.description()).arg(left));
    {
        QSignalBlocker block(m_shuffleBox);
        m_shuffleBox->setChecked(m_shuffled);
    }

    FlashcardView::Content content;
    if (const auto *item = m_session.current()) {
        content.kind = FlashcardView::Content::Kind::card;
        content.card = item->card;
        content.flipped = m_session.isFlipped();
        m_hint->setText(m_session.isFlipped() ? QStringLiteral("Rate with 1 – 4 · Space to flip back")
                                              : QStringLiteral("Space or click to flip"));
        for (QPushButton *b : m_ratingButtons) {
            const auto rating = CardRating(b->property("rating").toInt());
            std::optional<CardProgress> after;
            const auto it = m_session.progress().constFind(item->key);
            if (it != m_session.progress().constEnd())
                after = it.value();
            const QString label = FlashcardScheduler::intervalLabel(after, rating, now);
            b->setText(QStringLiteral("%1  ·  %2").arg(cardRatingTitle(rating), label));
            b->setToolTip(QStringLiteral("%1: back in %2").arg(int(rating)).arg(label));
        }
    } else if (m_session.stats().answers == 0) {
        content.kind = FlashcardView::Content::Kind::message;
        content.title = QStringLiteral("Nothing due right now");
        content.detail = QStringLiteral(
            "Every card in this deck is scheduled for later. Study them all anyway, or come back later.");
        m_hint->clear();
    } else {
        content.kind = FlashcardView::Content::Kind::finished;
        content.stats = m_session.stats();
        m_hint->clear();
    }
    m_card->setContent(content);

    const bool has = m_session.current() != nullptr;
    m_flipButton->property("wrap").value<QWidget *>()->setVisible(has && !m_session.isFlipped());
    m_ratingRow->setVisible(has && m_session.isFlipped());
    m_finishRow->setVisible(!has);
    m_studyAllButton->setText(m_session.stats().answers == 0 ? QStringLiteral("Study All Cards")
                                                             : QStringLiteral("Study Again"));
}

void FlashcardStudyWindow::flip()
{
    if (!m_session.current())
        return;
    m_session.flip();
    refresh();
}

void FlashcardStudyWindow::rate(CardRating rating)
{
    if (!m_session.isFlipped())
        return;
    const auto rated = m_session.rate(rating, QDateTime::currentDateTimeUtc());
    if (!rated)
        return;
    try {
        m_store->save(rated->progress, rated->key, m_notePath, m_deck.id);
    } catch (const std::exception &e) {
        qWarning("Whiteprint: couldn't save flashcard progress: %s", e.what());
    }
    emit AppEvents::instance().flashcardProgressDidChange();
    refresh();
}

void FlashcardStudyWindow::setShuffled(bool shuffled)
{
    m_shuffled = shuffled;
    if (m_session.stats().answers == 0)
        restart(false);
}

void FlashcardStudyWindow::studyAll()
{
    restart(true);
}

bool FlashcardStudyWindow::handleKey(int key, Qt::KeyboardModifiers modifiers)
{
    if (modifiers & (Qt::ControlModifier | Qt::AltModifier | Qt::MetaModifier))
        return false;
    switch (key) {
    case Qt::Key_Space:
    case Qt::Key_Return:
    case Qt::Key_Enter:
        if (!m_session.current())
            return false;
        flip();
        return true;
    case Qt::Key_1:
    case Qt::Key_2:
    case Qt::Key_3:
    case Qt::Key_4:
        rate(CardRating(key - Qt::Key_0));
        return true;
    case Qt::Key_Escape:
        close();
        return true;
    default:
        return false;
    }
}

void FlashcardStudyWindow::keyPressEvent(QKeyEvent *event)
{
    if (!handleKey(event->key(), event->modifiers()))
        QWidget::keyPressEvent(event);
}

void FlashcardStudyWindow::closeEvent(QCloseEvent *event)
{
    QWidget::closeEvent(event);
}

} // namespace wp
