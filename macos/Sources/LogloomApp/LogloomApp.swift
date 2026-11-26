import AppKit
import SwiftUI

final class AppDelegate: NSObject, NSApplicationDelegate {
    weak var model: AppModel?
    private var pendingURLs: [URL] = []

    func applicationDidFinishLaunching(_ notification: Notification) {
        NSApp.setActivationPolicy(.regular)
        NSApp.activate(ignoringOtherApps: true)
        enqueueLaunchArgumentFileIfPresent()
        flushPendingURLs()
    }

    private func enqueueLaunchArgumentFileIfPresent() {
        var skipNext = false
        for arg in CommandLine.arguments.dropFirst() {
            if skipNext {
                skipNext = false
                continue
            }
            if arg.hasPrefix("-") {
                // Skip Cocoa launch flags and their paired values such as -NSDocumentRevisionsDebugMode YES.
                skipNext = (arg == "-NSDocumentRevisionsDebugMode" || arg == "-AppleLanguages")
                continue
            }
            let url = URL(fileURLWithPath: arg)
            if FileManager.default.fileExists(atPath: url.path) {
                pendingURLs.append(url)
                break
            }
        }
    }

    func application(_ application: NSApplication, open urls: [URL]) {
        guard let first = urls.first else { return }
        if let model {
            Task { @MainActor in
                model.openFile(url: first)
            }
        } else {
            pendingURLs.append(first)
        }
    }

    func attach(model: AppModel) {
        self.model = model
        flushPendingURLs()
    }

    private func flushPendingURLs() {
        guard let model, let first = pendingURLs.first else { return }
        pendingURLs.removeAll()
        Task { @MainActor in
            model.openFile(url: first)
        }
    }
}

@main
struct LogloomDesktopApp: App {
    @NSApplicationDelegateAdaptor(AppDelegate.self) private var appDelegate
    @StateObject private var model = AppModel()

    var body: some Scene {
        WindowGroup("Logloom") {
            ContentView(model: model)
                .onAppear {
                    appDelegate.attach(model: model)
                }
        }
        .commands {
            CommandGroup(replacing: .newItem) {
                Button("Open...") {
                    model.openPanel()
                }
                .keyboardShortcut("o", modifiers: .command)
            }
            CommandGroup(after: .textEditing) {
                Button("Find...") {
                    model.requestSearchFocus()
                }
                .keyboardShortcut("f", modifiers: .command)
                .disabled(!model.hasOpenFile)

                Button("Next Match") {
                    model.selectNextMatch()
                }
                .keyboardShortcut("g", modifiers: .command)
                .disabled(!model.hasOpenFile || model.rowCount == 0)

                Button("Previous Match") {
                    model.selectPreviousMatch()
                }
                .keyboardShortcut("g", modifiers: [.command, .shift])
                .disabled(!model.hasOpenFile || model.rowCount == 0)

                Divider()

                Button("Jump to Line...") {
                    model.requestJumpToLine()
                }
                .keyboardShortcut("l", modifiers: .command)
                .disabled(!model.hasOpenFile)

                Button("Export Filtered...") {
                    model.exportFilteredPanel()
                }
                .keyboardShortcut("e", modifiers: [.command, .shift])
                .disabled(!model.hasOpenFile || !model.isFilterActive || model.isExporting)

                Divider()

                Button(model.isInspectorVisible ? "Hide Inspector" : "Show Inspector") {
                    model.toggleInspector()
                }
                .keyboardShortcut("i", modifiers: [.command, .option])
                .disabled(!model.hasOpenFile)
            }
        }
    }
}
