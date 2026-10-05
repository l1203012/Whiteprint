#include "app/FlashcardProgressStore.h"

#include "app/AppEvents.h"
#include "app/AppUtil.h"
#include "app/FlashcardSession.h"
#include "app/NoteFiles.h"
#include "bridge/Bridge.h"

#include <QDir>
#include <QTimeZone>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>

namespace wp {

namespace {

QString isoString(const QDateTime &date)
{
    // Seconds precision, like Swift's .iso8601 date strategy.
    return QDateTime::fromSecsSinceEpoch(date.toSecsSinceEpoch(), QTimeZone::UTC).toString(Qt::ISODate);
}

QJsonObject toJson(const CardProgress &p)
{
    return {{QStringLiteral("interval"), p.interval},
            {QStringLiteral("ease"), p.ease},
            {QStringLiteral("reps"), p.reps},
            {QStringLiteral("lapses"), p.lapses},
            {QStringLiteral("due"), isoString(p.due)}};
}

std::optional<CardProgress> progressFromJson(const QJsonObject &o)
{
    const QJsonValue interval = o.value(QStringLiteral("interval"));
    const QJsonValue ease = o.value(QStringLiteral("ease"));
    const QJsonValue reps = o.value(QStringLiteral("reps"));
    const QJsonValue lapses = o.value(QStringLiteral("lapses"));
    const QJsonValue due = o.value(QStringLiteral("due"));
    if (!interval.isDouble() || !ease.isDouble() || !reps.isDouble() || !lapses.isDouble() || !due.isString())
        return std::nullopt;
    const QDateTime date = QDateTime::fromString(due.toString(), Qt::ISODate);
    if (!date.isValid())
        return std::nullopt;
    return CardProgress{interval.toDouble(), ease.toDouble(), reps.toInt(), lapses.toInt(), date.toUTC()};
}

} // namespace

QString FlashcardProgressStore::defaultDirectory()
{
    return QDir::cleanPath(BridgePaths::supportDirectory() + QStringLiteral("/Flashcards"));
}

FlashcardProgressStore::FlashcardProgressStore(const QString &directory, QObject *parent)
    : QObject(parent), m_directory(directory)
{
}

QHash<QString, CardProgress> FlashcardProgressStore::progress(const QString &note, const QString &deck)
{
    const auto file = load(note);
    if (!file)
        return {};
    return file->decks.value(deck);
}

void FlashcardProgressStore::save(const CardProgress &progress, const QString &card, const QString &note, const QString &deck)
{
    NoteProgress file = load(note).value_or(NoteProgress{canonicalFile(note), {}});
    file.decks[deck].insert(card, progress);
    write(file);
    emit progressDidChange();
    emit AppEvents::instance().flashcardProgressDidChange();
}

void FlashcardProgressStore::moveNotes(const QString &from, const QString &to)
{
    const QFileInfoList files = QDir(m_directory).entryInfoList({QStringLiteral("*.json")}, QDir::Files);
    for (const QFileInfo &info : files) {
        QFile source(info.absoluteFilePath());
        if (!source.open(QIODevice::ReadOnly))
            continue;
        auto file = decode(source.readAll());
        source.close();
        if (!file)
            continue;
        const auto moved = NoteFiles::relocated(file->note, from, to);
        if (!moved)
            continue;
        m_cache.remove(info.fileName());
        file->note = *moved;
        try {
            write(*file);
        } catch (...) {
            continue;
        }
        if (info.fileName() != fileName(*moved))
            QFile::remove(info.absoluteFilePath());
    }
}

QByteArray FlashcardProgressStore::encode(const NoteProgress &file)
{
    QJsonObject decks;
    for (auto deck = file.decks.constBegin(); deck != file.decks.constEnd(); ++deck) {
        QJsonObject cards;
        for (auto card = deck->constBegin(); card != deck->constEnd(); ++card)
            cards.insert(card.key(), toJson(card.value()));
        decks.insert(deck.key(), cards);
    }
    return QJsonDocument(QJsonObject{{QStringLiteral("note"), file.note}, {QStringLiteral("decks"), decks}})
        .toJson(QJsonDocument::Compact);
}

std::optional<FlashcardProgressStore::NoteProgress> FlashcardProgressStore::decode(const QByteArray &data)
{
    const QJsonDocument document = QJsonDocument::fromJson(data);
    if (!document.isObject())
        return std::nullopt;
    const QJsonObject root = document.object();
    if (!root.value(QStringLiteral("note")).isString() || !root.value(QStringLiteral("decks")).isObject())
        return std::nullopt;
    NoteProgress file;
    file.note = root.value(QStringLiteral("note")).toString();
    const QJsonObject decks = root.value(QStringLiteral("decks")).toObject();
    for (auto deck = decks.constBegin(); deck != decks.constEnd(); ++deck) {
        if (!deck->isObject())
            return std::nullopt;
        const QJsonObject cards = deck->toObject();
        for (auto card = cards.constBegin(); card != cards.constEnd(); ++card) {
            const auto progress = card->isObject() ? progressFromJson(card->toObject()) : std::nullopt;
            if (!progress)
                return std::nullopt;
            file.decks[deck.key()].insert(card.key(), *progress);
        }
    }
    return file;
}

QString FlashcardProgressStore::fileName(const QString &note) const
{
    return StableHash::hex(canonicalFile(note)) + QStringLiteral(".json");
}

std::optional<FlashcardProgressStore::NoteProgress> FlashcardProgressStore::load(const QString &note)
{
    const QString name = fileName(note);
    if (const auto cached = m_cache.constFind(name); cached != m_cache.constEnd())
        return *cached;
    QFile file(QDir(m_directory).filePath(name));
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    const auto decoded = decode(file.readAll());
    if (!decoded)
        return std::nullopt;
    m_cache.insert(name, *decoded);
    return decoded;
}

void FlashcardProgressStore::write(const NoteProgress &file)
{
    if (!QDir().mkpath(m_directory))
        throw FileIOError(QStringLiteral("The folder “%1” can’t be created.").arg(QDir::toNativeSeparators(m_directory)));
    const QString name = fileName(file.note);
    writeFileAtomically(QDir(m_directory).filePath(name), encode(file));
    m_cache.insert(name, file);
}

} // namespace wp
