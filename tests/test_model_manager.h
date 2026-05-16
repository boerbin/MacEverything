#pragma once
#include "ModelManager.h"
#include "IModelBackend.h"
#include <cassert>
#include <iostream>
#include <filesystem>
#include <fstream>
#include <unistd.h>

namespace fs = std::filesystem;

static const char* kRealModelForMM =
    "/Users/wujian/Library/Application Support/MacEverything/models/"
    "qwen2.5-0.5b-instruct-q4_k_m.gguf";

inline void runModelManagerTests() {
    std::cout << "=== ModelManager Tests ===" << std::endl;

    std::string tmpDir = "/tmp/maceverything_test_models_" + std::to_string(getpid());
    fs::create_directories(tmpDir);

    // Test 1: Empty directory — no models, not ready
    {
        std::string emptyDir = tmpDir + "/empty";
        fs::create_directories(emptyDir);
        ModelManager mgr(emptyDir);
        auto models = mgr.availableModels();
        CHECK(models.empty());
        CHECK(!mgr.isReady());
        CHECK(mgr.currentBackend() == nullptr);
        std::cout << "  [PASS] Empty directory: no models, not ready" << std::endl;
    }

    // Test 2: Discovers .gguf files, ignores others
    {
        std::string mixDir = tmpDir + "/mixed";
        fs::create_directories(mixDir);
        // Create fake .gguf files
        std::ofstream(mixDir + "/alpha.gguf") << "fake";
        std::ofstream(mixDir + "/beta.gguf") << "fake";
        // Create non-gguf files
        std::ofstream(mixDir + "/readme.txt") << "text";
        std::ofstream(mixDir + "/model.bin") << "binary";
        fs::create_directories(mixDir + "/subdir");

        ModelManager mgr(mixDir);
        auto models = mgr.availableModels();
        CHECK(models.size() == 2);
        CHECK(models[0].fileName == "alpha.gguf");
        CHECK(models[1].fileName == "beta.gguf");
        CHECK(models[0].displayName == "alpha");
        CHECK(models[1].displayName == "beta");
        CHECK(models[0].fileSize > 0);
        std::cout << "  [PASS] Discovers .gguf files, ignores others" << std::endl;
    }

    // Test 3: switchModel with real model via symlink
    {
        bool hasModel = fs::exists(kRealModelForMM);
        if (!hasModel) {
            std::cout << "  [SKIP] Real model not found, skipping switchModel test" << std::endl;
        } else {
            std::string realDir = tmpDir + "/real";
            fs::create_directories(realDir);
            std::string linkPath = realDir + "/qwen2.5-0.5b-instruct-q4_k_m.gguf";
            fs::create_symlink(kRealModelForMM, linkPath);

            ModelManager mgr(realDir);
            CHECK(!mgr.isReady());
            bool ok = mgr.switchModel("qwen2.5-0.5b-instruct-q4_k_m.gguf");
            CHECK(ok);
            CHECK(mgr.isReady());
            auto backend = mgr.currentBackend();
            CHECK(backend != nullptr);
            CHECK(backend->isAvailable());
            std::cout << "  [PASS] switchModel with real model succeeds" << std::endl;
        }
    }

    // Test 4: switchModel non-existent returns false
    {
        std::string emptyDir2 = tmpDir + "/empty2";
        fs::create_directories(emptyDir2);
        ModelManager mgr(emptyDir2);
        bool ok = mgr.switchModel("nonexistent.gguf");
        CHECK(!ok);
        CHECK(!mgr.isReady());
        std::cout << "  [PASS] switchModel non-existent returns false" << std::endl;
    }

    // Cleanup
    fs::remove_all(tmpDir);

    std::cout << "=== ModelManager Tests: ALL PASSED ===" << std::endl;
}
