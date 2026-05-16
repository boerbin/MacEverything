#pragma once
#include <string>
#include <vector>
#include <utility>
#include <memory>
#include <mutex>

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

    // Prompt management
    bool loadPromptFromFile(const std::string& path);
    void setPromptFile(const std::string& path);
    std::string promptSource() const;

    // Static defaults (fallback)
    static std::string getDefaultSystemPrompt();
    static std::vector<std::pair<std::string, std::string>> getDefaultFewShotExamples();

    // Utilities
    static bool looksLikeQuerySyntax(const std::string& text);
    static std::string cleanLLMResponse(const std::string& raw);

private:
    std::shared_ptr<IModelBackend> backend_;
    std::string systemPrompt_;
    std::vector<std::pair<std::string, std::string>> fewShotExamples_;
    std::string promptFilePath_;
    bool usingFilePrompt_ = false;
    mutable std::mutex promptMutex_;

    std::vector<std::pair<std::string, std::string>> buildMessages(const std::string& userQuery);
    bool parsePromptFile(const std::string& content);
    void resetToDefaults();
};
