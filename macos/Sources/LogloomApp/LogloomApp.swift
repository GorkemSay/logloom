import AppKit
import SwiftUI

final class AppDelegate: NSObject, NSApplicationDelegate {
  weak var model: AppModel?
  private var pendingURLs: [URL] = []
  private var pendingLineNumber: UInt64?
  private var didParseLaunchArgs = false

  func applicationDidFinishLaunching(_ notification: Notification) {
    NSApp.setActivationPolicy(.regular)
    NSApp.activate(ignoringOtherApps: true)
    enqueueLaunchArgumentFileIfPresent()
    flushPendingURLs()
  }

  private func enqueueLaunchArgumentFileIfPresent() {
    guard !didParseLaunchArgs else { return }
    didParseLaunchArgs = true
    let args = Array(CommandLine.arguments.dropFirst())
    var index = 0
    while index < args.count {
      let arg = args[index]
      if arg == "--line", index + 1 < args.count,
        let parsed = UInt64(args[index + 1]), parsed > 0
      {
        pendingLineNumber = parsed
        index += 2
        continue
      }
      if arg.hasPrefix("-") {
        // Skip Cocoa launch flags and their paired values such as -NSDocumentRevisionsDebugMode YES.
        if arg == "-NSDocumentRevisionsDebugMode" || arg == "-AppleLanguages" {
          index += 2
        } else {
          index += 1
        }
        continue
      }
      let url = URL(fileURLWithPath: arg)
      if FileManager.default.fileExists(atPath: url.path) {
        pendingURLs.append(url)
      }
      index += 1
    }
  }

  func application(_ application: NSApplication, open urls: [URL]) {
    enqueueLaunchArgumentFileIfPresent()
    guard let first = urls.first else { return }
    let lineToSelect = pendingLineNumber
    pendingLineNumber = nil
    if let model {
      Task { @MainActor in
        model.openFile(url: first)
        if let lineToSelect {
          try? await Task.sleep(for: .milliseconds(250))
          model.jumpToLine(lineToSelect)
        }
      }
    } else {
      pendingURLs.append(first)
      if let lineToSelect {
        pendingLineNumber = lineToSelect
      }
    }
  }

  func attach(model: AppModel) {
    self.model = model
    flushPendingURLs()
  }

  private func flushPendingURLs() {
    guard let model, let first = pendingURLs.first else { return }
    let lineToSelect = pendingLineNumber
    pendingURLs.removeAll()
    pendingLineNumber = nil
    Task { @MainActor in
      model.openFile(url: first)
      if let lineToSelect {
        try? await Task.sleep(for: .milliseconds(250))
        model.jumpToLine(lineToSelect)
      }
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
    .defaultSize(width: 1120, height: 680)
    .windowStyle(.hiddenTitleBar)
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
