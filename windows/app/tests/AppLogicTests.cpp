// Port of AppLogicTests.swift.
#include "app/ClaudeSetup.h"
#include "app/NoteRegistry.h"
#include "app/AppUtil.h"
#include "app/NoteTitle.h"
#include "app/NoteWorkspace.h"
#include "app/NotesFolder.h"
#include "app/PageOutline.h"
#include "app/PaletteSearch.h"
#include "app/WelcomeNote.h"
#include "core/DrawingCompiler.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QSettings>
#include <QTemporaryDir>
#include <QtTest>

using namespace wp;

class AppLogicTests : public QObject {
    Q_OBJECT

    static QString C(const QString &path) { return canonicalFile(path); }
    static QJsonObject object(const QByteArray &data) { return QJsonDocument::fromJson(data).object(); }

private slots:
    // NoteRegistry
    void registryIDsAreSequentialAndStable()
    {
        NoteRegistry registry;
        const QString a = QStringLiteral("/notes/A.wprint"), b = QStringLiteral("/notes/B.wprint");
        QCOMPARE(registry.id(a), QStringLiteral("n1"));
        QCOMPARE(registry.id(b), QStringLiteral("n2"));
        QCOMPARE(registry.id(QStringLiteral("/notes/./A.wprint")), QStringLiteral("n1"));
        QCOMPARE(*registry.url(QStringLiteral("n2")), C(b));
        QCOMPARE(*registry.url(QStringLiteral(" N2 ")), C(b));
        QVERIFY(!registry.url(QStringLiteral("n3")));
    }

    void registryIDFollowsRenamedFile()
    {
        NoteRegistry registry;
        const QString oldPath = QStringLiteral("/notes/A.wprint"), newPath = QStringLiteral("/notes/Renamed.wprint");
        registry.id(oldPath);
        registry.move(oldPath, newPath);
        QCOMPARE(registry.id(newPath), QStringLiteral("n1"));
        QCOMPARE(*registry.url(QStringLiteral("n1")), C(newPath));
        QCOMPARE(registry.id(oldPath), QStringLiteral("n2"));
    }

    // NotesFolder
    void listsOnlyNotesSortedByName()
    {
        QTemporaryDir dir;
        NotesFolder folder(dir.path());
        for (const QString &name : {"b.wprint", "A 10.wprint", "A 2.wprint", "notes.txt", ".hidden.wprint"}) {
            QFile file(folder.url + "/" + name);
            QVERIFY(file.open(QIODevice::WriteOnly));
            file.write("x");
        }
        QStringList names;
        for (const QString &url : folder.noteURLs())
            names << QFileInfo(url).fileName();
        QCOMPARE(names, (QStringList{"A 2.wprint", "A 10.wprint", "b.wprint"}));
    }

    void unusedURLCountsUp()
    {
        QTemporaryDir dir;
        NotesFolder folder(dir.path());
        QCOMPARE(QFileInfo(folder.unusedURL("Untitled")).fileName(), QStringLiteral("Untitled.wprint"));
        folder.save(Note(), "Untitled");
        QCOMPARE(QFileInfo(folder.unusedURL("Untitled")).fileName(), QStringLiteral("Untitled 2.wprint"));
        folder.save(Note(), "Untitled");
        QCOMPARE(QFileInfo(folder.unusedURL("Untitled")).fileName(), QStringLiteral("Untitled 3.wprint"));
    }

    void saveWritesTheNote()
    {
        QTemporaryDir dir;
        NotesFolder folder(dir.path());
        const Note note("Plan");
        const QString url = folder.save(note, "Plan");
        QVERIFY(folder.contains(url));
        QVERIFY(Note::parsing(*readUtf8File(url)) == note);
    }

    void fileNamesAreSafe()
    {
        QCOMPARE(NotesFolder::fileName("TCP/IP: basics"), QStringLiteral("TCP-IP- basics"));
        QCOMPARE(NotesFolder::fileName("..hidden"), QStringLiteral("hidden"));
        QCOMPARE(NotesFolder::fileName("  "), QStringLiteral("Untitled"));
        QCOMPARE(NotesFolder::fileName(QString(300, 'a')).size(), 100);
    }

    void folderOverrides()
    {
        QTemporaryDir dir;
        QSettings defaults(dir.filePath("settings.ini"), QSettings::IniFormat);
        QProcessEnvironment none;
        QCOMPARE(NotesFolder::current(defaults, none).url, NotesFolder::defaultURL());
        defaults.setValue(NotesFolder::defaultsKey, "C:/tmp/chosen");
        QCOMPARE(NotesFolder::current(defaults, none).url, QStringLiteral("C:/tmp/chosen"));
        QProcessEnvironment env;
        env.insert(NotesFolder::environmentKey, "C:/tmp/env");
        QCOMPARE(NotesFolder::current(defaults, env).url, QStringLiteral("C:/tmp/env"));
    }

    // NoteTitle & friends
    void titleFallsBackToFileName()
    {
        const QString url = "/notes/Lecture 3.wprint";
        QCOMPARE(NoteTitle::display(Note("Networks"), url), QStringLiteral("Networks"));
        QCOMPARE(NoteTitle::display(Note("  "), url), QStringLiteral("Lecture 3"));
        QCOMPARE(NoteTitle::display(Note(), url), QStringLiteral("Lecture 3"));
        QCOMPARE(NoteTitle::display(Note(), QString()), QStringLiteral("Untitled"));
    }

    void renameChangesTitleOnlyWhenTheNameChanged()
    {
        const QString a = "/notes/A.wprint";
        QCOMPARE(*NoteTitle::titleAfterRename(a, "/notes/B.wprint"), QStringLiteral("B"));
        QVERIFY(!NoteTitle::titleAfterRename(a, "/elsewhere/A.wprint"));
        QVERIFY(!NoteTitle::titleAfterRename(QString(), a));
    }

    void pageTitles()
    {
        const Note note = Note::parsing("Intro text\n# Overview\n+++page\n\n## Routing basics\n+++page\n```wp\nbox a\n```");
        QCOMPARE(PageOutline::titles(note), (QStringList{"Overview", "Routing basics", "Page 3"}));
    }

    void welcomeNoteParses()
    {
        const Note note = Note::parsing(WelcomeNote::source());
        QCOMPARE(*note.frontMatter.title(), QStringLiteral("Welcome"));
        QCOMPARE(note.pages().size(), 2);
        QCOMPARE(note.drawings().size(), 1);
        QCOMPARE(note.drawings()[0].source,
                 QStringLiteral("flow You>Whiteprint>Claude\ntext \"Claude writes and draws in your notes, on your own subscription\""));
        QVERIFY(DrawingCompiler::compile(note.drawings()[0].source).errors.isEmpty());
    }

    // PaletteSearch
    void paletteRanking()
    {
        const QList<QString> titles{"Insert drawing", "New note", "Networks", "Generate study plan", "Settings"};
        const auto id = [](const QString &s) { return s; };
        QCOMPARE(PaletteSearch::filter(titles, "ne", id),
                 (QList<QString>{"New note", "Networks", "Generate study plan", "Insert drawing"}));
        QCOMPARE(PaletteSearch::filter(titles, "draw", id), (QList<QString>{"Insert drawing"}));
        QCOMPARE(PaletteSearch::filter(titles, "gsp", id), (QList<QString>{"Generate study plan"}));
        QCOMPARE(PaletteSearch::filter(titles, "", id), titles);
        QCOMPARE(PaletteSearch::filter(titles, "zzz", id), QList<QString>());
    }

    // ClaudeSetup
    void mergeKeepsOtherServersAndKeys()
    {
        const QString helper = "C:/Program Files/Whiteprint/whiteprint-mcp.exe";
        const QByteArray existing = R"({"mcpServers":{"other":{"command":"/bin/other","args":["x"]}},"theme":"dark"})";
        const QJsonObject merged = object(ClaudeDesktopConfig::merging(helper, existing));
        QCOMPARE(merged["theme"].toString(), QStringLiteral("dark"));
        const QJsonObject servers = merged["mcpServers"].toObject();
        QCOMPARE(servers["other"].toObject()["command"].toString(), QStringLiteral("/bin/other"));
        QCOMPARE(servers["whiteprint"].toObject()["command"].toString(), helper);
    }

    void mergeIntoNothing()
    {
        for (const std::optional<QByteArray> &input : {std::optional<QByteArray>(), std::optional<QByteArray>(QByteArray()),
                                                       std::optional<QByteArray>(QByteArray(" \n"))}) {
            const QJsonObject merged = object(ClaudeDesktopConfig::merging("h.exe", input));
            QCOMPARE(merged.keys(), (QStringList{"mcpServers"}));
        }
    }

    void mergeRefusesNonObjects()
    {
        QVERIFY_THROWS_EXCEPTION(ClaudeDesktopConfig::ConfigError, ClaudeDesktopConfig::merging("h", QByteArray("[1]")));
        QVERIFY_THROWS_EXCEPTION(ClaudeDesktopConfig::ConfigError, ClaudeDesktopConfig::merging("h", QByteArray("{broken")));
    }

    void connectWritesBackupFirst()
    {
        QTemporaryDir dir;
        const QString helper = "C:/Whiteprint/whiteprint-mcp.exe";
        const QString config = dir.path() + "/Claude/claude_desktop_config.json";
        QDir().mkpath(dir.path() + "/Claude");
        const QByteArray original = R"({"mcpServers":{"other":{"command":"/bin/other"}}})";
        { QFile f(config); QVERIFY(f.open(QIODevice::WriteOnly)); f.write(original); }
        QVERIFY(!ClaudeDesktopConfig::isConnected(helper, config));

        const auto backup = ClaudeDesktopConfig::connect(helper, config);
        QVERIFY(backup);
        QCOMPARE(QFileInfo(*backup).fileName(), QStringLiteral("claude_desktop_config.json.backup"));
        { QFile f(*backup); QVERIFY(f.open(QIODevice::ReadOnly)); QCOMPARE(f.readAll(), original); }
        QVERIFY(ClaudeDesktopConfig::isConnected(helper, config));
        QFile f(config);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(object(f.readAll())["mcpServers"].toObject().keys(), (QStringList{"other", "whiteprint"}));
    }

    void connectCreatesMissingConfig()
    {
        QTemporaryDir dir;
        const QString config = dir.path() + "/Claude/claude_desktop_config.json";
        QVERIFY(!ClaudeDesktopConfig::connect("h.exe", config));
        QVERIFY(ClaudeDesktopConfig::isConnected("h.exe", config));
    }

    void brokenConfigIsLeftAlone()
    {
        QTemporaryDir dir;
        const QString config = dir.path() + "/claude_desktop_config.json";
        { QFile f(config); QVERIFY(f.open(QIODevice::WriteOnly)); f.write("{broken"); }
        QVERIFY_THROWS_EXCEPTION(ClaudeDesktopConfig::ConfigError, ClaudeDesktopConfig::connect("h", config));
        QFile f(config);
        QVERIFY(f.open(QIODevice::ReadOnly));
        QCOMPARE(f.readAll(), QByteArray("{broken"));
    }

    void claudeCodeCommand()
    {
        const QString helper = "C:/Whiteprint/whiteprint-mcp.exe";
        QCOMPARE(ClaudeCodeSetup::arguments(helper),
                 (QStringList{"mcp", "add", "--scope", "user", "whiteprint", "--", helper}));
        const QString spaced = "C:/Users/me/My Apps/whiteprint-mcp.exe";
        QCOMPARE(ClaudeCodeSetup::commandLine(std::nullopt, spaced),
                 QStringLiteral("claude mcp add --scope user whiteprint -- \"C:/Users/me/My Apps/whiteprint-mcp.exe\""));
        QCOMPARE(ClaudeCodeSetup::shellQuoted("say \"hi\""), QStringLiteral("\"say \\\"hi\\\"\""));
    }

    void errorLines()
    {
        QCOMPARE(errorLine(NoteEditError::lastPage()), QStringLiteral("can't remove the only page"));
        QCOMPARE(errorLine(NoteFormatError::unsupportedVersion(9)),
                 QStringLiteral("note uses format 9; this Whiteprint reads up to 1"));
        QCOMPARE(errorLine(std::runtime_error("boom")), QStringLiteral("boom"));
    }
};

QTEST_GUILESS_MAIN(AppLogicTests)
#include "AppLogicTests.moc"
