#pragma once
#include "NLTranslator.h"
#include "LiteLLMClient.h"
#include <cassert>
#include <iostream>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>
#include <set>
#include <utility>
#include <memory>
#include <filesystem>

namespace nl_translation_data {

static std::set<std::string> tokenize(const std::string& s) {
    std::set<std::string> tokens;
    std::istringstream iss(s);
    std::string tok;
    while (iss >> tok) {
        tokens.insert(tok);
    }
    return tokens;
}

static bool tokenSetMatch(const std::string& a, const std::string& b) {
    return tokenize(a) == tokenize(b);
}

struct TestCase {
    std::string input;
    std::string expected;
    int lineNum;
};

static std::vector<TestCase> loadTestCases(const std::string& path) {
    std::vector<TestCase> cases;
    std::ifstream file(path);
    if (!file.is_open()) {
        std::cerr << "    [ERROR] Cannot open test data: " << path << std::endl;
        return cases;
    }
    std::string line;
    int lineNum = 0;
    while (std::getline(file, line)) {
        lineNum++;
        if (line.empty() || line[0] == '#') continue;
        auto tabPos = line.find('\t');
        if (tabPos == std::string::npos) {
            std::cerr << "    [WARN] Skipping malformed line " << lineNum
                      << ": no tab separator" << std::endl;
            continue;
        }
        cases.push_back({
            line.substr(0, tabPos),
            line.substr(tabPos + 1),
            lineNum
        });
    }
    return cases;
}

static std::string findDataFile() {
    namespace fs = std::filesystem;
    std::vector<std::string> candidates = {
        "tests/data/nl_translation_cases.tsv",
        "../tests/data/nl_translation_cases.tsv",
    };
    auto exe = fs::current_path();
    for (const auto& c : candidates) {
        fs::path p = exe / c;
        if (fs::exists(p)) return p.string();
    }
    return candidates[0];
}

} // namespace nl_translation_data

inline void runNLTranslationDataTests() {
    std::cout << "=== NL Translation Data-Driven Tests ===" << std::endl;

    auto dataPath = nl_translation_data::findDataFile();
    auto cases = nl_translation_data::loadTestCases(dataPath);
    if (cases.empty()) {
        std::cout << "  [SKIP] No test cases loaded from " << dataPath << std::endl;
        return;
    }
    std::cout << "  Loaded " << cases.size() << " test cases from " << dataPath << std::endl;

    auto client = std::make_shared<LiteLLMClient>();
    if (!client->isAvailable()) {
        std::cout << "  [SKIP] LiteLLM not available at "
                  << client->getHost() << ":" << client->getPort() << std::endl;
        return;
    }

    NLTranslator translator(client);
    int localPassed = 0, localFailed = 0;

    for (const auto& tc : cases) {
        auto result = translator.translate(tc.input);
        if (!result.success) {
            std::cout << "    [FAIL] Line " << tc.lineNum
                      << " \"" << tc.input << "\" -> error: " << result.error << std::endl;
            localFailed++;
            failed++;
            continue;
        }

        if (nl_translation_data::tokenSetMatch(result.translatedQuery, tc.expected)) {
            std::cout << "    [PASS] \"" << tc.input << "\" -> \""
                      << result.translatedQuery << "\"" << std::endl;
            localPassed++;
            passed++;
        } else {
            std::cout << "    [FAIL] Line " << tc.lineNum
                      << " \"" << tc.input << "\"" << std::endl;
            std::cout << "           Expected: \"" << tc.expected << "\"" << std::endl;
            std::cout << "           Got:      \"" << result.translatedQuery << "\"" << std::endl;
            localFailed++;
            failed++;
        }
    }

    std::cout << "  Results: " << localPassed << " passed, " << localFailed << " failed"
              << " out of " << cases.size() << " cases" << std::endl;
    std::cout << "=== NL Translation Data-Driven Tests: "
              << (localFailed == 0 ? "ALL PASSED" : "SOME FAILED") << " ===" << std::endl;
}
