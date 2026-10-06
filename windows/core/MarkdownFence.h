#pragma once
#include <QChar>
#include <QString>
#include <QStringList>
#include <QStringView>
#include <optional>

namespace wp {

/// A CommonMark fenced code block opener (```` ``` ```` or `~~~`).
///
/// The note parser has to track fences so a `+++page` line or a ```` ```wp ````
/// line inside someone's code block isn't mistaken for structure.
struct MarkdownFence {
    QChar marker;
    int length = 0;
    QString info;

    bool operator==(const MarkdownFence &) const = default;

    /// Parses `line` as an opening fence, or returns nullopt if it isn't one.
    static std::optional<MarkdownFence> opening(QStringView line);

    bool isClosed(QStringView line) const;

    /// The info string split on spaces and tabs.
    QStringList infoWords() const;

    /// Reads `key=value` from the info string, e.g. `id` from ```` ```wp id=d1 ````.
    std::optional<QString> attribute(const QString &key) const;

    /// Returns the fence left open at the end of `text`, if any.
    static std::optional<MarkdownFence> unclosed(const QString &text);
};

} // namespace wp
