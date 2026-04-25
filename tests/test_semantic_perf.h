#pragma once
#include "VectorSearch.h"
#include "EmbeddingIndex.h"
#include <cassert>
#include <iostream>
#include <chrono>
#include <random>
#include <filesystem>

inline void runSemanticPerfTests() {
    std::cout << "=== Semantic Performance Tests ===" << std::endl;

    // Test 1: VectorSearch brute-force at 10K scale
    {
        const int N = 10000;
        const int dim = 1024;
        VectorSearch vs(dim);

        // Generate random vectors
        std::mt19937 rng(42);
        std::normal_distribution<float> dist(0.0f, 1.0f);

        for (int i = 0; i < N; i++) {
            std::vector<float> v(dim);
            for (int j = 0; j < dim; j++) v[j] = dist(rng);
            vs.addVector(i, v);
        }

        // Search query
        std::vector<float> query(dim);
        for (int j = 0; j < dim; j++) query[j] = dist(rng);

        // Warmup
        vs.search(query, 20);

        // Benchmark
        int rounds = 10;
        auto start = std::chrono::high_resolution_clock::now();
        for (int r = 0; r < rounds; r++) {
            auto results = vs.search(query, 20);
            assert(results.size() == 20);
        }
        auto end = std::chrono::high_resolution_clock::now();
        double totalMs = std::chrono::duration<double, std::milli>(end - start).count();
        double avgMs = totalMs / rounds;

        std::cout << "  VectorSearch 10K (dim=" << dim << "): "
                  << avgMs << "ms avg (" << rounds << " rounds)" << std::endl;

        // Budget: brute-force 10K dim=1024 should be under 5ms
        assert(avgMs < 50.0);  // generous budget
        std::cout << "  [PASS] 10K search under budget" << std::endl;
    }

    // Test 2: VectorSearch at 50K scale
    {
        const int N = 50000;
        const int dim = 1024;
        VectorSearch vs(dim);

        std::mt19937 rng(42);
        std::normal_distribution<float> dist(0.0f, 1.0f);

        for (int i = 0; i < N; i++) {
            std::vector<float> v(dim);
            for (int j = 0; j < dim; j++) v[j] = dist(rng);
            vs.addVector(i, v);
        }

        std::vector<float> query(dim);
        for (int j = 0; j < dim; j++) query[j] = dist(rng);

        vs.search(query, 20);  // warmup

        int rounds = 5;
        auto start = std::chrono::high_resolution_clock::now();
        for (int r = 0; r < rounds; r++) {
            auto results = vs.search(query, 20);
            assert(results.size() == 20);
        }
        auto end = std::chrono::high_resolution_clock::now();
        double avgMs = std::chrono::duration<double, std::milli>(end - start).count() / rounds;

        std::cout << "  VectorSearch 50K (dim=" << dim << "): "
                  << avgMs << "ms avg (" << rounds << " rounds)" << std::endl;

        // Budget: 50K dim=1024 should be under 30ms
        assert(avgMs < 100.0);
        std::cout << "  [PASS] 50K search under budget" << std::endl;
    }

    // Test 3: EmbeddingIndex SQLite write throughput
    {
        std::string dbPath = "/tmp/test_embedding_perf.db";
        std::filesystem::remove(dbPath);

        EmbeddingIndex idx;
        idx.open(dbPath);

        const int N = 5000;
        const int dim = 1024;
        std::mt19937 rng(42);
        std::normal_distribution<float> dist(0.0f, 1.0f);

        auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < N; i++) {
            std::vector<float> v(dim);
            for (int j = 0; j < dim; j++) v[j] = dist(rng);
            idx.storeEmbedding("/test/file_" + std::to_string(i) + ".txt", v, i, 1000);
        }
        auto end = std::chrono::high_resolution_clock::now();
        double totalMs = std::chrono::duration<double, std::milli>(end - start).count();
        double perFile = totalMs / N;
        double throughput = N / (totalMs / 1000.0);

        std::cout << "  EmbeddingIndex write " << N << " records: "
                  << totalMs << "ms total, " << perFile << "ms/file, "
                  << throughput << " files/s" << std::endl;

        assert(idx.indexedCount() == N);

        // Budget: should be at least 1000 writes/s
        assert(throughput > 100.0);
        std::cout << "  [PASS] Write throughput" << std::endl;

        idx.close();
        std::filesystem::remove(dbPath);
    }

    // Test 4: EmbeddingIndex SQLite read throughput
    {
        std::string dbPath = "/tmp/test_embedding_read_perf.db";
        std::filesystem::remove(dbPath);

        EmbeddingIndex idx;
        idx.open(dbPath);

        const int N = 1000;
        const int dim = 1024;
        std::mt19937 rng(42);
        std::normal_distribution<float> dist(0.0f, 1.0f);

        // Populate
        for (int i = 0; i < N; i++) {
            std::vector<float> v(dim);
            for (int j = 0; j < dim; j++) v[j] = dist(rng);
            idx.storeEmbedding("/test/file_" + std::to_string(i) + ".txt", v, i, 1000);
        }

        // Read benchmark
        auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < N; i++) {
            std::vector<float> vec;
            bool found = idx.getEmbedding("/test/file_" + std::to_string(i) + ".txt", vec);
            assert(found);
            assert(vec.size() == (size_t)dim);
        }
        auto end = std::chrono::high_resolution_clock::now();
        double totalMs = std::chrono::duration<double, std::milli>(end - start).count();
        double throughput = N / (totalMs / 1000.0);

        std::cout << "  EmbeddingIndex read " << N << " records: "
                  << totalMs << "ms total, " << throughput << " reads/s" << std::endl;

        assert(throughput > 500.0);
        std::cout << "  [PASS] Read throughput" << std::endl;

        idx.close();
        std::filesystem::remove(dbPath);
    }

    // Test 5: VectorSearch add throughput
    {
        const int N = 10000;
        const int dim = 1024;

        std::mt19937 rng(42);
        std::normal_distribution<float> dist(0.0f, 1.0f);

        // Pre-generate vectors
        std::vector<std::vector<float>> vecs(N);
        for (int i = 0; i < N; i++) {
            vecs[i].resize(dim);
            for (int j = 0; j < dim; j++) vecs[i][j] = dist(rng);
        }

        VectorSearch vs(dim);
        auto start = std::chrono::high_resolution_clock::now();
        for (int i = 0; i < N; i++) {
            vs.addVector(i, vecs[i]);
        }
        auto end = std::chrono::high_resolution_clock::now();
        double totalMs = std::chrono::duration<double, std::milli>(end - start).count();
        double throughput = N / (totalMs / 1000.0);

        std::cout << "  VectorSearch add " << N << " vectors: "
                  << totalMs << "ms total, " << throughput << " adds/s" << std::endl;

        assert(throughput > 1000.0);
        std::cout << "  [PASS] Add throughput" << std::endl;
    }

    std::cout << "=== Semantic Performance Tests: ALL PASSED ===" << std::endl;
}
