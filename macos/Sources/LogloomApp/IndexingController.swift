import Foundation
import LogloomCore

@MainActor
final class IndexingController {
    private var generation: UInt64 = 0
    private var cancelToken: CancelToken?
    private var task: Task<Void, Never>?

    func cancel() {
        generation &+= 1
        cancelToken?.cancel()
        cancelToken = nil
        task?.cancel()
        task = nil
    }

    func start(
        file: LogFile,
        onProgress: @escaping @MainActor (UInt64, Double) -> Void,
        onComplete: @escaping @MainActor (IndexStats) -> Void,
        onError: @escaping @MainActor (Error) -> Void
    ) {
        cancel()
        let activeGeneration = generation
        let token = CancelToken()
        cancelToken = token

        task = Task.detached(priority: .userInitiated) { [weak self] in
            var lastDispatch: CFAbsoluteTime = 0
            do {
                let stats = try file.buildIndex(cancelToken: token) {
                    bytesProcessed, bytesTotal, linesIndexed in
                    let now = CFAbsoluteTimeGetCurrent()
                    if now - lastDispatch >= 0.05 {
                        lastDispatch = now
                        let ratio: Double = bytesTotal > 0
                            ? min(1.0, max(0.0, Double(bytesProcessed) / Double(bytesTotal)))
                            : 1.0
                        Task { @MainActor [weak self] in
                            guard let self, self.generation == activeGeneration else { return }
                            onProgress(linesIndexed, ratio)
                        }
                    }
                    return !token.isCancelled
                }

                await MainActor.run { [weak self] in
                    guard let self, self.generation == activeGeneration else { return }
                    onComplete(stats)
                }
            } catch {
                await MainActor.run { [weak self] in
                    guard let self, self.generation == activeGeneration else { return }
                    if case LogloomError.cancelled = error {
                        return
                    }
                    if token.isCancelled {
                        return
                    }
                    onError(error)
                }
            }
        }
    }
}
