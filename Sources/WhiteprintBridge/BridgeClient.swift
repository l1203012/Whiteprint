import Foundation

/// Used by `whiteprint-mcp`. Blocking; one request at a time.
///
/// Each `send` opens a fresh connection, so the app may restart between calls.
/// Throws `BridgeError.appNotRunning` when nothing listens on the socket (the
/// request was not delivered and can be retried), `.timedOut` when the app
/// doesn't reply in time.
public final class BridgeClient {
    private let socketURL: URL

    public init(socketURL: URL = BridgePaths.socket) {
        self.socketURL = socketURL
    }

    public func send(_ request: BridgeRequest, timeout: TimeInterval = 120) throws -> BridgeResponse {
        let deadline = Date().addingTimeInterval(timeout)
        let fd = try connect(deadline: deadline, timeout: timeout)
        defer { close(fd) }

        var line = try JSONEncoder().encode(request)
        line.append(0x0A)
        try write(line, to: fd, deadline: deadline, timeout: timeout)
        let reply = try readLine(from: fd, deadline: deadline, timeout: timeout)
        do {
            return try JSONDecoder().decode(BridgeResponse.self, from: reply)
        } catch {
            throw BridgeError.badReply("not a response")
        }
    }

    private func connect(deadline: Date, timeout: TimeInterval) throws -> Int32 {
        let addr = try UnixSocket.address(socketURL.path)
        let fd = try UnixSocket.make()
        UnixSocket.setNonBlocking(fd, true)
        if UnixSocket.connect(fd, addr) == 0 {
            return fd
        }
        let code = errno
        switch code {
        case ENOENT, ECONNREFUSED, ENOTDIR, ENOTSOCK:
            close(fd)
            throw BridgeError.appNotRunning
        case EINPROGRESS, EAGAIN:
            do {
                try wait(fd, for: Int16(POLLOUT), deadline: deadline, timeout: timeout)
            } catch {
                close(fd)
                throw error
            }
            var error: Int32 = 0
            var length = socklen_t(MemoryLayout<Int32>.size)
            getsockopt(fd, SOL_SOCKET, SO_ERROR, &error, &length)
            guard error == 0 else {
                close(fd)
                throw error == ECONNREFUSED ? BridgeError.appNotRunning : BridgeError.system("connect", errno: error)
            }
            return fd
        default:
            close(fd)
            throw BridgeError.system("connect", errno: code)
        }
    }

    private func write(_ data: Data, to fd: Int32, deadline: Date, timeout: TimeInterval) throws {
        try data.withUnsafeBytes { (buffer: UnsafeRawBufferPointer) in
            guard let base = buffer.baseAddress else { return }
            var offset = 0
            while offset < buffer.count {
                let n = Darwin.write(fd, base + offset, buffer.count - offset)
                if n > 0 {
                    offset += n
                } else if n < 0 && errno == EAGAIN {
                    try wait(fd, for: Int16(POLLOUT), deadline: deadline, timeout: timeout)
                } else if n < 0 && errno == EINTR {
                    continue
                } else {
                    throw BridgeError.badReply("Whiteprint closed the connection")
                }
            }
        }
    }

    private func readLine(from fd: Int32, deadline: Date, timeout: TimeInterval) throws -> Data {
        var received = Data()
        var chunk = [UInt8](repeating: 0, count: 64 << 10)
        while true {
            let n = Darwin.read(fd, &chunk, chunk.count)
            if n > 0 {
                let start = received.count
                received.append(contentsOf: chunk[0..<n])
                if let newline = received[start...].firstIndex(of: 0x0A) {
                    return Data(received[..<newline])
                }
            } else if n == 0 {
                throw BridgeError.badReply("Whiteprint closed the connection")
            } else if errno == EAGAIN {
                try wait(fd, for: Int16(POLLIN), deadline: deadline, timeout: timeout)
            } else if errno != EINTR {
                throw BridgeError.system("read", errno: errno)
            }
        }
    }

    private func wait(_ fd: Int32, for events: Int16, deadline: Date, timeout: TimeInterval) throws {
        while true {
            let remaining = deadline.timeIntervalSinceNow
            guard remaining > 0 else { throw BridgeError.timedOut(timeout) }
            var pfd = pollfd(fd: fd, events: events, revents: 0)
            let ready = poll(&pfd, 1, Int32(min(remaining * 1000, Double(Int32.max)).rounded(.up)))
            if ready > 0 { return }
            if ready < 0 && errno != EINTR { throw BridgeError.system("poll", errno: errno) }
        }
    }
}
