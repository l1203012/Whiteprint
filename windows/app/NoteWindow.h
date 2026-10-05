#pragma once
// Port of NoteWindowController.swift: one note's window with the sidebar, a slim breadcrumb toolbar
// (view mode, layout, "more" menu) and the editor. One window per open NoteDocument.
#include "app/NoteDocument.h"
#include "core/Note.h"
#include "editor/EditorCommand.h"

#include <QMainWindow>
#include <QPointer>
#include <QString>
#include <optional>

class QButtonGroup;
class QSplitter;
class QToolButton;

namespace wp {

class NoteDocument;
class NoteEditorView;
class SidebarWidget;
class Breadcrumb;

class NoteWindow : public QMainWindow {
    Q_OBJECT
public:
    explicit NoteWindow(NoteDocument *document, QWidget *parent = nullptr);
    ~NoteWindow() override;

    NoteDocument *noteDocument() const { return m_document; }
    NoteEditorView *editor() const { return m_editor; }
    SidebarWidget *sidebar() const { return m_sidebar; }
    /// The document's note, or the editor's when the document is gone.
    Note note() const;
    int visiblePage() const { return m_visiblePage; }
    /// The folder Ctrl+N creates notes in: the one selected in the sidebar.
    std::optional<QString> selectedFolder() const;
    bool isSidebarVisible() const;

    /// The title trail shown in the toolbar, e.g. `Notes / Courses / Networks / Page 2`.
    QString breadcrumbText() const;

    // MARK: Actions (menus, toolbar, palette)
    void addPage();
    void insertDrawing();
    void insertFlashcards();
    void performEditorCommand(EditorCommand command);
    void studyFlashcards();
    void newFolder();
    void toggleSidebar();
    void scrollToPage(int page);
    void focusEditor();

    void undo();
    void redo();
    QString undoText() const;
    QString redoText() const;
    /// Cut / copy / paste / select all go to the focused text widget.
    void forwardToFocus(const char *slot);

    // Document actions (File menu).
    void saveDocument();
    void duplicateDocument();
    void renameDocument();
    void moveDocument();
    void revertDocument();
    void exportMarkdown();
    void exportPDF(bool blueprint);
    void showInExplorer();
    bool hasFile() const;

    void applyViewPreferences();
    void updateBreadcrumb();

    /// Splitter and window geometry are remembered across launches.
    static constexpr const char *geometryKey = "NoteWindowGeometry";
    static constexpr const char *splitKey = "NoteWindowSplit";

protected:
    void closeEvent(QCloseEvent *event) override;
    void changeEvent(QEvent *event) override;
    void showEvent(QShowEvent *event) override;

private:
    void buildToolbar(QWidget *container);
    void documentChanged(int origin);
    void visiblePageChanged(int page);
    void study(const CardDeck &deck);
    void saveGeometryNow();
    void syncModeButtons();
    QMenu *moreMenu();

    QPointer<NoteDocument> m_document;
    NoteEditorView *m_editor = nullptr;
    SidebarWidget *m_sidebar = nullptr;
    QSplitter *m_split = nullptr;
    Breadcrumb *m_breadcrumb = nullptr;
    QToolButton *m_sidebarButton = nullptr;
    QToolButton *m_slidesButton = nullptr;
    QToolButton *m_a4Button = nullptr;
    QToolButton *m_syntaxButton = nullptr;
    QToolButton *m_moreButton = nullptr;
    int m_visiblePage = 1;
    int m_sidebarWidth = 240;
    bool m_restoredGeometry = false;
    bool m_closing = false;
    bool m_showingSaveError = false;
};

} // namespace wp
