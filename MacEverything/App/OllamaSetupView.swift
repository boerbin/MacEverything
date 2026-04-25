import SwiftUI

struct OllamaSetupView: View {
    @Environment(\.dismiss) private var dismiss
    @State private var ollamaInstalled = false
    @State private var ollamaRunning = false
    @State private var modelAvailable = false
    @State private var checking = true

    var allReady: Bool { ollamaInstalled && ollamaRunning && modelAvailable }

    var body: some View {
        VStack(alignment: .leading, spacing: 16) {
            HStack {
                Image(systemName: "cpu")
                    .font(.title2)
                    .foregroundColor(.purple)
                Text("AI Search Setup")
                    .font(.title2)
                    .fontWeight(.semibold)
            }

            Text("MacEverything uses Ollama to run a local AI model for natural language search. The following components are required:")
                .font(.callout)
                .foregroundColor(.secondary)

            VStack(alignment: .leading, spacing: 8) {
                statusRow("Ollama installed", ready: ollamaInstalled)
                statusRow("Ollama running", ready: ollamaRunning)
                statusRow("Model qwen2.5:3b", ready: modelAvailable)
            }
            .padding(.vertical, 4)

            if allReady {
                HStack {
                    Image(systemName: "checkmark.circle.fill")
                        .foregroundColor(.green)
                    Text("All set! AI search is ready to use.")
                        .fontWeight(.medium)
                }
            } else {
                Text("Commands that will be executed:")
                    .font(.callout)
                    .fontWeight(.medium)

                Text(OllamaSetupHelper.setupCommands)
                    .font(.system(.caption, design: .monospaced))
                    .padding(10)
                    .frame(maxWidth: .infinity, alignment: .leading)
                    .background(Color(nsColor: .textBackgroundColor))
                    .cornerRadius(6)
                    .overlay(
                        RoundedRectangle(cornerRadius: 6)
                            .stroke(Color.gray.opacity(0.3), lineWidth: 1)
                    )
            }

            Spacer()

            HStack {
                Spacer()
                Button("Cancel") {
                    dismiss()
                }
                .keyboardShortcut(.cancelAction)

                if allReady {
                    Button("Done") {
                        dismiss()
                    }
                    .keyboardShortcut(.defaultAction)
                } else {
                    Button("Execute in Terminal") {
                        OllamaSetupHelper.executeSetupInTerminal()
                        dismiss()
                    }
                    .keyboardShortcut(.defaultAction)
                }
            }
        }
        .padding(24)
        .frame(width: 480, height: 380)
        .task {
            await checkStatuses()
        }
    }

    private func statusRow(_ label: String, ready: Bool) -> some View {
        HStack(spacing: 8) {
            if checking {
                ProgressView()
                    .controlSize(.small)
            } else {
                Circle()
                    .fill(ready ? Color.green : Color.red)
                    .frame(width: 8, height: 8)
            }
            Text(label)
                .font(.callout)
        }
    }

    private func checkStatuses() async {
        checking = true
        ollamaInstalled = OllamaSetupHelper.isOllamaInstalled()
        ollamaRunning = await OllamaSetupHelper.isOllamaRunning()
        if ollamaRunning {
            modelAvailable = await OllamaSetupHelper.isModelAvailable()
        }
        checking = false
    }
}
