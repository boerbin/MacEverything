import SwiftUI

struct SemanticSettingsView: View {
    @State private var indexedCount: UInt32 = 0
    @State private var isLiteLLMAvailable: Bool = false

    private let bridge = MacSearchBridge.shared()

    var body: some View {
        Form {
            Section("LiteLLM Status") {
                HStack(spacing: 6) {
                    Circle()
                        .fill(isLiteLLMAvailable ? Color.green : Color.red)
                        .frame(width: 8, height: 8)
                    Text(isLiteLLMAvailable ? "Connected" : "Unavailable")
                        .foregroundColor(isLiteLLMAvailable ? .green : .red)
                        .fontWeight(.medium)
                }
            }

            Section("Index Status") {
                Text("Indexed Files: \(indexedCount)")
                    .font(.headline)
            }

            Section {
                Text("File types and size limits follow Content Settings.")
                    .font(.callout)
                    .foregroundColor(.secondary)
            }

            Section {
                Button("Rebuild Semantic Index") {
                    DispatchQueue.global().async {
                        bridge.rebuildSemanticIndex()
                    }
                }
            }
        }
        .padding()
        .frame(width: 400, height: 280)
        .onAppear { loadSettings() }
    }

    private func loadSettings() {
        indexedCount = bridge.semanticIndexedCount()
        isLiteLLMAvailable = bridge.isLiteLLMAvailable()
    }
}
