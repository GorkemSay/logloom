import Foundation

public enum JSONFormatter {
    public static let maxPrettyPrintBytes = 1_048_576

    public static func prettyPrint(_ raw: String) -> String? {
        guard !raw.isEmpty, raw.utf8.count <= maxPrettyPrintBytes else {
            return nil
        }
        guard let data = raw.data(using: .utf8) else {
            return nil
        }
        do {
            let parsed = try JSONSerialization.jsonObject(with: data, options: [])
            guard parsed is [String: Any] || parsed is [Any] else {
                return nil
            }
            let formatted = try JSONSerialization.data(
                withJSONObject: parsed,
                options: [.prettyPrinted, .sortedKeys, .withoutEscapingSlashes]
            )
            return String(data: formatted, encoding: .utf8)
        } catch {
            return nil
        }
    }

    /// Formats an ISO-8601 UTC timestamp for compact table display (`HH:MM:SS.mmm` or raw fallback).
    public static func formatTimestampForTable(_ raw: String?) -> String {
        guard let raw, !raw.isEmpty else {
            return ""
        }
        guard let tIndex = raw.firstIndex(of: "T") else {
            return raw
        }
        let afterT = raw[raw.index(after: tIndex)...]
        if afterT.hasSuffix("Z") {
            return String(afterT.dropLast())
        }
        return String(afterT)
    }
}

public enum LogExportError: Error, LocalizedError, Equatable {
    case cannotOverwriteSourceFile
    case cannotCreateDestination(String)

    public var errorDescription: String? {
        switch self {
        case .cannotOverwriteSourceFile:
            return "Export destination cannot overwrite the open source log file."
        case .cannotCreateDestination(let path):
            return "Unable to create export file at \(path)."
        }
    }
}

public enum LogExporter {
    public static func exportFiltered(
        file: LogFile,
        sourcePath: String?,
        query: SearchQuery,
        to destinationURL: URL,
        cancelToken: CancelToken? = nil
    ) throws -> UInt64 {
        if let sourcePath {
            let sourceURL = URL(fileURLWithPath: sourcePath).standardizedFileURL
            if sourceURL == destinationURL.standardizedFileURL {
                throw LogExportError.cannotOverwriteSourceFile
            }
        }

        var exportQuery = query
        exportQuery.limit = 0

        let created = FileManager.default.createFile(
            atPath: destinationURL.path,
            contents: nil,
            attributes: nil
        )
        guard created else {
            throw LogExportError.cannotCreateDestination(destinationURL.path)
        }

        let handle: FileHandle
        do {
            handle = try FileHandle(forWritingTo: destinationURL)
        } catch {
            try? FileManager.default.removeItem(at: destinationURL)
            throw LogExportError.cannotCreateDestination(destinationURL.path)
        }

        var exportedCount: UInt64 = 0
        var buffer = Data()
        buffer.reserveCapacity(65_536)
        var writeError: Error?

        do {
            _ = try file.search(exportQuery, cancelToken: cancelToken) { match in
                if let token = cancelToken, token.isCancelled {
                    return false
                }
                buffer.append(contentsOf: match.content.utf8)
                buffer.append(0x0A)
                exportedCount += 1

                if buffer.count >= 65_536 {
                    do {
                        try handle.write(contentsOf: buffer)
                        buffer.removeAll(keepingCapacity: true)
                    } catch {
                        writeError = error
                        return false
                    }
                }
                return true
            }

            if let writeError {
                throw writeError
            }

            if !buffer.isEmpty {
                try handle.write(contentsOf: buffer)
                buffer.removeAll(keepingCapacity: false)
            }

            try handle.close()
            return exportedCount
        } catch {
            try? handle.close()
            try? FileManager.default.removeItem(at: destinationURL)
            throw error
        }
    }
}
