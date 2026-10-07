// Tests for the app shell: the sidebar model and widget, the note window, the menus and the app
// controller (single instance, opening files). There is no Swift counterpart: AppKit windows were not
// unit tested. Set WP_SCREENSHOT_DIR to also render the windows to PNG files.
#include "app/AppController.h"
#include "app/AppDefaults.h"
#include "app/AppServices.h"
#include "app/AppUtil.h"
#include "app/MacStyle.h"
#include "app/MainMenu.h"
#include "app/NoteDocuments.h"
#include "app/NoteTree.h"
#include "app/NoteWindow.h"
#include "app/SidebarModel.h"
#include "app/SidebarWidget.h"
#include "editor/NoteEditorView.h"
#include "render/Fonts.h"

#include <QApplication>
#include <QDir>
#include <QMenuBar>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>
#include <thread>

using namespace wp;

namespace {

QString write(const QString &path, const QString &text)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile f(path);
    f.open(QIODevice::WriteOnly);
    f.write(text.toUtf8());
    return canonicalFile(path);
}

QString noteText(const QString &title, int pages = 1)
{
    QString text = QStringLiteral("---\nwhiteprint: 1\ntitle: %1\n---\n# %1\n\nSome text.\n").arg(title);
    for (int i = 2; i <= pages; ++i)
        text += QStringLiteral("\n+++page\n# Page heading %1\n\nMore text.\n").arg(i);
    return text;
}

QStringList titles(const QList<SidebarNode> &nodes)
{
    QStringList result;
    for (const auto &n : nodes)
        result << n.title;
    return result;
}

} // namespace

class ShellTests : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;
    QString m_notes;

    static void closeAll()
    {
        for (NoteWindow *w : AppController::instance().windows())
            w->close();
        QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
        QApplication::processEvents();
    }

    void resetNotes()
    {
        QDir(m_notes).removeRecursively();
        QDir().mkpath(m_notes);
    }

private slots:
    void initTestCase()
    {
        m_notes = canonicalFile(m_dir.filePath("notes"));
        QDir().mkpath(m_notes);
        qputenv("WHITEPRINT_NOTES_DIR", m_notes.toUtf8());
        qputenv("WHITEPRINT_DEFAULTS_SUITE", "shell-tests");
        qputenv("WHITEPRINT_SOCKET", QStringLiteral("wp-shell-tests-%1").arg(QCoreApplication::applicationPid()).toUtf8());
        qApp->setFont(fonts::system(13));
        mac::setDarkOverride(0);
        AppDefaults::store().clear();
    }

    // MARK: SidebarModel

    void nodesPutFoldersFirst()
    {
        NoteEntry a;
        a.url = "/n/A.wprint";
        a.title = "A";
        a.folder = QString();
        NoteEntry b;
        b.url = "/n/Courses/B.wprint";
        b.title = "B";
        b.folder = QStringLiteral("Courses");
        const auto tree = NoteTree::build({a, b}, {"Courses", "Empty"});
        const auto nodes = SidebarModel::nodes(tree, "/n");
        QCOMPARE(titles(nodes), (QStringList{"Courses", "Empty", "A"}));
        QVERIFY(nodes[0].isFolder());
        QCOMPARE(nodes[0].path, QStringLiteral("Courses"));
        QCOMPARE(nodes[0].url, QStringLiteral("/n/Courses"));
        QCOMPARE(titles(nodes[0].children), (QStringList{"B"}));
        QVERIFY(nodes[0].isExpandable());
        QVERIFY(!nodes[1].isExpandable());
        QVERIFY(nodes[2].isNote());
        QCOMPARE(*nodes[2].noteURL(), QStringLiteral("/n/A.wprint"));
        QVERIFY(SidebarModel::find(nodes, [](const SidebarNode &n) { return n.title == "B"; }));
        QVERIFY(!SidebarModel::find(nodes, [](const SidebarNode &n) { return n.title == "Z"; }));
    }

    void pageAndStudyNodes()
    {
        const Note note = Note::parsing(noteText("T", 3));
        const auto pages = SidebarModel::pageNodes(note);
        QCOMPARE(pages.size(), 3);
        QCOMPARE(pages[1].title, QStringLiteral("Page heading 2"));
        QCOMPARE(pages[1].detail(), QStringLiteral("2"));

        auto none = SidebarModel::studyNodes({}, {}, {});
        QCOMPARE(none.size(), 1);
        QCOMPARE(int(none[0].kind), int(SidebarNode::Kind::addMaterial));

        StudyImport item("i1", "Lecture.pptx", 14, 4, 1, QDateTime::currentDateTime());
        auto some = SidebarModel::studyNodes({item}, {"Other.pdf"}, {});
        QCOMPARE(some.size(), 3);
        QCOMPARE(some[0].detail(), QStringLiteral("1/4"));
        QCOMPARE(some[1].detail(), QStringLiteral("…"));
        QCOMPARE(int(some[2].kind), int(SidebarNode::Kind::generate));

        DeckCatalog::Entry deck;
        deck.summary.due = 2;
        deck.summary.newCount = 1;
        auto withDeck = SidebarModel::studyNodes({}, {}, {deck});
        QCOMPARE(withDeck.last().detail(), QStringLiteral("3"));
        deck.summary = {};
        QCOMPARE(SidebarModel::studyNodes({}, {}, {deck}).last().detail(), QStringLiteral("✓"));
    }

    void selectedFolderRules()
    {
        SidebarNode folder;
        folder.kind = SidebarNode::Kind::folder;
        folder.url = "/n/Courses";
        QCOMPARE(*SidebarModel::selectedFolder(&folder), QStringLiteral("/n/Courses"));
        SidebarNode inside;
        inside.kind = SidebarNode::Kind::note;
        inside.url = "/n/Courses/B.wprint";
        inside.entry.url = inside.url;
        inside.entry.folder = QStringLiteral("Courses");
        QCOMPARE(*SidebarModel::selectedFolder(&inside), QStringLiteral("/n/Courses"));
        SidebarNode top = inside;
        top.entry.folder = QString();
        QVERIFY(SidebarModel::selectedFolder(&top).has_value() == true);
        top.entry.folder.reset(); // saved elsewhere
        QVERIFY(!SidebarModel::selectedFolder(&top));
        QVERIFY(!SidebarModel::selectedFolder(nullptr));
        SidebarNode page;
        page.kind = SidebarNode::Kind::page;
        QVERIFY(!SidebarModel::selectedFolder(&page));
    }

    void dropRules()
    {
        const QString root = m_notes;
        SidebarNode folder;
        folder.kind = SidebarNode::Kind::folder;
        folder.url = root + "/Courses";
        QCOMPARE(*SidebarModel::dropFolder(&folder, root), canonicalFile(root + "/Courses"));
        SidebarNode section;
        section.title = "Notes";
        QCOMPARE(*SidebarModel::dropFolder(&section, root), root);
        section.title = "Pages";
        QVERIFY(!SidebarModel::dropFolder(&section, root));
        QVERIFY(!SidebarModel::dropFolder(nullptr, root));

        const QString note = root + "/A.wprint";
        QVERIFY(SidebarModel::canMove({note}, root + "/Courses"));
        QVERIFY(!SidebarModel::canMove({note}, root)); // already there
        QVERIFY(!SidebarModel::canMove({}, root));
        QVERIFY(!SidebarModel::canMove({root + "/Courses"}, root + "/Courses/Sub")); // into itself
        QVERIFY(!SidebarModel::canMove({root + "/Courses"}, root + "/Courses"));
        QCOMPARE(SidebarModel::draggable({note, "C:/elsewhere/x.wprint", root}, root).size(), 1);
    }

    void expansionBookkeeping()
    {
        QCOMPARE(SidebarModel::ancestorPaths("a/b/c"), (QStringList{"a", "a/b", "a/b/c"}));
        QVERIFY(SidebarModel::ancestorPaths("").isEmpty());
        const QSet<QString> set{"a", "a/b", "ab", "c"};
        QCOMPARE(SidebarModel::afterCollapse(set, "a"), (QSet<QString>{"ab", "c"}));
        QCOMPARE(SidebarModel::afterRename(set, "a/b", "a/z"), (QSet<QString>{"a", "a/z", "ab", "c"}));
        QCOMPARE(SidebarModel::afterRename(set, "nope", "x"), set);
    }

    void contextMenus()
    {
        auto actions = [](const SidebarNode *n) {
            QStringList ids;
            for (const auto &e : SidebarModel::contextMenu(n))
                if (!e.separator)
                    ids << e.action;
            return ids;
        };
        SidebarNode folder;
        folder.kind = SidebarNode::Kind::folder;
        QCOMPARE(actions(&folder), (QStringList{"newNote", "newFolder", "rename", "show", "trash"}));
        SidebarNode note;
        note.kind = SidebarNode::Kind::note;
        note.entry.folder = QString();
        QCOMPARE(actions(&note), (QStringList{"newNote", "rename", "show", "trash"}));
        note.entry.folder.reset();
        QCOMPARE(actions(&note), (QStringList{"newNote", "show"}));
        SidebarNode deck;
        deck.kind = SidebarNode::Kind::deck;
        QCOMPARE(actions(&deck), (QStringList{"study", "open"}));
        QCOMPARE(actions(nullptr), (QStringList{"newNote", "newFolder"}));
    }

    void trashPromptText()
    {
        NoteEntry e;
        e.url = m_notes + "/F/a.wprint";
        const auto empty = SidebarModel::trashPrompt(m_notes + "/G", true, {e});
        QVERIFY(empty.message.contains("“G”"));
        QCOMPARE(empty.detail, QStringLiteral("The folder is empty."));
        const auto one = SidebarModel::trashPrompt(m_notes + "/F", true, {e});
        QVERIFY(one.detail.contains("1 note in it"));
        const auto note = SidebarModel::trashPrompt(m_notes + "/F/a.wprint", false, {});
        QVERIFY(note.message.contains("“a”"));
    }

    // MARK: SidebarWidget

    void sidebarShowsTheTreeAndMovesFiles()
    {
        resetNotes();
        write(m_notes + "/Top.wprint", noteText("Top"));
        const QString inner = write(m_notes + "/Courses/Networks/Inner.wprint", noteText("Inner"));
        QDir().mkpath(m_notes + "/Empty");
        auto &library = AppServices::shared().library();
        library.reload();

        SidebarWidget sidebar({nullptr, [] { return Note(); }, [] { return 1; }, nullptr});
        sidebar.resize(260, 500);
        sidebar.show();
        QVERIFY(sidebar.item([](const SidebarNode &n) { return n.title == "Top"; }));
        QVERIFY(sidebar.item([](const SidebarNode &n) { return n.title == "Empty"; }));
        QTreeWidgetItem *courses = sidebar.item([](const SidebarNode &n) { return n.title == "Courses"; });
        QVERIFY(courses);
        QVERIFY(!courses->isExpanded());

        // Expansion is remembered.
        sidebar.toggleExpansion(courses);
        QVERIFY(sidebar.expandedFolders().contains("Courses"));
        sidebar.toggleExpansion(courses);
        QVERIFY(!sidebar.expandedFolders().contains("Courses"));
        // An empty folder has nothing to expand.
        sidebar.toggleExpansion(sidebar.item([](const SidebarNode &n) { return n.title == "Empty"; }));
        QVERIFY(!sidebar.expandedFolders().contains("Empty"));
        sidebar.setExpandedFolders({"Courses", "Courses/Networks"});
        sidebar.reloadAll();
        courses = sidebar.item([](const SidebarNode &n) { return n.title == "Courses"; });
        QVERIFY(courses->isExpanded());
        QVERIFY(sidebar.item([](const SidebarNode &n) { return n.title == "Inner"; }));

        // Selecting a folder makes it the target for new notes.
        QTreeWidgetItem *networks = sidebar.item([](const SidebarNode &n) { return n.title == "Networks"; });
        sidebar.rowClicked(networks);
        QCOMPARE(*sidebar.selectedFolder(), canonicalFile(m_notes + "/Courses/Networks"));

        // Drop rules and a real move.
        QTreeWidgetItem *empty = sidebar.item([](const SidebarNode &n) { return n.title == "Empty"; });
        QVERIFY(sidebar.canDrop(empty, {inner}));
        QVERIFY(!sidebar.canDrop(networks, {inner}));
        QVERIFY(sidebar.acceptDrop(empty, {inner}));
        QVERIFY(QFileInfo::exists(m_notes + "/Empty/Inner.wprint"));
        QVERIFY(!QFileInfo::exists(inner));

        // Rename.
        library.reload();
        QTreeWidgetItem *top = sidebar.item([](const SidebarNode &n) { return n.title == "Top"; });
        sidebar.beginRename(top);
        QVERIFY(sidebar.isRenaming());
        sidebar.endRename("Renamed", false);
        QVERIFY(!sidebar.isRenaming());
        QVERIFY(QFileInfo::exists(m_notes + "/Renamed.wprint"));
        QVERIFY(!QFileInfo::exists(m_notes + "/Top.wprint"));
        // A cancelled rename changes nothing.
        QTreeWidgetItem *renamed = sidebar.item([](const SidebarNode &n) { return n.noteURL().has_value() && n.title == "Renamed"; });
        QVERIFY(renamed);
        sidebar.beginRename(renamed);
        sidebar.endRename("Nope", true);
        QVERIFY(QFileInfo::exists(m_notes + "/Renamed.wprint"));
    }

    // MARK: NoteWindow and menus

    void windowBreadcrumbMenusAndSidebarToggle()
    {
        resetNotes();
        const QString path = write(m_notes + "/Courses/Net.wprint", noteText("Networks", 3));
        AppServices::shared().library().reload();
        NoteDocument *doc = NoteDocument::open(path);
        NoteWindow window(doc);
        window.setAttribute(Qt::WA_DeleteOnClose, false);
        window.show();
        QCOMPARE(window.breadcrumbText(), QStringLiteral("Notes  /  Courses  /  Networks  /  Page 1"));
        QCOMPARE(window.windowTitle(), QStringLiteral("Networks[*]"));
        window.scrollToPage(2);
        QCOMPARE(window.visiblePage(), 2);
        QVERIFY(window.breadcrumbText().endsWith("Page 2"));

        QStringList menus;
        for (QAction *a : window.menuBar()->actions())
            menus << a->text().remove('&');
        QCOMPARE(menus, MainMenu::menuTitles());
        auto shortcutOf = [&](const QString &title) {
            QString found = QStringLiteral("<missing>");
            for (QAction *a : window.findChildren<QAction *>())
                if (a->text() == title && !a->shortcut().isEmpty())
                    found = a->shortcut().toString();
            return found;
        };
        QCOMPARE(shortcutOf("New Note"), QStringLiteral("Ctrl+N"));
        QCOMPARE(shortcutOf("New Folder"), QStringLiteral("Ctrl+Shift+N"));
        QCOMPARE(shortcutOf("Toggle Sidebar"), QStringLiteral("Ctrl+\\"));
        QCOMPARE(shortcutOf("Command Palette"), QStringLiteral("Ctrl+K"));
        QCOMPARE(shortcutOf("Bold"), QStringLiteral("Ctrl+B"));
        QCOMPARE(shortcutOf("Heading 1"), QStringLiteral("Ctrl+Alt+1"));
        QCOMPARE(shortcutOf("Add Page"), QStringLiteral("Ctrl+Alt+N"));
        QCOMPARE(shortcutOf("Show Markdown Syntax"), QStringLiteral("Ctrl+Shift+M"));

        QVERIFY(window.isSidebarVisible());
        window.toggleSidebar();
        QVERIFY(!window.isSidebarVisible());
        window.toggleSidebar();
        QVERIFY(window.isSidebarVisible());

        // An edit made outside the editor shows up in the editor and the page outline.
        const int pages = int(window.editor()->note().pages().size());
        window.addPage();
        QVERIFY(int(window.editor()->note().pages().size()) >= pages);
        closeAll();
    }

    // MARK: AppController

    void controllerOpensFilesAndForwardsToTheRunningInstance()
    {
        resetNotes();
        const QString path = write(m_notes + "/Open Me.wprint", noteText("Open Me"));
        auto &controller = AppController::instance();
        controller.start();
        QVERIFY(controller.isBridgeRunning());
        QVERIFY(controller.listenForInstances());

        controller.openPaths({path, "C:/not-a-note.txt"});
        QCOMPARE(controller.windows().size(), 1);
        QVERIFY(controller.frontWindow());
        QCOMPARE(controller.frontWindow()->noteDocument()->filePath(), path);
        controller.openPaths({path}); // the same file again: still one window
        QCOMPARE(controller.windows().size(), 1);

        // A second launch hands its files to us.
        const QString other = write(m_notes + "/Second.wprint", noteText("Second"));
        bool forwarded = false;
        std::thread client([&] { forwarded = AppController::forwardToRunningInstance({other}); });
        QTRY_COMPARE_WITH_TIMEOUT(controller.windows().size(), 2, 5000);
        client.join();
        QVERIFY(forwarded);

        QVERIFY(AppController::recentNotes().contains(path));
        AppController::clearRecent();
        QVERIFY(AppController::recentNotes().isEmpty());

        // Palette commands follow the window's note.
        const auto commands = controller.paletteCommands(controller.frontWindow());
        QStringList commandTitles;
        for (const auto &c : commands)
            commandTitles << c.title;
        QVERIFY(commandTitles.contains("New note"));
        QVERIFY(commandTitles.contains("Insert drawing"));
        QVERIFY(commandTitles.contains("Settings"));

        closeAll();
        QCOMPARE(controller.windows().size(), 0);
        controller.shutdown();
    }

    void openStartupNoteCreatesWelcomeInAnEmptyFolder()
    {
        resetNotes();
        auto &controller = AppController::instance();
        AppServices::shared().library().reload();
        controller.openStartupNote();
        QVERIFY(QFileInfo::exists(m_notes + "/Welcome.wprint"));
        QCOMPARE(controller.windows().size(), 1);
        closeAll();
    }

    // MARK: Screenshots

    void screenshots()
    {
        const QString out = qEnvironmentVariable("WP_SCREENSHOT_DIR");
        if (out.isEmpty())
            QSKIP("WP_SCREENSHOT_DIR not set");
        QDir().mkpath(out);
        resetNotes();
        write(m_notes + "/Welcome.wprint", noteText("Welcome", 3));
        write(m_notes + "/Shopping list.wprint", noteText("Shopping list"));
        const QString net = write(m_notes + "/Courses/Networks/TCP basics.wprint",
                                  QStringLiteral("---\nwhiteprint: 1\ntitle: TCP basics\n---\n# TCP basics\n\nA **reliable** byte stream over IP.\n\n"
                                                 "- Three-way handshake\n- Congestion control\n\n```wp\nbox a at 100,100 size 120x50 \"Client\"\n```\n\n"
                                                 "```cards\nQ: What does SYN stand for?\nA: Synchronize.\n```\n\n+++page\n## Congestion control\n\nSlow start, then AIMD.\n"));
        write(m_notes + "/Courses/Networks/UDP.wprint", noteText("UDP"));
        write(m_notes + "/Courses/Algorithms/Sorting.wprint", noteText("Sorting"));
        QDir().mkpath(m_notes + "/Ideas");
        AppDefaults::store().setValue(SidebarModel::expandedFoldersKey, QStringList{"Courses", "Courses/Networks"});
        AppServices::shared().library().reload();
        for (const int dark : {0, 1}) {
            mac::setDarkOverride(dark);
            mac::apply(*qApp);
            NoteDocument *doc = NoteDocument::open(net);
            NoteWindow window(doc);
            window.setAttribute(Qt::WA_DeleteOnClose, false);
            window.resize(1080, 760);
            window.show();
            QApplication::processEvents();
            QTest::qWait(200);
            QApplication::processEvents();
            window.grab().save(QDir(out).filePath(QStringLiteral("note-window-%1.png").arg(dark ? "dark" : "light")));
            window.close();
        }
        mac::setDarkOverride(0);
        mac::apply(*qApp);
    }
};

QTEST_MAIN(ShellTests)
#include "ShellTests.moc"
