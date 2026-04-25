#pragma once
// Part 78: EmbeddingIndex — SQLite vector storage tests

#include <cassert>
#include <iostream>
#include <cmath>
#include <filesystem>

inline void runEmbeddingIndexTests() {
    std::cout << "=== EmbeddingIndex Tests ===" << std::endl;

    std::string dbPath = "/tmp/test_embedding_index.db";
    std::filesystem::remove(dbPath);  // clean slate

    // Test 1: Open creates database
    {
        EmbeddingIndex idx;
        idx.open(dbPath);
        assert(std::filesystem::exists(dbPath));
        idx.close();
        std::cout << "  [PASS] Open creates database" << std::endl;
    }

    // Test 2: Store and retrieve embedding
    {
        EmbeddingIndex idx;
        idx.open(dbPath);
        std::vector<float> vec = {0.1f, 0.2f, 0.3f, 0.4f};
        idx.storeEmbedding("/path/to/file.txt", vec, 12345, 1000);

        std::vector<float> retrieved;
        bool found = idx.getEmbedding("/path/to/file.txt", retrieved);
        assert(found);
        assert(retrieved.size() == 4);
        assert(std::abs(retrieved[0] - 0.1f) < 0.001f);
        assert(std::abs(retrieved[3] - 0.4f) < 0.001f);
        idx.close();
        std::cout << "  [PASS] Store and retrieve" << std::endl;
    }

    // Test 3: Get non-existent embedding returns false
    {
        EmbeddingIndex idx;
        idx.open(dbPath);
        std::vector<float> vec;
        bool found = idx.getEmbedding("/nonexistent", vec);
        assert(!found);
        idx.close();
        std::cout << "  [PASS] Non-existent returns false" << std::endl;
    }

    // Test 4: Update embedding on content change
    {
        EmbeddingIndex idx;
        idx.open(dbPath);
        std::vector<float> vec1 = {1.0f, 2.0f};
        idx.storeEmbedding("/update/test.txt", vec1, 111, 1000);

        std::vector<float> vec2 = {3.0f, 4.0f};
        idx.storeEmbedding("/update/test.txt", vec2, 222, 2000);

        std::vector<float> retrieved;
        idx.getEmbedding("/update/test.txt", retrieved);
        assert(retrieved.size() == 2);
        assert(std::abs(retrieved[0] - 3.0f) < 0.001f);
        idx.close();
        std::cout << "  [PASS] Update on re-store" << std::endl;
    }

    // Test 5: needsUpdate
    {
        EmbeddingIndex idx;
        idx.open(dbPath);
        std::vector<float> vec = {1.0f};
        idx.storeEmbedding("/needs/update.txt", vec, 999, 1000);
        assert(!idx.needsUpdate("/needs/update.txt", 999));  // same hash
        assert(idx.needsUpdate("/needs/update.txt", 888));   // different hash
        assert(idx.needsUpdate("/not/exists.txt", 123));      // not indexed
        idx.close();
        std::cout << "  [PASS] needsUpdate" << std::endl;
    }

    // Test 6: Remove embedding
    {
        EmbeddingIndex idx;
        idx.open(dbPath);
        std::vector<float> vec = {1.0f};
        idx.storeEmbedding("/remove/me.txt", vec, 100, 1000);
        idx.removeEmbedding("/remove/me.txt");
        std::vector<float> retrieved;
        assert(!idx.getEmbedding("/remove/me.txt", retrieved));
        idx.close();
        std::cout << "  [PASS] Remove" << std::endl;
    }

    // Test 7: indexedCount
    {
        std::string freshDb = "/tmp/test_embedding_count.db";
        std::filesystem::remove(freshDb);
        EmbeddingIndex idx;
        idx.open(freshDb);
        assert(idx.indexedCount() == 0);
        idx.storeEmbedding("/a.txt", {1.0f}, 1, 1000);
        idx.storeEmbedding("/b.txt", {2.0f}, 2, 1000);
        assert(idx.indexedCount() == 2);
        idx.removeEmbedding("/a.txt");
        assert(idx.indexedCount() == 1);
        idx.close();
        std::filesystem::remove(freshDb);
        std::cout << "  [PASS] indexedCount" << std::endl;
    }

    // Test 8: getAllEmbeddings
    {
        std::string freshDb = "/tmp/test_embedding_getall.db";
        std::filesystem::remove(freshDb);
        EmbeddingIndex idx;
        idx.open(freshDb);
        idx.storeEmbedding("/x.txt", {1.0f, 2.0f}, 1, 1000);
        idx.storeEmbedding("/y.txt", {3.0f, 4.0f}, 2, 1000);
        auto all = idx.getAllEmbeddings();
        assert(all.size() == 2);
        idx.close();
        std::filesystem::remove(freshDb);
        std::cout << "  [PASS] getAllEmbeddings" << std::endl;
    }

    // Test 9: Persistence across close/reopen
    {
        std::string freshDb = "/tmp/test_embedding_persist.db";
        std::filesystem::remove(freshDb);
        {
            EmbeddingIndex idx;
            idx.open(freshDb);
            idx.storeEmbedding("/persist.txt", {5.0f, 6.0f}, 500, 1000);
            idx.close();
        }
        {
            EmbeddingIndex idx;
            idx.open(freshDb);
            std::vector<float> vec;
            assert(idx.getEmbedding("/persist.txt", vec));
            assert(vec.size() == 2);
            assert(std::abs(vec[0] - 5.0f) < 0.001f);
            idx.close();
        }
        std::filesystem::remove(freshDb);
        std::cout << "  [PASS] Persistence across close/reopen" << std::endl;
    }

    std::filesystem::remove(dbPath);
    std::cout << "=== EmbeddingIndex Tests: ALL PASSED ===" << std::endl;
}
