#include "ModelManager.h"
#include "LlamaBackend.h"
#include <filesystem>
#include <algorithm>
#include <thread>

namespace fs = std::filesystem;

ModelManager::ModelManager(const std::string& modelsDir)
    : modelsDir_(modelsDir) {}

std::vector<ModelInfo> ModelManager::availableModels() {
    std::vector<ModelInfo> models;
    if (!fs::exists(modelsDir_) || !fs::is_directory(modelsDir_)) {
        return models;
    }
    for (const auto& entry : fs::directory_iterator(modelsDir_)) {
        if (!fs::is_regular_file(entry.status())) {
            continue;
        }
        const auto& path = entry.path();
        if (path.extension() != ".gguf") {
            continue;
        }
        ModelInfo info;
        info.fileName = path.filename().string();
        info.displayName = path.stem().string();
        info.fileSize = fs::file_size(path);
        models.push_back(std::move(info));
    }
    std::sort(models.begin(), models.end(), [](const ModelInfo& a, const ModelInfo& b) {
        return a.fileName < b.fileName;
    });
    return models;
}

bool ModelManager::switchModel(const std::string& modelFileName) {
    std::string fullPath = (fs::path(modelsDir_) / modelFileName).string();
    if (!fs::exists(fullPath)) {
        return false;
    }
    auto newBackend = std::make_shared<LlamaBackend>();
    if (!newBackend->loadModel(fullPath)) {
        return false;
    }
    {
        std::lock_guard<std::mutex> lock(mutex_);
        llamaBackend_ = newBackend;
        currentModelFile_ = modelFileName;
    }
    ready_.store(true, std::memory_order_release);
    return true;
}

void ModelManager::loadAsync(std::function<void(bool success)> onReady) {
    auto self = shared_from_this();
    std::thread([self, onReady = std::move(onReady)]() {
        auto models = self->availableModels();
        if (models.empty()) {
            if (onReady) onReady(false);
            return;
        }
        // Prefer a model with "qwen" in its name
        std::string chosen = models[0].fileName;
        for (const auto& m : models) {
            if (m.fileName.find("qwen") != std::string::npos) {
                chosen = m.fileName;
                break;
            }
        }
        bool ok = self->switchModel(chosen);
        if (onReady) onReady(ok);
    }).detach();
}

std::shared_ptr<IModelBackend> ModelManager::currentBackend() {
    std::lock_guard<std::mutex> lock(mutex_);
    return llamaBackend_;
}
