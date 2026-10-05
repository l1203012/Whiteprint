import Foundation

/// Runs the latest scheduled action at most once per `delay`.
final class ChangeCoalescer {
    let delay: TimeInterval
    private var action: (() -> Void)?
    private var generation = 0

    init(delay: TimeInterval) {
        self.delay = delay
    }

    var isPending: Bool { action != nil }

    func schedule(_ action: @escaping () -> Void) {
        let wasPending = isPending
        self.action = action
        guard !wasPending else { return }
        let scheduled = generation
        DispatchQueue.main.asyncAfter(deadline: .now() + delay) { [weak self] in
            guard let self, self.generation == scheduled else { return }
            self.flush()
        }
    }

    /// Runs the pending action now, if any.
    func flush() {
        generation += 1
        let pending = action
        action = nil
        pending?()
    }
}
