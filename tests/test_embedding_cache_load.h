#pragma once
// Test: cached embeddings from SQLite are loaded into VectorSearch on startup

#include <cassert>
#include <iostream>
#include <cmath>
#include <filesystem>
#include "EmbeddingIndex.h"
#include "VectorSearch.h"

inline void runEmbeddingCacheLoadTests() {
    std::cout << "=== Embedding Cache Load Tests ===" << std::endl;

    // Test 1: Embeddings stored in SQLite can be loaded into VectorSearch
    {
        std::string dbPath = "/tmp/test_emb_cache_load.db";
        std::filesystem::remove(dbPath);

        const int dim = 4;
        std::vector<float> vec1 = {1.0f, 0.0f, 0.0f, 0.0f};
        std::vector<float> vec2 = {0.0f, 1.0f, 0.0f, 0.0f};
        std::vector<float> vec3 = {0.0f, 0.0f, 1.0f, 0.0f};

        // Phase 1: store embeddings in SQLite then close
        {
            EmbeddingIndex idx;
            idx.open(dbPath);
            idx.storeEmbedding("/file_a.txt", vec1, 100, 1000);
            idx.storeEmbedding("/file_b.txt", vec2, 200, 2000);
            idx.storeEmbedding("/file_c.txt", vec3, 300, 3000);
            assert(idx.indexedCount() == 3);
            idx.close();
        }

        // Phase 2: reopen DB, load into VectorSearch (simulates restart)
        {
            EmbeddingIndex idx;
            idx.open(dbPath);
            VectorSearch vs(dim);

            auto allEmbeddings = idx.getAllEmbeddings();
            assert(allEmbeddings.size() == 3);

            uint32_t loaded = 0;
            for (size_t i = 0; i < allEmbeddings.size(); i++) {
                auto& [path, vec] = allEmbeddings[i];
                if (!vec.empty()) {
                    vs.addVector(static_cast<uint32_t>(i), vec);
                    loaded++;
                }
            }

            assert(loaded == 3);
            assert(vs.size() == 3);

            // Verify search works against loaded vectors
            auto results = vs.search(vec1, 3);
            assert(results.size() == 3);
            assert(results[0].similarity > 0.99f);

            idx.close();
        }

        std::filesystem::remove(dbPath);
        std::cout << "  [PASS] Cached embeddings loaded into VectorSearch after reopen" << std::endl;
    }

    // Test 2: Empty DB produces no vectors in VectorSearch
    {
        std::string dbPath = "/tmp/test_emb_cache_empty.db";
        std::filesystem::remove(dbPath);

        EmbeddingIndex idx;
        idx.open(dbPath);
        VectorSearch vs(4);

        auto all = idx.getAllEmbeddings();
        assert(all.empty());
        assert(vs.size() == 0);

        idx.close();
        std::filesystem::remove(dbPath);
        std::cout << "  [PASS] Empty DB loads zero vectors" << std::endl;
    }

    // Test 3: New embeddings added after cache load coexist correctly
    {
        std::string dbPath = "/tmp/test_emb_cache_mixed.db";
        std::filesystem::remove(dbPath);

        const int dim = 3;
        std::vector<float> cached_vec = {1.0f, 0.0f, 0.0f};
        std::vector<float> new_vec = {0.0f, 1.0f, 0.0f};

        // Store one embedding
        {
            EmbeddingIndex idx;
            idx.open(dbPath);
            idx.storeEmbedding("/cached.txt", cached_vec, 111, 1000);
            idx.close();
        }

        // Reopen, load cache, then add new embedding
        {
            EmbeddingIndex idx;
            idx.open(dbPath);
            VectorSearch vs(dim);

            auto all = idx.getAllEmbeddings();
            for (size_t i = 0; i < all.size(); i++) {
                vs.addVector(static_cast<uint32_t>(i), all[i].second);
            }
            assert(vs.size() == 1);

            // Simulate new file indexed in this session
            vs.addVector(100, new_vec);
            assert(vs.size() == 2);

            // Both should be searchable
            auto results = vs.search(cached_vec, 2);
            assert(results.size() == 2);
            assert(results[0].similarity > 0.99f);

            idx.close();
        }

        std::filesystem::remove(dbPath);
        std::cout << "  [PASS] Cached + new embeddings coexist" << std::endl;
    }

    std::cout << "=== Embedding Cache Load Tests: ALL PASSED ===" << std::endl;
}
