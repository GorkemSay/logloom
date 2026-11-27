import AppKit
import Foundation
import LogloomCore
import UniformTypeIdentifiers

@MainActor
final class ExportController {
    func exportFiltered(
        file: LogFile,
        sourcePath: String?,
        fileName: String?,
        isolatedTraceID: String?,
        query: SearchQuery,
        onStart: @escaping @MainActor () -> Void,
        onComplete: @escaping @MainActor (UInt64, String) -> Void,
        onError: @escaping @MainActor (Error) -> Void
    ) {
        let panel = NSSavePanel()
        panel.title = "Export Filtered Logs"
        panel.canCreateDirectories = true

        var allowedTypes: [UTType] = [.plainText]
        if let jsonl = UTType(filenameExtension: "jsonl") {
            allowedTypes.insert(jsonl, at: 0)
        }
        panel.allowedContentTypes = allowedTypes

        let baseStem = (fileName as NSString?)?.deletingPathExtension ?? "logloom"
        let suffix = isolatedTraceID.map { "trace-\($0)" } ?? "filtered"
        panel.nameFieldStringValue = "\(baseStem)-\(suffix).jsonl"

        guard panel.runModal() == .OK, let destinationURL = panel.url else {
            return
        }

        onStart()

        Task.detached(priority: .userInitiated) {
            do {
                let count = try LogExporter.exportFiltered(
                    file: file,
                    sourcePath: sourcePath,
                    query: query,
                    to: destinationURL
                )
                await MainActor.run {
                    onComplete(count, destinationURL.lastPathComponent)
                }
            } catch {
                await MainActor.run {
                    onError(error)
                }
            }
        }
    }
}
