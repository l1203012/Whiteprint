#pragma once
#include "bridge/Bridge.h"

#include <QByteArray>
#include <QHash>
#include <QLocalServer>
#include <QObject>
#include <memory>

class QLocalSocket;

namespace wp {

/// Listens on a named pipe (`\\.\pipe\<name>`, restricted to the current user) and dispatches
/// requests to the handler. Lives on the thread that created it (the main thread in the app),
/// which is also where the handler is called; replies may come from any thread.
///
/// Each connection may send any number of request lines and gets the replies in the same order.
/// The handler is not owned; keep it alive until `stop()`.
class BridgeServer : public QObject
{
    Q_OBJECT

public:
    /// Most unanswered request bytes buffered per connection. A whole study plan is well below this.
    static constexpr qint64 maxBuffered = 64 << 20;

    explicit BridgeServer(BridgeHandler *handler, QString pipeName = BridgePaths::socketName(),
                          QObject *parent = nullptr);
    ~BridgeServer() override;

    /// Starts listening. Throws BridgeError if the name is too long, the pipe can't be created
    /// or another process is already serving it. Calling it again while running does nothing.
    void start();

    /// Closes the pipe and every connection. Replies still pending are dropped.
    void stop();

    bool isListening() const { return server_.isListening(); }
    const QString &pipeName() const { return pipeName_; }

private:
    struct Connection;

    void acceptPending();
    void readable(quint64 id);
    void peerClosed(quint64 id);
    void processLines(const std::shared_ptr<Connection> &c);
    void complete(quint64 id, int token, const BridgeResponse &response);
    bool reply(const std::shared_ptr<Connection> &c, const BridgeResponse &response);
    void finishIfDone(const std::shared_ptr<Connection> &c);
    void drop(const std::shared_ptr<Connection> &c);

    BridgeHandler *handler_;
    QString pipeName_;
    QLocalServer server_;
    QHash<quint64, std::shared_ptr<Connection>> connections_;
    quint64 nextID_ = 0;
};

} // namespace wp
