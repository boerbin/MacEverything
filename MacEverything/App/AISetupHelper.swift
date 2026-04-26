import Foundation
import AppKit

@MainActor
final class AISetupHelper {

    struct SetupStatus {
        var ollamaInstalled = false
        var ollamaRunning = false
        var chatModelAvailable = false    // qwen2.5:3b
        var embedModelAvailable = false   // bge-m3
        var litellmInstalled = false
        var litellmRunning = false

        var allReady: Bool {
            ollamaInstalled && ollamaRunning && chatModelAvailable
            && embedModelAvailable && litellmInstalled && litellmRunning
        }

        var missingSteps: [SetupStep] {
            var steps: [SetupStep] = []
            if !ollamaInstalled { steps.append(.installOllama) }
            if !ollamaRunning { steps.append(.startOllama) }
            if !chatModelAvailable { steps.append(.pullChatModel) }
            if !embedModelAvailable { steps.append(.pullEmbedModel) }
            if !litellmInstalled { steps.append(.installLiteLLM) }
            if !litellmRunning { steps.append(.startLiteLLM) }
            return steps
        }
    }

    enum SetupStep: Hashable {
        case installOllama
        case startOllama
        case pullChatModel
        case pullEmbedModel
        case installLiteLLM
        case startLiteLLM

        var command: String {
            switch self {
            case .installOllama: return "brew install ollama"
            case .startOllama: return "ollama serve &"
            case .pullChatModel: return "ollama pull qwen2.5:3b"
            case .pullEmbedModel: return "ollama pull bge-m3"
            case .installLiteLLM: return "pip3 install 'litellm[proxy]'"
            case .startLiteLLM: return "litellm --config config.yaml --port 19861"
            }
        }

        var description: String {
            switch self {
            case .installOllama: return "Install Ollama"
            case .startOllama: return "Start Ollama service"
            case .pullChatModel: return "Download chat model (qwen2.5:3b)"
            case .pullEmbedModel: return "Download embedding model (bge-m3)"
            case .installLiteLLM: return "Install LiteLLM gateway"
            case .startLiteLLM: return "Start LiteLLM gateway"
            }
        }
    }

    // MARK: - Detection

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
        guard let url = URL(string: "http://localhost:11434/api/tags") else { return false }
        var request = URLRequest(url: url)
        request.timeoutInterval = 2
        do {
            let (_, response) = try await URLSession.shared.data(for: request)
            return (response as? HTTPURLResponse)?.statusCode == 200
        } catch {
            return false
        }
    }

    static func isModelAvailable(_ model: String) async -> Bool {
        guard let url = URL(string: "http://localhost:11434/api/tags") else { return false }
        var request = URLRequest(url: url)
        request.timeoutInterval = 2
        do {
            let (data, response) = try await URLSession.shared.data(for: request)
            guard (response as? HTTPURLResponse)?.statusCode == 200 else { return false }
            if let json = try JSONSerialization.jsonObject(with: data) as? [String: Any],
               let models = json["models"] as? [[String: Any]] {
                return models.contains { ($0["name"] as? String)?.hasPrefix(model) == true }
            }
            return false
        } catch {
            return false
        }
    }

    static func isLiteLLMInstalled() -> Bool {
        let process = Process()
        process.executableURL = URL(fileURLWithPath: "/usr/bin/which")
        process.arguments = ["litellm"]
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

    static func isLiteLLMRunning() async -> Bool {
        guard let url = URL(string: "http://localhost:19861/v1/models") else { return false }
        var request = URLRequest(url: url)
        request.timeoutInterval = 2
        do {
            let (_, response) = try await URLSession.shared.data(for: request)
            return (response as? HTTPURLResponse)?.statusCode == 200
        } catch {
            return false
        }
    }

    static func checkAll() async -> SetupStatus {
        var status = SetupStatus()
        status.ollamaInstalled = isOllamaInstalled()
        status.ollamaRunning = await isOllamaRunning()
        if status.ollamaRunning {
            status.chatModelAvailable = await isModelAvailable("qwen2.5:3b")
            status.embedModelAvailable = await isModelAvailable("bge-m3")
        }
        status.litellmInstalled = isLiteLLMInstalled()
        status.litellmRunning = await isLiteLLMRunning()
        return status
    }

    // MARK: - Terminal Execution

    static func executeInTerminal(steps: [SetupStep]) {
        // Write litellm config to /tmp
        let litellmConfig = """
        model_list:
          - model_name: "translate"
            litellm_params:
              model: "ollama/qwen2.5:3b"
              api_base: "http://localhost:11434"
          - model_name: "embed"
            litellm_params:
              model: "ollama/bge-m3"
              api_base: "http://localhost:11434"
          - model_name: "summarize"
            litellm_params:
              model: "ollama/qwen2.5:3b"
              api_base: "http://localhost:11434"
        general_settings:
          master_key: ""
        """
        let configPath = "/tmp/maceverything_litellm_config.yaml"
        try? litellmConfig.write(toFile: configPath, atomically: true, encoding: .utf8)

        // Build bash script with only needed steps
        var script = """
        #!/bin/bash

        echo "======================================"
        echo "  MacEverything AI Setup"
        echo "======================================"
        echo ""

        """

        let total = steps.count
        for (i, step) in steps.enumerated() {
            let num = i + 1
            switch step {
            case .installOllama:
                script += """
                echo "[\(num)/\(total)] Installing Ollama..."
                if command -v ollama &> /dev/null; then
                    echo "  OK: Already installed"
                else
                    brew install ollama
                    echo "  OK: Installed"
                fi

                """
            case .startOllama:
                script += """
                echo "[\(num)/\(total)] Starting Ollama..."
                if curl -s http://localhost:11434/api/tags > /dev/null 2>&1; then
                    echo "  OK: Already running"
                else
                    ollama serve &
                    sleep 3
                    echo "  OK: Started"
                fi

                """
            case .pullChatModel:
                script += """
                echo "[\(num)/\(total)] Pulling qwen2.5:3b..."
                ollama pull qwen2.5:3b
                echo "  OK: Done"

                """
            case .pullEmbedModel:
                script += """
                echo "[\(num)/\(total)] Pulling bge-m3..."
                ollama pull bge-m3
                echo "  OK: Done"

                """
            case .installLiteLLM:
                script += """
                echo "[\(num)/\(total)] Installing LiteLLM..."
                if command -v litellm &> /dev/null; then
                    echo "  OK: Already installed"
                else
                    pip3 install 'litellm[proxy]'
                    echo "  OK: Installed"
                fi

                """
            case .startLiteLLM:
                script += """
                echo "[\(num)/\(total)] Starting LiteLLM gateway..."
                if curl -s http://localhost:19861/v1/models > /dev/null 2>&1; then
                    echo "  OK: Already running"
                else
                    nohup litellm --config \(configPath) --port 19861 > /tmp/litellm.log 2>&1 &
                    sleep 3
                    if curl -s http://localhost:19861/v1/models > /dev/null 2>&1; then
                        echo "  OK: Started on port 19861"
                    else
                        echo "  FAIL: Failed to start. Check /tmp/litellm.log"
                    fi
                fi

                """
            }
        }

        script += """
        echo ""
        echo "======================================"
        echo "  Setup Complete!"
        echo "  Return to MacEverything and click"
        echo "  Re-check to verify."
        echo "======================================"
        echo ""
        read -n 1 -s -r -p "Press any key to close this window..."
        exit 0
        """

        // Write script and execute in Terminal
        let scriptPath = "/tmp/maceverything_ai_setup.sh"
        try? script.write(toFile: scriptPath, atomically: true, encoding: .utf8)
        try? FileManager.default.setAttributes([.posixPermissions: 0o755], ofItemAtPath: scriptPath)

        let appleScript = """
        tell application "Terminal"
            activate
            set newTab to do script "\(scriptPath)"
            set current settings of newTab to settings set "Basic"
        end tell
        """
        var error: NSDictionary?
        NSAppleScript(source: appleScript)?.executeAndReturnError(&error)
    }
}
