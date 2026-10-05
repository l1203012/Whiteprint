#pragma once
// Port of DeckCatalog.swift.
#include "app/FlashcardSession.h"
#include "app/NoteEntry.h"
#include "core/CardDeck.h"

#include <QDateTime>
#include <QHash>
#include <QList>
#include <QString>
#include <functional>

namespace wp {

/// Every flashcard deck across the notes, with what's left to study.
namespace DeckCatalog {

struct Entry {
    /// The note's file path.
    QString note;
    QString noteTitle;
    CardDeck deck;
    FlashcardSession::Summary summary;

    /// The deck's title, or its note's.
    QString title() const;
};

/// `progress(note, deckID)` returns the card progress of a deck.
using ProgressFn = std::function<QHash<QString, CardProgress>(const QString &note, const QString &deck)>;

/// Decks with at least one card, in note order.
QList<Entry> entries(const QList<NoteEntry> &notes, const ProgressFn &progress, const QDateTime &now = QDateTime::currentDateTimeUtc());

/// Every deck in the notes library (AppServices::shared()).
QList<Entry> all();

} // namespace DeckCatalog

} // namespace wp
