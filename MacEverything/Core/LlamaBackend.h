#pragma once
#include "IModelBackend.h"
#include <string>
#include <vector>
#include <utility>
#include <mutex>

struct llama_model;
struct llama_context;

class LlamaBackend : public IModelBackend {
public:
    LlamaBackend() = default;
    ~LlamaBackend() override;
    LlamaBackend(const LlamaBackend&) = delete;
    LlamaBackend& operator=(const LlamaBackend&) = delete;

    bool loadModel(const std::string& ggufPath);
    void unloadModel();

    std::string chat(const std::vector<std::pair<std::string, std::string>>& messages,
                     float temperature = 0.1f, int maxTokens = 200) override;
    bool isAvailable() override;
    std::string modelName() override;

private:
    llama_model* model_ = nullptr;
    llama_context* ctx_ = nullptr;
    std::string modelPath_;
    std::string modelName_;
    mutable std::mutex mutex_;
};
