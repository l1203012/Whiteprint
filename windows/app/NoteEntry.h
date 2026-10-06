#pragma once
#include "core/CardDeck.h"

#include <QList>
#include <QString>
#include <optional>

namespace wp {

/// A note as listed in the sidebar, the palette and `list_notes`.
struct NoteEntry {
    /// Canonical file path.
    QString url;
    QString title;
    int pageCount = 0;
    /// Relative to the notes folder (`Courses/Networks`), `""` at the top level, nullopt for an open
    /// note saved elsewhere.
    std::optional<QString> folder;
    QList<CardDeck> decks;

    bool operator==(const NoteEntry &) const = default;
};

} // namespace wp
