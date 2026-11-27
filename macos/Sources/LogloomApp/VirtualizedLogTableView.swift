import AppKit
import LogloomCore
import SwiftUI

final class CopperAccentTableRowView: NSTableRowView {
  override func drawSelection(in dirtyRect: NSRect) {
    guard selectionHighlightStyle != .none else { return }
    let bg =
      isEmphasized
      ? LogloomTheme.nsSelectedRowBg
      : LogloomTheme.nsSelectedRowUnfocusedBg
    bg.setFill()
    bounds.fill()

    let barColor =
      isEmphasized
      ? LogloomTheme.nsCopper
      : LogloomTheme.nsCopper.withAlphaComponent(0.55)
    barColor.setFill()
    NSRect(x: 0, y: 0, width: 3, height: bounds.height).fill()
  }
}

final class LogTableHeaderCell: NSTableHeaderCell {
  override func draw(withFrame cellFrame: NSRect, in controlView: NSView) {
    LogloomTheme.nsCanvas.setFill()
    cellFrame.fill()

    LogloomTheme.nsBorder.setFill()
    NSRect(x: cellFrame.minX, y: cellFrame.maxY - 1, width: cellFrame.width, height: 1).fill()

    drawInterior(withFrame: cellFrame, in: controlView)
  }

  override func drawInterior(withFrame cellFrame: NSRect, in controlView: NSView) {
    let attrs: [NSAttributedString.Key: Any] = [
      .font: NSFont.systemFont(ofSize: 11, weight: .medium),
      .foregroundColor: LogloomTheme.nsTextMuted,
    ]
    let insetX: CGFloat = 6
    let textRect = NSRect(
      x: cellFrame.minX + insetX,
      y: cellFrame.minY + (cellFrame.height - 15) * 0.5,
      width: max(0, cellFrame.width - insetX * 2),
      height: 15
    )
    let para = NSMutableParagraphStyle()
    para.alignment = alignment
    para.lineBreakMode = .byTruncatingTail
    var styledAttrs = attrs
    styledAttrs[.paragraphStyle] = para
    (stringValue as NSString).draw(in: textRect, withAttributes: styledAttrs)
  }
}

final class LogTableHeaderView: NSTableHeaderView {
  override func draw(_ dirtyRect: NSRect) {
    LogloomTheme.nsCanvas.setFill()
    bounds.fill()
    super.draw(dirtyRect)
    LogloomTheme.nsBorder.setFill()
    NSRect(x: 0, y: bounds.maxY - 1, width: bounds.width, height: 1).fill()
  }
}

struct VirtualizedLogTableView: NSViewRepresentable {
  let viewportStore: ViewportStore
  let viewportRevision: UInt64
  let isStructuredMode: Bool
  let isolatedTraceID: String?
  @Binding var selectedLineNumber: UInt64?
  let scrollToSelectionTrigger: UInt64
  let onTraceIDClick: (String) -> Void
  let onClearTraceIsolation: () -> Void
  let onObserveStructured: () -> Void

  func makeCoordinator() -> Coordinator {
    Coordinator(
      viewportStore: viewportStore,
      isStructuredMode: isStructuredMode,
      isolatedTraceID: isolatedTraceID,
      selectedLineNumber: $selectedLineNumber,
      onTraceIDClick: onTraceIDClick,
      onClearTraceIsolation: onClearTraceIsolation,
      onObserveStructured: onObserveStructured
    )
  }

  func makeNSView(context: Context) -> NSScrollView {
    let scrollView = NSScrollView()
    scrollView.hasVerticalScroller = true
    scrollView.hasHorizontalScroller = true
    scrollView.autohidesScrollers = true
    scrollView.borderType = .noBorder
    scrollView.drawsBackground = true
    scrollView.backgroundColor = LogloomTheme.nsCanvas

    let tableView = NSTableView()
    tableView.style = .plain
    tableView.usesAlternatingRowBackgroundColors = false
    tableView.backgroundColor = LogloomTheme.nsCanvas
    tableView.rowHeight = 28
    tableView.usesAutomaticRowHeights = false
    tableView.intercellSpacing = NSSize(width: 8, height: 0)
    tableView.allowsMultipleSelection = false
    tableView.allowsEmptySelection = true
    tableView.allowsColumnSelection = false
    tableView.allowsColumnReordering = false
    tableView.columnAutoresizingStyle = .lastColumnOnlyAutoresizingStyle
    tableView.gridStyleMask = []
    tableView.selectionHighlightStyle = .regular
    tableView.headerView = LogTableHeaderView(frame: NSRect(x: 0, y: 0, width: 0, height: 26))

    for spec in ColumnSpec.allCases {
      let column = NSTableColumn(identifier: spec.identifier)
      let headerCell = LogTableHeaderCell(textCell: spec.title(structured: isStructuredMode))
      headerCell.alignment = spec.alignment
      column.headerCell = headerCell
      column.title = spec.title(structured: isStructuredMode)
      column.width = spec.defaultWidth
      column.minWidth = spec.minWidth
      column.maxWidth = spec.maxWidth
      column.resizingMask = spec.resizingMask
      column.isHidden = spec.isStructuredOnly && !isStructuredMode
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
    coordinator.onObserveStructured = onObserveStructured

    guard let tableView = coordinator.tableView else { return }

    let structureChanged = coordinator.isStructuredMode != isStructuredMode
    coordinator.isStructuredMode = isStructuredMode
    if structureChanged {
      applyColumnVisibility(tableView: tableView, structured: isStructuredMode)
    }

    if coordinator.lastRevision != viewportRevision || structureChanged {
      let modeChanged = coordinator.lastMode != viewportStore.mode
      let shouldScrollToSelection =
        modeChanged || coordinator.lastScrollTrigger != scrollToSelectionTrigger
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

  private func applyColumnVisibility(tableView: NSTableView, structured: Bool) {
    for column in tableView.tableColumns {
      guard let spec = ColumnSpec(identifier: column.identifier) else { continue }
      column.isHidden = spec.isStructuredOnly && !structured
      column.title = spec.title(structured: structured)
    }
    tableView.sizeLastColumnToFit()
  }

  private func syncExternalSelection(tableView: NSTableView, coordinator: Coordinator) {
    let desiredRow: Int? = selectedLineNumber.flatMap { viewportStore.rowIndex(forLineNumber: $0) }
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

    var isStructuredOnly: Bool {
      switch self {
      case .timestamp, .level, .traceID: return true
      case .lineNumber, .content: return false
      }
    }

    func title(structured: Bool) -> String {
      switch self {
      case .lineNumber: return "Line"
      case .timestamp: return "Time · UTC"
      case .level: return "Level"
      case .traceID: return "Trace ID"
      case .content: return structured ? "Message" : "Content"
      }
    }

    var defaultWidth: CGFloat {
      switch self {
      case .lineNumber: return 46
      case .timestamp: return 96
      case .level: return 56
      case .traceID: return 92
      case .content: return 440
      }
    }

    var minWidth: CGFloat {
      switch self {
      case .lineNumber: return 40
      case .timestamp: return 86
      case .level: return 48
      case .traceID: return 76
      case .content: return 160
      }
    }

    var maxWidth: CGFloat {
      switch self {
      case .lineNumber: return 96
      case .timestamp: return 220
      case .level: return 96
      case .traceID: return 260
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
    var isStructuredMode: Bool
    var isolatedTraceID: String?
    var selectedLineNumberBinding: Binding<UInt64?>
    var onTraceIDClick: (String) -> Void
    var onClearTraceIsolation: () -> Void
    var onObserveStructured: () -> Void
    weak var tableView: NSTableView?
    weak var scrollView: NSScrollView?
    var lastRevision: UInt64 = 0
    var lastMode: ViewportMode = .fullFile
    var lastScrollTrigger: UInt64 = 0
    var isProgrammaticUpdate = false
    private var scheduledStructuredNotify = false

    private let lineNumberFont = NSFont.monospacedDigitSystemFont(ofSize: 11.5, weight: .regular)
    private let monoRegularFont = NSFont.monospacedSystemFont(ofSize: 12, weight: .regular)
    private let monoLevelFont = NSFont.monospacedSystemFont(ofSize: 10.5, weight: .semibold)

    init(
      viewportStore: ViewportStore,
      isStructuredMode: Bool,
      isolatedTraceID: String?,
      selectedLineNumber: Binding<UInt64?>,
      onTraceIDClick: @escaping (String) -> Void,
      onClearTraceIsolation: @escaping () -> Void,
      onObserveStructured: @escaping () -> Void
    ) {
      self.viewportStore = viewportStore
      self.isStructuredMode = isStructuredMode
      self.isolatedTraceID = isolatedTraceID
      self.selectedLineNumberBinding = selectedLineNumber
      self.onTraceIDClick = onTraceIDClick
      self.onClearTraceIsolation = onClearTraceIsolation
      self.onObserveStructured = onObserveStructured
    }

    func numberOfRows(in tableView: NSTableView) -> Int {
      viewportStore.rowCount
    }

    func tableView(_ tableView: NSTableView, rowViewForRow row: Int) -> NSTableRowView? {
      CopperAccentTableRowView()
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

      if !isStructuredMode && line.hasStructuredFields && !scheduledStructuredNotify {
        scheduledStructuredNotify = true
        Task { @MainActor [weak self] in
          guard let self else { return }
          self.scheduledStructuredNotify = false
          self.onObserveStructured()
        }
      }

      switch columnSpec {
      case .lineNumber:
        textField.font = lineNumberFont
        textField.textColor = LogloomTheme.nsTextMuted
        textField.stringValue = "\(line.lineNumber)"

      case .timestamp:
        textField.font = lineNumberFont
        textField.textColor = LogloomTheme.nsTextMuted
        textField.stringValue = JSONFormatter.formatTimestampForTable(line.fields.timestamp)

      case .level:
        textField.font = monoLevelFont
        let levelText = (line.fields.level ?? "").uppercased()
        textField.stringValue = levelText
        textField.textColor = LogloomTheme.nsLevelColor(for: levelText)

      case .traceID:
        textField.font = monoRegularFont
        let traceText = line.fields.traceID ?? ""
        textField.stringValue = traceText
        if !traceText.isEmpty {
          textField.textColor =
            (traceText == isolatedTraceID)
            ? LogloomTheme.nsCopper
            : LogloomTheme.nsTextSoft
        } else {
          textField.textColor = LogloomTheme.nsTextMuted
        }

      case .content:
        textField.font = monoRegularFont
        if line.exceedsMaxLength {
          textField.textColor = LogloomTheme.nsTextMuted
          textField.stringValue = "[oversized line: \(line.byteLength) bytes skipped]"
        } else {
          textField.textColor = LogloomTheme.nsTextPrimary
          textField.stringValue = isStructuredMode ? line.displayMessage : line.content
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
  }
}
