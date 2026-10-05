import Foundation

/// Thin wrappers over the BSD socket calls shared by the server and the client.
enum UnixSocket {
    /// A new `AF_UNIX` stream socket that never raises SIGPIPE and isn't inherited by children.
    static func make() throws -> Int32 {
        let fd = socket(AF_UNIX, SOCK_STREAM, 0)
        guard fd >= 0 else { throw BridgeError.system("socket", errno: errno) }
        var on: Int32 = 1
        setsockopt(fd, SOL_SOCKET, SO_NOSIGPIPE, &on, socklen_t(MemoryLayout<Int32>.size))
        _ = fcntl(fd, F_SETFD, FD_CLOEXEC)
        return fd
    }

    static func address(_ path: String) throws -> sockaddr_un {
        var addr = sockaddr_un()
        let bytes = Array(path.utf8)
        let capacity = MemoryLayout.size(ofValue: addr.sun_path)
        guard bytes.count < capacity else { throw BridgeError.pathTooLong(path) }
        addr.sun_family = sa_family_t(AF_UNIX)
        addr.sun_len = UInt8(MemoryLayout<sockaddr_un>.size)
        withUnsafeMutableBytes(of: &addr.sun_path) { buffer in
            buffer.copyBytes(from: bytes)
        }
        return addr
    }

    static func bind(_ fd: Int32, _ addr: sockaddr_un) -> Int32 {
        withSockaddr(addr) { Darwin.bind(fd, $0, $1) }
    }

    static func connect(_ fd: Int32, _ addr: sockaddr_un) -> Int32 {
        withSockaddr(addr) { Darwin.connect(fd, $0, $1) }
    }

    static func setNonBlocking(_ fd: Int32, _ on: Bool) {
        let flags = fcntl(fd, F_GETFL)
        _ = fcntl(fd, F_SETFL, on ? flags | O_NONBLOCK : flags & ~O_NONBLOCK)
    }

    /// Whether something is accepting connections on `path` right now.
    static func isAccepting(_ path: String) -> Bool {
        guard let addr = try? address(path), let fd = try? make() else { return false }
        defer { close(fd) }
        return connect(fd, addr) == 0
    }

    /// Writes all of `data`, retrying on partial writes. False on error or send timeout.
    static func writeAll(_ fd: Int32, _ data: Data) -> Bool {
        data.withUnsafeBytes { (buffer: UnsafeRawBufferPointer) -> Bool in
            guard let base = buffer.baseAddress else { return true }
            var offset = 0
            while offset < buffer.count {
                let n = write(fd, base + offset, buffer.count - offset)
                if n > 0 {
                    offset += n
                } else if n < 0 && errno == EINTR {
                    continue
                } else {
                    return false
                }
            }
            return true
        }
    }

    private static func withSockaddr(_ addr: sockaddr_un, _ body: (UnsafePointer<sockaddr>, socklen_t) -> Int32) -> Int32 {
        var addr = addr
        return withUnsafePointer(to: &addr) {
            $0.withMemoryRebound(to: sockaddr.self, capacity: 1) {
                body($0, socklen_t(MemoryLayout<sockaddr_un>.size))
            }
        }
    }
}
