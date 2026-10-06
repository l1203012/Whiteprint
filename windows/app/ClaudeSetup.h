#pragma once
// Port of ClaudeSetup.swift: registering the whiteprint-mcp helper with Claude Desktop and Claude Code.
#include <QByteArray>
#include <QObject>
#include <QString>
#include <QStringList>
#include <functional>
#include <optional>

namespace wp {

/// The MCP server name Whiteprint registers under, in both Claude clients.
inline const QString mcpServerName = QStringLiteral("whiteprint");

/// Adds the Whiteprint helper to Claude Desktop's config file.
namespace ClaudeDesktopConfig {

struct ConfigError {
    enum class Kind { notAnObject, invalidJSON };
    Kind kind = Kind::notAnObject;
    /// For invalidJSON: the parser's message.
    QString detail;
    QString description() const;
};

/// `%APPDATA%\Claude\claude_desktop_config.json`.
QString defaultURL();

/// `config` (nullopt or blank when there's no file yet) with `mcpServers.whiteprint` set to the helper.
/// Other servers and keys are kept. Throws ConfigError.
QByteArray merging(const QString &helper, const std::optional<QByteArray> &config);

/// Whether the config already starts this helper.
bool isConnected(const QString &helper, const QString &configURL = defaultURL());

/// Merges the helper into the config file, copying the old file to `<name>.backup` first. Returns the
/// backup's path, if there was a file. A config that isn't a JSON object is left alone (throws
/// ConfigError); I/O failures throw FileIOError.
std::optional<QString> connect(const QString &helper, const QString &configURL = defaultURL());

/// The snippet to paste by hand.
QString snippet(const QString &helper);

} // namespace ClaudeDesktopConfig

/// Registers the helper with Claude Code (`claude mcp add`).
namespace ClaudeCodeSetup {

/// `mcp add --scope user whiteprint -- <helper>`.
QStringList arguments(const QString &helper);

/// The same command as a line to paste into a terminal (arguments with spaces are double-quoted).
QString commandLine(const std::optional<QString> &claude, const QString &helper);

QString shellQuoted(const QString &word);

struct Result {
    bool ok = false;
    /// The command's output on success, or the failure message.
    QString text;
};

/// Runs `claude mcp add ...` without blocking (npm `.cmd` shims go through cmd.exe) and calls
/// `completion` on the calling thread with the result. Needs an event loop; `context` bounds the
/// callback's lifetime (it isn't called when `context` is destroyed first).
void registerHelper(const QString &claude, const QString &helper, QObject *context,
                    std::function<void(const Result &)> completion);

} // namespace ClaudeCodeSetup

} // namespace wp
