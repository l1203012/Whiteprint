#pragma once
// Port of ClaudeStreamParser.swift.
#include "study/RunnerEvent.h"
#include "study/ToolStatus.h"

#include <QByteArray>
#include <QList>

namespace wp {

/// Turns Claude Code's `--output-format stream-json` output (one JSON object per line) into runner
/// events. Feed it bytes as they arrive; partial lines are buffered until their newline. Lines that
/// aren't JSON are ignored.
class ClaudeStreamParser {
public:
    using Event = RunnerEvent;

    /// `importInfo` looks up an import for progress messages like "Reading chunk 3 of 12 · Lecture3.pptx".
    explicit ClaudeStreamParser(ImportInfo importInfo = {});

    QList<Event> feed(const QByteArray &data);
    /// Parses whatever is left after the stream closed.
    QList<Event> finish();
    QList<Event> parse(const QByteArray &line);

    /// True once a `result` message (or a fatal init problem) was seen.
    bool isFinished() const { return m_finished; }

private:
    QList<Event> system(const QJsonObject &message);
    QList<Event> assistant(const QJsonObject &message) const;
    Event result(const QJsonObject &message);

    ImportInfo m_importInfo;
    QByteArray m_buffer;
    bool m_finished = false;
};

} // namespace wp
