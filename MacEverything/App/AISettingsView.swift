import SwiftUI

struct AISettingsView: View {
    @State private var backendType = "ollama"
    @State private var ollamaModel = "qwen2.5:3b"
    @State private var aiServiceRunning = false
    @State private var ollamaAvailable = false
    @State private var checking = false

    var body: some View {
        Form {
            Section("AI Backend") {
                Picker("Backend", selection: $backendType) {
                    Text("Ollama (Local)").tag("ollama")
                    Text("Claude API").tag("claude")
                }
                .pickerStyle(.segmented)

                if backendType == "ollama" {
                    TextField("Model", text: $ollamaModel)
                        .textFieldStyle(.roundedBorder)

                    HStack {
                        Circle()
                            .fill(ollamaAvailable ? Color.green : Color.red)
                            .frame(width: 8, height: 8)
                        Text(ollamaAvailable ? "Ollama is running" : "Ollama not detected")
                            .font(.caption)
                            .foregroundColor(.secondary)
                    }
                }
            }

            Section("AI Service") {
                HStack {
                    Circle()
                        .fill(aiServiceRunning ? Color.green : Color.red)
                        .frame(width: 8, height: 8)
                    Text(aiServiceRunning ? "Running on port 19861" : "Not running")
                        .font(.caption)
                        .foregroundColor(.secondary)
                    Spacer()
                    Button(checking ? "Checking..." : "Check Status") {
                        checkStatus()
                    }
                    .disabled(checking)
                }
            }

            Section("Setup Guide") {
                VStack(alignment: .leading, spacing: 8) {
                    Text("1. Install Ollama:")
                        .font(.caption.bold())
                    Text("brew install ollama")
                        .font(.system(.caption, design: .monospaced))
                        .textSelection(.enabled)

                    Text("2. Pull model:")
                        .font(.caption.bold())
                    Text("ollama pull qwen2.5:3b")
                        .font(.system(.caption, design: .monospaced))
                        .textSelection(.enabled)

                    Text("3. Start AI service:")
                        .font(.caption.bold())
                    Text("cd ai_service && python -m maceverything_ai.server")
                        .font(.system(.caption, design: .monospaced))
                        .textSelection(.enabled)
                }
                .foregroundColor(.secondary)
            }
        }
        .formStyle(.grouped)
        .frame(width: 400, height: 350)
        .onAppear { checkStatus() }
    }

    private func checkStatus() {
        checking = true
        Task {
            let available = await AIServiceClient.shared.isAvailable()
            await MainActor.run {
                aiServiceRunning = available
                checking = false
            }
            if available {
                if let status = try? await AIServiceClient.shared.status() {
                    await MainActor.run {
                        ollamaAvailable = status.backendAvailable
                    }
                }
            }
        }
    }
}
