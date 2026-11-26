import AppKit
import LogloomCore
import SwiftUI

struct VirtualizedLogTableView: NSViewRepresentable {
  let viewportStore: ViewportStore
  let viewportRevision: UInt64
  let isolatedTraceID: String?
  @Binding var selectedLineNumber: UInt64?
  let scrollToSelectionTrigger: UInt64
  let onTraceIDClick: (String) -> Void
  let onClearTraceIsolation: () -> Void

  func makeCoordinator() -> Coordinator {
    Coordinator(
      viewportStore: viewportStore,
      isolatedTraceID: isolatedTraceID,
      selectedLineNumber: $selectedLineNumber,
      onTraceIDClick: onTraceIDClick,
      onClearTraceIsolation: onClearTraceIsolation
    )
  }

  func makeNSView(context: Context) -> NSScrollView {
    let scrollView = NSScrollView()
    scrollView.hasVerticalScroller = true
    scrollView.hasHorizontalScroller = true
    scrollView.autohidesScrollers = true
    scrollView.borderType = .noBorder
    scrollView.drawsBackground = true
    scrollView.backgroundColor = .textBackgroundColor

    let tableView = NSTableView()
    tableView.style = .plain
    tableView.usesAlternatingRowBackgroundColors = false
    tableView.rowHeight = 20
    tableView.usesAutomaticRowHeights = false
    tableView.intercellSpacing = NSSize(width: 8, height: 0)
    tableView.allowsMultipleSelection = false
    tableView.allowsEmptySelection = true
    tableView.allowsColumnSelection = false
    tableView.allowsColumnReordering = false
    tableView.columnAutoresizingStyle = .lastColumnOnlyAutoresizingStyle
    tableView.gridStyleMask = []
    tableView.headerView = NSTableHeaderView()

    for spec in ColumnSpec.allCases {
      let column = NSTableColumn(identifier: spec.identifier)
      column.title = spec.title
      column.width = spec.defaultWidth
      column.minWidth = spec.minWidth
      column.maxWidth = spec.maxWidth
      column.resizingMask = spec.resizingMask
      column.headerCell.alignment = spec.alignment
      tableView.addTableColumn(column)
    }

    tableView.delegate = context.coordinator
    tableView.dataSource = context.coordinator
    tableView.target = context.coordinator
    tableView.action = #selector(Coordinator.handleTableClick(_:))

    let menu = NSMenu()
    menu.delegate = context.coordinator
    tableView.menu = menu

    scrollView.documentView = tableView
    context.coordinator.tableView = tableView
    context.coordinator.scrollView = scrollView
    context.coordinator.lastRevision = viewportRevision
    context.coordinator.lastMode = viewportStore.mode
    context.coordinator.lastScrollTrigger = scrollToSelectionTrigger

    return scrollView
  }

  func updateNSView(_ scrollView: NSScrollView, context: Context) {
    let coordinator = context.coordinator
    coordinator.selectedLineNumberBinding = $selectedLineNumber
    coordinator.viewportStore = viewportStore
    coordinator.isolatedTraceID = isolatedTraceID
    coordinator.onTraceIDClick = onTraceIDClick
    coordinator.onClearTraceIsolation = onClearTraceIsolation

    guard let tableView = coordinator.tableView else { return }

    if coordinator.lastRevision != viewportRevision {
      let modeChanged = coordinator.lastMode != viewportStore.mode
      let shouldScrollToSelection = modeChanged || coordinator.lastScrollTrigger != scrollToSelectionTrigger
      coordinator.lastRevision = viewportRevision
      coordinator.lastMode = viewportStore.mode
      coordinator.lastScrollTrigger = scrollToSelectionTrigger

      let previousOrigin = scrollView.contentView.bounds.origin
      let savedSelection = selectedLineNumber

      coordinator.isProgrammaticUpdate = true
      tableView.reloadData()

      var restoredRow: Int?
      if let savedSelection, let targetRow = viewportStore.rowIndex(forLineNumber: savedSelection) {
        tableView.selectRowIndexes(IndexSet(integer: targetRow), byExtendingSelection: false)
        restoredRow = targetRow
      }

      if shouldScrollToSelection {
        if let restoredRow {
          tableView.scrollRowToVisible(restoredRow)
        } else if modeChanged {
          scrollView.contentView.scroll(to: .zero)
          scrollView.reflectScrolledClipView(scrollView.contentView)
        }
      } else if previousOrigin.y > 0 || previousOrigin.x > 0 {
        let contentHeight = tableView.bounds.height
        let clipHeight = scrollView.contentView.bounds.height
        let maxY = max(0, contentHeight - clipHeight)
        let clampedOrigin = NSPoint(x: previousOrigin.x, y: min(previousOrigin.y, maxY))
        scrollView.contentView.scroll(to: clampedOrigin)
        scrollView.reflectScrolledClipView(scrollView.contentView)
      }

      coordinator.isProgrammaticUpdate = false
    } else {
      syncExternalSelection(tableView: tableView, coordinator: coordinator)
    }
  }

  private func syncExternalSelection(tableView: NSTableView, coordinator: Coordinator) {
    let desiredRow: Int?
    if let selectedLineNumber {
      desiredRow = viewportStore.rowIndex(forLineNumber: selectedLineNumber)
    } else {
      desiredRow = nil
    }

    let scrollRequested = coordinator.lastScrollTrigger != scrollToSelectionTrigger
    let currentRow = tableView.selectedRow >= 0 ? tableView.selectedRow : nil
    guard currentRow != desiredRow || scrollRequested else { return }

    coordinator.lastScrollTrigger = scrollToSelectionTrigger
    coordinator.isProgrammaticUpdate = true
    if let desiredRow {
      tableView.selectRowIndexes(IndexSet(integer: desiredRow), byExtendingSelection: false)
      if scrollRequested {
        tableView.scrollRowToVisible(desiredRow)
      }
    } else {
      tableView.deselectAll(nil)
    }
    coordinator.isProgrammaticUpdate = false
  }

  enum ColumnSpec: CaseIterable {
    case lineNumber
    case timestamp
    case level
    case traceID
    case content

    var identifier: NSUserInterfaceItemIdentifier {
      switch self {
      case .lineNumber: return NSUserInterfaceItemIdentifier("lineNumber")
      case .timestamp: return NSUserInterfaceItemIdentifier("timestamp")
      case .level: return NSUserInterfaceItemIdentifier("level")
      case .traceID: return NSUserInterfaceItemIdentifier("traceID")
      case .content: return NSUserInterfaceItemIdentifier("content")
      }
    }

    var title: String {
      switch self {
      case .lineNumber: return "#"
      case .timestamp: return "Timestamp"
      case .level: return "Level"
      case .traceID: return "Trace ID"
      case .content: return "Content"
      }
    }

    var defaultWidth: CGFloat {
      switch self {
      case .lineNumber: return 68
      case .timestamp: return 170
      case .level: return 62
      case .traceID: return 145
      case .content: return 540
      }
    }

    var minWidth: CGFloat {
      switch self {
      case .lineNumber: return 48
      case .timestamp: return 110
      case .level: return 50
      case .traceID: return 90
      case .content: return 220
      }
    }

    var maxWidth: CGFloat {
      switch self {
      case .lineNumber: return 110
      case .timestamp: return 280
      case .level: return 96
      case .traceID: return 320
      case .content: return 10_000
      }
    }

    var alignment: NSTextAlignment {
      switch self {
      case .lineNumber: return .right
      default: return .left
      }
    }

    var resizingMask: NSTableColumn.ResizingOptions {
      switch self {
      case .content: return [.autoresizingMask, .userResizingMask]
      default: return .userResizingMask
      }
    }

    init?(identifier: NSUserInterfaceItemIdentifier?) {
      guard let identifier else { return nil }
      guard let match = ColumnSpec.allCases.first(where: { $0.identifier == identifier }) else {
        return nil
      }
      self = match
    }
  }

  @MainActor
  final class Coordinator: NSObject, NSTableViewDataSource, NSTableViewDelegate, NSMenuDelegate {
    var viewportStore: ViewportStore
    var isolatedTraceID: String?
    var selectedLineNumberBinding: Binding<UInt64?>
    var onTraceIDClick: (String) -> Void
    var onClearTraceIsolation: () -> Void
    weak var tableView: NSTableView?
    weak var scrollView: NSScrollView?
    var lastRevision: UInt64 = 0
    var lastMode: ViewportMode = .fullFile
    var lastScrollTrigger: UInt64 = 0
    var isProgrammaticUpdate = false

    private let lineNumberFont = NSFont.monospacedDigitSystemFont(ofSize: 11.5, weight: .regular)
    private let monoRegularFont = NSFont.monospacedSystemFont(ofSize: 12, weight: .regular)
    private let monoMediumFont = NSFont.monospacedSystemFont(ofSize: 11, weight: .medium)

    init(
      viewportStore: ViewportStore,
      isolatedTraceID: String?,
      selectedLineNumber: Binding<UInt64?>,
      onTraceIDClick: @escaping (String) -> Void,
      onClearTraceIsolation: @escaping () -> Void
    ) {
      self.viewportStore = viewportStore
      self.isolatedTraceID = isolatedTraceID
      self.selectedLineNumberBinding = selectedLineNumber
      self.onTraceIDClick = onTraceIDClick
      self.onClearTraceIsolation = onClearTraceIsolation
    }

    func numberOfRows(in tableView: NSTableView) -> Int {
      viewportStore.rowCount
    }

    func tableView(_ tableView: NSTableView, viewFor tableColumn: NSTableColumn?, row: Int)
      -> NSView?
    {
      guard let columnSpec = ColumnSpec(identifier: tableColumn?.identifier) else {
        return nil
      }

      let cellView = reusableCellView(in: tableView, columnSpec: columnSpec)
      guard let textField = cellView.textField else {
        return cellView
      }

      guard let line = viewportStore.line(atRow: row) else {
        textField.stringValue = ""
        return cellView
      }

      switch columnSpec {
      case .lineNumber:
        textField.font = lineNumberFont
        textField.textColor = .secondaryLabelColor
        textField.stringValue = "\(line.lineNumber)"

      case .timestamp:
        textField.font = lineNumberFont
        textField.textColor = .secondaryLabelColor
        textField.stringValue = line.fields.timestamp ?? ""

      case .level:
        textField.font = monoMediumFont
        let levelText = line.fields.level ?? ""
        textField.stringValue = levelText
        textField.textColor = levelColor(for: levelText)

      case .traceID:
        textField.font = monoRegularFont
        let traceText = line.fields.traceID ?? ""
        textField.stringValue = traceText
        if !traceText.isEmpty {
          textField.textColor = (traceText == isolatedTraceID) ? .controlAccentColor : .linkColor
        } else {
          textField.textColor = .secondaryLabelColor
        }

      case .content:
        textField.font = monoRegularFont
        if line.exceedsMaxLength {
          textField.textColor = .secondaryLabelColor
          textField.stringValue = "[oversized line: \(line.byteLength) bytes skipped]"
        } else {
          textField.textColor = .labelColor
          textField.stringValue = line.content
        }
      }

      return cellView
    }

    func tableViewSelectionDidChange(_ notification: Notification) {
      guard !isProgrammaticUpdate, let tableView else { return }
      let row = tableView.selectedRow
      if row >= 0, let line = viewportStore.line(atRow: row) {
        selectedLineNumberBinding.wrappedValue = line.lineNumber
      } else {
        selectedLineNumberBinding.wrappedValue = nil
      }
    }

    @objc func handleTableClick(_ sender: NSTableView) {
      let row = sender.clickedRow
      let col = sender.clickedColumn
      guard row >= 0, col >= 0, col < sender.tableColumns.count else { return }
      guard sender.tableColumns[col].identifier == ColumnSpec.traceID.identifier else { return }
      guard let line = viewportStore.line(atRow: row),
        let traceID = line.fields.traceID,
        !traceID.isEmpty
      else {
        return
      }
      onTraceIDClick(traceID)
    }

    func menuNeedsUpdate(_ menu: NSMenu) {
      menu.removeAllItems()
      guard let tableView else { return }
      let targetRow = tableView.clickedRow >= 0 ? tableView.clickedRow : tableView.selectedRow

      if targetRow >= 0,
        let line = viewportStore.line(atRow: targetRow),
        let traceID = line.fields.traceID,
        !traceID.isEmpty
      {
        let item = NSMenuItem(
          title: "Isolate Trace \"\(traceID)\"",
          action: #selector(handleIsolateTraceMenuItem(_:)),
          keyEquivalent: ""
        )
        item.target = self
        item.representedObject = traceID
        menu.addItem(item)
      }

      if isolatedTraceID != nil {
        let clearItem = NSMenuItem(
          title: "Clear Trace Isolation",
          action: #selector(handleClearTraceMenuItem(_:)),
          keyEquivalent: ""
        )
        clearItem.target = self
        menu.addItem(clearItem)
      }
    }

    @objc private func handleIsolateTraceMenuItem(_ sender: NSMenuItem) {
      guard let traceID = sender.representedObject as? String, !traceID.isEmpty else { return }
      onTraceIDClick(traceID)
    }

    @objc private func handleClearTraceMenuItem(_ sender: NSMenuItem) {
      onClearTraceIsolation()
    }

    private func reusableCellView(in tableView: NSTableView, columnSpec: ColumnSpec)
      -> NSTableCellView
    {
      if let existing = tableView.makeView(withIdentifier: columnSpec.identifier, owner: nil)
        as? NSTableCellView
      {
        return existing
      }

      let cell = NSTableCellView()
      cell.identifier = columnSpec.identifier

      let textField = NSTextField(labelWithString: "")
      textField.translatesAutoresizingMaskIntoConstraints = false
      textField.isBezeled = false
      textField.drawsBackground = false
      textField.isEditable = false
      textField.isSelectable = false
      textField.lineBreakMode = .byTruncatingTail
      textField.maximumNumberOfLines = 1
      textField.alignment = columnSpec.alignment
      textField.cell?.truncatesLastVisibleLine = true

      cell.addSubview(textField)
      cell.textField = textField

      NSLayoutConstraint.activate([
        textField.leadingAnchor.constraint(equalTo: cell.leadingAnchor, constant: 2),
        textField.trailingAnchor.constraint(equalTo: cell.trailingAnchor, constant: -2),
        textField.centerYAnchor.constraint(equalTo: cell.centerYAnchor),
      ])

      return cell
    }

    private func levelColor(for level: String) -> NSColor {
      switch level.uppercased() {
      case "ERROR", "FATAL", "CRITICAL":
        return .systemRed
      case "WARN", "WARNING":
        return .systemOrange
      case "INFO":
        return .systemBlue
      default:
        return .secondaryLabelColor
      }
    }
  }
}
