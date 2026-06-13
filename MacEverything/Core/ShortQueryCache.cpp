#include "ShortQueryCache.h"
#include "SIMDSearch.h"
#include <fstream>
#include <cstring>

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

namespace {

constexpr char kSearchAliasSeparator = '\x1F';

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

uint8_t ShortQueryCache::bestAliasTermQuality(const char* name, uint16_t nameLen,
                                               const char* term, size_t termLen) {
    uint8_t best = 3;
    size_t start = 0;
    for (size_t i = 0; i <= nameLen; i++) {
        if (i == nameLen || name[i] == kSearchAliasSeparator) {
            if (i > start && me::simdContains(name + start, i - start, term, termLen)) {
                best = std::min(best, termQuality(name + start, static_cast<uint16_t>(i - start), term, termLen));
            }
            start = i + 1;
        }
    }
    return best;
}

uint32_t ShortQueryCache::computeScore(const char* name, uint16_t nameLen,
                                        uint32_t pathLen,
                                        const char* term, size_t termLen) {
    uint8_t quality = bestAliasTermQuality(name, nameLen, term, termLen);
    uint8_t pathByte = static_cast<uint8_t>(std::min<uint32_t>(pathLen, 255));
    return ((uint32_t)0 << 16) | ((uint32_t)quality << 8) | pathByte;
}

void ShortQueryCache::collectHitKeys(const char* name, uint16_t nameLen,
                                      bool seen[], std::vector<int>& hitKeys) const {
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
}

void ShortQueryCache::insertIntoKey(int ki, const char* name, uint16_t nameLen,
                                     uint32_t fullPathLen, uint32_t recordIdx) {
    char term[2];
    size_t termLen;
    if (ki < static_cast<int>(kUnigramCount)) {
        term[0] = 'a' + static_cast<char>(ki);
        termLen = 1;
    } else {
        size_t bi = ki - kUnigramCount;
        term[0] = 'a' + static_cast<char>(bi / 26);
        term[1] = 'a' + static_cast<char>(bi % 26);
        termLen = 2;
    }
    uint32_t score = computeScore(name, nameLen, fullPathLen, term, termLen);
    entries_[ki].results.insert({score, recordIdx});
}

void ShortQueryCache::rebuild(const std::vector<uint8_t>& types,
                               const StringPool& searchableNamePool,
                               const StringPool& canonicalNamePool,
                               const StringPool& lowerPathPool,
                               const std::vector<uint32_t>& pathIndices,
                               size_t totalSize) {
    for (auto& e : entries_) { e.results.clear(); e.totalMatches = 0; }

    bool seen[kTotalKeys];
    std::vector<int> hitKeys;
    hitKeys.reserve(128);

    for (size_t i = 0; i < totalSize; i++) {
        if (types[i] == 0) continue;
        const char* name = searchableNamePool.data(static_cast<uint32_t>(i));
        uint16_t nameLen = searchableNamePool.length(static_cast<uint32_t>(i));
        if (nameLen == 0) continue;

        uint32_t pi = pathIndices[i];
        uint16_t pathLen = lowerPathPool.length(pi);
        uint32_t fullPathLen = static_cast<uint32_t>(pathLen) + 1 + canonicalNamePool.length(static_cast<uint32_t>(i));

        hitKeys.clear();
        memset(seen, 0, sizeof(seen));
        collectHitKeys(name, nameLen, seen, hitKeys);

        for (int ki : hitKeys) {
            entries_[ki].totalMatches++;
            insertIntoKey(ki, name, nameLen, fullPathLen, static_cast<uint32_t>(i));
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

void ShortQueryCache::tryInsert(uint32_t recordIdx, const char* name, uint16_t nameLen,
                                 uint32_t fullPathLen) {
    if (!built_ || nameLen == 0) return;
    bool seen[kTotalKeys];
    memset(seen, 0, sizeof(seen));
    std::vector<int> hitKeys;
    hitKeys.reserve(64);
    collectHitKeys(name, nameLen, seen, hitKeys);

    for (int ki : hitKeys) {
        entries_[ki].totalMatches++;
        insertIntoKey(ki, name, nameLen, fullPathLen, recordIdx);
    }
}

void ShortQueryCache::eraseRecord(uint32_t recordIdx, const char* name, uint16_t nameLen) {
    if (!built_) return;
    bool seen[kTotalKeys];
    memset(seen, 0, sizeof(seen));
    std::vector<int> hitKeys;
    hitKeys.reserve(64);
    collectHitKeys(name, nameLen, seen, hitKeys);

    for (int ki : hitKeys) {
        auto& vec = entries_[ki].results.mutableData();
        vec.erase(std::remove_if(vec.begin(), vec.end(),
            [recordIdx](const ScoredResult& r) { return r.idx == recordIdx; }), vec.end());
    }
}

void ShortQueryCache::saveTo(const std::string& path) const {
    if (!built_) return;
    std::ofstream out(path, std::ios::binary);
    if (!out) return;

    uint32_t magic = kMagic, version = kVersion, entryCount = kTotalKeys;
    out.write(reinterpret_cast<const char*>(&magic), 4);
    out.write(reinterpret_cast<const char*>(&version), 4);
    out.write(reinterpret_cast<const char*>(&entryCount), 4);

    for (auto& entry : entries_) {
        uint32_t resultCount = static_cast<uint32_t>(entry.results.size());
        out.write(reinterpret_cast<const char*>(&resultCount), 4);
        out.write(reinterpret_cast<const char*>(&entry.totalMatches), 4);
        if (resultCount > 0) {
            out.write(reinterpret_cast<const char*>(entry.results.data().data()),
                      resultCount * sizeof(ScoredResult));
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
        if (resultCount > kMaxResults) return false;
        entry.results.mutableData().resize(resultCount);
        if (resultCount > 0) {
            in.read(reinterpret_cast<char*>(entry.results.mutableData().data()),
                    resultCount * sizeof(ScoredResult));
        }
    }

    if (!in) return false;
    built_ = true;
    return true;
}
