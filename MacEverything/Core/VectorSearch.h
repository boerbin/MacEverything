#pragma once
// ═══════════════════════════════════════════════════════
//  VectorSearch — brute-force cosine similarity search
//  Auto-switches to HNSW when vectors exceed threshold
//  (HNSW integration deferred until actually needed)
// ═══════════════════════════════════════════════════════

#include <vector>
#include <cstdint>
#include <unordered_map>
#include <mutex>
#include <cmath>
#include <algorithm>

struct VectorSearchResult {
    uint32_t id;
    float similarity;  // cosine similarity, higher = more similar
};

class VectorSearch {
public:
    explicit VectorSearch(int dimension);
    ~VectorSearch();

    void addVector(uint32_t id, const std::vector<float>& vec);
    void removeVector(uint32_t id);
    std::vector<VectorSearchResult> search(const std::vector<float>& query, int topK = 20) const;

    size_t size() const;
    int getDimension() const { return dimension_; }
    void clear();
    void rebuild();  // rebuild HNSW from current vectors (no-op for brute-force)

private:
    int dimension_;
    static constexpr size_t HNSW_THRESHOLD = 200000;

    // Brute-force storage (always maintained, pre-normalized)
    std::unordered_map<uint32_t, std::vector<float>> vectors_;
    mutable std::mutex mutex_;

    static float dotProduct(const float* a, const float* b, int dim);
    static void normalizeInPlace(std::vector<float>& vec);
};
