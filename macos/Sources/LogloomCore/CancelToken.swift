import CLogloom
import Foundation

public final class CancelToken: @unchecked Sendable {
    let raw: OpaquePointer

    public init() {
        guard let ptr = logloom_cancel_token_create() else {
            fatalError("Failed to allocate logloom_cancel_token_t")
        }
        self.raw = ptr
    }

    deinit {
        logloom_cancel_token_destroy(raw)
    }

    public func cancel() {
        logloom_cancel_token_cancel(raw)
    }

    public func reset() {
        logloom_cancel_token_reset(raw)
    }

    public var isCancelled: Bool {
        logloom_cancel_token_is_cancelled(raw)
    }
}
