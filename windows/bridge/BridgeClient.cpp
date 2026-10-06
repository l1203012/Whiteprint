#include "bridge/BridgeClient.h"
#include "bridge/LocalPipe.h"
#include "core/JsonCoding.h"

#include <QElapsedTimer>
#include <QJsonDocument>
#include <QLocalSocket>
#include <algorithm>

namespace wp {

namespace {

int remainingMs(const QElapsedTimer &timer, double timeoutSeconds)
{
    const double left = timeoutSeconds * 1000.0 - static_cast<double>(timer.elapsed());
    return left <= 0 ? 0 : static_cast<int>(std::min(left, 2.0e9) + 0.999);
}

} // namespace

BridgeResponse BridgeClient::send(const BridgeRequest &request, double timeoutSeconds) const
{
    LocalPipe::validate(pipeName_);
    QElapsedTimer timer;
    timer.start();

    QLocalSocket socket;
    socket.connectToServer(pipeName_);
    if (socket.state() != QLocalSocket::ConnectedState && !socket.waitForConnected(remainingMs(timer, timeoutSeconds))) {
        switch (socket.error()) {
        case QLocalSocket::ServerNotFoundError:
        case QLocalSocket::ConnectionRefusedError:
            throw BridgeError::appNotRunning();
        case QLocalSocket::SocketTimeoutError:
            throw BridgeError::timedOut(timeoutSeconds);
        default:
            throw BridgeError::system(QStringLiteral("connect"), socket.errorString());
        }
    }

    QByteArray line = QJsonDocument(request.toJson()).toJson(QJsonDocument::Compact);
    line.append('\n');
    socket.write(line);
    while (socket.bytesToWrite() > 0) {
        if (!socket.waitForBytesWritten(std::max(1, remainingMs(timer, timeoutSeconds)))) {
            if (remainingMs(timer, timeoutSeconds) == 0)
                throw BridgeError::timedOut(timeoutSeconds);
            throw BridgeError::badReply(QStringLiteral("Whiteprint closed the connection"));
        }
    }

    QByteArray received;
    for (;;) {
        const int newline = received.indexOf('\n');
        if (newline >= 0) {
            received.truncate(newline);
            break;
        }
        if (socket.bytesAvailable() == 0) {
            if (socket.state() != QLocalSocket::ConnectedState)
                throw BridgeError::badReply(QStringLiteral("Whiteprint closed the connection"));
            if (remainingMs(timer, timeoutSeconds) == 0)
                throw BridgeError::timedOut(timeoutSeconds);
            if (!socket.waitForReadyRead(std::max(1, remainingMs(timer, timeoutSeconds)))
                && socket.bytesAvailable() == 0) {
                if (socket.state() != QLocalSocket::ConnectedState)
                    throw BridgeError::badReply(QStringLiteral("Whiteprint closed the connection"));
                if (remainingMs(timer, timeoutSeconds) == 0)
                    throw BridgeError::timedOut(timeoutSeconds);
            }
        }
        received.append(socket.readAll());
    }

    QJsonParseError parseError;
    const QJsonDocument doc = QJsonDocument::fromJson(received, &parseError);
    try {
        if (parseError.error != QJsonParseError::NoError || !doc.isObject())
            throw DecodingError{QStringLiteral("not an object")};
        return BridgeResponse::fromJson(doc.object());
    } catch (const DecodingError &) {
        throw BridgeError::badReply(QStringLiteral("not a response"));
    }
}

} // namespace wp
