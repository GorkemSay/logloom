import AppKit
import LogloomCore
import SwiftUI

enum InspectorTab: String, CaseIterable, Identifiable {
    case formatted = "Formatted"
    case raw = "Raw"
    case context = "Context"

    var id: String { rawValue }
}

struct LineInspectorView: View {
    @ObservedObject var model: AppModel
    @State private var activeTab: InspectorTab = .formatted

    private static let contextRadiusOptions = [5, 10, 25, 50]

    var body: some View {
        VStack(spacing: 0) {
            if let line = model.selectedLine {
                inspectorHeader(for: line)
                Divider()

                Picker("Inspector Mode", selection: $activeTab) {
                    ForEach(InspectorTab.allCases) { tab in
                        Text(tab.rawValue).tag(tab)
                    }
                }
                .labelsHidden()
                .pickerStyle(.segmented)
                .controlSize(.small)
                .padding(.horizontal, 12)
                .padding(.vertical, 8)
                .background(.bar)

                Divider()

                switch activeTab {
                case .formatted:
                    formattedTab(for: line)
                case .raw:
                    rawTab(for: line)
                case .context:
                    contextTab(for: line)
                }
            } else {
                VStack(spacing: 8) {
                    Text("Select a log line to inspect")
                        .font(.callout)
                        .foregroundStyle(.secondary)
                    Text("Use ⌘G / ⇧⌘G to step rows, ⌘L to jump to a line.")
                        .font(.caption)
                        .foregroundStyle(.tertiary)
                }
                .padding(24)
                .frame(maxWidth: .infinity, maxHeight: .infinity)
            }
        }
        .background(Color(nsColor: .textBackgroundColor))
    }

    private func inspectorHeader(for line: LogLine) -> some View {
        VStack(alignment: .leading, spacing: 6) {
            HStack(spacing: 8) {
                Text("Line \(line.lineNumber)")
                    .font(.headline.monospacedDigit())

                if let level = line.fields.level, !level.isEmpty {
                    Text(level)
                        .font(.system(.caption2, design: .monospaced).weight(.medium))
                        .padding(.horizontal, 5)
                        .padding(.vertical, 1)
                        .background(levelBadgeColor(level).opacity(0.14))
                        .foregroundStyle(levelBadgeColor(level))
                        .clipShape(RoundedRectangle(cornerRadius: 4))
                }

                Spacer()

                Button("Copy Raw") {
                    NSPasteboard.general.clearContents()
                    NSPasteboard.general.setString(line.content, forType: .string)
                }
                .controlSize(.small)
            }

            HStack(spacing: 8) {
                Text("Offset \(line.byteOffset) · \(line.byteLength) B")
                    .font(.caption.monospacedDigit())
                    .foregroundStyle(.secondary)

                if line.hasCRLF {
                    tagLabel("CRLF")
                }
                if line.isTruncatedEOF {
                    tagLabel("Truncated EOF")
                }

                Spacer()

                if let traceID = line.fields.traceID, !traceID.isEmpty {
                    if model.isolatedTraceID == traceID {
                        Button("Exit Trace") {
                            model.clearTraceIsolation()
                        }
                        .font(.caption)
                        .buttonStyle(.borderless)
                    } else {
                        Button("Isolate Trace") {
                            model.isolateTrace(traceID)
                        }
                        .font(.caption)
                        .buttonStyle(.borderless)
                    }
                }
            }
        }
        .padding(.horizontal, 12)
        .padding(.vertical, 10)
        .background(.bar)
    }

    private func formattedTab(for line: LogLine) -> some View {
        VStack(alignment: .leading, spacing: 0) {
            if line.exceedsMaxLength {
                oversizedNotice(byteLength: line.byteLength)
            } else if let pretty = JSONFormatter.prettyPrint(line.content) {
                ScrollView([.vertical, .horizontal]) {
                    Text(pretty)
                        .font(.system(.callout, design: .monospaced))
                        .textSelection(.enabled)
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .padding(12)
                }
            } else {
                HStack(spacing: 6) {
                    Text(fallbackNote(for: line.fields.kind))
                        .font(.caption)
                        .foregroundStyle(.secondary)
                    Spacer()
                }
                .padding(.horizontal, 12)
                .padding(.vertical, 6)
                .background(.bar)
                Divider()

                ScrollView([.vertical, .horizontal]) {
                    Text(line.content)
                        .font(.system(.callout, design: .monospaced))
                        .textSelection(.enabled)
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .padding(12)
                }
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    private func rawTab(for line: LogLine) -> some View {
        Group {
            if line.exceedsMaxLength {
                oversizedNotice(byteLength: line.byteLength)
            } else {
                ScrollView([.vertical, .horizontal]) {
                    Text(line.content)
                        .font(.system(.callout, design: .monospaced))
                        .textSelection(.enabled)
                        .frame(maxWidth: .infinity, alignment: .leading)
                        .padding(12)
                }
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    private func contextTab(for line: LogLine) -> some View {
        VStack(spacing: 0) {
            HStack(spacing: 8) {
                Text("Radius")
                    .font(.caption)
                    .foregroundStyle(.secondary)

                Picker("Radius", selection: $model.contextRadius) {
                    ForEach(Self.contextRadiusOptions, id: \.self) { radius in
                        Text("±\(radius)").tag(radius)
                    }
                }
                .labelsHidden()
                .pickerStyle(.menu)
                .controlSize(.small)
                .frame(width: 72)

                Spacer()

                if model.isFilterActive {
                    Button("Show in Full Log") {
                        model.jumpToLine(line.lineNumber, clearFiltersIfNeeded: true)
                    }
                    .font(.caption)
                    .buttonStyle(.borderless)
                }
            }
            .padding(.horizontal, 12)
            .padding(.vertical, 6)
            .background(.bar)

            Divider()

            if !model.isIndexed {
                VStack(spacing: 6) {
                    Text("Indexing in progress")
                        .font(.callout)
                        .foregroundStyle(.secondary)
                    Text("Context window becomes available as soon as indexing finishes.")
                        .font(.caption)
                        .foregroundStyle(.tertiary)
                }
                .padding(20)
                .frame(maxWidth: .infinity, maxHeight: .infinity)
            } else {
                ScrollView {
                    LazyVStack(alignment: .leading, spacing: 0) {
                        ForEach(model.contextLines) { ctxLine in
                            let isCenter = ctxLine.lineNumber == line.lineNumber
                            HStack(alignment: .firstTextBaseline, spacing: 8) {
                                Text("\(ctxLine.lineNumber)")
                                    .font(.system(.caption, design: .monospaced).monospacedDigit())
                                    .foregroundStyle(isCenter ? Color.accentColor : .secondary)
                                    .frame(minWidth: 46, alignment: .trailing)

                                Text(ctxLine.fields.level ?? "")
                                    .font(.system(.caption2, design: .monospaced).weight(.medium))
                                    .foregroundStyle(levelBadgeColor(ctxLine.fields.level ?? ""))
                                    .frame(minWidth: 44, alignment: .leading)

                                Text(
                                    ctxLine.exceedsMaxLength
                                        ? "[oversized line: \(ctxLine.byteLength) bytes skipped]"
                                        : ctxLine.content
                                )
                                .font(.system(.caption, design: .monospaced))
                                .lineLimit(1)
                                .truncationMode(.tail)
                                .frame(maxWidth: .infinity, alignment: .leading)
                            }
                            .padding(.horizontal, 10)
                            .padding(.vertical, 3)
                            .background(isCenter ? Color.accentColor.opacity(0.12) : Color.clear)
                            .contentShape(Rectangle())
                            .onTapGesture {
                                model.jumpToLine(ctxLine.lineNumber, clearFiltersIfNeeded: false)
                            }
                        }
                    }
                    .padding(.vertical, 4)
                }
            }
        }
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    private func oversizedNotice(byteLength: UInt64) -> some View {
        VStack(spacing: 6) {
            Text("Oversized line (\(byteLength) bytes)")
                .font(.callout.monospacedDigit())
                .foregroundStyle(.secondary)
            Text("Content exceeds max line cap and was skipped by the reader.")
                .font(.caption)
                .foregroundStyle(.tertiary)
        }
        .padding(20)
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    private func fallbackNote(for kind: LogLineKind) -> String {
        switch kind {
        case .malformed:
            return "Malformed JSON — showing raw content"
        case .plainText:
            return "Plain text — showing raw content"
        case .jsonObject:
            return "Showing raw content"
        }
    }

    private func tagLabel(_ text: String) -> some View {
        Text(text)
            .font(.caption2)
            .foregroundStyle(.secondary)
            .padding(.horizontal, 5)
            .padding(.vertical, 1)
            .background(Color(nsColor: .quaternaryLabelColor).opacity(0.25))
            .clipShape(RoundedRectangle(cornerRadius: 3))
    }

    private func levelBadgeColor(_ level: String) -> Color {
        switch level.uppercased() {
        case "ERROR", "FATAL", "CRITICAL":
            return .red
        case "WARN", "WARNING":
            return .orange
        case "INFO":
            return .blue
        default:
            return .secondary
        }
    }
}
