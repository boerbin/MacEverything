#include "VectorSearch.h"
#include <numeric>

// ═══════════════════════════════════════════════════════
//  VectorSearch — brute-force cosine similarity
// ═══════════════════════════════════════════════════════

VectorSearch::VectorSearch(int dimension)
    : dimension_(dimension) {}

VectorSearch::~VectorSearch() = default;

void VectorSearch::addVector(uint32_t id, const std::vector<float>& vec) {
    if (static_cast<int>(vec.size()) != dimension_) return;

    std::vector<float> normalized = vec;
    normalizeInPlace(normalized);

    std::lock_guard<std::mutex> lock(mutex_);
    vectors_[id] = std::move(normalized);
}

void VectorSearch::removeVector(uint32_t id) {
    std::lock_guard<std::mutex> lock(mutex_);
    vectors_.erase(id);
}

std::vector<VectorSearchResult> VectorSearch::search(
    const std::vector<float>& query, int topK) const {

    if (static_cast<int>(query.size()) != dimension_ || topK <= 0)
        return {};

    // Normalize query
    std::vector<float> normQuery = query;
    normalizeInPlace(normQuery);

    std::lock_guard<std::mutex> lock(mutex_);

    if (vectors_.empty()) return {};

    // Compute similarities
    std::vector<VectorSearchResult> all;
    all.reserve(vectors_.size());
    for (const auto& [id, vec] : vectors_) {
        float sim = dotProduct(normQuery.data(), vec.data(), dimension_);
        all.push_back({id, sim});
    }

    // Partial sort for top-K
    size_t k = std::min(static_cast<size_t>(topK), all.size());
    std::partial_sort(all.begin(), all.begin() + k, all.end(),
        [](const VectorSearchResult& a, const VectorSearchResult& b) {
            return a.similarity > b.similarity;
        });

    all.resize(k);
    return all;
}

size_t VectorSearch::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return vectors_.size();
}

void VectorSearch::clear() {
    std::lock_guard<std::mutex> lock(mutex_);
    vectors_.clear();
}

void VectorSearch::rebuild() {
    // No-op for brute-force mode.
    // When HNSW is integrated, this will rebuild the index.
}

float VectorSearch::dotProduct(const float* a, const float* b, int dim) {
    float sum = 0.0f;
    for (int i = 0; i < dim; i++) {
        sum += a[i] * b[i];
    }
    return sum;
}

void VectorSearch::normalizeInPlace(std::vector<float>& vec) {
    float norm = 0.0f;
    for (float v : vec) norm += v * v;
    norm = std::sqrt(norm);
    if (norm > 1e-9f) {
        float inv = 1.0f / norm;
        for (float& v : vec) v *= inv;
    }
}
