#pragma once
// Port of ClaudeCodeRunner.swift.
#include "study/ClaudeStreamParser.h"
#include "study/RunnerEvent.h"
#include "study/StudyStore.h"

#include <QObject>
#include <QProcessEnvironment>
#include <QStringList>
#include <functional>
#include <memory>
#include <optional>

namespace wp {

/// Runs the user's own logged-in Claude Code headlessly (`claude -p`) with the Whiteprint MCP server,
/// so the study plan uses their subscription.
///
/// Lives on the thread that created it (normally the GUI thread) and never blocks it: the process is
/// driven by QProcess and events are delivered by calling the handler passed to `start` on that thread.
class ClaudeCodeRunner : public QObject {
    Q_OBJECT
public:
    using Event = RunnerEvent;
    using EventHandler = std::function<void(const Event &)>;

    /// Looks for `claude` on PATH and in the usual Windows install locations (see `candidatePaths`).
    /// Does file system checks only, so it is quick; still fine to call off the GUI thread.
    static std::optional<QString> locateClaude();
    /// Where Claude Code is usually installed, in the order they are tried after PATH:
    /// `%USERPROFILE%\.local\bin\claude.exe` (native installer), `%USERPROFILE%\.claude\local\claude.exe`
    /// and `.cmd`, `%APPDATA%\npm\claude.cmd` (npm global), `%LOCALAPPDATA%\Programs\claude\claude.exe`.
    static QStringList candidatePaths();

    /// `claudePath` is a `claude.exe`, or an npm `claude.cmd` shim. `store` (optional, must outlive the
    /// runner) names files and chunk counts in progress messages.
    explicit ClaudeCodeRunner(QString claudePath, StudyStore *store = nullptr, QObject *parent = nullptr);
    ~ClaudeCodeRunner() override;

    const QString &claudePath() const { return m_claudePath; }
    bool isRunning() const { return m_run != nullptr; }

    /// Starts `claude -p <prompt> --output-format stream-json --verbose --strict-mcp-config --mcp-config
    /// <file> --allowedTools mcp__whiteprint__*,... --permission-mode dontAsk --no-session-persistence`
    /// for the given imports (see `arguments`), in a temporary directory holding the MCP config.
    /// `onEvent` is called on this object's thread, ending with exactly one `finished` or `failed`.
    /// Throws StudyError::alreadyRunning while a run is in progress.
    void start(const QStringList &importIDs, const QString &helperPath, EventHandler onEvent);

    /// Stops the run and everything it started (Claude Code and the MCP helper). The run then ends
    /// with `failed("Cancelled")`.
    void cancel();

    // Command line, public for tests.
    /// Whiteprint's MCP tools, plus reading the `whiteprint://dsl` resource.
    static const QString allowedTools;
    static QString prompt(const QStringList &importIDs);
    /// Anything not allowed is refused without asking (`dontAsk`), so Claude can't wander off into the
    /// user's files or shell.
    static QStringList arguments(const QString &prompt, const QString &configPath);
    static QByteArray mcpConfig(const QString &helperPath);
    /// The app's environment with Claude's folder, `~\.local\bin` and the npm folder on PATH.
    static QProcessEnvironment environment(const QString &claudePath);

private:
    struct Run;
    void output(const std::shared_ptr<Run> &run, const QByteArray &data);
    void exited(const std::shared_ptr<Run> &run, int exitCode, bool crashed);
    void stop(const std::shared_ptr<Run> &run);
    void finish(const std::shared_ptr<Run> &run);
    void failLater(const std::shared_ptr<Run> &run, const QString &message);

    QString m_claudePath;
    StudyStore *m_store;
    std::shared_ptr<Run> m_run;
};

} // namespace wp
