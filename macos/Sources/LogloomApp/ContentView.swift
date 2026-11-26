import LogloomCore
import SwiftUI
import UniformTypeIdentifiers

struct ContentView: View {
    @ObservedObject var model: AppModel
    @State private var isDropTargeted = false

    var body: some View {
        Group {
            if model.hasOpenFile {
                loadedView
            } else {
                emptyStateView
            }
        }
        .frame(minWidth: 760, minHeight: 480)
        .onDrop(of: [UTType.fileURL], isTargeted: $isDropTargeted, perform: handleDrop(providers:))
        .overlay {
            if isDropTargeted {
                RoundedRectangle(cornerRadius: 8)
                    .strokeBorder(Color.accentColor, lineWidth: 2)
                    .padding(6)
                    .allowsHitTesting(false)
            }
        }
        .toolbar {
            ToolbarItemGroup(placement: .primaryAction) {
                Button("Open...") {
                    model.openPanel()
                }
                if model.hasOpenFile {
                    Button("Close") {
                        model.closeFile()
                    }
                }
            }
        }
    }

    private var emptyStateView: some View {
        VStack(spacing: 12) {
            Text("Logloom")
                .font(.title2.weight(.semibold))

            Text("Drop a .jsonl or .log file here, or choose one to inspect read-only.")
                .font(.callout)
                .foregroundStyle(.secondary)

            Button("Open Log File...") {
                model.openPanel()
            }
            .buttonStyle(.borderedProminent)
            .keyboardShortcut("o", modifiers: .command)

            if let errorMessage = model.errorMessage {
                Text(errorMessage)
                    .font(.caption)
                    .foregroundStyle(.red)
                    .padding(.top, 4)
            }

            Text("liblogloom \(LogFile.engineVersion)")
                .font(.caption2.monospaced())
                .foregroundStyle(.tertiary)
                .padding(.top, 8)
        }
        .padding(32)
        .frame(maxWidth: .infinity, maxHeight: .infinity)
    }

    private var loadedView: some View {
        VStack(spacing: 0) {
            HStack(spacing: 12) {
                Text(model.fileName ?? "")
                    .font(.headline)
                    .lineLimit(1)

                Text(model.formattedFileSize)
                    .font(.caption.monospacedDigit())
                    .foregroundStyle(.secondary)

                Spacer()

                Text("Preview: \(model.headLines.count) line(s)")
                    .font(.caption.monospacedDigit())
                    .foregroundStyle(.secondary)
            }
            .padding(.horizontal, 14)
            .padding(.vertical, 8)
            .background(.bar)

            Divider()

            if let errorMessage = model.errorMessage {
                HStack {
                    Text(errorMessage)
                        .font(.caption)
                        .foregroundStyle(.red)
                    Spacer()
                }
                .padding(.horizontal, 14)
                .padding(.vertical, 6)
                Divider()
            }

            ScrollView {
                LazyVStack(alignment: .leading, spacing: 2) {
                    ForEach(model.headLines) { line in
                        HStack(alignment: .firstTextBaseline, spacing: 10) {
                            Text("\(line.lineNumber)")
                                .font(.system(.caption, design: .monospaced).monospacedDigit())
                                .foregroundStyle(.secondary)
                                .frame(minWidth: 48, alignment: .trailing)

                            if let level = line.fields.level {
                                Text(level)
                                    .font(.system(.caption2, design: .monospaced).weight(.medium))
                                    .foregroundStyle(levelColor(level))
                                    .frame(minWidth: 46, alignment: .leading)
                            }

                            Text(line.exceedsMaxLength
                                ? "[oversized line: \(line.byteLength) bytes skipped]"
                                : line.content)
                                .font(.system(.callout, design: .monospaced))
                                .textSelection(.enabled)
                                .frame(maxWidth: .infinity, alignment: .leading)
                        }
                        .padding(.horizontal, 12)
                        .padding(.vertical, 2)
                    }
                }
                .padding(.vertical, 6)
            }
        }
    }

    private func levelColor(_ level: String) -> Color {
        switch level.uppercased() {
        case "ERROR", "FATAL":
            return .red
        case "WARN", "WARNING":
            return .orange
        case "DEBUG":
            return .secondary
        default:
            return .primary
        }
    }

    private func handleDrop(providers: [NSItemProvider]) -> Bool {
        guard let provider = providers.first(where: {
            $0.hasItemConformingToTypeIdentifier(UTType.fileURL.identifier)
        }) else {
            return false
        }

        provider.loadItem(forTypeIdentifier: UTType.fileURL.identifier, options: nil) { item, _ in
            let resolvedURL: URL?
            if let data = item as? Data {
                resolvedURL = URL(dataRepresentation: data, relativeTo: nil)
            } else if let url = item as? URL {
                resolvedURL = url
            } else {
                resolvedURL = nil
            }
            guard let fileURL = resolvedURL else {
                return
            }
            Task { @MainActor in
                model.openFile(url: fileURL)
            }
        }
        return true
    }
}
