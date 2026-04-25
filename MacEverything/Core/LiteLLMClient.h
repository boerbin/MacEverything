#pragma once
#include <string>
#include <vector>
#include <utility>

class LiteLLMClient {
public:
    LiteLLMClient(const std::string& host = "127.0.0.1", int port = 19861);

    // High-level API (synchronous, blocking)
    std::string chat(const std::string& model,
                     const std::vector<std::pair<std::string, std::string>>& messages,
                     float temperature = 0.1f, int maxTokens = 200);

    std::vector<float> embed(const std::string& model, const std::string& text);

    bool isAvailable();

    // Testable building blocks
    std::string buildChatRequestBody(const std::string& model,
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
    static std::string jsonEscape(const std::string& s);
    static std::string trim(const std::string& s);
};
