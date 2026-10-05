#include "app/NoteFileActions.h"

#include "app/AppServices.h"
#include "app/AppUtil.h"
#include "app/NoteDocuments.h"
#include "app/NoteFiles.h"
#include "app/NoteTitle.h"
#include "app/NotesFolder.h"
#include "app/NotesLibrary.h"

#include <QFileInfo>

namespace wp::NoteFileActions {

void move(const QStringList &items, const QString &folder)
{
    struct Reload {
        ~Reload() { AppServices::shared().library().reload(); }
    } reload;
    for (const QString &item : items) {
        const QString moved = NoteFiles::move(item, folder);
        if (pathKey(moved) != pathKey(item))
            AppServices::shared().itemDidMove(item, moved);
    }
}

QString rename(const QString &item, const QString &name)
{
    const bool wasOpen = !NoteDocuments::instance().documents(item).isEmpty();
    const QString renamed = NoteFiles::rename(item, name);
    if (pathKey(renamed) == pathKey(item))
        return renamed;
    AppServices::shared().itemDidMove(item, renamed);
    if (!wasOpen && QFileInfo(renamed).suffix().toLower() == NotesFolder::fileExtension) {
        try {
            Note note = NotesLibrary::read(renamed);
            note.frontMatter.setTitle(NoteTitle::baseName(renamed));
            writeFileAtomically(renamed, note.serialized());
            AppServices::shared().library().reload();
        } catch (const FileIOError &) {
            throw;
        } catch (...) {
            // Unreadable notes keep their old title.
        }
    }
    return renamed;
}

void trash(const QString &item)
{
    for (NoteDocument *document : NoteDocuments::instance().documents(item))
        NoteDocuments::instance().close(document);
    NoteFiles::trash(item);
    AppServices::shared().library().reload();
}

} // namespace wp::NoteFileActions
