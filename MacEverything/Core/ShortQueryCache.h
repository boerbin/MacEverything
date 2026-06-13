#pragma once
#include <array>
#include <vector>
#include <string>
#include <cstdint>
#include <cstring>
#include "StringPool.h"
#include "BoundedSortedVec.h"

class ShortQueryCache {
public:
    struct ScoredResult {
        uint32_t score;
        uint32_t idx;
        bool operator<(const ScoredResult& o) const { return score < o.score; }
    };

    struct CacheEntry {
        BoundedSortedVec<ScoredResult> results{kMaxResults};
        uint32_t totalMatches = 0;
    };

    void rebuild(const std::vector<uint8_t>& types,
                 const StringPool& searchableNamePool,
                 const StringPool& canonicalNamePool,
                 const StringPool& lowerPathPool,
                 const std::vector<uint32_t>& pathIndices,
                 size_t totalSize);

    const CacheEntry* lookup(const std::string& key) const;

    void eraseRecord(uint32_t recordIdx, const char* name, uint16_t nameLen);

    void tryInsert(uint32_t recordIdx, const char* name, uint16_t nameLen, uint32_t fullPathLen);

    bool isBuilt() const { return built_; }

    void saveTo(const std::string& path) const;
    bool loadFrom(const std::string& path);

    static constexpr size_t kUnigramCount = 26;
    static constexpr size_t kBigramCount = 676;
    static constexpr size_t kTotalKeys = 702;
    static constexpr size_t kMaxResults = 100;

private:
    static constexpr uint32_t kMagic = 0x56435153;
    static constexpr uint32_t kVersion = 3;

    std::array<CacheEntry, kTotalKeys> entries_;
    bool built_ = false;

    static int keyIndex(const std::string& key);

    static uint8_t termQuality(const char* name, uint16_t nameLen,
                               const char* term, size_t termLen);
    static uint8_t bestAliasTermQuality(const char* name, uint16_t nameLen,
                                        const char* term, size_t termLen);
    static uint32_t computeScore(const char* name, uint16_t nameLen,
                                 uint32_t pathLen,
                                 const char* term, size_t termLen);

    void collectHitKeys(const char* name, uint16_t nameLen,
                        bool seen[], std::vector<int>& hitKeys) const;

    void insertIntoKey(int ki, const char* name, uint16_t nameLen,
                       uint32_t fullPathLen, uint32_t recordIdx);
};
