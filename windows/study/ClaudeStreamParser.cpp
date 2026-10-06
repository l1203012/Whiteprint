#include "study/ClaudeStreamParser.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <utility>

namespace wp {

ClaudeStreamParser::ClaudeStreamParser(ImportInfo importInfo) : m_importInfo(std::move(importInfo)) {}

QList<RunnerEvent> ClaudeStreamParser::feed(const QByteArray &data)
{
    m_buffer.append(data);
    QList<Event> events;
    qsizetype newline;
    while ((newline = m_buffer.indexOf('\n')) >= 0) {
        events += parse(m_buffer.left(newline));
        m_buffer.remove(0, newline + 1);
    }
    return events;
}

QList<RunnerEvent> ClaudeStreamParser::finish()
{
    const QByteArray rest = std::exchange(m_buffer, QByteArray());
    return rest.isEmpty() ? QList<Event>() : parse(rest);
}

QList<RunnerEvent> ClaudeStreamParser::parse(const QByteArray &line)
{
    if (m_finished)
        return {};
    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(line, &error);
    if (error.error != QJsonParseError::NoError || !doc.isObject())
        return {};
    const QJsonObject message = doc.object();
    const QString type = message.value("type").toString();
    if (type == "system") return system(message);
    if (type == "assistant") return assistant(message);
    if (type == "result") return {result(message)};
    return {};
}

QList<RunnerEvent> ClaudeStreamParser::system(const QJsonObject &message)
{
    if (message.value("subtype").toString() != "init")
        return {};
    for (const QJsonValue &server : message.value("mcp_servers").toArray()) {
        const QJsonObject s = server.toObject();
        if (s.value("name").toString() == "whiteprint") {
            if (s.value("status").toString() == "failed") {
                m_finished = true;
                return {Event::failed(QStringLiteral("Claude Code couldn't start Whiteprint's tools. Try reinstalling Whiteprint."))};
            }
            break;
        }
    }
    return {Event::status(QStringLiteral("Starting Claude…"))};
}

QList<RunnerEvent> ClaudeStreamParser::assistant(const QJsonObject &message) const
{
    QList<Event> events;
    const QJsonArray content = message.value("message").toObject().value("content").toArray();
    for (const QJsonValue &value : content) {
        const QJsonObject block = value.toObject();
        const QString type = block.value("type").toString();
        if (type == "text") {
            const QString text = block.value("text").toString().trimmed();
            if (!text.isEmpty())
                events.append(Event::text(text));
        } else if (type == "tool_use") {
            events.append(Event::status(
                ToolStatus::text(block.value("name").toString(), block.value("input").toObject(), m_importInfo)));
        }
    }
    return events;
}

RunnerEvent ClaudeStreamParser::result(const QJsonObject &message)
{
    m_finished = true;
    const bool isError = message.value("is_error").toBool(false);
    const QString subtype = message.value("subtype").toString();
    if (!isError && subtype == "success")
        return Event::finished();
    QString text = message.value("result").toString();
    if (!message.value("result").isString()) {
        const QJsonArray errors = message.value("errors").toArray();
        text = errors.isEmpty() ? QString() : errors.first().toString();
    }
    text = text.trimmed();
    if (!text.isEmpty())
        return Event::failed(text);
    if (subtype == "error_max_turns")
        return Event::failed(QStringLiteral("Claude stopped after too many steps. Run it again to continue."));
    if (subtype == "error_max_budget_usd")
        return Event::failed(QStringLiteral("Claude stopped at its spending limit."));
    return Event::failed(QStringLiteral("Claude Code ran into an error (%1).")
                             .arg(subtype.isEmpty() ? QStringLiteral("unknown") : subtype));
}

} // namespace wp
