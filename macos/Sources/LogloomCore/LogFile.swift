import CLogloom
import Foundation

private final class ProgressBox {
    let handler: (UInt64, UInt64, UInt64) -> Bool

    init(_ handler: @escaping (UInt64, UInt64, UInt64) -> Bool) {
        self.handler = handler
    }
}

private final class MatchBox {
    let handler: (SearchMatch) -> Bool

    init(_ handler: @escaping (SearchMatch) -> Bool) {
        self.handler = handler
    }
}

private func withOptionalCString<Result>(
    _ string: String?,
    _ body: (UnsafePointer<CChar>?) throws -> Result
) rethrows -> Result {
    guard let string else {
        return try body(nil)
    }
    return try string.withCString { try body($0) }
}

private func decodeUTF8Slice(_ ptr: UnsafePointer<CChar>?, length: Int) -> String? {
    guard let ptr else {
        return nil
    }
    if length == 0 {
        return ""
    }
    let raw = UnsafeRawBufferPointer(start: ptr, count: length)
    return String(decoding: raw, as: UTF8.self)
}

public final class LogFile: @unchecked Sendable {
    private let raw: OpaquePointer

    public static var engineVersion: String {
        String(cString: logloom_version())
    }

    public init(path: String) throws {
        var handle: OpaquePointer?
        let status = path.withCString { cPath in
            logloom_file_open(cPath, &handle)
        }
        guard status == LOGLOOM_OK, let opened = handle else {
            throw LogloomError.fromStatus(status)
        }
        self.raw = opened
    }

    deinit {
        logloom_file_close(raw)
    }

    public var fileSize: UInt64 {
        logloom_file_size(raw)
    }

    public var isIndexed: Bool {
        logloom_file_is_indexed(raw)
    }

    public var lineCount: UInt64 {
        logloom_file_line_count(raw)
    }

    public static func extractFields(
        from line: String,
        traceKey: String? = nil,
        timestampKey: String? = nil,
        levelKey: String? = nil
    ) -> ExtractedFields {
        let bytes = Array(line.utf8)
        return bytes.withUnsafeBufferPointer { buffer in
            withOptionalCString(traceKey) { cTrace in
                withOptionalCString(timestampKey) { cTimestamp in
                    withOptionalCString(levelKey) { cLevel in
                        var cFields = logloom_extracted_fields_t()
                        let status: logloom_status_t
                        if let base = buffer.baseAddress, !bytes.isEmpty {
                            status = base.withMemoryRebound(
                                to: CChar.self,
                                capacity: bytes.count
                            ) { cPtr in
                                logloom_extract_fields(
                                    cPtr,
                                    bytes.count,
                                    cTrace,
                                    cTimestamp,
                                    cLevel,
                                    &cFields
                                )
                            }
                        } else {
                            status = logloom_extract_fields(
                                nil,
                                0,
                                cTrace,
                                cTimestamp,
                                cLevel,
                                &cFields
                            )
                        }

                        guard status == LOGLOOM_OK else {
                            return ExtractedFields(kind: .plainText)
                        }

                        return ExtractedFields(
                            kind: LogLineKind(cKind: cFields.kind),
                            timestamp: decodeUTF8Slice(cFields.timestamp, length: cFields.timestamp_length),
                            level: decodeUTF8Slice(cFields.level, length: cFields.level_length),
                            traceID: decodeUTF8Slice(cFields.trace_id, length: cFields.trace_id_length),
                            hasEscapesInTraceID: cFields.has_escapes_in_trace_id
                        )
                    }
                }
            }
        }
    }

    public func readHead(maxLines: Int) throws -> [LogLine] {
        guard maxLines >= 0 else {
            throw LogloomError.invalidArgument("maxLines must be non-negative")
        }
        var list = logloom_line_list_t()
        let status = logloom_file_read_head(raw, maxLines, &list)
        defer { logloom_line_list_free(&list) }
        guard status == LOGLOOM_OK else {
            throw LogloomError.fromStatus(status, file: raw)
        }
        return Self.convertLineList(list)
    }

    public func buildIndex(
        cancelToken: CancelToken? = nil,
        progress: ((UInt64, UInt64, UInt64) -> Bool)? = nil
    ) throws -> IndexStats {
        var cStats = logloom_index_stats_t()
        let status: logloom_status_t

        if let progress {
            let box = ProgressBox(progress)
            let userData = Unmanaged.passRetained(box).toOpaque()
            defer { Unmanaged<ProgressBox>.fromOpaque(userData).release() }

            let cb: logloom_progress_cb = { processed, total, lines, ctx in
                guard let ctx else { return true }
                let box = Unmanaged<ProgressBox>.fromOpaque(ctx).takeUnretainedValue()
                return box.handler(processed, total, lines)
            }
            status = logloom_file_build_index(raw, cb, userData, cancelToken?.raw, &cStats)
        } else {
            status = logloom_file_build_index(raw, nil, nil, cancelToken?.raw, &cStats)
        }

        guard status == LOGLOOM_OK else {
            throw LogloomError.fromStatus(status, file: raw)
        }

        return IndexStats(
            totalLines: cStats.total_lines,
            oversizedLines: cStats.oversized_lines,
            bytesRead: cStats.bytes_read,
            stride: cStats.stride,
            indexMemoryBytes: cStats.index_memory_bytes,
            hadUTF8BOM: cStats.had_utf8_bom,
            hasTruncatedFinalLine: cStats.has_truncated_final_line
        )
    }

    public func readLine(_ lineNumber: UInt64) throws -> LogLine {
        var cLine = logloom_line_t()
        let status = logloom_file_read_line(raw, lineNumber, &cLine)
        defer { logloom_line_free(&cLine) }
        guard status == LOGLOOM_OK else {
            throw LogloomError.fromStatus(status, file: raw)
        }
        return Self.convertLine(cLine)
    }

    public func readContext(around centerLine: UInt64, before: UInt64, after: UInt64) throws -> [LogLine] {
        var list = logloom_line_list_t()
        let status = logloom_file_read_context(raw, centerLine, before, after, &list)
        defer { logloom_line_list_free(&list) }
        guard status == LOGLOOM_OK else {
            throw LogloomError.fromStatus(status, file: raw)
        }
        return Self.convertLineList(list)
    }

    public func search(
        _ query: SearchQuery = SearchQuery(),
        cancelToken: CancelToken? = nil,
        onMatch: ((SearchMatch) -> Bool)? = nil
    ) throws -> SearchStats {
        var cStats = logloom_search_stats_t()
        let status: logloom_status_t = withOptionalCString(query.traceID) { cTraceID in
            query.traceKey.withCString { cTraceKey in
                withOptionalCString(query.level) { cLevel in
                    query.levelKey.withCString { cLevelKey in
                        query.timestampKey.withCString { cTimestampKey in
                            withOptionalCString(query.textQuery) { cTextQuery in
                                var cQuery = logloom_search_query_default()
                                cQuery.trace_id = cTraceID
                                cQuery.trace_key = cTraceKey
                                cQuery.level = cLevel
                                cQuery.level_key = cLevelKey
                                cQuery.timestamp_key = cTimestampKey
                                cQuery.text_query = cTextQuery
                                cQuery.case_sensitive = query.caseSensitive
                                cQuery.is_regex = query.isRegex
                                cQuery.limit = query.limit
                                cQuery.num_threads = max(query.numThreads, 0)

                                if let onMatch {
                                    let box = MatchBox(onMatch)
                                    let userData = Unmanaged.passRetained(box).toOpaque()
                                    defer { Unmanaged<MatchBox>.fromOpaque(userData).release() }

                                    let cb: logloom_match_cb = { cMatchPtr, ctx in
                                        guard let cMatchPtr, let ctx else { return true }
                                        let m = cMatchPtr.pointee
                                        let box = Unmanaged<MatchBox>.fromOpaque(ctx).takeUnretainedValue()
                                        let content = decodeUTF8Slice(m.content, length: m.content_length) ?? ""
                                        let fields = ExtractedFields(
                                            kind: LogLineKind(cKind: m.kind),
                                            timestamp: decodeUTF8Slice(m.timestamp, length: m.timestamp_length),
                                            level: decodeUTF8Slice(m.level, length: m.level_length),
                                            traceID: decodeUTF8Slice(m.trace_id, length: m.trace_id_length),
                                            hasEscapesInTraceID: false
                                        )
                                        let match = SearchMatch(
                                            lineNumber: m.line_number,
                                            byteOffset: m.byte_offset,
                                            byteLength: m.byte_length,
                                            content: content,
                                            fields: fields,
                                            hasCRLF: m.has_crlf,
                                            isTruncatedEOF: m.is_truncated_eof
                                        )
                                        return box.handler(match)
                                    }

                                    return logloom_file_search(
                                        raw,
                                        &cQuery,
                                        cb,
                                        userData,
                                        cancelToken?.raw,
                                        &cStats
                                    )
                                } else {
                                    return logloom_file_search(
                                        raw,
                                        &cQuery,
                                        nil,
                                        nil,
                                        cancelToken?.raw,
                                        &cStats
                                    )
                                }
                            }
                        }
                    }
                }
            }
        }

        guard status == LOGLOOM_OK else {
            throw LogloomError.fromStatus(status, file: raw)
        }

        return SearchStats(
            linesScanned: cStats.lines_scanned,
            matchesFound: cStats.matches_found,
            jsonLines: cStats.json_lines,
            plainTextLines: cStats.plain_text_lines,
            malformedLines: cStats.malformed_lines,
            oversizedLines: cStats.oversized_lines,
            truncatedLines: cStats.truncated_lines,
            hasTruncatedFinalLine: cStats.has_truncated_final_line,
            cancelled: cStats.cancelled,
            limitReached: cStats.limit_reached
        )
    }

    private static func convertLine(_ cLine: logloom_line_t) -> LogLine {
        let content = decodeUTF8Slice(cLine.content, length: cLine.content_length) ?? ""
        let fields = extractFields(from: content)
        return LogLine(
            lineNumber: cLine.line_number,
            byteOffset: cLine.byte_offset,
            byteLength: cLine.byte_length,
            content: content,
            hasCRLF: cLine.has_crlf,
            isTruncatedEOF: cLine.is_truncated_eof,
            exceedsMaxLength: cLine.exceeds_max_length,
            fields: fields
        )
    }

    private static func convertLineList(_ list: logloom_line_list_t) -> [LogLine] {
        guard let base = list.lines, list.count > 0 else {
            return []
        }
        var result: [LogLine] = []
        result.reserveCapacity(list.count)
        for i in 0..<list.count {
            result.append(convertLine(base[i]))
        }
        return result
    }
}
