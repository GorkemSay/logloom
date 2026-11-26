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

                if model.isIndexing {
                    Text("Indexing \(Int((model.indexProgress * 100).rounded()))% (\(model.indexedLinesSoFar) lines)")
                        .font(.caption.monospacedDigit())
                        .foregroundStyle(.secondary)
                } else {
                    Text("\(model.rowCount) line(s)")
                        .font(.caption.monospacedDigit())
                        .foregroundStyle(.secondary)
                }
            }
            .padding(.horizontal, 14)
            .padding(.vertical, 8)
            .background(.bar)

            if model.isIndexing {
                ProgressView(value: model.indexProgress)
                    .progressViewStyle(.linear)
                    .controlSize(.small)
            } else {
                Divider()
            }

            if let errorMessage = model.errorMessage {
                HStack(spacing: 8) {
                    Text(errorMessage)
                        .font(.caption)
                        .foregroundStyle(.red)
                    Spacer()
                }
                .padding(.horizontal, 14)
                .padding(.vertical, 6)
                Divider()
            }

            if model.hasTruncatedFinalLine && !model.truncatedBannerDismissed {
                HStack(spacing: 8) {
                    Image(systemName: "exclamationmark.triangle")
                        .font(.caption)
                        .foregroundStyle(.orange)
                    Text("Final line has no trailing newline (truncated EOF).")
                        .font(.caption)
                        .foregroundStyle(.primary)
                    Spacer()
                    Button("Dismiss") {
                        model.truncatedBannerDismissed = true
                    }
                    .font(.caption)
                    .buttonStyle(.borderless)
                }
                .padding(.horizontal, 14)
                .padding(.vertical, 6)
                .background(Color.orange.opacity(0.10))
                Divider()
            }

            VirtualizedLogTableView(
                lineCache: model.lineCache,
                viewportRevision: model.viewportRevision,
                selectedLineNumber: $model.selectedLineNumber
            )
            .frame(maxWidth: .infinity, maxHeight: .infinity)

            Divider()

            HStack(spacing: 12) {
                if model.isIndexed {
                    Text("\(model.rowCount) lines")
                } else {
                    Text("Preview: \(model.rowCount) lines (indexing in background)")
                }

                if model.oversizedLineCount > 0 {
                    Text("\(model.oversizedLineCount) oversized line(s) skipped")
                        .foregroundStyle(.orange)
                }

                Spacer()

                if let selectedLine = model.selectedLineNumber {
                    Text("Selected line \(selectedLine)")
                }
            }
            .font(.caption.monospacedDigit())
            .foregroundStyle(.secondary)
            .padding(.horizontal, 14)
            .padding(.vertical, 5)
            .background(.bar)
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
