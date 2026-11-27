import SwiftUI

struct JumpToLineSheet: View {
    @ObservedObject var model: AppModel
    @Environment(\.dismiss) private var dismiss
    @State private var lineInput: String = ""
    @FocusState private var isInputFocused: Bool

    private var parsedLineNumber: UInt64? {
        let trimmed = lineInput.trimmingCharacters(in: .whitespacesAndNewlines)
        guard let value = UInt64(trimmed), value > 0 else {
            return nil
        }
        return value
    }

    var body: some View {
        VStack(alignment: .leading, spacing: 12) {
            Text("Jump to Line")
                .font(.headline)

            TextField("Line number (1...)", text: $lineInput)
                .textFieldStyle(.roundedBorder)
                .font(.system(.body, design: .monospaced).monospacedDigit())
                .focused($isInputFocused)
                .onSubmit(submit)

            HStack {
                Spacer()
                Button("Cancel") {
                    dismiss()
                }
                .keyboardShortcut(.cancelAction)

                Button("Jump") {
                    submit()
                }
                .keyboardShortcut(.defaultAction)
                .disabled(parsedLineNumber == nil)
            }
        }
        .padding(18)
        .frame(width: 280)
        .onAppear {
            if let selected = model.selectedLineNumber {
                lineInput = "\(selected)"
            }
            isInputFocused = true
        }
    }

    private func submit() {
        guard let target = parsedLineNumber else { return }
        model.jumpToLine(target, clearFiltersIfNeeded: true)
        dismiss()
    }
}
