import SwiftUI

struct AISettingsView: View {
    @State private var aiAvailable = false
    @State private var modelName = ""
    @State private var statusText = ""
    @State private var checking = false

    var body: some View {
        Form {
            Section("AI Status") {
                HStack(spacing: 8) {
                    Circle()
                        .fill(aiAvailable ? Color.green : Color.red)
                        .frame(width: 8, height: 8)
                    Text(aiAvailable ? "Ready" : "Not Available")
                        .foregroundColor(aiAvailable ? .green : .red)
                        .fontWeight(.medium)
                    Spacer()
                    Button(checking ? "Checking..." : "Check") {
                        checkStatus()
                    }
                    .disabled(checking)
                }

                if !modelName.isEmpty {
                    HStack {
                        Text("Model:")
                            .foregroundColor(.secondary)
                        Text(modelName)
                            .fontWeight(.medium)
                    }
                }

                if !statusText.isEmpty {
                    HStack {
                        Text("Status:")
                            .foregroundColor(.secondary)
                        Text(statusText)
                    }
                }
            }

            Section("About") {
                VStack(alignment: .leading, spacing: 6) {
                    Text("AI powers natural language search translation.")
                        .font(.callout)
                        .foregroundColor(.secondary)
                    Text("The built-in model loads automatically on startup.")
                        .font(.callout)
                        .foregroundColor(.secondary)
                }
            }
        }
        .formStyle(.grouped)
        .frame(width: 380, height: 220)
        .onAppear { checkStatus() }
    }

    private func checkStatus() {
        checking = true
        Task {
            let available = await AIServiceClient.shared.isAvailable()
            if available, let status = try? await AIServiceClient.shared.status() {
                await MainActor.run {
                    aiAvailable = true
                    modelName = status.model
                    statusText = status.status
                    checking = false
                }
            } else {
                await MainActor.run {
                    aiAvailable = false
                    modelName = ""
                    statusText = available ? "Unknown" : "Service not running"
                    checking = false
                }
            }
        }
    }
}
