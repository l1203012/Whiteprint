// Port of FolderTests.swift.
#include "app/AppUtil.h"
#include "app/NoteFiles.h"
#include "app/NoteRegistry.h"
#include "app/NoteTree.h"
#include "app/NotesFolder.h"
#include "app/NotesLibrary.h"

#include <QDir>
#include <QTemporaryDir>
#include <QtTest>

using namespace wp;

class FolderTests : public QObject {
    Q_OBJECT

    QTemporaryDir m_dir;
    NotesFolder m_folder;

    QString touch(const QString &path)
    {
        const QString url = m_folder.url + "/" + path;
        QDir().mkpath(QFileInfo(url).absolutePath());
        QFile file(url);
        file.open(QIODevice::WriteOnly);
        file.write("x");
        return canonicalFile(url);
    }

    static QString C(const QString &p) { return canonicalFile(p); }

    static NoteEntry entry(const QString &name, const std::optional<QString> &folder)
    {
        NoteEntry e;
        e.url = "/notes/" + folder.value_or("x") + "/" + name + ".wprint";
        e.title = name;
        e.pageCount = 1;
        e.folder = folder;
        return e;
    }

private slots:
    void init()
    {
        QVERIFY(m_dir.isValid());
        m_folder = NotesFolder(m_dir.path() + "/tree");
        m_folder.create();
    }

    void cleanup() { QDir(m_folder.url).removeRecursively(); }

    void listingIsRecursiveTopLevelFirst()
    {
        for (const QString &path : {"b.wprint", "Courses/Networks/TCP.wprint", "Courses/Intro.wprint", "A.wprint",
                                    "Courses/notes.txt", ".hidden/x.wprint"})
            touch(path);
        QDir().mkpath(m_folder.url + "/Empty");
        const auto listing = m_folder.listing();
        QStringList relative;
        for (const QString &n : listing.notes)
            relative << *m_folder.relativePath(n);
        QCOMPARE(relative, (QStringList{"A.wprint", "b.wprint", "Courses/Intro.wprint", "Courses/Networks/TCP.wprint"}));
        QCOMPARE(listing.folders, (QStringList{"Courses", "Courses/Networks", "Empty"}));
    }

    void relativeFolders()
    {
        const QString top = touch("A.wprint");
        const QString nested = touch("Courses/Networks/TCP.wprint");
        QCOMPARE(*m_folder.relativeFolder(top), QString());
        QCOMPARE(*m_folder.relativeFolder(nested), QStringLiteral("Courses/Networks"));
        QVERIFY(!m_folder.relativeFolder("Z:/elsewhere/A.wprint"));
        QVERIFY(m_folder.contains(nested));
        QVERIFY(!m_folder.contains(m_folder.url));
    }

    void folderPathsMustStayInside()
    {
        QCOMPARE(m_folder.folderURL(" Courses//Networks/ "), C(m_folder.url) + "/Courses/Networks");
        QCOMPARE(m_folder.folderURL(""), C(m_folder.url));
        for (const QString &bad : {"..", "../x", "a/../../b", "/tmp", "~/x", "./a", "a:b"}) {
            bool threw = false;
            try {
                m_folder.folderURL(bad);
            } catch (const NotesFolder::FolderError &) {
                threw = true;
            }
            QVERIFY2(threw, qPrintable(bad));
        }
    }

    void createFolderCountsUp()
    {
        const QString first = m_folder.createFolder();
        const QString second = m_folder.createFolder();
        QCOMPARE(QFileInfo(first).fileName(), QStringLiteral("New Folder"));
        QCOMPARE(QFileInfo(second).fileName(), QStringLiteral("New Folder 2"));
        const QString nested = m_folder.createFolder("Week 1", first);
        QCOMPARE(*m_folder.relativePath(nested), QStringLiteral("New Folder/Week 1"));
    }

    void saveIntoSubfolder()
    {
        const QString url = m_folder.save(Note("TCP"), "TCP", m_folder.folderURL("Courses/Networks"));
        QCOMPARE(*m_folder.relativeFolder(url), QStringLiteral("Courses/Networks"));
    }

    void moveKeepsNamesUnique()
    {
        const QString note = touch("A.wprint");
        touch("Courses/A.wprint");
        const QString courses = m_folder.url + "/Courses";
        const QString moved = NoteFiles::move(note, courses);
        QCOMPARE(*m_folder.relativePath(moved), QStringLiteral("Courses/A 2.wprint"));
        QVERIFY(!QFileInfo::exists(note));
        QCOMPARE(NoteFiles::move(moved, courses), moved);
    }

    void folderCantMoveIntoItself()
    {
        touch("Courses/Networks/TCP.wprint");
        const QString courses = m_folder.url + "/Courses";
        QVERIFY_THROWS_EXCEPTION(NoteFiles::FileError, NoteFiles::move(courses, courses + "/Networks"));
        QVERIFY_THROWS_EXCEPTION(NoteFiles::FileError, NoteFiles::move(courses, courses));
        const QString moved = NoteFiles::move(courses + "/Networks", m_folder.url);
        QCOMPARE(*m_folder.relativePath(moved), QStringLiteral("Networks"));
        QVERIFY(QFileInfo::exists(moved + "/TCP.wprint"));
    }

    void rename()
    {
        const QString note = touch("Courses/A.wprint");
        touch("Courses/Taken.wprint");
        const QString renamed = NoteFiles::rename(note, "TCP/IP");
        QCOMPARE(*m_folder.relativePath(renamed), QStringLiteral("Courses/TCP-IP.wprint"));
        QVERIFY_THROWS_EXCEPTION(NoteFiles::FileError, NoteFiles::rename(renamed, "Taken"));
        QVERIFY_THROWS_EXCEPTION(NoteFiles::FileError, NoteFiles::rename(renamed, "  "));
        const QString folderRenamed = NoteFiles::rename(m_folder.url + "/Courses", "Classes");
        QCOMPARE(*m_folder.relativePath(folderRenamed), QStringLiteral("Classes"));
        // Case-only renames work too.
        const QString cased = NoteFiles::rename(folderRenamed, "classes");
        QCOMPARE(QFileInfo(cased).fileName(), QStringLiteral("classes"));
    }

    void relocated()
    {
        const QString oldPath = "/notes/Courses", newPath = "/notes/Archive/Courses";
        QCOMPARE(*NoteFiles::relocated("/notes/Courses/Net/TCP.wprint", oldPath, newPath), C("/notes/Archive/Courses/Net/TCP.wprint"));
        QVERIFY(!NoteFiles::relocated("/notes/CoursesOld/TCP.wprint", oldPath, newPath));
    }

    void registryFollowsFolderMoves()
    {
        NoteRegistry registry;
        const QString tcp = "/notes/Courses/TCP.wprint", other = "/notes/Other.wprint";
        registry.id(tcp);
        registry.id(other);
        registry.move("/notes/Courses", "/notes/Archive/Courses");
        QCOMPARE(*registry.url("n1"), C("/notes/Archive/Courses/TCP.wprint"));
        QCOMPARE(registry.id("/notes/Archive/Courses/TCP.wprint"), QStringLiteral("n1"));
        QCOMPARE(*registry.url("n2"), C(other));
    }

    void treeOrdersFoldersFirst()
    {
        const NoteTree tree = NoteTree::build(
            {entry("A", QString()), entry("TCP", "Courses/Networks"), entry("Intro", "Courses"), entry("Outside", std::nullopt),
             entry("B", QString())},
            {"Zeta", "Courses", "Courses/Networks", "alpha"});
        QStringList names;
        for (const NoteTree &f : tree.folders)
            names << f.name;
        QCOMPARE(names, (QStringList{"alpha", "Courses", "Zeta"}));
        QStringList titles;
        for (const NoteEntry &n : tree.notes)
            titles << n.title;
        QCOMPARE(titles, (QStringList{"A", "B", "Outside"}));
        const NoteTree &courses = tree.folders[1];
        QCOMPARE(courses.path, QStringLiteral("Courses"));
        QCOMPARE(courses.folders.size(), 1);
        QCOMPARE(courses.folders[0].path, QStringLiteral("Courses/Networks"));
        QCOMPARE(courses.notes.size(), 1);
        QCOMPARE(courses.notes[0].title, QStringLiteral("Intro"));
        QCOMPARE(courses.folders[0].notes[0].title, QStringLiteral("TCP"));
    }

    void mergedListsOpenNotesElsewhereLast()
    {
        const QString a = "/notes/A.wprint", b = "/elsewhere/B.wprint";
        QCOMPARE(NotesLibrary::merged({a}, {a, b}), (QStringList{a, b}));
    }
};

QTEST_GUILESS_MAIN(FolderTests)
#include "FolderTests.moc"
