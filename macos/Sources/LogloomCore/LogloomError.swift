import CLogloom
import Foundation

public enum LogloomError: Error, LocalizedError, Equatable, Sendable {
    case invalidArgument(String)
    case io(String)
    case outOfRange(String)
    case notIndexed(String)
    case invalidRegex(String)
    case cancelled(String)
    case internalError(String)

    public var errorDescription: String? {
        switch self {
        case .invalidArgument(let message),
             .io(let message),
             .outOfRange(let message),
             .notIndexed(let message),
             .invalidRegex(let message),
             .cancelled(let message),
             .internalError(let message):
            return message
        }
    }

    static func fromStatus(_ status: logloom_status_t, file: OpaquePointer? = nil) -> LogloomError {
        let rawMsg: UnsafePointer<CChar>? = file.map { logloom_file_last_error($0) } ?? logloom_last_error()
        let message = rawMsg.map { String(cString: $0) }.flatMap { $0.isEmpty ? nil : $0 }
            ?? "Logloom error (\(status.rawValue))"

        switch status {
        case LOGLOOM_ERR_INVALID_ARG:
            return .invalidArgument(message)
        case LOGLOOM_ERR_IO:
            return .io(message)
        case LOGLOOM_ERR_OUT_OF_RANGE:
            return .outOfRange(message)
        case LOGLOOM_ERR_NOT_INDEXED:
            return .notIndexed(message)
        case LOGLOOM_ERR_INVALID_REGEX:
            return .invalidRegex(message)
        case LOGLOOM_ERR_CANCELLED:
            return .cancelled(message)
        default:
            return .internalError(message)
        }
    }
}
