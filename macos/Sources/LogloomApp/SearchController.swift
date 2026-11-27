import Foundation
import LogloomCore

@MainActor
final class SearchController {
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
        query: SearchQuery,
        immediate: Bool,
        onBegin: @escaping @MainActor () -> Void,
        onBatch: @escaping @MainActor ([SearchMatch]) -> Void,
        onComplete: @escaping @MainActor ([SearchMatch], SearchStats, Double) -> Void,
        onError: @escaping @MainActor (Error) -> Void
    ) {
        cancel()
        let activeGeneration = generation
        let token = CancelToken()
        cancelToken = token

        task = Task.detached(priority: .userInitiated) { [weak self] in
            if !immediate {
                try? await Task.sleep(nanoseconds: 120_000_000)
            }
            if Task.isCancelled || token.isCancelled {
                return
            }

            await MainActor.run { [weak self] in
                guard let self, self.generation == activeGeneration else { return }
                onBegin()
            }

            if Task.isCancelled || token.isCancelled {
                return
            }

            let startTime = CFAbsoluteTimeGetCurrent()
            var batch: [SearchMatch] = []
            batch.reserveCapacity(200)
            var lastFlushTime = startTime

            do {
                let stats = try file.search(query, cancelToken: token) { match in
                    if token.isCancelled {
                        return false
                    }
                    batch.append(match)
                    let now = CFAbsoluteTimeGetCurrent()
                    if batch.count >= 200 || (now - lastFlushTime) >= 0.05 {
                        let chunk = batch
                        batch.removeAll(keepingCapacity: true)
                        lastFlushTime = now
                        Task { @MainActor [weak self] in
                            guard let self, self.generation == activeGeneration else { return }
                            onBatch(chunk)
                        }
                    }
                    return !token.isCancelled
                }

                let remaining = batch
                let elapsedMs = (CFAbsoluteTimeGetCurrent() - startTime) * 1000.0
                await MainActor.run { [weak self] in
                    guard let self, self.generation == activeGeneration else { return }
                    onComplete(remaining, stats, elapsedMs)
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
