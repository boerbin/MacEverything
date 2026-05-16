#pragma once
#include <string>
#include <vector>
#include <utility>
#include <memory>

class IModelBackend;

struct TranslationResult {
    std::string originalQuery;
    std::string translatedQuery;
    bool success = false;
    bool alreadySyntax = false;
    std::string error;
};

class NLTranslator {
public:
    explicit NLTranslator(std::shared_ptr<IModelBackend> backend);

    TranslationResult translate(const std::string& query);

    static bool looksLikeQuerySyntax(const std::string& text);
    static std::string cleanLLMResponse(const std::string& raw);
    static std::string getSystemPrompt();
    static std::vector<std::pair<std::string, std::string>> getFewShotExamples();
    static std::vector<std::pair<std::string, std::string>> buildMessages(const std::string& userQuery);

private:
    std::shared_ptr<IModelBackend> backend_;
};
