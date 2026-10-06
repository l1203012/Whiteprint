#include "bridge/LocalPipe.h"
#include "bridge/Bridge.h"

#include <QLocalSocket>

namespace wp::LocalPipe {

void validate(const QString &name)
{
    if (name.isEmpty() || name.size() > maxNameLength)
        throw BridgeError::pathTooLong(name);
}

bool isAccepting(const QString &name, int timeoutMs)
{
    if (name.isEmpty() || name.size() > maxNameLength)
        return false;
    QLocalSocket socket;
    socket.connectToServer(name);
    const bool connected = socket.waitForConnected(timeoutMs);
    socket.abort();
    return connected;
}

} // namespace wp::LocalPipe
