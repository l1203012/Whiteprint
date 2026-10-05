#include "bridge/BridgeServer.h"
#include "bridge/LocalPipe.h"
#include "core/JsonCoding.h"

#include <QJsonDocument>
#include <QLocalSocket>
#include <QPointer>

namespace wp {

/// One client connection and its line buffer. Used only on the server's thread.
struct BridgeServer::Connection {
    quint64 id = 0;
    QLocalSocket *socket = nullptr;
    QByteArray input;
    /// Bytes of `input` already searched for a newline.
    qsizetype scanned = 0;
    bool reachedEOF = false;
    bool isBusy = false;
    bool isClosed = false;
    int token = 0;

    /// The next complete line, without its newline.
    bool nextLine(QByteArray &line)
    {
        const qsizetype newline = input.indexOf('\n', scanned);
        if (newline < 0) {
            scanned = input.size();
            return false;
        }
        line = input.left(newline);
        input.remove(0, newline + 1);
        scanned = 0;
        return true;
    }

    int beginRequest()
    {
        isBusy = true;
        return ++token;
    }

    /// Whether `t` is the request in flight (a second reply is ignored).
    bool endRequest(int t)
    {
        if (!isBusy || t != token || isClosed)
            return false;
        isBusy = false;
        return true;
    }

    /// The client hung up and every request it sent has been answered.
    bool isDone() const { return isClosed || (reachedEOF && !isBusy && !input.contains('\n')); }
};

BridgeServer::BridgeServer(BridgeHandler *handler, QString pipeName, QObject *parent)
    : QObject(parent), handler_(handler), pipeName_(std::move(pipeName)), server_(this)
{
    server_.setSocketOptions(QLocalServer::UserAccessOption);
    server_.setMaxPendingConnections(64);
    connect(&server_, &QLocalServer::newConnection, this, &BridgeServer::acceptPending);
}

BridgeServer::~BridgeServer()
{
    stop();
}

void BridgeServer::start()
{
    if (server_.isListening())
        return;
    LocalPipe::validate(pipeName_);
    if (LocalPipe::isAccepting(pipeName_))
        throw BridgeError::alreadyRunning(pipeName_);
    QLocalServer::removeServer(pipeName_);
    if (!server_.listen(pipeName_))
        throw BridgeError::system(QStringLiteral("listen"), server_.errorString());
}

void BridgeServer::stop()
{
    server_.close();
    const auto all = connections_.values();
    connections_.clear();
    for (const auto &c : all) {
        c->isClosed = true;
        if (c->socket) {
            c->socket->disconnect(this);
            c->socket->abort();
            delete c->socket;
            c->socket = nullptr;
        }
    }
}

void BridgeServer::acceptPending()
{
    while (QLocalSocket *socket = server_.nextPendingConnection()) {
        auto c = std::make_shared<Connection>();
        c->id = ++nextID_;
        c->socket = socket;
        socket->setParent(this);
        connections_.insert(c->id, c);
        const quint64 id = c->id;
        connect(socket, &QLocalSocket::readyRead, this, [this, id] { readable(id); });
        connect(socket, &QLocalSocket::disconnected, this, [this, id] { peerClosed(id); });
        // Bytes may have arrived before the signals were connected.
        if (socket->bytesAvailable() > 0)
            QMetaObject::invokeMethod(this, [this, id] { readable(id); }, Qt::QueuedConnection);
    }
}

void BridgeServer::readable(quint64 id)
{
    const auto c = connections_.value(id);
    if (!c || c->isClosed || !c->socket || c->reachedEOF)
        return;
    c->input.append(c->socket->readAll());
    if (c->input.size() > maxBuffered) {
        reply(c, BridgeResponse::failure(QStringLiteral("request too long")));
        c->reachedEOF = true;
        c->socket->disconnect(this);
        finishIfDone(c);
        return;
    }
    processLines(c);
}

void BridgeServer::peerClosed(quint64 id)
{
    const auto c = connections_.value(id);
    if (!c || c->isClosed)
        return;
    if (c->socket && c->socket->bytesAvailable() > 0)
        c->input.append(c->socket->readAll());
    c->reachedEOF = true;
    if (!c->input.isEmpty() && !c->input.endsWith('\n'))
        c->input.append('\n');
    processLines(c);
}

void BridgeServer::processLines(const std::shared_ptr<Connection> &c)
{
    QByteArray line;
    while (!c->isClosed && !c->isBusy && c->nextLine(line)) {
        bool blank = true;
        for (char ch : std::as_const(line)) {
            if (ch != ' ' && ch != '\r') {
                blank = false;
                break;
            }
        }
        if (blank)
            continue;

        std::optional<BridgeRequest> request;
        QJsonParseError parseError;
        const QJsonDocument doc = QJsonDocument::fromJson(line, &parseError);
        if (parseError.error == QJsonParseError::NoError && doc.isObject()) {
            try {
                request = BridgeRequest::fromJson(doc.object());
            } catch (const DecodingError &) {
            }
        }
        if (!request) {
            reply(c, BridgeResponse::failure(QStringLiteral("malformed request")));
            continue;
        }
        if (!handler_ || !server_.isListening()) {
            reply(c, BridgeResponse::failure(QStringLiteral("Whiteprint is shutting down")));
            continue;
        }

        const int token = c->beginRequest();
        const quint64 id = c->id;
        QPointer<BridgeServer> guard(this);
        // The handler runs on the next turn of the event loop, never inside this call.
        QMetaObject::invokeMethod(
            this,
            [this, guard, request = std::move(*request), id, token] {
                if (!guard || !handler_ || !server_.isListening()) {
                    return;
                }
                handler_->handle(request, [guard, id, token](BridgeResponse response) {
                    if (!guard)
                        return;
                    QMetaObject::invokeMethod(
                        guard.data(),
                        [guard, id, token, response = std::move(response)] {
                            if (guard)
                                guard->complete(id, token, response);
                        },
                        Qt::QueuedConnection);
                });
            },
            Qt::QueuedConnection);
    }
    finishIfDone(c);
}

void BridgeServer::complete(quint64 id, int token, const BridgeResponse &response)
{
    const auto c = connections_.value(id);
    if (!c || !c->endRequest(token))
        return;
    reply(c, response);
    processLines(c);
}

bool BridgeServer::reply(const std::shared_ptr<Connection> &c, const BridgeResponse &response)
{
    if (c->isClosed || !c->socket)
        return false;
    if (c->socket->state() != QLocalSocket::ConnectedState) {
        drop(c);
        return false;
    }
    QByteArray data = QJsonDocument(response.toJson()).toJson(QJsonDocument::Compact);
    data.append('\n');
    if (c->socket->write(data) < 0) {
        drop(c);
        return false;
    }
    return true;
}

void BridgeServer::finishIfDone(const std::shared_ptr<Connection> &c)
{
    if (c->isDone())
        drop(c);
}

void BridgeServer::drop(const std::shared_ptr<Connection> &c)
{
    if (c->isClosed && !c->socket)
        return;
    c->isClosed = true;
    connections_.remove(c->id);
    if (QLocalSocket *socket = c->socket) {
        c->socket = nullptr;
        socket->disconnect(this);
        // Let buffered replies flush before closing; then free it.
        if (socket->state() == QLocalSocket::ConnectedState) {
            connect(socket, &QLocalSocket::bytesWritten, socket, [socket] {
                if (socket->bytesToWrite() == 0)
                    socket->disconnectFromServer();
            });
            connect(socket, &QLocalSocket::disconnected, socket, &QObject::deleteLater);
            if (socket->bytesToWrite() == 0)
                socket->disconnectFromServer();
        } else {
            socket->deleteLater();
        }
    }
}

} // namespace wp
