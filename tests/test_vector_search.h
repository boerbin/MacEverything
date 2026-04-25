#pragma once
// ═══════════════════════════════════════════════════════
//  Part 78: VectorSearch Tests
// ═══════════════════════════════════════════════════════

#include "VectorSearch.h"
#include <cmath>
#include <numeric>

static void runVectorSearchTests() {
    std::cout << "========================================\n";
    std::cout << "  Part 78: VectorSearch Tests\n";
    std::cout << "========================================\n\n";

    // Test 1: Empty search returns empty
    {
        std::cout << "  --- Test 1: empty search ---\n";
        VectorSearch vs(3);
        auto results = vs.search({1.0f, 0.0f, 0.0f}, 5);
        check(results.empty(), "empty search returns empty");
    }

    // Test 2: Add and search single vector
    {
        std::cout << "  --- Test 2: single vector search ---\n";
        VectorSearch vs(3);
        vs.addVector(1, {1.0f, 0.0f, 0.0f});
        auto results = vs.search({1.0f, 0.0f, 0.0f}, 5);
        check(results.size() == 1, "single vector: size == 1");
        check(results[0].id == 1, "single vector: id == 1");
        check(results[0].similarity > 0.99f, "single vector: similarity ~ 1.0");
    }

    // Test 3: Top-K ordering by similarity
    {
        std::cout << "  --- Test 3: top-K ordering ---\n";
        VectorSearch vs(3);
        vs.addVector(1, {1.0f, 0.0f, 0.0f});   // exact match
        vs.addVector(2, {0.7f, 0.7f, 0.0f});   // partial match
        vs.addVector(3, {0.0f, 1.0f, 0.0f});   // orthogonal
        auto results = vs.search({1.0f, 0.0f, 0.0f}, 3);
        check(results.size() == 3, "top-K: size == 3");
        check(results[0].id == 1, "top-K: most similar first");
        check(results[0].similarity > results[1].similarity, "top-K: 1st > 2nd");
        check(results[1].similarity > results[2].similarity, "top-K: 2nd > 3rd");
    }

    // Test 4: Top-K limits results
    {
        std::cout << "  --- Test 4: top-K limits ---\n";
        VectorSearch vs(2);
        for (int i = 0; i < 10; i++) {
            vs.addVector(i, {static_cast<float>(i), 1.0f});
        }
        auto results = vs.search({5.0f, 1.0f}, 3);
        check(results.size() == 3, "top-K limits: size == 3");
    }

    // Test 5: Remove vector
    {
        std::cout << "  --- Test 5: remove vector ---\n";
        VectorSearch vs(2);
        vs.addVector(1, {1.0f, 0.0f});
        vs.addVector(2, {0.0f, 1.0f});
        check(vs.size() == 2, "remove: initial size == 2");
        vs.removeVector(1);
        check(vs.size() == 1, "remove: size == 1 after remove");
        auto results = vs.search({1.0f, 0.0f}, 5);
        check(results.size() == 1, "remove: search returns 1");
        check(results[0].id == 2, "remove: remaining is id 2");
    }

    // Test 6: Cosine similarity correctness
    {
        std::cout << "  --- Test 6: cosine similarity ---\n";
        VectorSearch vs(3);
        vs.addVector(1, {1.0f, 0.0f, 0.0f});
        vs.addVector(2, {0.0f, 1.0f, 0.0f});  // orthogonal
        auto results = vs.search({1.0f, 0.0f, 0.0f}, 2);
        check(std::abs(results[0].similarity - 1.0f) < 0.01f, "cosine: self ~ 1.0");
        check(std::abs(results[1].similarity - 0.0f) < 0.01f, "cosine: orthogonal ~ 0.0");
    }

    // Test 7: size() tracks correctly, update existing
    {
        std::cout << "  --- Test 7: size tracking ---\n";
        VectorSearch vs(2);
        check(vs.size() == 0, "size: initial == 0");
        vs.addVector(10, {1.0f, 0.0f});
        vs.addVector(20, {0.0f, 1.0f});
        check(vs.size() == 2, "size: after 2 adds == 2");
        vs.addVector(10, {0.5f, 0.5f});  // update existing
        check(vs.size() == 2, "size: update existing, still 2");
    }

    // Test 8: 1K scale brute-force
    {
        std::cout << "  --- Test 8: 1K scale ---\n";
        VectorSearch vs(8);
        for (uint32_t i = 0; i < 1000; i++) {
            std::vector<float> v(8, 0.0f);
            v[i % 8] = 1.0f;
            vs.addVector(i, v);
        }
        check(vs.size() == 1000, "1K scale: size == 1000");
        std::vector<float> query(8, 0.0f);
        query[0] = 1.0f;
        auto results = vs.search(query, 10);
        check(results.size() == 10, "1K scale: top-10 returned");
        bool allDim0 = true;
        for (auto& r : results) {
            if (r.id % 8 != 0) { allDim0 = false; break; }
        }
        check(allDim0, "1K scale: top results all one-hot dim 0");
    }

    // Test 9: getDimension
    {
        std::cout << "  --- Test 9: getDimension ---\n";
        VectorSearch vs(384);
        check(vs.getDimension() == 384, "getDimension == 384");
    }

    // Test 10: clear()
    {
        std::cout << "  --- Test 10: clear ---\n";
        VectorSearch vs(3);
        vs.addVector(1, {1.0f, 0.0f, 0.0f});
        vs.addVector(2, {0.0f, 1.0f, 0.0f});
        vs.clear();
        check(vs.size() == 0, "clear: size == 0");
        check(vs.search({1.0f, 0.0f, 0.0f}, 5).empty(), "clear: search empty");
    }

    std::cout << "\n  Part 78: VectorSearch Tests DONE\n\n";
}
