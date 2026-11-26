import AppKit
import SwiftUI

final class AppDelegate: NSObject, NSApplicationDelegate {
    weak var model: AppModel?
    private var pendingURLs: [URL] = []

    func applicationDidFinishLaunching(_ notification: Notification) {
        NSApp.setActivationPolicy(.regular)
        NSApp.activate(ignoringOtherApps: true)
        flushPendingURLs()
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
            }
        }
    }
}
