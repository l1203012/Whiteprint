// Tests for ClaudeCodeRunner. The test executable doubles as a fake `claude`: when WP_FAKE_CLAUDE is set
// it records how it was started and then follows the steps in the file named by WP_FAKE_SCRIPT.
#ifndef Q_MOC_RUN
#include "StreamSamples.h"
#endif
#include "core/StudyPrompt.h"
#include "core/tests/TestSupport.h"
#include "study/ClaudeCodeRunner.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

using namespace wp;
using namespace wp::testing;
using Event = RunnerEvent;

namespace {

// MARK: - The fake claude

void writeFile(const QString &path, const QByteArray &data)
{
    QFile f(path);
    if (f.open(QIODevice::WriteOnly))
        f.write(data);
}

int fakeClaude(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    if (qEnvironmentVariableIsSet("WP_FAKE_CHILD")) {
        QThread::sleep(60);
        return 0;
    }
    const QString out = qEnvironmentVariable("WP_FAKE_OUT");
    // The wide command line: argv loses non-ASCII characters on Windows.
    QStringList arguments;
#ifdef Q_OS_WIN
    int count = 0;
    wchar_t **wide = CommandLineToArgvW(GetCommandLineW(), &count);
    for (int i = 0; i < count; ++i)
        arguments << QString::fromWCharArray(wide[i]);
    LocalFree(wide);
#else
    arguments = QCoreApplication::arguments();
#endif
    QByteArray args;
    for (qsizetype i = 1; i < arguments.size(); ++i)
        args += arguments[i].toUtf8() + '\0';
    writeFile(out + "/args", args);
    writeFile(out + "/cwd", QDir::currentPath().toUtf8());
    QFile config("mcp.json");
    if (config.open(QIODevice::ReadOnly))
        writeFile(out + "/mcp.json", config.readAll());
    writeFile(out + "/path", qgetenv("PATH"));

    QFile stdoutFile;
    stdoutFile.open(stdout, QIODevice::WriteOnly);
    QFile script(qEnvironmentVariable("WP_FAKE_SCRIPT"));
    script.open(QIODevice::ReadOnly);
    int exitCode = 0;
    for (const QByteArray &raw : script.readAll().split('\n')) {
        const QByteArray line = raw.trimmed();
        const QByteArray arg = line.mid(line.indexOf(' ') + 1);
        if (line.startsWith("cat ")) {
            QFile f(QString::fromUtf8(arg));
            f.open(QIODevice::ReadOnly);
            stdoutFile.write(f.readAll());
            stdoutFile.flush();
        } else if (line.startsWith("stderr ")) {
            fprintf(stderr, "%s\n", arg.constData());
            fflush(stderr);
        } else if (line.startsWith("exit ")) {
            exitCode = arg.toInt();
        } else if (line == "child") {
            qputenv("WP_FAKE_CHILD", "1");
            qint64 pid = 0;
            QProcess::startDetached(QCoreApplication::applicationFilePath(), {}, QString(), &pid);
            writeFile(out + "/child", QByteArray::number(pid));
        } else if (line == "hang") {
            QThread::sleep(60);
        }
    }
    return exitCode;
}

bool isAlive(qint64 pid)
{
#ifdef Q_OS_WIN
    HANDLE h = OpenProcess(SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION, FALSE, DWORD(pid));
    if (!h)
        return false;
    const bool alive = WaitForSingleObject(h, 0) == WAIT_TIMEOUT;
    CloseHandle(h);
    return alive;
#else
    return QFileInfo::exists(QStringLiteral("/proc/%1").arg(pid));
#endif
}

} // namespace

class ClaudeCodeRunnerTests : public QObject
{
    Q_OBJECT

    QTemporaryDir root;
    QList<Event> events;
    const QString helper = "C:/Program Files/Whiteprint/whiteprint-mcp.exe";

    QString fakePath() const { return QCoreApplication::applicationFilePath(); }

    /// Writes the fake's script; every step is one line.
    void script(const QStringList &steps)
    {
        writeFile(root.filePath("script"), steps.join('\n').toUtf8());
        qputenv("WP_FAKE_SCRIPT", root.filePath("script").toUtf8());
    }
    QString samples(const QStringList &lines)
    {
        const QString path = root.filePath("samples.jsonl");
        writeFile(path, (lines.join('\n') + "\n").toUtf8());
        return "cat " + path;
    }

    ClaudeCodeRunner::EventHandler recorder()
    {
        return [this](const Event &e) { events.append(e); };
    }

    void waitForEnd()
    {
        QTRY_VERIFY_WITH_TIMEOUT(!events.isEmpty() && events.last().isTerminal(), 20000);
        // Anything delivered after the end would be a bug.
        QTest::qWait(150);
    }
    int terminalCount() const
    {
        return int(std::count_if(events.begin(), events.end(), [](const Event &e) { return e.isTerminal(); }));
    }
    QByteArray fileContent(const QString &name) const
    {
        QFile f(root.filePath(name));
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    }
    QList<QString> recordedArgs() const
    {
        QList<QString> out;
        for (const QByteArray &a : fileContent("args").split('\0'))
            if (!a.isEmpty())
                out << QString::fromUtf8(a);
        return out;
    }

private slots:
    void initTestCase() { QVERIFY(root.isValid()); }

    void init()
    {
        events.clear();
        for (const QString &f : {"args", "cwd", "mcp.json", "path", "child", "script", "samples.jsonl", "stdin.txt"})
            QFile::remove(root.filePath(f));
        qputenv("WP_FAKE_CLAUDE", "1");
        qputenv("WP_FAKE_OUT", root.path().toUtf8());
    }

    void cleanup()
    {
        qunsetenv("WP_FAKE_CLAUDE");
        qunsetenv("WP_FAKE_OUT");
        qunsetenv("WP_FAKE_SCRIPT");
    }

    void successfulRun()
    {
        script({samples(StreamSamples::run())});
        StudyStore store(root.filePath("Study"));
        ClaudeCodeRunner runner(fakePath(), &store);
        runner.start({"i1", "i2"}, helper, recorder());
        QVERIFY(runner.isRunning());
        waitForEnd();

        QCOMPARE(events.first(), Event::status("Starting Claude…"));
        QVERIFY(events.contains(Event::status("Reading chunk 3 · i1")));
        QCOMPARE(events.last(), Event::finished());
        QCOMPARE(terminalCount(), 1);
        QTRY_VERIFY(!runner.isRunning());

        const QStringList args = recordedArgs();
        const qsizetype configIndex = args.indexOf("--mcp-config") + 1;
        QVERIFY(configIndex > 0);
        const QString configPath = args[configIndex];
        QCOMPARE(args, ClaudeCodeRunner::arguments(WhiteprintText::studyPlanPrompt() + "\n\nImports: i1, i2", configPath));
        // The config sits in the working directory.
        const QString cwd = QDir::cleanPath(QString::fromUtf8(fileContent("cwd")));
        QCOMPARE(cwd.toLower(), QDir::cleanPath(QFileInfo(configPath).absolutePath()).toLower());

        const QJsonObject config = QJsonDocument::fromJson(fileContent("mcp.json")).object();
        const QJsonObject server = config["mcpServers"].toObject()["whiteprint"].toObject();
        QCOMPARE(server["command"].toString(), helper);
        QCOMPARE(server["args"].toArray().size(), 0);

        const QStringList path = QString::fromUtf8(fileContent("path")).split(QDir::listSeparator());
        const QString dir = QDir::toNativeSeparators(QFileInfo(fakePath()).absolutePath());
        QVERIFY2(std::any_of(path.begin(), path.end(), [&](const QString &p) { return p.compare(dir, Qt::CaseInsensitive) == 0; }),
                 qPrintable(path.join(';')));
        QVERIFY2(!QFileInfo::exists(configPath), "temporary MCP config left behind");
    }

    void nonZeroExitReportsStderr()
    {
        script({"stderr starting", "stderr Error: not logged in", "exit 3"});
        ClaudeCodeRunner runner(fakePath());
        runner.start({}, helper, recorder());
        waitForEnd();
        WP_EQ(events, QList<Event>{Event::failed("Claude Code exited with status 3: Error: not logged in")});
        QVERIFY(recordedArgs()[1].endsWith("Imports: all"));
    }

    void errorResultFails()
    {
        script({samples({StreamSamples::initLine, StreamSamples::notLoggedIn}), "exit 1"});
        ClaudeCodeRunner runner(fakePath());
        runner.start({"i1"}, helper, recorder());
        waitForEnd();
        WP_EQ(events, (QList<Event>{Event::status("Starting Claude…"), Event::failed("Invalid API key · Please run /login")}));
    }

    void exitWithoutResultFails()
    {
        script({samples(StreamSamples::run().mid(0, 3))});
        ClaudeCodeRunner runner(fakePath());
        runner.start({"i1"}, helper, recorder());
        waitForEnd();
        QCOMPARE(events.last(), Event::failed("Claude Code stopped without finishing the study plan."));
    }

    void missingBinaryFails()
    {
        const QString missing = root.filePath("nope/claude.exe");
        ClaudeCodeRunner runner(missing);
        runner.start({"i1"}, helper, recorder());
        waitForEnd();
        WP_EQ(events, QList<Event>{Event::failed(QString("Claude Code wasn't found at %1.").arg(missing))});
        QVERIFY(!runner.isRunning());
    }

    void cancelStopsTheProcessTree()
    {
        script({samples({StreamSamples::initLine}), "child", "hang"});
        ClaudeCodeRunner runner(fakePath());
        runner.start({"i1"}, helper, recorder());
        QTRY_VERIFY_WITH_TIMEOUT(!events.isEmpty() && QFileInfo::exists(root.filePath("child")), 15000);
        QTest::qWait(100);
        const qint64 child = fileContent("child").trimmed().toLongLong();
        QVERIFY(child > 0);
        QVERIFY(runner.isRunning());
        QVERIFY(isAlive(child));
        const auto error = caught<StudyError>([&] { runner.start({}, helper, [](const Event &) {}); });
        QVERIFY(error && *error == StudyError::alreadyRunning());

        runner.cancel();
        waitForEnd();
        WP_EQ(events, (QList<Event>{Event::status("Starting Claude…"), Event::failed("Cancelled")}));
        QVERIFY(!runner.isRunning());
        QTRY_VERIFY2(!isAlive(child), "child process survived cancel");
    }

    void helperFailureStopsTheRun()
    {
        script({samples({StreamSamples::helperFailed}), "hang"});
        ClaudeCodeRunner runner(fakePath());
        QElapsedTimer timer;
        timer.start();
        runner.start({"i1"}, helper, recorder());
        waitForEnd();
        WP_EQ(events, QList<Event>{Event::failed("Claude Code couldn't start Whiteprint's tools. Try reinstalling Whiteprint.")});
        QVERIFY(timer.elapsed() < 15000);
        QTRY_VERIFY(!runner.isRunning());
    }

    void leftoverChildDoesNotHoldUpTheEnd()
    {
        script({samples(StreamSamples::run()), "child"});
        ClaudeCodeRunner runner(fakePath());
        runner.start({"i1"}, helper, recorder());
        waitForEnd();
        QCOMPARE(events.last(), Event::finished());
        QTRY_VERIFY_WITH_TIMEOUT(!runner.isRunning(), 5000);
        const qint64 child = fileContent("child").trimmed().toLongLong();
        QVERIFY(child > 0);
        QTRY_VERIFY2(!isAlive(child), "leftover child kept running");
    }

    void runnerCanRunAgainAfterTheEnd()
    {
        script({samples(StreamSamples::run())});
        ClaudeCodeRunner runner(fakePath());
        runner.start({"i1"}, helper, recorder());
        waitForEnd();
        QTRY_VERIFY(!runner.isRunning());
        events.clear();
        runner.start({"i1"}, helper, recorder());
        waitForEnd();
        QCOMPARE(events.last(), Event::finished());
    }

#ifdef Q_OS_WIN
    void npmCmdShimGetsThePromptOnStdin()
    {
        // An npm-style claude.cmd we can't see through: started via cmd.exe, prompt on stdin.
        const QString shim = root.filePath("claude.cmd");
        writeFile(shim, "@echo off\r\nmore > \"%WP_FAKE_OUT%\\stdin.txt\"\r\ntype \"%WP_FAKE_OUT%\\samples.jsonl\"\r\n");
        script({samples(StreamSamples::run())});
        ClaudeCodeRunner runner(shim);
        runner.start({"i1"}, helper, recorder());
        waitForEnd();
        QCOMPARE(events.last(), Event::finished());
        QVERIFY(QString::fromUtf8(fileContent("stdin.txt")).contains("Imports: i1"));
    }
#endif

    void arguments()
    {
        const QStringList expected = {"-p", "P", "--output-format", "stream-json", "--verbose", "--strict-mcp-config",
                                      "--mcp-config", "C:/x/mcp.json", "--allowedTools", "mcp__whiteprint__*,ReadMcpResourceTool",
                                      "--permission-mode", "dontAsk", "--no-session-persistence"};
        QCOMPARE(ClaudeCodeRunner::arguments("P", "C:/x/mcp.json"), expected);
    }

    void locateClaudeFindsAFile()
    {
        if (const auto path = ClaudeCodeRunner::locateClaude()) {
            QVERIFY(QFileInfo(*path).isFile());
            QVERIFY(QFileInfo(*path).completeBaseName().compare("claude", Qt::CaseInsensitive) == 0);
        }
        const QStringList candidates = ClaudeCodeRunner::candidatePaths();
        QVERIFY(std::any_of(candidates.begin(), candidates.end(), [](const QString &c) { return c.endsWith(".local/bin/claude.exe"); }));
    }

    void environmentExtendsPath()
    {
        const QProcessEnvironment env = ClaudeCodeRunner::environment("D:/tools/claude.exe");
        const QStringList path = env.value("PATH").split(QDir::listSeparator());
        QVERIFY(path.contains(QDir::toNativeSeparators("D:/tools")));
        QVERIFY(path.contains(QDir::toNativeSeparators(QDir::homePath() + "/.local/bin")));
    }

    void promptIsCompact()
    {
        const QString prompt = WhiteprintText::studyPlanPrompt();
        QVERIFY(prompt.size() < 2000);
        for (const char *tool : {"list_imports", "read_chunk", "save_points", "get_points", "build_study_plan", "whiteprint://dsl"})
            QVERIFY2(prompt.contains(tool), tool);
    }
};

int main(int argc, char **argv)
{
    if (qEnvironmentVariableIsSet("WP_FAKE_CLAUDE"))
        return fakeClaude(argc, argv);
    QCoreApplication app(argc, argv);
    ClaudeCodeRunnerTests tests;
    return QTest::qExec(&tests, argc, argv);
}

#include "ClaudeCodeRunnerTests.moc"
