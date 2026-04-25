#pragma once
#include "NLTranslator.h"
#include <cassert>
#include <iostream>
#include <string>

inline void runNLTranslatorTests() {
    std::cout << "=== NLTranslator Tests ===" << std::endl;

    // Test 1: Syntax passthrough — queries with known filters bypass LLM
    {
        NLTranslator translator(nullptr);  // no LLM client needed
        assert(NLTranslator::looksLikeQuerySyntax("ext:pdf size:>1mb"));
        assert(NLTranslator::looksLikeQuerySyntax("path:Downloads ext:py"));
        assert(NLTranslator::looksLikeQuerySyntax("dm:today"));
        assert(NLTranslator::looksLikeQuerySyntax("content:TODO"));
        assert(NLTranslator::looksLikeQuerySyntax("pic:"));
        assert(!NLTranslator::looksLikeQuerySyntax("最近下载的PDF"));
        assert(!NLTranslator::looksLikeQuerySyntax("recent large files"));
        assert(!NLTranslator::looksLikeQuerySyntax("hello world"));
        std::cout << "  [PASS] looksLikeQuerySyntax" << std::endl;
    }

    // Test 2: Clean markdown fences
    {
        assert(NLTranslator::cleanLLMResponse("```\npath:Downloads ext:pdf\n```") == "path:Downloads ext:pdf");
        assert(NLTranslator::cleanLLMResponse("```text\npath:Downloads\n```") == "path:Downloads");
        std::cout << "  [PASS] cleanLLMResponse markdown fences" << std::endl;
    }

    // Test 3: Clean explanation prefixes
    {
        assert(NLTranslator::cleanLLMResponse("Query: path:Downloads ext:pdf") == "path:Downloads ext:pdf");
        assert(NLTranslator::cleanLLMResponse("Result: ext:py dm:today") == "ext:py dm:today");
        assert(NLTranslator::cleanLLMResponse("Translation: ext:md") == "ext:md");
        assert(NLTranslator::cleanLLMResponse("查询: ext:pdf") == "ext:pdf");
        std::cout << "  [PASS] cleanLLMResponse prefixes" << std::endl;
    }

    // Test 4: Clean whitespace and quotes
    {
        assert(NLTranslator::cleanLLMResponse("  ext:pdf  \n") == "ext:pdf");
        assert(NLTranslator::cleanLLMResponse("\"ext:pdf dm:today\"") == "ext:pdf dm:today");
        assert(NLTranslator::cleanLLMResponse("'ext:pdf'") == "ext:pdf");
        std::cout << "  [PASS] cleanLLMResponse whitespace/quotes" << std::endl;
    }

    // Test 5: Multi-line response takes first line only
    {
        assert(NLTranslator::cleanLLMResponse("ext:pdf\nThis searches for PDF files") == "ext:pdf");
        std::cout << "  [PASS] cleanLLMResponse first line only" << std::endl;
    }

    // Test 6: Empty input
    {
        assert(NLTranslator::cleanLLMResponse("") == "");
        assert(NLTranslator::cleanLLMResponse("   ") == "");
        std::cout << "  [PASS] cleanLLMResponse empty" << std::endl;
    }

    // Test 7: Translate with syntax passthrough
    {
        NLTranslator translator(nullptr);
        auto result = translator.translate("ext:pdf size:>1mb");
        assert(result.success);
        assert(result.alreadySyntax);
        assert(result.translatedQuery == "ext:pdf size:>1mb");
        std::cout << "  [PASS] translate syntax passthrough" << std::endl;
    }

    // Test 8: Translate empty query
    {
        NLTranslator translator(nullptr);
        auto result = translator.translate("");
        assert(!result.success);
        assert(result.error.find("Empty") != std::string::npos);
        std::cout << "  [PASS] translate empty query" << std::endl;
    }

    // Test 9: System prompt contains required elements
    {
        auto prompt = NLTranslator::getSystemPrompt();
        assert(prompt.find("ext:") != std::string::npos);
        assert(prompt.find("dm:") != std::string::npos);
        assert(prompt.find("path:") != std::string::npos);
        assert(prompt.find("size:") != std::string::npos);
        assert(prompt.find("pic:") != std::string::npos);
        assert(prompt.find("content:") != std::string::npos);
        std::cout << "  [PASS] system prompt contains syntax reference" << std::endl;
    }

    // Test 10: Few-shot examples are populated
    {
        auto examples = NLTranslator::getFewShotExamples();
        assert(examples.size() >= 8);
        // First example should be Chinese
        assert(!examples[0].first.empty());
        assert(!examples[0].second.empty());
        std::cout << "  [PASS] few-shot examples populated (" << examples.size() << " examples)" << std::endl;
    }

    // Test 11: buildMessages constructs correct structure
    {
        auto messages = NLTranslator::buildMessages("最近的PDF");
        assert(messages.size() >= 3);  // system + at least 1 few-shot pair + user
        assert(messages[0].first == "system");
        assert(messages.back().first == "user");
        assert(messages.back().second == "最近的PDF");
        std::cout << "  [PASS] buildMessages structure" << std::endl;
    }

    std::cout << "=== NLTranslator Tests: ALL PASSED ===" << std::endl;
}
