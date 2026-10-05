import Foundation

/// Listens on a Unix domain socket and dispatches requests to the handler.
/// Removes a stale socket file on start, and the socket file on stop.
///
/// Socket I/O runs on a private background queue; the handler is called on
/// the main queue. Each connection may send any number of request lines and
/// gets the replies in the same order. The server keeps itself and the handler
/// alive until `stop()`.
public final class BridgeServer {
    /// Most unanswered request bytes buffered per connection. A whole study plan is well below this.
    static let maxBuffered = 64 << 20
    /// How long a reply write may stall on a client that stopped reading.
    private static let sendTimeout = timeval(tv_sec: 5, tv_usec: 0)

    private let socketURL: URL
    private var handler: BridgeHandler?
    private let queue = DispatchQueue(label: "Whiteprint.BridgeServer")
    private var listener: DispatchSourceRead?
    private var connections: [ObjectIdentifier: Connection] = [:]

    public init(socketURL: URL = BridgePaths.socket, handler: BridgeHandler) {
        self.socketURL = socketURL
        self.handler = handler
    }

    /// Starts listening. Throws if the socket can't be created or another
    /// process is already serving it. Calling it again while running does nothing.
    public func start() throws {
        try queue.sync { try listen() }
    }

    /// Closes the socket and every connection, and removes the socket file.
    /// Replies still pending are dropped.
    public func stop() {
        queue.sync {
            listener?.cancel()
            listener = nil
            for connection in connections.values {
                connection.close()
            }
            connections.removeAll()
            handler = nil
            unlink(socketURL.path)
        }
    }

    private func listen() throws {
        guard listener == nil else { return }
        let path = socketURL.path
        let addr = try UnixSocket.address(path)
        try? FileManager.default.createDirectory(at: socketURL.deletingLastPathComponent(), withIntermediateDirectories: true)
        if UnixSocket.isAccepting(path) {
            throw BridgeError.alreadyRunning(path)
        }
        unlink(path)

        let fd = try UnixSocket.make()
        guard UnixSocket.bind(fd, addr) == 0 else {
            let code = errno
            close(fd)
            throw BridgeError.system("bind", errno: code)
        }
        chmod(path, 0o600)
        guard Darwin.listen(fd, 64) == 0 else {
            let code = errno
            close(fd)
            unlink(path)
            throw BridgeError.system("listen", errno: code)
        }
        UnixSocket.setNonBlocking(fd, true)

        let source = DispatchSource.makeReadSource(fileDescriptor: fd, queue: queue)
        source.setEventHandler { self.acceptPending(on: fd) }
        source.setCancelHandler { Darwin.close(fd) }
        source.resume()
        listener = source
    }

    private func acceptPending(on listenFD: Int32) {
        while true {
            let fd = accept(listenFD, nil, nil)
            guard fd >= 0 else { return }
            // Accepted sockets inherit O_NONBLOCK; replies are written blocking,
            // bounded by the send timeout.
            UnixSocket.setNonBlocking(fd, false)
            var on: Int32 = 1
            setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, socklen_t(MemoryLayout<Int32>.size))
            var timeout = Self.sendTimeout
            setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &timeout, socklen_t(MemoryLayout<timeval>.size))
            _ = fcntl(fd, F_SETFD, FD_CLOEXEC)

            let connection = Connection(fd: fd, queue: queue)
            connections[ObjectIdentifier(connection)] = connection
            connection.onReadable = { self.read(connection) }
            connection.resume()
        }
    }

    private func read(_ connection: Connection) {
        guard connection.readAvailable() else {
            reply(.failure("request too long"), to: connection)
            connection.stopReading()
            finishIfDone(connection)
            return
        }
        processLines(connection)
    }

    private func processLines(_ connection: Connection) {
        while !connection.isBusy, let line = connection.nextLine() {
            guard !line.allSatisfy({ $0 == 0x20 || $0 == 0x0D }) else { continue }
            guard let request = try? JSONDecoder().decode(BridgeRequest.self, from: line) else {
                reply(.failure("malformed request"), to: connection)
                continue
            }
            guard let handler = handler else {
                reply(.failure("Whiteprint is shutting down"), to: connection)
                continue
            }
            let token = connection.beginRequest()
            DispatchQueue.main.async {
                handler.handle(request) { response in
                    self.queue.async { self.complete(connection, token: token, with: response) }
                }
            }
        }
        finishIfDone(connection)
    }

    private func complete(_ connection: Connection, token: Int, with response: BridgeResponse) {
        guard connection.endRequest(token) else { return }
        reply(response, to: connection)
        processLines(connection)
    }

    private func reply(_ response: BridgeResponse, to connection: Connection) {
        guard !connection.isClosed, var data = try? JSONEncoder().encode(response) else { return }
        data.append(0x0A)
        if !UnixSocket.writeAll(connection.fd, data) {
            drop(connection)
        }
    }

    private func finishIfDone(_ connection: Connection) {
        if connection.isDone {
            drop(connection)
        }
    }

    private func drop(_ connection: Connection) {
        connection.close()
        connections[ObjectIdentifier(connection)] = nil
    }
}

/// One client connection: the read source and its line buffer. Used only on the server queue.
private final class Connection {
    let fd: Int32
    var onReadable: (() -> Void)?
    private let source: DispatchSourceRead
    private var input = Data()
    /// Bytes of `input` already searched for a newline.
    private var scanned = 0
    private var reachedEOF = false
    private var suspended = false
    private var token = 0
    private(set) var isBusy = false
    private(set) var isClosed = false

    init(fd: Int32, queue: DispatchQueue) {
        self.fd = fd
        source = DispatchSource.makeReadSource(fileDescriptor: fd, queue: queue)
        source.setEventHandler { [unowned self] in self.onReadable?() }
        source.setCancelHandler { Darwin.close(fd) }
    }

    func resume() {
        source.resume()
    }

    /// Reads what's available. False when the buffered input grows past the limit.
    func readAvailable() -> Bool {
        var chunk = [UInt8](repeating: 0, count: 64 << 10)
        let n = Darwin.read(fd, &chunk, chunk.count)
        if n > 0 {
            input.append(contentsOf: chunk[0..<n])
        } else if n == 0 || (errno != EINTR && errno != EAGAIN) {
            reachedEOF = true
            stopReading()
            if !input.isEmpty && input.last != 0x0A {
                input.append(0x0A)
            }
        }
        return input.count <= BridgeServer.maxBuffered
    }

    /// The next complete line, without its newline.
    func nextLine() -> Data? {
        guard let newline = input[scanned...].firstIndex(of: 0x0A) else {
            scanned = input.count
            return nil
        }
        let line = Data(input[..<newline])
        input = Data(input[(newline + 1)...])
        scanned = 0
        return line
    }

    func beginRequest() -> Int {
        isBusy = true
        token += 1
        return token
    }

    /// Whether `token` is the request in flight (a second reply is ignored).
    func endRequest(_ token: Int) -> Bool {
        guard isBusy, token == self.token, !isClosed else { return false }
        isBusy = false
        return true
    }

    /// The client hung up and every request it sent has been answered.
    var isDone: Bool {
        isClosed || (reachedEOF && !isBusy && !input.contains(0x0A))
    }

    func stopReading() {
        guard !suspended else { return }
        source.suspend()
        suspended = true
        reachedEOF = true
    }

    func close() {
        guard !isClosed else { return }
        isClosed = true
        onReadable = nil
        if suspended {
            source.resume()
        }
        source.cancel()
    }
}
