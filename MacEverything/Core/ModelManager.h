#pragma once
#include <string>
#include <vector>
#include <memory>
#include <functional>
#include <atomic>
#include <mutex>

class IModelBackend;
class LlamaBackend;

struct ModelInfo {
    std::string fileName;
    std::string displayName;
    uint64_t fileSize = 0;
};

class ModelManager : public std::enable_shared_from_this<ModelManager> {
public:
    explicit ModelManager(const std::string& modelsDir);
    ~ModelManager() = default;

    ModelManager(const ModelManager&) = delete;
    ModelManager& operator=(const ModelManager&) = delete;

    std::vector<ModelInfo> availableModels();
    bool switchModel(const std::string& modelFileName);
    void loadAsync(std::function<void(bool success)> onReady);

    bool isReady() const { return ready_.load(std::memory_order_acquire); }
    std::shared_ptr<IModelBackend> currentBackend();
    std::string modelsDir() const { return modelsDir_; }

private:
    std::string modelsDir_;
    std::shared_ptr<LlamaBackend> llamaBackend_;
    std::string currentModelFile_;
    std::atomic<bool> ready_{false};
    mutable std::mutex mutex_;
};
