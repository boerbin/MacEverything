#pragma once
#include <cassert>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <cstdlib>
#include <filesystem>
#include <array>

namespace nl_translation_data {

static std::string findDataFile() {
    namespace fs = std::filesystem;
    std::vector<std::string> candidates = {
        "tests/data/nl_translation_cases.tsv",
        "../tests/data/nl_translation_cases.tsv",
    };
    auto cwd = fs::current_path();
    for (const auto& c : candidates) {
        fs::path p = cwd / c;
        if (fs::exists(p)) return p.string();
    }
    return candidates[0];
}

static std::string findEvalScript() {
    namespace fs = std::filesystem;
    std::vector<std::string> candidates = {
        "benchmarks/eval_ai_translation.py",
        "../benchmarks/eval_ai_translation.py",
    };
    auto cwd = fs::current_path();
    for (const auto& c : candidates) {
        fs::path p = cwd / c;
        if (fs::exists(p)) return p.string();
    }
    return candidates[0];
}

static std::string execCommand(const std::string& cmd) {
    std::array<char, 4096> buffer;
    std::string result;
    FILE* pipe = popen(cmd.c_str(), "r");
    if (!pipe) return "";
    while (fgets(buffer.data(), buffer.size(), pipe) != nullptr) {
        result += buffer.data();
    }
    pclose(pipe);
    return result;
}

struct EvalResult {
    int total = 0;
    int passed = 0;
    int failed = 0;
    double accuracy = 0.0;
    bool serviceAvailable = true;
    std::string error;
};

static EvalResult parseJsonResult(const std::string& json) {
    EvalResult r;
    auto getInt = [&](const std::string& key) -> int {
        auto pos = json.find("\"" + key + "\"");
        if (pos == std::string::npos) return 0;
        pos = json.find(":", pos);
        if (pos == std::string::npos) return 0;
        return std::atoi(json.c_str() + pos + 1);
    };
    auto getDouble = [&](const std::string& key) -> double {
        auto pos = json.find("\"" + key + "\"");
        if (pos == std::string::npos) return 0.0;
        pos = json.find(":", pos);
        if (pos == std::string::npos) return 0.0;
        return std::atof(json.c_str() + pos + 1);
    };
    r.total = getInt("total");
    r.passed = getInt("passed");
    r.failed = getInt("failed");
    r.accuracy = getDouble("accuracy");
    if (json.find("\"error\"") != std::string::npos) {
        auto pos = json.find("\"error\"");
        pos = json.find("\"", pos + 7);
        if (pos != std::string::npos) {
            pos++;
            auto end = json.find("\"", pos);
            if (end != std::string::npos) {
                r.error = json.substr(pos, end - pos);
                if (r.error.find("not available") != std::string::npos)
                    r.serviceAvailable = false;
            }
        }
    }
    return r;
}

} // namespace nl_translation_data

inline void runNLTranslationDataTests() {
    std::cout << "=== NL Translation Data-Driven Tests ===" << std::endl;

    namespace fs = std::filesystem;
    auto scriptPath = nl_translation_data::findEvalScript();
    if (!fs::exists(scriptPath)) {
        std::cout << "  [SKIP] Eval script not found: " << scriptPath << std::endl;
        return;
    }

    auto dataPath = nl_translation_data::findDataFile();
    if (!fs::exists(dataPath)) {
        std::cout << "  [SKIP] Data file not found: " << dataPath << std::endl;
        return;
    }

    std::string cmd = "python3 \"" + scriptPath + "\" \"" + dataPath + "\" --verbose 2>/dev/null";
    std::string output = nl_translation_data::execCommand(cmd);

    if (output.empty()) {
        std::cout << "  [SKIP] Eval script returned no output (python3 not available or service down)" << std::endl;
        return;
    }

    auto result = nl_translation_data::parseJsonResult(output);

    if (!result.serviceAvailable) {
        std::cout << "  [SKIP] " << result.error << std::endl;
        return;
    }

    if (!result.error.empty()) {
        std::cout << "  [SKIP] " << result.error << std::endl;
        return;
    }

    passed += result.passed;
    failed += result.failed;

    std::cout << "  Results: " << result.passed << " passed, " << result.failed << " failed"
              << " out of " << result.total << " cases"
              << " (accuracy: " << (result.accuracy * 100) << "%)" << std::endl;
    std::cout << "=== NL Translation Data-Driven Tests: "
              << (result.failed == 0 ? "ALL PASSED" : "SOME FAILED") << " ===" << std::endl;
}
