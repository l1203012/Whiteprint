#include "app/AppServices.h"

#include "app/AppEvents.h"

namespace wp {

AppServices &AppServices::shared()
{
    static AppServices *services = new AppServices;
    return *services;
}

AppServices::AppServices()
{
    m_library = std::make_unique<NotesLibrary>();
    try {
        m_studyStore = std::make_unique<StudyStore>();
    } catch (...) {
    }
    m_workspace = std::make_unique<DocumentWorkspace>(m_library.get());
    m_bridge = std::make_unique<BridgeService>(m_workspace.get(), &m_registry, m_studyStore.get());
    m_study = std::make_unique<StudySession>(m_studyStore.get(), m_bridge.get());
    connect(&AppEvents::instance(), &AppEvents::noteDocumentDidMove, this, [this](NoteDocument *document, const QString &old) {
        if (!document)
            return;
        m_registry.move(old, document->filePath());
        m_flashcards.moveNotes(old, document->filePath());
    });
}

void AppServices::itemDidMove(const QString &old, const QString &newPath)
{
    NoteDocuments::instance().relocate(old, newPath);
    m_registry.move(old, newPath);
    m_flashcards.moveNotes(old, newPath);
    m_library->reload();
}

} // namespace wp
