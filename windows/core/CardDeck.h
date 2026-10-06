#pragma once
#include <QJsonObject>
#include <QList>
#include <QString>
#include <optional>

namespace wp {

/// A question/answer card for studying.
struct Flashcard {
    QString question;
    QString answer;
    /// Where the answer comes from, e.g. `Lecture3.pptx · slide 14`.
    std::optional<QString> ref;

    Flashcard() = default;
    Flashcard(QString question, QString answer, std::optional<QString> ref = std::nullopt)
        : question(std::move(question)), answer(std::move(answer)), ref(std::move(ref))
    {
    }
    bool operator==(const Flashcard &) const = default;

    /// `ref` is omitted when empty, like Swift's synthesized Codable.
    QJsonObject toJson() const;
    /// Throws DecodingError.
    static Flashcard fromJson(const QJsonObject &object);
};

/// A deck of flashcards, stored in a note as a ```` ```cards id=c1 ```` block:
///
///     # TCP basics
///     Q: What does TCP guarantee?
///     A: Ordered, reliable delivery.
///     ref: Lecture3.pptx · slide 4
///
///     Q: ...
///
/// Cards are separated by blank lines. Lines that don't start a field continue
/// the previous one, so questions and answers can span lines.
struct CardDeck {
    /// Stable id, unique within the note (e.g. `c2`).
    QString id;
    std::optional<QString> title;
    QList<Flashcard> cards;

    CardDeck() = default;
    CardDeck(QString id, std::optional<QString> title, QList<Flashcard> cards)
        : id(std::move(id)), title(std::move(title)), cards(std::move(cards))
    {
    }
    explicit CardDeck(QList<Flashcard> cards) : cards(std::move(cards)) {}
    bool operator==(const CardDeck &) const = default;

    /// Parses the block's body. Lenient: unknown lines continue the previous field.
    static CardDeck parsing(const QString &id, const QString &source);

    /// The block's body in the format described above.
    QString source() const;
};

} // namespace wp
