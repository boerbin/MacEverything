#include "ShortQueryCache.h"
#include "SIMDSearch.h"
#include <fstream>
#include <cstring>
#include <queue>
#include <array>

int ShortQueryCache::keyIndex(const std::string& key) {
    if (key.size() == 1) {
        char c = key[0];
        if (c >= 'a' && c <= 'z') return c - 'a';
        return -1;
    }
    if (key.size() == 2) {
        char a = key[0], b = key[1];
        if (a >= 'a' && a <= 'z' && b >= 'a' && b <= 'z')
            return kUnigramCount + (a - 'a') * 26 + (b - 'a');
        return -1;
    }
    return -1;
}

int ShortQueryCache::keyIndexForChars(char a, char b) {
    if (b == 0) {
        if (a >= 'a' && a <= 'z') return a - 'a';
        return -1;
    }
    if (a >= 'a' && a <= 'z' && b >= 'a' && b <= 'z')
        return kUnigramCount + (a - 'a') * 26 + (b - 'a');
    return -1;
}

uint8_t ShortQueryCache::termQuality(const char* name, uint16_t nameLen,
                                      const char* term, size_t termLen) {
    if (nameLen == termLen && memcmp(name, term, nameLen) == 0) return 0;
    if (nameLen >= termLen && memcmp(name, term, termLen) == 0) return 1;
    size_t pos = me::simdFind(name, nameLen, term, termLen);
    if (pos < nameLen) {
        if (pos == 0) return 1;
        char prev = name[pos - 1];
        if (!std::isalnum(static_cast<unsigned char>(prev))) return 2;
    }
    return 3;
}

uint32_t ShortQueryCache::computeScore(const char* name, uint16_t nameLen,
                                        uint32_t pathLen,
                                        const char* term, size_t termLen) {
    uint8_t quality = termQuality(name, nameLen, term, termLen);
    uint8_t pathByte = static_cast<uint8_t>(std::min<uint32_t>(pathLen, 255));
    return ((uint32_t)0 << 16) | ((uint32_t)quality << 8) | pathByte;
}

void ShortQueryCache::rebuild(const std::vector<uint8_t>& types,
                               const StringPool& namePool,
                               const StringPool& lowerPathPool,
                               const std::vector<uint32_t>& pathIndices,
                               size_t totalSize) {
    struct ScoredIdx {
        uint32_t idx;
        uint32_t score;
        bool operator>(const ScoredIdx& o) const { return score > o.score; }
    };

    using MinHeap = std::priority_queue<ScoredIdx, std::vector<ScoredIdx>, std::greater<ScoredIdx>>;
    // Max-heap: we keep worst at top so we can pop it when we find something better
    struct MaxCmp { bool operator()(const ScoredIdx& a, const ScoredIdx& b) const { return a.score < b.score; } };
    using MaxHeap = std::priority_queue<ScoredIdx, std::vector<ScoredIdx>, MaxCmp>;

    std::array<MaxHeap, kTotalKeys> heaps;
    std::array<uint32_t, kTotalKeys> matchCounts{};

    bool seen[kTotalKeys];
    std::vector<int> hitKeys;
    hitKeys.reserve(128);

    for (size_t i = 0; i < totalSize; i++) {
        if (types[i] == 0) continue;

        const char* name = namePool.data(static_cast<uint32_t>(i));
        uint16_t nameLen = namePool.length(static_cast<uint32_t>(i));
        if (nameLen == 0) continue;

        uint32_t pi = pathIndices[i];
        uint16_t pathLen = lowerPathPool.length(pi);
        uint32_t fullPathLen = static_cast<uint32_t>(pathLen) + 1 + nameLen;

        hitKeys.clear();
        memset(seen, 0, sizeof(seen));

        for (uint16_t j = 0; j < nameLen; j++) {
            char c = name[j];
            if (c >= 'a' && c <= 'z') {
                int ki = c - 'a';
                if (!seen[ki]) { seen[ki] = true; hitKeys.push_back(ki); }
            }
        }

        for (uint16_t j = 0; j + 1 < nameLen; j++) {
            char a = name[j], b = name[j + 1];
            if (a >= 'a' && a <= 'z' && b >= 'a' && b <= 'z') {
                int ki = kUnigramCount + (a - 'a') * 26 + (b - 'a');
                if (!seen[ki]) { seen[ki] = true; hitKeys.push_back(ki); }
            }
        }

        for (int ki : hitKeys) {
            matchCounts[ki]++;

            // Compute score for this key
            char term[2];
            size_t termLen;
            if (ki < kUnigramCount) {
                term[0] = 'a' + static_cast<char>(ki);
                termLen = 1;
            } else {
                size_t bi = ki - kUnigramCount;
                term[0] = 'a' + static_cast<char>(bi / 26);
                term[1] = 'a' + static_cast<char>(bi % 26);
                termLen = 2;
            }

            uint32_t score = computeScore(name, nameLen, fullPathLen, term, termLen);
            auto& heap = heaps[ki];

            if (heap.size() < kMaxResults) {
                heap.push({static_cast<uint32_t>(i), score});
            } else if (score < heap.top().score) {
                heap.pop();
                heap.push({static_cast<uint32_t>(i), score});
            }
        }
    }

    // Extract results from heaps into sorted vectors
    for (size_t ki = 0; ki < kTotalKeys; ki++) {
        auto& entry = entries_[ki];
        auto& heap = heaps[ki];
        entry.totalMatches = matchCounts[ki];
        entry.deletedCount = 0;
        entry.results.resize(heap.size());

        // Heap contains best results but in heap order; extract and sort
        std::vector<ScoredIdx> items;
        items.reserve(heap.size());
        while (!heap.empty()) {
            items.push_back(heap.top());
            heap.pop();
        }
        std::sort(items.begin(), items.end(), [](const ScoredIdx& a, const ScoredIdx& b) {
            return a.score < b.score;
        });

        entry.results.resize(items.size());
        for (size_t j = 0; j < items.size(); j++) {
            entry.results[j] = items[j].idx;
        }
    }

    built_ = true;
}

const ShortQueryCache::CacheEntry* ShortQueryCache::lookup(const std::string& key) const {
    if (!built_) return nullptr;
    int ki = keyIndex(key);
    if (ki < 0) return nullptr;
    return &entries_[ki];
}

void ShortQueryCache::markDeleted(uint32_t recordIdx, const char* name, uint16_t nameLen) {
    if (!built_) return;

    bool seen[kTotalKeys];
    memset(seen, 0, sizeof(seen));
    std::vector<int> hitKeys;
    hitKeys.reserve(64);

    for (uint16_t j = 0; j < nameLen; j++) {
        char c = name[j];
        if (c >= 'a' && c <= 'z') {
            int ki = c - 'a';
            if (!seen[ki]) { seen[ki] = true; hitKeys.push_back(ki); }
        }
    }
    for (uint16_t j = 0; j + 1 < nameLen; j++) {
        char a = name[j], b = name[j + 1];
        if (a >= 'a' && a <= 'z' && b >= 'a' && b <= 'z') {
            int ki = kUnigramCount + (a - 'a') * 26 + (b - 'a');
            if (!seen[ki]) { seen[ki] = true; hitKeys.push_back(ki); }
        }
    }

    for (int ki : hitKeys) {
        auto& entry = entries_[ki];
        for (uint32_t idx : entry.results) {
            if (idx == recordIdx) {
                entry.deletedCount++;
                break;
            }
        }
    }
}

bool ShortQueryCache::needsRebuild() const {
    if (!built_) return false;
    for (auto& entry : entries_) {
        if (entry.results.size() > 0 &&
            entry.deletedCount > entry.results.size() * kRebuildThreshold) {
            return true;
        }
    }
    return false;
}

void ShortQueryCache::saveTo(const std::string& path) const {
    if (!built_) return;

    std::ofstream out(path, std::ios::binary);
    if (!out) return;

    uint32_t magic = kMagic;
    uint32_t version = kVersion;
    uint32_t entryCount = kTotalKeys;
    out.write(reinterpret_cast<const char*>(&magic), 4);
    out.write(reinterpret_cast<const char*>(&version), 4);
    out.write(reinterpret_cast<const char*>(&entryCount), 4);

    for (auto& entry : entries_) {
        uint32_t resultCount = static_cast<uint32_t>(entry.results.size());
        out.write(reinterpret_cast<const char*>(&resultCount), 4);
        out.write(reinterpret_cast<const char*>(&entry.totalMatches), 4);
        out.write(reinterpret_cast<const char*>(&entry.deletedCount), 4);
        if (resultCount > 0) {
            out.write(reinterpret_cast<const char*>(entry.results.data()),
                      resultCount * sizeof(uint32_t));
        }
    }
}

bool ShortQueryCache::loadFrom(const std::string& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) return false;

    uint32_t magic, version, entryCount;
    in.read(reinterpret_cast<char*>(&magic), 4);
    in.read(reinterpret_cast<char*>(&version), 4);
    in.read(reinterpret_cast<char*>(&entryCount), 4);

    if (magic != kMagic || version != kVersion || entryCount != kTotalKeys)
        return false;

    for (auto& entry : entries_) {
        uint32_t resultCount;
        in.read(reinterpret_cast<char*>(&resultCount), 4);
        in.read(reinterpret_cast<char*>(&entry.totalMatches), 4);
        in.read(reinterpret_cast<char*>(&entry.deletedCount), 4);
        if (resultCount > kMaxResults) return false;
        entry.results.resize(resultCount);
        if (resultCount > 0) {
            in.read(reinterpret_cast<char*>(entry.results.data()),
                    resultCount * sizeof(uint32_t));
        }
    }

    if (!in) return false;
    built_ = true;
    return true;
}
