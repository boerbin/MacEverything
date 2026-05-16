#pragma once
#include "LlamaBackend.h"
#include "NLTranslator.h"
#include <cassert>
#include <iostream>
#include <memory>
#include <filesystem>

static const char* kModelPath =
    "/Users/wujian/Library/Application Support/MacEverything/models/"
    "qwen2.5-0.5b-instruct-q4_k_m.gguf";

inline void runLlamaBackendTests() {
    std::cout << "=== LlamaBackend Tests ===" << std::endl;

    // Check if model file exists — skip real-model tests if not
    bool hasModel = std::filesystem::exists(kModelPath);
    if (!hasModel) {
        std::cout << "  [SKIP] Model not found at: " << kModelPath << std::endl;
        std::cout << "  [SKIP] Skipping all LlamaBackend tests that require a model" << std::endl;
    }

    // Test 1: Implements IModelBackend (shared_ptr cast)
    {
        auto backend = std::make_shared<LlamaBackend>();
        std::shared_ptr<IModelBackend> iface = backend;
        CHECK(iface != nullptr);
        std::cout << "  [PASS] Implements IModelBackend" << std::endl;
    }

    // Test 2: Not available before loading
    {
        LlamaBackend backend;
        CHECK(!backend.isAvailable());
        std::cout << "  [PASS] Not available before loading" << std::endl;
    }

    // Test 3: loadModel with nonexistent path returns false
    {
        LlamaBackend backend;
        CHECK(!backend.loadModel("/nonexistent/path/model.gguf"));
        CHECK(!backend.isAvailable());
        std::cout << "  [PASS] loadModel(nonexistent) returns false" << std::endl;
    }

    if (!hasModel) {
        std::cout << "=== LlamaBackend Tests: SKIPPED (no model) ===" << std::endl;
        return;
    }

    // ── Tests below require the real GGUF model ──

    auto backend = std::make_shared<LlamaBackend>();

    // Test 4: loadModel with real GGUF succeeds
    {
        bool loaded = backend->loadModel(kModelPath);
        CHECK(loaded);
        CHECK(backend->isAvailable());
        std::cout << "  [INFO] modelName = " << backend->modelName() << std::endl;
        std::cout << "  [PASS] loadModel(real GGUF) succeeds" << std::endl;
    }

    // Test 5: Simple chat returns non-empty string
    {
        std::vector<std::pair<std::string, std::string>> messages = {
            {"user", "Say hello in one word."}
        };
        auto reply = backend->chat(messages, 0.1f, 50);
        std::cout << "  [INFO] Simple chat reply: " << reply << std::endl;
        CHECK(!reply.empty());
        std::cout << "  [PASS] Simple chat returns non-empty string" << std::endl;
    }

    // Test 6: NL Translation — Chinese "PDF downloaded last week"
    {
        NLTranslator translator(backend);
        auto result = translator.translate("上周下载的PDF");
        std::cout << "  [INFO] '上周下载的PDF' -> '" << result.translatedQuery << "'" << std::endl;
        CHECK(result.success);
        std::string q = result.translatedQuery;
        // Should contain date-related or extension filter
        bool hasDm = q.find("dm:") != std::string::npos;
        bool hasExt = q.find("ext:pdf") != std::string::npos || q.find("ext:PDF") != std::string::npos;
        bool hasPdf = q.find("pdf") != std::string::npos || q.find("PDF") != std::string::npos;
        CHECK(hasDm || hasExt || hasPdf);
        std::cout << "  [PASS] NL Translation: Chinese PDF query" << std::endl;
    }

    // Test 7: NL Translation — English "pdf files downloaded last week"
    {
        NLTranslator translator(backend);
        auto result = translator.translate("pdf files downloaded last week");
        std::cout << "  [INFO] 'pdf files downloaded last week' -> '" << result.translatedQuery << "'" << std::endl;
        CHECK(result.success);
        std::string q = result.translatedQuery;
        bool hasPdf = q.find("pdf") != std::string::npos || q.find("PDF") != std::string::npos;
        CHECK(hasPdf);
        std::cout << "  [PASS] NL Translation: English PDF query" << std::endl;
    }

    // Test 8: NL Translation — Chinese "images on desktop"
    {
        NLTranslator translator(backend);
        auto result = translator.translate("桌面上的图片");
        std::cout << "  [INFO] '桌面上的图片' -> '" << result.translatedQuery << "'" << std::endl;
        CHECK(result.success);
        std::string q = result.translatedQuery;
        bool hasDesktop = q.find("Desktop") != std::string::npos || q.find("desktop") != std::string::npos;
        bool hasPic = q.find("pic:") != std::string::npos;
        bool hasPath = q.find("path:") != std::string::npos;
        CHECK(hasDesktop || hasPic || hasPath);
        std::cout << "  [PASS] NL Translation: Desktop images" << std::endl;
    }

    // Test 9: NL Translation — Chinese "videos larger than 100M"
    {
        NLTranslator translator(backend);
        auto result = translator.translate("大于100M的视频");
        std::cout << "  [INFO] '大于100M的视频' -> '" << result.translatedQuery << "'" << std::endl;
        CHECK(result.success);
        std::string q = result.translatedQuery;
        bool hasSize = q.find("size:") != std::string::npos;
        bool hasVideo = q.find("video:") != std::string::npos;
        bool hasVidExt = q.find("ext:mp4") != std::string::npos || q.find("ext:avi") != std::string::npos;
        CHECK(hasSize || hasVideo || hasVidExt);
        std::cout << "  [PASS] NL Translation: Large videos" << std::endl;
    }

    // Test 10: unloadModel works
    {
        backend->unloadModel();
        CHECK(!backend->isAvailable());
        std::cout << "  [PASS] unloadModel works" << std::endl;
    }

    std::cout << "=== LlamaBackend Tests: ALL PASSED ===" << std::endl;
}
