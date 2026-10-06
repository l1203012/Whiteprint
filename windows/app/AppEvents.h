#pragma once
// The app-wide notifications that are not tied to one object (Swift posted them through
// NotificationCenter). Object-scoped events are signals on the object itself.
#include <QObject>
#include <QString>

namespace wp {

class NoteDocument;

/// Process-wide signal hub; connect from the GUI thread.
class AppEvents : public QObject {
    Q_OBJECT
public:
    static AppEvents &instance();

signals:
    /// A document's note changed, from the editor, Claude or a reload (`noteDocumentDidChange`).
    /// `fromEditor` is true for edits the editor reported itself (don't send them back to it).
    void noteDocumentDidChange(wp::NoteDocument *document, bool fromEditor);
    /// A document's file was renamed or moved (`noteDocumentDidMove`).
    void noteDocumentDidMove(wp::NoteDocument *document, const QString &oldPath);
    /// Imports or saved study points changed (`studyStoreDidChange`); emitted on the GUI thread.
    void studyStoreDidChange();
    /// A card was rated, so due counts refresh (`flashcardProgressDidChange`).
    void flashcardProgressDidChange();

private:
    AppEvents() = default;
};

} // namespace wp
