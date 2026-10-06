#pragma once
#include <QString>

namespace wp {

/// Thrown by `Note::parsing`. Only an unreadable version fails; everything else parses leniently.
struct NoteFormatError {
    enum class Kind {
        /// The file was written by a newer Whiteprint.
        unsupportedVersion,
        /// The `whiteprint:` field isn't a number.
        invalidVersion,
    };

    Kind kind = Kind::invalidVersion;
    /// For unsupportedVersion.
    int version = 0;
    /// For invalidVersion: the field's text.
    QString text;

    static NoteFormatError unsupportedVersion(int version) { return {Kind::unsupportedVersion, version, {}}; }
    static NoteFormatError invalidVersion(QString text) { return {Kind::invalidVersion, 0, std::move(text)}; }
    bool operator==(const NoteFormatError &) const = default;

    QString description() const
    {
        return kind == Kind::unsupportedVersion
            ? QStringLiteral("this note was written by a newer Whiteprint (format version %1)").arg(version)
            : QStringLiteral("invalid format version '%1'").arg(text);
    }
};

} // namespace wp
