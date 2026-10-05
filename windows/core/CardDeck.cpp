#include "core/CardDeck.h"

#include "core/JsonCoding.h"
#include "core/TextUtil.h"

namespace wp {

QJsonObject Flashcard::toJson() const
{
    QJsonObject o;
    o.insert(QStringLiteral("question"), question);
    o.insert(QStringLiteral("answer"), answer);
    if (ref)
        o.insert(QStringLiteral("ref"), *ref);
    return o;
}

Flashcard Flashcard::fromJson(const QJsonObject &object)
{
    return Flashcard(json::requiredString(object, QStringLiteral("question")),
                     json::requiredString(object, QStringLiteral("answer")),
                     json::optionalString(object, QStringLiteral("ref")));
}

namespace {

std::optional<QString> valueOf(const QString &prefix, const QString &line)
{
    if (!line.startsWith(prefix))
        return std::nullopt;
    return trimmedWhitespace(QStringView(line).mid(prefix.size()));
}

QString singleLine(const QString &text)
{
    QStringList parts;
    QString current;
    for (QChar c : text) {
        if (isNewlineChar(c)) {
            if (!current.isEmpty())
                parts.append(current);
            current.clear();
        } else {
            current.append(c);
        }
    }
    if (!current.isEmpty())
        parts.append(current);
    return parts.join(QLatin1Char(' '));
}

/// Keeps a multi-line field from splitting the card: blank lines are dropped,
/// and continuation lines that look like a field or a fence get a `\`.
QString escaped(const QString &text)
{
    QStringList lines;
    for (const QStringView &raw : QStringView(text).split(QLatin1Char('\n'), Qt::SkipEmptyParts)) {
        const QString trimmed = trimmedWhitespace(raw);
        if (!trimmed.isEmpty())
            lines.append(trimmed);
    }
    if (lines.isEmpty())
        return QString();
    static const QStringList fieldPrefixes = {QStringLiteral("Q:"), QStringLiteral("A:"), QStringLiteral("ref:"),
                                              QStringLiteral("# ")};
    QStringList out{lines.first()};
    for (qsizetype i = 1; i < lines.size(); ++i) {
        const QString &line = lines[i];
        bool structural = line.startsWith(QLatin1Char('\\')) || line.startsWith(QLatin1String("```"))
            || line.startsWith(QLatin1String("~~~"));
        for (const QString &prefix : fieldPrefixes)
            structural = structural || line.startsWith(prefix);
        out.append(structural ? QLatin1Char('\\') + line : line);
    }
    return out.join(QLatin1Char('\n'));
}

} // namespace

CardDeck CardDeck::parsing(const QString &id, const QString &source)
{
    enum class Field { none, question, answer };

    std::optional<QString> title;
    QList<Flashcard> cards;
    std::optional<Flashcard> current;
    Field field = Field::none;

    auto finishCard = [&]() {
        if (current && (!current->question.isEmpty() || !current->answer.isEmpty()))
            cards.append(*current);
        current.reset();
        field = Field::none;
    };

    for (const QStringView &rawLine : QStringView(source).split(QLatin1Char('\n'), Qt::KeepEmptyParts)) {
        const QString trimmed = trimmedWhitespace(rawLine);
        if (trimmed.isEmpty()) {
            finishCard();
        } else if (auto question = valueOf(QStringLiteral("Q:"), trimmed)) {
            finishCard();
            current = Flashcard(*question, QString());
            field = Field::question;
        } else if (auto answer = valueOf(QStringLiteral("A:"), trimmed)) {
            if (!current)
                current = Flashcard(QString(), QString());
            current->answer = *answer;
            field = Field::answer;
        } else if (auto ref = valueOf(QStringLiteral("ref:"), trimmed); ref && current) {
            if (ref->isEmpty())
                current->ref.reset();
            else
                current->ref = *ref;
            field = Field::none;
        } else if (!title && cards.isEmpty() && !current && trimmed.startsWith(QLatin1String("# "))) {
            title = trimmedWhitespace(QStringView(trimmed).mid(2));
        } else if (field != Field::none && current) {
            const QString text = trimmed.startsWith(QLatin1Char('\\')) ? trimmed.mid(1) : trimmed;
            if (field == Field::question)
                current->question += QLatin1Char('\n') + text;
            else
                current->answer += QLatin1Char('\n') + text;
        }
    }
    finishCard();
    return CardDeck(id, title, cards);
}

QString CardDeck::source() const
{
    QStringList parts;
    if (title && !title->isEmpty())
        parts.append(QStringLiteral("# ") + singleLine(*title));
    for (const Flashcard &card : cards) {
        QStringList lines{QStringLiteral("Q: ") + escaped(card.question), QStringLiteral("A: ") + escaped(card.answer)};
        if (card.ref && !card.ref->isEmpty())
            lines.append(QStringLiteral("ref: ") + singleLine(*card.ref));
        parts.append(lines.join(QLatin1Char('\n')));
    }
    return parts.join(QLatin1String("\n\n"));
}

} // namespace wp
