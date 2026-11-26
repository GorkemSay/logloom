import AppKit
import Foundation
import LogloomCore
import UniformTypeIdentifiers

private final class SecurityScopeGuard {
    let url: URL
    private let didStartAccess: Bool

    init(url: URL) {
        self.url = url
        self.didStartAccess = url.startAccessingSecurityScopedResource()
    }

    deinit {
        if didStartAccess {
            url.stopAccessingSecurityScopedResource()
        }
    }
}

@MainActor
final class AppModel: ObservableObject {
    @Published private(set) var fileName: String?
    @Published private(set) var filePath: String?
    @Published private(set) var fileSize: UInt64 = 0
    @Published private(set) var headLines: [LogLine] = []
    @Published var errorMessage: String?

    private var scopeGuard: SecurityScopeGuard?
    private var openedFile: LogFile?

    var hasOpenFile: Bool {
        openedFile != nil
    }

    var formattedFileSize: String {
        ByteCountFormatter.string(fromByteCount: Int64(clamping: fileSize), countStyle: .file)
    }

    func openPanel() {
        let panel = NSOpenPanel()
        panel.title = "Open Log File"
        panel.canChooseFiles = true
        panel.canChooseDirectories = false
        panel.allowsMultipleSelection = false
        panel.canCreateDirectories = false

        var types: [UTType] = [.plainText, .log]
        if let json = UTType(filenameExtension: "jsonl") {
            types.append(json)
        }
        if let ndjson = UTType(filenameExtension: "ndjson") {
            types.append(ndjson)
        }
        panel.allowedContentTypes = types

        if panel.runModal() == .OK, let url = panel.url {
            openFile(url: url)
        }
    }

    func openFile(url: URL) {
        guard url.isFileURL else {
            errorMessage = "Only local files are supported."
            return
        }

        let guardScope = SecurityScopeGuard(url: url)
        do {
            let file = try LogFile(path: url.path)
            let head = try file.readHead(maxLines: 100)

            self.openedFile = file
            self.scopeGuard = guardScope
            self.fileName = url.lastPathComponent
            self.filePath = url.path
            self.fileSize = file.fileSize
            self.headLines = head
            self.errorMessage = nil
        } catch {
            self.errorMessage = error.localizedDescription
        }
    }

    func closeFile() {
        openedFile = nil
        scopeGuard = nil
        fileName = nil
        filePath = nil
        fileSize = 0
        headLines = []
        errorMessage = nil
    }
}
