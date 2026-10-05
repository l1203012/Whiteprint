#pragma once
#include <QString>
#include <QStringView>
#include <optional>

namespace wp {

// Small string helpers that reproduce the Swift/Foundation behaviour the core relies on.

/// Foundation's `trimmingCharacters(in: .whitespaces)`: spaces, tabs and other Zs characters.
QString trimmedWhitespace(QStringView text);
/// True for every line break Swift's `Character.isNewline` accepts.
bool isNewlineChar(QChar c);
/// Swift's `Int(String)`: optional sign and ASCII digits only.
std::optional<qint64> swiftInt(QStringView text);
/// Number of user-perceived characters (Swift's `String.count`).
int graphemeCount(QStringView text);
/// Swift's `String(Double)` for finite values, e.g. `1.4` (integral values give `5`, not `5.0`).
QString formatDouble(double value);
/// Swift's `Character.isLetter` / `isNumber` on a code point.
bool isLetterCodePoint(char32_t c);
bool isNumberCodePoint(char32_t c);
/// Decodes the code point at `index` (handles surrogate pairs) and reports its UTF-16 length.
char32_t codePointAt(QStringView text, qsizetype index, int *length = nullptr);

} // namespace wp
