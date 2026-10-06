#include "study/ClaudeCodeRunner.h"

#include "core/StudyPrompt.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>
#include <QUuid>
#include <algorithm>
#include <stdexcept>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

namespace wp {

const QString ClaudeCodeRunner::allowedTools = QStringLiteral("mcp__whiteprint__*,ReadMcpResourceTool");

namespace {

#ifdef Q_OS_WIN
constexpr bool onWindows = true;
#else
constexpr bool onWindows = false;
#endif

bool samePath(const QString &a, const QString &b)
{
    const QString x = QDir::cleanPath(a), y = QDir::cleanPath(b);
    return onWindows ? x.compare(y, Qt::CaseInsensitive) == 0 : x == y;
}

/// How the `claude` file is actually started.
struct Launch {
    QString program;
    QStringList arguments;
    /// Run through `cmd.exe /c` (an npm `.cmd` shim we couldn't see through). The prompt then goes to
    /// stdin, because a multi-line argument can't survive cmd.exe.
    bool viaCmd = false;
    QByteArray stdinText;
};

Launch resolveLaunch(const QString &claudePath, QStringList arguments)
{
    Launch launch{claudePath, arguments, false, {}};
    const QString suffix = QFileInfo(claudePath).suffix().toLower();
    if (suffix != "cmd" && suffix != "bat")
        return launch;

    // npm's shim only starts the real thing: use that directly when it is where npm puts it.
    const QDir dir = QFileInfo(claudePath).absoluteDir();
    const QString package = dir.filePath(QStringLiteral("node_modules/@anthropic-ai/claude-code"));
    const QString native = package + QStringLiteral("/bin/claude.exe");
    if (QFileInfo(native).isFile())
        return {native, arguments, false, {}};
    const QString script = package + QStringLiteral("/cli.js");
    if (QFileInfo(script).isFile()) {
        QString node = dir.filePath(QStringLiteral("node.exe"));
        if (!QFileInfo(node).isFile())
            node = QStandardPaths::findExecutable(QStringLiteral("node"));
        if (!node.isEmpty())
            return {node, QStringList{script} + arguments, false, {}};
    }

    launch.viaCmd = true;
    launch.program = qEnvironmentVariable("COMSPEC", QStringLiteral("cmd.exe"));
    const qsizetype p = arguments.indexOf(QStringLiteral("-p"));
    if (p >= 0 && p + 1 < arguments.size()) {
        launch.stdinText = arguments[p + 1].toUtf8();
        arguments.removeAt(p + 1);
    }
    launch.arguments = arguments;
    return launch;
}

#ifdef Q_OS_WIN
/// A Windows job object: everything Claude Code starts joins it, and it is killed together.
class Job {
public:
    Job()
    {
        m_handle = CreateJobObjectW(nullptr, nullptr);
        if (!m_handle)
            return;
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION info{};
        info.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        SetInformationJobObject(m_handle, JobObjectExtendedLimitInformation, &info, sizeof(info));
    }
    ~Job()
    {
        if (m_handle)
            CloseHandle(m_handle);
    }
    Job(const Job &) = delete;
    Job &operator=(const Job &) = delete;

    bool assign(qint64 pid)
    {
        if (!m_handle || pid <= 0)
            return false;
        HANDLE process = OpenProcess(PROCESS_SET_QUOTA | PROCESS_TERMINATE, FALSE, DWORD(pid));
        if (!process)
            return false;
        const bool ok = AssignProcessToJobObject(m_handle, process) != 0;
        CloseHandle(process);
        m_assigned = m_assigned || ok;
        return ok;
    }
    bool assigned() const { return m_assigned; }
    void terminate()
    {
        if (m_handle && m_assigned)
            TerminateJobObject(m_handle, 1);
    }

private:
    HANDLE m_handle = nullptr;
    bool m_assigned = false;
};
#endif

QString lastLine(const QByteArray &stderrTail)
{
    const QStringList lines = QString::fromUtf8(stderrTail).split('\n');
    for (qsizetype i = lines.size() - 1; i >= 0; --i) {
        const QString line = lines[i].trimmed();
        if (!line.isEmpty())
            return line;
    }
    return {};
}

} // namespace

/// One `claude -p` process and what it has reported so far.
struct ClaudeCodeRunner::Run {
    explicit Run(ClaudeStreamParser parser, EventHandler onEvent) : parser(std::move(parser)), onEvent(std::move(onEvent)) {}

    QProcess *process = nullptr;
    QString folder;
    ClaudeStreamParser parser;
    EventHandler onEvent;
    bool cancelled = false;
    bool ended = false;
    /// The end of stderr, for the error message when Claude Code exits without a result.
    QByteArray errorTail;
    std::shared_ptr<std::wstring> cmdLine;
#ifdef Q_OS_WIN
    std::unique_ptr<Job> job;
#endif

    void deliver(const Event &event)
    {
        if (ended)
            return;
        ended = event.isTerminal();
        onEvent(event);
    }

    void appendError(const QByteArray &data)
    {
        errorTail.append(data);
        if (errorTail.size() > 4096)
            errorTail = errorTail.right(2048);
    }
};

// MARK: - Locating

QStringList ClaudeCodeRunner::candidatePaths()
{
    const QString home = QDir::homePath();
    const QString appData = qEnvironmentVariable("APPDATA");
    const QString localAppData = qEnvironmentVariable("LOCALAPPDATA");
    QStringList list;
    list << home + "/.local/bin/claude.exe" << home + "/.claude/local/claude.exe" << home + "/.claude/local/claude.cmd";
    if (!appData.isEmpty())
        list << appData + "/npm/claude.cmd" << appData + "/npm/claude.exe";
    if (!localAppData.isEmpty())
        list << localAppData + "/Programs/claude/claude.exe" << localAppData + "/Microsoft/WinGet/Links/claude.exe";
    if (!onWindows)
        list << home + "/.local/bin/claude" << "/opt/homebrew/bin/claude" << "/usr/local/bin/claude" << home + "/.claude/local/claude";
    return list;
}

std::optional<QString> ClaudeCodeRunner::locateClaude()
{
    const QString onPath = QStandardPaths::findExecutable(QStringLiteral("claude"));
    if (!onPath.isEmpty())
        return QDir::toNativeSeparators(onPath);
    for (const QString &candidate : candidatePaths()) {
        if (QFileInfo(candidate).isFile())
            return QDir::toNativeSeparators(candidate);
    }
    return std::nullopt;
}

// MARK: - Running

ClaudeCodeRunner::ClaudeCodeRunner(QString claudePath, StudyStore *store, QObject *parent)
    : QObject(parent), m_claudePath(std::move(claudePath)), m_store(store)
{
}

ClaudeCodeRunner::~ClaudeCodeRunner()
{
    if (const auto run = std::exchange(m_run, nullptr)) {
        if (run->process) {
            run->process->disconnect(this);
            run->process->kill();
            run->process->waitForFinished(2000);
        }
#ifdef Q_OS_WIN
        if (run->job)
            run->job->terminate();
#endif
        QDir(run->folder).removeRecursively();
    }
}

void ClaudeCodeRunner::start(const QStringList &importIDs, const QString &helperPath, EventHandler onEvent)
{
    if (m_run)
        throw StudyError::alreadyRunning();

    const QString folder = QDir(QDir::tempPath()).filePath(QStringLiteral("Whiteprint-") + QUuid::createUuid().toString(QUuid::WithoutBraces));
    if (!QDir().mkpath(folder))
        throw std::runtime_error("Couldn't create a temporary folder for Claude Code.");
    const QString config = QDir(folder).filePath(QStringLiteral("mcp.json"));
    {
        QFile file(config);
        if (!file.open(QIODevice::WriteOnly) || file.write(mcpConfig(helperPath)) < 0) {
            QDir(folder).removeRecursively();
            throw std::runtime_error("Couldn't write Claude Code's MCP config.");
        }
    }

    StudyStore *store = m_store;
    ImportInfo info;
    if (store)
        info = [store](const QString &id) { return store->importInfo(id); };
    auto run = std::make_shared<Run>(ClaudeStreamParser(info), std::move(onEvent));
    run->folder = folder;
    m_run = run;

    if (!QFileInfo(m_claudePath).isFile()) {
        failLater(run, QStringLiteral("Claude Code wasn't found at %1.").arg(m_claudePath));
        return;
    }

    const Launch launch = resolveLaunch(m_claudePath, arguments(prompt(importIDs), QDir::toNativeSeparators(config)));
    auto *process = new QProcess(this);
    run->process = process;
    process->setProcessEnvironment(environment(m_claudePath));
    process->setWorkingDirectory(folder);
    process->setProgram(launch.program);
    process->setArguments(launch.arguments);
#ifdef Q_OS_WIN
    if (launch.viaCmd) {
        QString line = QStringLiteral("\"%1\" /d /s /c \"\"%2\"").arg(QDir::toNativeSeparators(launch.program), QDir::toNativeSeparators(m_claudePath));
        for (const QString &a : launch.arguments)
            line += QStringLiteral(" \"%1\"").arg(a);
        line += '"';
        run->cmdLine = std::make_shared<std::wstring>(line.toStdWString());
        const auto text = run->cmdLine;
        process->setCreateProcessArgumentsModifier([text](QProcess::CreateProcessArguments *args) {
            args->arguments = text->data();
        });
    }
    run->job = std::make_unique<Job>();
#endif

    connect(process, &QProcess::started, this, [this, run, process, stdinText = launch.stdinText] {
        if (!stdinText.isEmpty())
            process->write(stdinText);
        process->closeWriteChannel();
#ifdef Q_OS_WIN
        run->job->assign(process->processId());
#else
        Q_UNUSED(run)
#endif
    });
    connect(process, &QProcess::readyReadStandardOutput, this, [this, run, process] {
        output(run, process->readAllStandardOutput());
    });
    connect(process, &QProcess::readyReadStandardError, this, [run, process] {
        run->appendError(process->readAllStandardError());
    });
    connect(process, &QProcess::errorOccurred, this, [this, run, process](QProcess::ProcessError error) {
        if (error != QProcess::FailedToStart)
            return;
        const QString reason = process->errorString();
        finish(run);
        run->deliver(Event::failed(QStringLiteral("Claude Code couldn't be started: %1").arg(reason)));
    });
    connect(process, &QProcess::finished, this, [this, run](int exitCode, QProcess::ExitStatus status) {
        exited(run, exitCode, status == QProcess::CrashExit);
    });
    process->start();
}

void ClaudeCodeRunner::cancel()
{
    if (m_run)
        stop(m_run);
}

/// Delivers a failure after `start` has returned, and frees the runner for the next run.
void ClaudeCodeRunner::failLater(const std::shared_ptr<Run> &run, const QString &message)
{
    finish(run);
    QTimer::singleShot(0, this, [run, message] { run->deliver(Event::failed(message)); });
}

void ClaudeCodeRunner::output(const std::shared_ptr<Run> &run, const QByteArray &data)
{
    if (run->cancelled)
        return;
    const QList<Event> events = run->parser.feed(data);
    for (const Event &event : events)
        run->deliver(event);
    if (events.isEmpty() || !events.last().isTerminal())
        return;
    // Claude Code exits by itself after a successful result; after a failure (or if it lingers) it's stopped.
    const int delay = events.last().kind == Event::Kind::finished ? 5000 : 0;
    QTimer::singleShot(delay, this, [this, run] { stop(run); });
}

/// Kills the process and everything below it (the whole job on Windows).
void ClaudeCodeRunner::stop(const std::shared_ptr<Run> &run)
{
    if (!run->process || run->process->state() == QProcess::NotRunning || run->cancelled)
        return;
    run->cancelled = true;
#ifdef Q_OS_WIN
    if (run->job && run->job->assigned()) {
        run->job->terminate();
        return;
    }
    // No job (assignment failed): ask taskkill to take the tree down.
    QProcess::startDetached(QStringLiteral("taskkill"), {"/T", "/F", "/PID", QString::number(run->process->processId())});
#endif
    run->process->kill();
}

void ClaudeCodeRunner::exited(const std::shared_ptr<Run> &run, int exitCode, bool crashed)
{
    if (run->process) {
        const QByteArray rest = run->process->readAllStandardOutput();
        if (!rest.isEmpty())
            output(run, rest);
        run->appendError(run->process->readAllStandardError());
    }
#ifdef Q_OS_WIN
    // Whatever Claude Code left running (normally nothing).
    if (run->job)
        run->job->terminate();
#endif
    QList<Event> pending;
    if (!run->cancelled)
        pending = run->parser.finish();
    if (run->cancelled)
        pending.append(Event::failed(QStringLiteral("Cancelled")));
    else if (crashed)
        pending.append(Event::failed(QStringLiteral("Claude Code was stopped (code %1).").arg(exitCode)));
    else if (exitCode != 0) {
        const QString line = lastLine(run->errorTail);
        pending.append(Event::failed(QStringLiteral("Claude Code exited with status %1").arg(exitCode)
                                     + (line.isEmpty() ? QStringLiteral(".") : QStringLiteral(": ") + line)));
    } else
        pending.append(Event::failed(QStringLiteral("Claude Code stopped without finishing the study plan.")));
    finish(run);
    for (const Event &event : pending)
        run->deliver(event);
}

void ClaudeCodeRunner::finish(const std::shared_ptr<Run> &run)
{
    if (QProcess *process = std::exchange(run->process, nullptr)) {
        process->disconnect(this);
        process->deleteLater();
    }
#ifdef Q_OS_WIN
    run->job.reset();
#endif
    QDir(run->folder).removeRecursively();
    if (m_run == run)
        m_run.reset();
}

// MARK: - Command line

QString ClaudeCodeRunner::prompt(const QStringList &importIDs)
{
    const QString imports = importIDs.isEmpty() ? QStringLiteral("all") : importIDs.join(QStringLiteral(", "));
    return WhiteprintText::studyPlanPrompt() + QStringLiteral("\n\nImports: ") + imports;
}

QStringList ClaudeCodeRunner::arguments(const QString &prompt, const QString &configPath)
{
    return {
        "-p", prompt,
        "--output-format", "stream-json",
        "--verbose",
        "--strict-mcp-config",
        "--mcp-config", configPath,
        "--allowedTools", allowedTools,
        "--permission-mode", "dontAsk",
        "--no-session-persistence",
    };
}

QByteArray ClaudeCodeRunner::mcpConfig(const QString &helperPath)
{
    const QJsonObject server{{"command", helperPath}, {"args", QJsonArray()}};
    const QJsonObject config{{"mcpServers", QJsonObject{{"whiteprint", server}}}};
    return QJsonDocument(config).toJson(QJsonDocument::Indented);
}

QProcessEnvironment ClaudeCodeRunner::environment(const QString &claudePath)
{
    QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
    QStringList path = environment.value(QStringLiteral("PATH")).split(QDir::listSeparator(), Qt::SkipEmptyParts);
    QStringList extras{QDir::toNativeSeparators(QFileInfo(claudePath).absolutePath()),
                       QDir::toNativeSeparators(QDir::homePath() + "/.local/bin")};
    const QString appData = qEnvironmentVariable("APPDATA");
    if (!appData.isEmpty())
        extras << QDir::toNativeSeparators(appData + "/npm");
    if (!onWindows)
        extras << "/opt/homebrew/bin" << "/usr/local/bin";
    for (const QString &extra : extras) {
        const bool known = std::any_of(path.begin(), path.end(), [&](const QString &p) { return samePath(p, extra); });
        if (!known)
            path.append(extra);
    }
    environment.insert(QStringLiteral("PATH"), path.join(QDir::listSeparator()));
    return environment;
}

} // namespace wp
