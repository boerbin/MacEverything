import SwiftUI

struct SemanticSettingsView: View {
    @State private var extensions: [String] = []
    @State private var newExtension: String = ""
    @State private var maxFileSizeMB: Double = 1.0
    @State private var indexedCount: UInt32 = 0
    @State private var isLiteLLMAvailable: Bool = false

    @State private var initialExtensions: [String] = []
    @State private var initialMaxFileSizeMB: Double = 1.0

    private let bridge = MacSearchBridge.shared()

    private static let defaultExtensions = ["md", "txt", "py", "js", "ts", "go", "rs", "cpp", "h", "swift", "java"]

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

            Section("Semantic Index Extensions") {
                VStack(alignment: .leading, spacing: 8) {
                    ScrollView {
                        FlowLayout(spacing: 4) {
                            ForEach(extensions, id: \.self) { ext in
                                HStack(spacing: 2) {
                                    Text(".\(ext)")
                                        .font(.caption)
                                    Button {
                                        removeExtension(ext)
                                    } label: {
                                        Image(systemName: "xmark.circle.fill")
                                            .font(.caption2)
                                    }
                                    .buttonStyle(.plain)
                                }
                                .padding(.horizontal, 6)
                                .padding(.vertical, 2)
                                .background(Capsule().fill(Color.secondary.opacity(0.2)))
                            }
                        }
                    }
                    .frame(maxHeight: 120)

                    HStack {
                        TextField("Add extension...", text: $newExtension)
                            .textFieldStyle(.roundedBorder)
                            .onSubmit { addExtension() }
                        Button("Add") { addExtension() }
                            .disabled(newExtension.isEmpty)
                    }
                }
            }

            Section("Max File Size") {
                HStack {
                    Slider(value: $maxFileSizeMB, in: 0.1...10.0, step: 0.1)
                    Text(String(format: "%.1f MB", maxFileSizeMB))
                        .frame(width: 60)
                }
            }

            Section {
                Button("Apply") {
                    applySettings()
                }
            }
        }
        .padding()
        .frame(width: 400, height: 480)
        .onAppear { loadSettings() }
    }

    private func loadSettings() {
        indexedCount = bridge.semanticIndexedCount()
        isLiteLLMAvailable = bridge.isLiteLLMAvailable()
        let bridgeExts = bridge.semanticExtensions() as? [String]
        extensions = (bridgeExts?.isEmpty == false) ? bridgeExts! : Self.defaultExtensions
        maxFileSizeMB = Double(bridge.semanticMaxFileSize()) / (1024.0 * 1024.0)

        initialExtensions = extensions
        initialMaxFileSizeMB = maxFileSizeMB
    }

    private func addExtension() {
        let ext = newExtension.trimmingCharacters(in: .whitespaces).lowercased()
            .replacingOccurrences(of: ".", with: "")
        guard !ext.isEmpty, !extensions.contains(ext) else { return }
        extensions.append(ext)
        newExtension = ""
    }

    private func removeExtension(_ ext: String) {
        extensions.removeAll { $0 == ext }
    }

    private func applySettings() {
        let dirty = extensions != initialExtensions || maxFileSizeMB != initialMaxFileSizeMB
        guard dirty else { return }
        bridge.setSemanticExtensions(extensions)
        bridge.setSemanticMaxFileSize(UInt64(maxFileSizeMB * 1024 * 1024))
        DispatchQueue.global().async {
            bridge.rebuildSemanticIndex()
        }
        initialExtensions = extensions
        initialMaxFileSizeMB = maxFileSizeMB
    }
}
