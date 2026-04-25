import SwiftUI

struct AISetupView: View {
    @Environment(\.dismiss) var dismiss
    @State private var status = AISetupHelper.SetupStatus()
    @State private var isChecking = true

    var body: some View {
        VStack(spacing: 16) {
            Text("AI Setup")
                .font(.title2.bold())

            Text("MacEverything needs these components for AI-powered search.")
                .font(.caption)
                .foregroundColor(.secondary)
                .multilineTextAlignment(.center)

            // Status checklist
            VStack(alignment: .leading, spacing: 8) {
                AISetupStatusRow(label: "Ollama installed", ok: status.ollamaInstalled, checking: isChecking)
                AISetupStatusRow(label: "Ollama running", ok: status.ollamaRunning, checking: isChecking)
                AISetupStatusRow(label: "Model: qwen2.5:3b", ok: status.chatModelAvailable, checking: isChecking)
                AISetupStatusRow(label: "Model: bge-m3", ok: status.embedModelAvailable, checking: isChecking)
                AISetupStatusRow(label: "LiteLLM installed", ok: status.litellmInstalled, checking: isChecking)
                AISetupStatusRow(label: "LiteLLM running", ok: status.litellmRunning, checking: isChecking)
            }
            .padding(.horizontal)

            Divider()

            // Commands preview
            if !status.allReady && !isChecking {
                VStack(alignment: .leading, spacing: 4) {
                    Text("Commands to execute:")
                        .font(.caption.bold())

                    ScrollView {
                        VStack(alignment: .leading, spacing: 2) {
                            ForEach(status.missingSteps, id: \.self) { step in
                                HStack(spacing: 4) {
                                    Text("$")
                                        .foregroundColor(.green.opacity(0.7))
                                    Text(step.command)
                                }
                                .font(.system(.caption, design: .monospaced))
                                .foregroundColor(.secondary)
                            }
                        }
                        .frame(maxWidth: .infinity, alignment: .leading)
                    }
                    .frame(maxHeight: 80)
                    .padding(8)
                    .background(Color(nsColor: .textBackgroundColor).opacity(0.5))
                    .cornerRadius(6)
                }
                .padding(.horizontal)
            }

            Spacer()

            // Buttons
            HStack {
                Button("Cancel") { dismiss() }
                    .keyboardShortcut(.cancelAction)

                Spacer()

                if status.allReady {
                    Button("Done") { dismiss() }
                        .keyboardShortcut(.defaultAction)
                        .buttonStyle(.borderedProminent)
                } else if !isChecking {
                    Button("Re-check") {
                        Task { await recheck() }
                    }

                    Button("Execute in Terminal") {
                        AISetupHelper.executeInTerminal(steps: status.missingSteps)
                    }
                    .keyboardShortcut(.defaultAction)
                    .buttonStyle(.borderedProminent)
                }
            }
        }
        .padding(24)
        .frame(width: 480, height: 420)
        .task { await recheck() }
    }

    private func recheck() async {
        isChecking = true
        status = await AISetupHelper.checkAll()
        isChecking = false
    }
}

private struct AISetupStatusRow: View {
    let label: String
    let ok: Bool
    let checking: Bool

    var body: some View {
        HStack(spacing: 8) {
            if checking {
                ProgressView()
                    .controlSize(.small)
                    .frame(width: 12, height: 12)
            } else {
                Image(systemName: ok ? "checkmark.circle.fill" : "xmark.circle.fill")
                    .foregroundColor(ok ? .green : .red)
                    .font(.system(size: 14))
            }
            Text(label)
                .font(.body)
            Spacer()
        }
    }
}
