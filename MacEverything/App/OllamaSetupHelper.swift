import Foundation
import AppKit

@MainActor
final class OllamaSetupHelper {
    static func isOllamaInstalled() -> Bool {
        let process = Process()
        process.executableURL = URL(fileURLWithPath: "/usr/bin/which")
        process.arguments = ["ollama"]
        process.standardOutput = FileHandle.nullDevice
        process.standardError = FileHandle.nullDevice
        do {
            try process.run()
            process.waitUntilExit()
            return process.terminationStatus == 0
        } catch {
            return false
        }
    }

    static func isOllamaRunning() async -> Bool {
        let url = URL(string: "http://localhost:11434/api/tags")!
        var request = URLRequest(url: url)
        request.timeoutInterval = 2
        do {
            let (_, response) = try await URLSession.shared.data(for: request)
            return (response as? HTTPURLResponse)?.statusCode == 200
        } catch {
            return false
        }
    }

    static func isModelAvailable(_ model: String = "qwen2.5:3b") async -> Bool {
        let url = URL(string: "http://localhost:11434/api/tags")!
        var request = URLRequest(url: url)
        request.timeoutInterval = 2
        do {
            let (data, response) = try await URLSession.shared.data(for: request)
            guard (response as? HTTPURLResponse)?.statusCode == 200 else { return false }
            guard let json = try JSONSerialization.jsonObject(with: data) as? [String: Any],
                  let models = json["models"] as? [[String: Any]] else { return false }
            return models.contains { ($0["name"] as? String)?.hasPrefix(model) == true }
        } catch {
            return false
        }
    }

    static func executeSetupInTerminal() {
        let script = """
        #!/bin/bash
        echo "=== MacEverything AI Setup ==="
        echo ""

        if ! command -v brew &>/dev/null; then
            echo "Error: Homebrew not found. Install from https://brew.sh"
            echo "Press any key to exit..."
            read -n 1
            exit 1
        fi

        if ! command -v ollama &>/dev/null; then
            echo "Installing Ollama..."
            brew install ollama || { echo "Failed to install Ollama"; echo "Press any key to exit..."; read -n 1; exit 1; }
        else
            echo "Ollama already installed."
        fi

        echo "Starting Ollama service..."
        brew services start ollama 2>/dev/null || true
        sleep 3

        echo "Pulling qwen2.5:3b model (may take a few minutes)..."
        ollama pull qwen2.5:3b || { echo "Failed to pull model"; echo "Press any key to exit..."; read -n 1; exit 1; }

        echo ""
        echo "Setup complete! You can now use AI search in MacEverything."
        echo "Press any key to exit..."
        read -n 1
        """

        let tmpPath = "/tmp/maceverything_ollama_setup.sh"
        try? script.write(toFile: tmpPath, atomically: true, encoding: .utf8)
        try? FileManager.default.setAttributes(
            [.posixPermissions: 0o755], ofItemAtPath: tmpPath
        )

        let appleScript = """
        tell application "Terminal"
            activate
            do script "\(tmpPath)"
        end tell
        """
        if let scriptObj = NSAppleScript(source: appleScript) {
            var error: NSDictionary?
            scriptObj.executeAndReturnError(&error)
        }
    }

    static let setupCommands = """
    # 1. Install Ollama (via Homebrew)
    brew install ollama

    # 2. Start Ollama service
    brew services start ollama

    # 3. Pull the AI model (~1.9GB)
    ollama pull qwen2.5:3b
    """
}
