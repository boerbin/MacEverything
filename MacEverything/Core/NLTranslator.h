#pragma once
#include <string>
#include <vector>
#include <utility>
#include <memory>

class LiteLLMBackend;

struct TranslationResult {
    std::string originalQuery;
    std::string translatedQuery;
    bool success = false;
    bool alreadySyntax = false;
    std::string error;
};

class NLTranslator {
public:
    explicit NLTranslator(std::shared_ptr<LiteLLMBackend> client);

    TranslationResult translate(const std::string& query);

    // Static utilities (testable without LLM)
    static bool looksLikeQuerySyntax(const std::string& text);
    static std::string cleanLLMResponse(const std::string& raw);
    static std::string getSystemPrompt();
    static std::vector<std::pair<std::string, std::string>> getFewShotExamples();
    static std::vector<std::pair<std::string, std::string>> buildMessages(const std::string& userQuery);

private:
    std::shared_ptr<LiteLLMBackend> client_;
};
