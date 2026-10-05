#include "app/AppUtil.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QStringDecoder>
#include <QTextBoundaryFinder>

namespace wp {

QString canonicalFile(const QString &path)
{
    if (path.isEmpty())
        return {};
    const QString absolute = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
    QString head = absolute;
    QString tail;
    for (;;) {
        if (head.size() == 2 && head.at(1) == QLatin1Char(':'))
            head += QLatin1Char('/');
        const QFileInfo info(head);
        if (info.exists()) {
            QString resolved = info.canonicalFilePath();
            if (resolved.isEmpty())
                resolved = head;
            return tail.isEmpty() ? resolved : QDir::cleanPath(resolved + QLatin1Char('/') + tail);
        }
        const qsizetype slash = head.lastIndexOf(QLatin1Char('/'));
        if (slash <= 0 || (head.size() == 3 && head.at(1) == QLatin1Char(':')))
            return absolute;
        const QString name = head.mid(slash + 1);
        tail = tail.isEmpty() ? name : name + QLatin1Char('/') + tail;
        head = head.left(slash);
    }
}

QString pathKey(const QString &path)
{
    const QString canonical = canonicalFile(path);
#ifdef Q_OS_WIN
    return canonical.toLower();
#else
    return canonical;
#endif
}

QStringList pathComponents(const QString &path)
{
    return canonicalFile(path).split(QLatin1Char('/'), Qt::SkipEmptyParts);
}

bool samePathPart(const QString &a, const QString &b)
{
#ifdef Q_OS_WIN
    return a.compare(b, Qt::CaseInsensitive) == 0;
#else
    return a == b;
#endif
}

QString expandingTildeInPath(const QString &path)
{
    if (path == QLatin1String("~"))
        return QDir::homePath();
    if (path.startsWith(QLatin1String("~/")) || path.startsWith(QLatin1String("~\\")))
        return QDir::homePath() + QLatin1Char('/') + path.mid(2);
    return path;
}

QString deletingLastPathComponent(const QString &path)
{
    const QString cleaned = QDir::cleanPath(path);
    const qsizetype slash = cleaned.lastIndexOf(QLatin1Char('/'));
    if (slash < 0)
        return QStringLiteral(".");
    if (slash == 0)
        return QStringLiteral("/");
    QString parent = cleaned.left(slash);
    if (parent.size() == 2 && parent.at(1) == QLatin1Char(':'))
        parent += QLatin1Char('/');
    return parent;
}

QString appendingPathComponent(const QString &dir, const QString &name)
{
    return QDir::cleanPath(dir + QLatin1Char('/') + name);
}

void writeFileAtomically(const QString &path, const QByteArray &data)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        throw FileIOError(QStringLiteral("“%1” can’t be written: %2").arg(QFileInfo(path).fileName(), file.errorString()));
    if (file.write(data) != data.size() || !file.commit())
        throw FileIOError(QStringLiteral("“%1” can’t be written: %2").arg(QFileInfo(path).fileName(), file.errorString()));
}

std::optional<QString> decodeUtf8(const QByteArray &data)
{
    QStringDecoder decoder(QStringDecoder::Utf8, QStringConverter::Flag::Stateless);
    const QString text = decoder.decode(data);
    if (decoder.hasError())
        return std::nullopt;
    return text;
}

std::optional<QString> readUtf8File(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    return decodeUtf8(file.readAll());
}

QString prefixGraphemes(const QString &text, int count)
{
    QTextBoundaryFinder finder(QTextBoundaryFinder::Grapheme, text);
    qsizetype end = 0;
    for (int i = 0; i < count; ++i) {
        const qsizetype next = finder.toNextBoundary();
        if (next < 0)
            return text;
        end = next;
    }
    return text.left(end);
}

QString capitalizedFirst(const QString &text)
{
    if (text.isEmpty())
        return text;
    return text.left(1).toUpper() + text.mid(1);
}

} // namespace wp
