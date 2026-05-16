#pragma once
#include <string>
#include <vector>
#include <utility>
#include <memory>

class IModelBackend {
public:
    virtual ~IModelBackend() = default;

    virtual std::string chat(
        const std::vector<std::pair<std::string, std::string>>& messages,
        float temperature = 0.1f,
        int maxTokens = 200) = 0;

    virtual bool isAvailable() = 0;

    virtual std::string modelName() = 0;
};
