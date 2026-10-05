#pragma once
// Port of ClaudeCodeRunner.Event: what a study run reports to the UI.
#include <QString>

namespace wp {

struct RunnerEvent {
    enum class Kind { status, text, finished, failed };

    Kind kind = Kind::status;
    QString message;

    /// Short human-readable progress, e.g. "Reading chunk 3 of 12".
    static RunnerEvent status(QString text) { return {Kind::status, std::move(text)}; }
    /// Assistant text, streamed.
    static RunnerEvent text(QString text) { return {Kind::text, std::move(text)}; }
    static RunnerEvent finished() { return {Kind::finished, {}}; }
    static RunnerEvent failed(QString reason) { return {Kind::failed, std::move(reason)}; }

    bool isTerminal() const { return kind == Kind::finished || kind == Kind::failed; }
    bool operator==(const RunnerEvent &) const = default;
};

} // namespace wp
