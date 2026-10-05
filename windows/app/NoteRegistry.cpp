#include "app/NoteRegistry.h"

#include "app/AppUtil.h"
#include "app/NoteFiles.h"
#include "core/TextUtil.h"

namespace wp {

QString NoteRegistry::id(const QString &forURL)
{
    const QString path = canonicalFile(forURL);
    const QString key = pathKey(path);
    const auto found = m_idsByPath.constFind(key);
    if (found != m_idsByPath.constEnd())
        return *found;
    const QString id = QStringLiteral("n%1").arg(m_next++);
    m_idsByPath.insert(key, id);
    m_urlsByID.insert(id, path);
    return id;
}

std::optional<QString> NoteRegistry::url(const QString &forID) const
{
    const auto found = m_urlsByID.constFind(trimmedWhitespace(forID).toLower());
    if (found == m_urlsByID.constEnd())
        return std::nullopt;
    return *found;
}

void NoteRegistry::move(const QString &from, const QString &to)
{
    if (pathKey(from) == pathKey(to))
        return;
    const auto snapshot = m_urlsByID;
    for (auto it = snapshot.constBegin(); it != snapshot.constEnd(); ++it) {
        const auto moved = NoteFiles::relocated(it.value(), from, to);
        if (!moved)
            continue;
        m_idsByPath.remove(pathKey(it.value()));
        m_idsByPath.insert(pathKey(*moved), it.key());
        m_urlsByID.insert(it.key(), *moved);
    }
}

} // namespace wp
