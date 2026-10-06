#pragma once
// Port of AppServices.swift: the app-wide objects shared by every window and the bridge.
#include "app/BridgeService.h"
#include "app/FlashcardProgressStore.h"
#include "app/NoteDocuments.h"
#include "app/NoteRegistry.h"
#include "app/NotesLibrary.h"
#include "app/StudySession.h"

#include <QObject>
#include <memory>

namespace wp {

class AppServices : public QObject {
    Q_OBJECT
public:
    /// Created on first use (after QApplication exists, from the GUI thread).
    static AppServices &shared();

    NotesLibrary &library() { return *m_library; }
    NoteRegistry &registry() { return m_registry; }
    /// Null when the study folder can't be created; study features then say so.
    StudyStore *studyStore() { return m_studyStore.get(); }
    StudySession &study() { return *m_study; }
    /// The handler to give a BridgeServer (and the Grok runner).
    BridgeService &bridge() { return *m_bridge; }
    FlashcardProgressStore &flashcards() { return m_flashcards; }

    /// Keeps documents, Claude's note ids and flashcard progress with a note or folder that moved from
    /// `old` to `newPath` on disk, and reloads the library.
    void itemDidMove(const QString &old, const QString &newPath);

private:
    AppServices();

    std::unique_ptr<NotesLibrary> m_library;
    NoteRegistry m_registry;
    std::unique_ptr<StudyStore> m_studyStore;
    std::unique_ptr<DocumentWorkspace> m_workspace;
    std::unique_ptr<BridgeService> m_bridge;
    std::unique_ptr<StudySession> m_study;
    FlashcardProgressStore m_flashcards;
};

} // namespace wp
