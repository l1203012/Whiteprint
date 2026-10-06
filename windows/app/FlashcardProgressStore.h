#pragma once
// Port of FlashcardProgressStore.swift.
#include "app/FlashcardScheduler.h"

#include <QHash>
#include <QObject>
#include <QString>

#include <optional>

namespace wp {

/// Review progress per note, deck and card, as one small JSON file per note in
/// `%LOCALAPPDATA%\Whiteprint\Flashcards` (BridgePaths::supportDirectory).
/// The file name is a hash of the note's canonical path; moved notes are re-keyed with `moveNotes`.
class FlashcardProgressStore : public QObject {
    Q_OBJECT
public:
    static QString defaultDirectory();

    explicit FlashcardProgressStore(const QString &directory = defaultDirectory(), QObject *parent = nullptr);

    const QString &directory() const { return m_directory; }

    /// Card key -> progress for one deck of a note (empty when nothing was rated yet).
    QHash<QString, CardProgress> progress(const QString &note, const QString &deck);

    /// Stores one card's progress. Throws FileIOError. Emits `progressDidChange` (and the global
    /// `AppEvents::flashcardProgressDidChange`).
    void save(const CardProgress &progress, const QString &card, const QString &note, const QString &deck);

    /// Keeps progress with a note (or every note in a folder) that moved.
    void moveNotes(const QString &from, const QString &to);

signals:
    void progressDidChange();

private:
    struct NoteProgress {
        QString note;
        /// Deck id -> card key -> progress.
        QHash<QString, QHash<QString, CardProgress>> decks;
    };

    static QByteArray encode(const NoteProgress &file);
    static std::optional<NoteProgress> decode(const QByteArray &data);
    QString fileName(const QString &note) const;
    std::optional<NoteProgress> load(const QString &note);
    void write(const NoteProgress &file);

    QString m_directory;
    QHash<QString, NoteProgress> m_cache;
};

} // namespace wp
