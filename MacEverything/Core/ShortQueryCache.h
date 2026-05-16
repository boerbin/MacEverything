#pragma once
#include <array>
#include <vector>
#include <string>
#include <cstdint>
#include <algorithm>
#include "StringPool.h"

class ShortQueryCache {
public:
    struct CacheEntry {
        std::vector<uint32_t> results;
        uint32_t totalMatches = 0;
        uint32_t deletedCount = 0;
    };

    void rebuild(const std::vector<uint8_t>& types,
                 const StringPool& namePool,
                 const StringPool& lowerPathPool,
                 const std::vector<uint32_t>& pathIndices,
                 size_t totalSize);

    const CacheEntry* lookup(const std::string& key) const;

    void markDeleted(uint32_t recordIdx, const char* name, uint16_t nameLen);

    bool needsRebuild() const;

    bool isBuilt() const { return built_; }

    void saveTo(const std::string& path) const;
    bool loadFrom(const std::string& path);

    static constexpr size_t kUnigramCount = 26;
    static constexpr size_t kBigramCount = 676;
    static constexpr size_t kTotalKeys = 702;
    static constexpr size_t kMaxResults = 100;

private:
    static constexpr float kRebuildThreshold = 0.5f;
    static constexpr uint32_t kMagic = 0x56435153; // "SQCV"
    static constexpr uint32_t kVersion = 1;

    std::array<CacheEntry, kTotalKeys> entries_;
    bool built_ = false;

    static int keyIndex(const std::string& key);
    static int keyIndexForChars(char a, char b = 0);

    static uint8_t termQuality(const char* name, uint16_t nameLen,
                               const char* term, size_t termLen);
    static uint32_t computeScore(const char* name, uint16_t nameLen,
                                 uint32_t pathLen,
                                 const char* term, size_t termLen);
};
