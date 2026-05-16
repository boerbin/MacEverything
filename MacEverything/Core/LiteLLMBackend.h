#pragma once
#include "IModelBackend.h"
#include <string>
#include <vector>
#include <utility>

class LiteLLMBackend : public IModelBackend {
public:
    LiteLLMBackend(const std::string& host = "127.0.0.1", int port = 19861,
                   const std::string& model = "translate");

    // IModelBackend interface
    std::string chat(const std::vector<std::pair<std::string, std::string>>& messages,
                     float temperature = 0.1f, int maxTokens = 200) override;
    bool isAvailable() override;
    std::string modelName() override;

    // Legacy embedding API (will be removed with semantic search in Task 8)
    std::vector<float> embed(const std::string& model, const std::string& text);

    // Testable building blocks
    std::string buildChatRequestBody(
        const std::vector<std::pair<std::string, std::string>>& messages,
        float temperature, int maxTokens);
    std::string buildEmbedRequestBody(const std::string& model, const std::string& text);
    std::string parseChatResponse(const std::string& json);
    std::vector<float> parseEmbedResponse(const std::string& json);

    const std::string& getHost() const { return host_; }
    int getPort() const { return port_; }

private:
    std::string host_;
    int port_;
    std::string model_;
    static std::string jsonEscape(const std::string& s);
    static std::string trim(const std::string& s);
};
