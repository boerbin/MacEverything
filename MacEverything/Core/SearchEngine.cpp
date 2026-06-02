#include "SearchEngine.h"
#include "StringUtils.h"
#include "IndexWAL.h"
#include "Logger.h"
#include <algorithm>
#include <thread>
#include <atomic>
#include <cstdio>
#include <cstring>

std::string SearchEngine::makeFullPath(std::string_view path, std::string_view name) {
    if (path.empty() || path.back() == '/') {
        std::string result;
        result.reserve(path.size() + name.size());
        result.append(path);
        result.append(name);
        return result;
    }
    std::string result;
    result.reserve(path.size() + 1 + name.size());
    result.append(path);
    result += '/';
    result.append(name);
    return result;
}

uint64_t SearchEngine::pathHash(const std::string& lowerPath) {
    uint64_t hash = 0xcbf29ce484222325ULL;
    for (unsigned char c : lowerPath) {
        hash ^= c;
        hash *= 0x100000001b3ULL;
    }
    return hash;
}

std::string SearchEngine::reconstructLowerPath(uint32_t idx) const {
    if (idx >= pathIndices_.size()) return {};
    auto pathView = lowerPathPool_.view(pathIndices_[idx]);
    auto nameView = namePool_.view(idx);
    std::string result;
    result.reserve(pathView.size() + 1 + nameView.size());
    result.append(pathView);
    result.push_back('/');
    result.append(nameView);
    return result;
}

bool SearchEngine::verifyPathIndex(uint64_t hash, uint32_t recordIdx, const std::string& lowerPath) const {
    return reconstructLowerPath(recordIdx) == lowerPath;
}

uint32_t SearchEngine::internPath(const std::string& path) {
    auto it = pathLookup_.find(pathHash(path));
    if (it != pathLookup_.end()) return it->second;
    uint32_t pIdx = pathPool_.append(path);
    lowerPathPool_.append(me::toLower(path));
    pathLookup_[pathHash(path)] = pIdx;
    lowerPathLookup_[pathHash(me::toLower(path))] = pIdx;
    return pIdx;
}

void SearchEngine::tombstoneAt(uint32_t idx) {
    if (shortQueryCache_.isBuilt()) {
        const char* name = namePool_.data(idx);
        uint16_t nameLen = namePool_.length(idx);
        if (nameLen > 0) shortQueryCache_.eraseRecord(idx, name, nameLen);
    }
    types_[idx] = 0;
    markPageDirty(idx);
    sizes_[idx] = 0;
    modTimes_[idx] = 0;
    if (idx < inodes_.size()) inodes_[idx] = 0;
    if (idx < devIds_.size()) devIds_[idx] = 0;
    namePool_.tombstone(idx);
    origNamePool_.tombstone(idx);
}

void SearchEngine::pushRecord(FileRecord&& rec) {
    types_.push_back(rec.type);
    sizes_.push_back(rec.size);
    modTimes_.push_back(static_cast<int64_t>(rec.modTime));
    inodes_.push_back(rec.inode);
    devIds_.push_back(rec.devId);
}

void SearchEngine::loadRecords(std::vector<FileRecord>&& records) {
    std::unique_lock lock(mutex_);

    const size_t n = records.size();

    // Build SoA columns from incoming records
    types_.resize(n);
    sizes_.resize(n);
    modTimes_.resize(n);
    inodes_.resize(n);
    devIds_.resize(n);
    for (size_t i = 0; i < n; i++) {
        types_[i] = records[i].type;
        sizes_[i] = records[i].size;
        modTimes_[i] = static_cast<int64_t>(records[i].modTime);
        inodes_[i] = records[i].inode;
        devIds_[i] = records[i].devId;
    }

    // Build origNamePool_ from original-case names (for v6 persistence)
    {
        std::vector<std::string> origNames(n);
        for (size_t i = 0; i < n; i++) {
            origNames[i] = records[i].name;
        }
        origNamePool_.loadBulk(origNames);
    }

    // Pre-compute lowercase names into a temporary vector (parallel),
    // then bulk-load into namePool_ (single-threaded append)
    std::vector<std::string> tempLowerNames(n);
    {
        unsigned numThreads = std::thread::hardware_concurrency();
        if (numThreads < 1) numThreads = 1;
        if (numThreads > 32) numThreads = 32;
        size_t chunkSize = (n + numThreads - 1) / numThreads;
        std::vector<std::thread> threads;
        threads.reserve(numThreads);
        for (unsigned t = 0; t < numThreads; t++) {
            size_t start = t * chunkSize;
            size_t end = std::min(start + chunkSize, n);
            if (start >= end) break;
            threads.emplace_back([&records, &tempLowerNames, start, end] {
                for (size_t i = start; i < end; i++) {
                    tempLowerNames[i] = me::toLower(records[i].name);
                }
            });
        }
        for (auto& th : threads) th.join();
    }
    namePool_.loadBulk(tempLowerNames);

    // Path deduplication: intern paths into pathPool_ + lowerPathPool_ + pathLookup_
    pathPool_.clear();
    lowerPathPool_.clear();
    pathLookup_.clear();
    lowerPathLookup_.clear();
    pathIndices_.resize(n);
    for (size_t i = 0; i < n; i++) {
        pathIndices_[i] = internPath(records[i].path);
    }

    // Build pathIndex_ (lowercase full path -> record index)
    // Parallelize toLower computation, insert into map sequentially
    unsigned numThreads = std::thread::hardware_concurrency();
    if (numThreads < 1) numThreads = 1;
    if (numThreads > 32) numThreads = 32;
    size_t chunkSize = (n + numThreads - 1) / numThreads;
    std::vector<std::string> loweredPaths(n);
    {
        std::vector<std::thread> pathThreads;
        pathThreads.reserve(numThreads);
        for (unsigned t = 0; t < numThreads; t++) {
            size_t start = t * chunkSize;
            size_t end = std::min(start + chunkSize, n);
            if (start >= end) break;
            pathThreads.emplace_back([this, &loweredPaths, start, end] {
                for (size_t i = start; i < end; i++) {
                    uint32_t idx = static_cast<uint32_t>(i);
                    loweredPaths[i] = makeFullPath(lowerPathPool_.view(pathIndices_[idx]),
                                                   namePool_.view(idx));
                }
            });
        }
        for (auto& th : pathThreads) th.join();
    }
    pathIndex_.clear();
    pathIndex_.reserve(n);
    for (size_t i = 0; i < n; i++) {
        if (types_[i] == 0) continue;
        pathIndex_[pathHash(loweredPaths[i])] = static_cast<uint32_t>(i);
    }

    // Tombstone orphaned duplicates: records not in pathIndex_ as winners
    std::vector<bool> isWinner(n, false);
    for (const auto& [_, idx] : pathIndex_) {
        isWinner[idx] = true;
    }
    uint32_t actualLive = 0;
    for (size_t i = 0; i < n; i++) {
        if (types_[i] == 0) continue;
        if (isWinner[i]) {
            actualLive++;
        } else {
            tombstoneAt(static_cast<uint32_t>(i));
        }
    }

    // Build trigram index for fast filename search
    buildTrigramIndex();
    // Build path trigram index for fast path-only search
    buildPathTrigramIndex();
    rebuildPathIdxToRecords();
    // Build extension index for fast ext: filter queries
    buildExtensionIndex();
    rebuildRecentCache();

    liveCount_.store(actualLive, std::memory_order_relaxed);

    // Initialize dirty page bitmap (no pages dirty after initial load)
    uint32_t pageCount = (static_cast<uint32_t>(n) + kRecordsPerPage - 1) / kRecordsPerPage;
    dirtyPages_.assign(pageCount, false);
    fullRewriteNeeded_.store(false, std::memory_order_relaxed);
}

void SearchEngine::loadRecordsV5(std::vector<FileRecord>&& records,
                                  std::vector<std::string>&& lowerNames,
                                  std::vector<uint32_t>&& pathIndices,
                                  StringPool&& pathDict,
                                  StringPool&& lowerPathDict) {
    std::unique_lock lock(mutex_);

    const size_t n = records.size();

    // Build SoA columns from incoming records
    types_.resize(n);
    sizes_.resize(n);
    modTimes_.resize(n);
    inodes_.resize(n);
    devIds_.resize(n);
    for (size_t i = 0; i < n; i++) {
        types_[i] = records[i].type;
        sizes_[i] = records[i].size;
        modTimes_[i] = static_cast<int64_t>(records[i].modTime);
        inodes_[i] = records[i].inode;
        devIds_[i] = records[i].devId;
    }

    // Build origNamePool_ from original-case names (for v6 persistence)
    {
        std::vector<std::string> origNames(n);
        for (size_t i = 0; i < n; i++) {
            origNames[i] = records[i].name;
        }
        origNamePool_.loadBulk(origNames);
    }

    // Install pre-lowered names directly into namePool_ (skip parallel toLower)
    namePool_.loadBulk(lowerNames);

    // Install pre-built path dictionaries (skip path dedup + internPath loop)
    pathPool_ = std::move(pathDict);
    lowerPathPool_ = std::move(lowerPathDict);
    pathIndices_ = std::move(pathIndices);

    // Rebuild pathLookup_ and lowerPathLookup_ from pathPool_ entries
    pathLookup_.clear();
    lowerPathLookup_.clear();
    pathLookup_.reserve(pathPool_.entryCount());
    lowerPathLookup_.reserve(pathPool_.entryCount());
    for (uint32_t i = 0; i < pathPool_.entryCount(); i++) {
        if (pathPool_.isLive(i)) {
            pathLookup_[pathHash(pathPool_.str(i))] = i;
            lowerPathLookup_[pathHash(lowerPathPool_.str(i))] = i;
        }
    }

    // Build pathIndex_ (lowercase full path -> record index)
    unsigned numThreads = std::thread::hardware_concurrency();
    if (numThreads < 1) numThreads = 1;
    if (numThreads > 32) numThreads = 32;
    size_t chunkSize = (n + numThreads - 1) / numThreads;
    std::vector<std::string> loweredPaths(n);
    {
        std::vector<std::thread> pathThreads;
        pathThreads.reserve(numThreads);
        for (unsigned t = 0; t < numThreads; t++) {
            size_t start = t * chunkSize;
            size_t end = std::min(start + chunkSize, n);
            if (start >= end) break;
            pathThreads.emplace_back([this, &loweredPaths, start, end] {
                for (size_t i = start; i < end; i++) {
                    uint32_t idx = static_cast<uint32_t>(i);
                    loweredPaths[i] = makeFullPath(lowerPathPool_.view(pathIndices_[idx]),
                                                   namePool_.view(idx));
                }
            });
        }
        for (auto& th : pathThreads) th.join();
    }
    pathIndex_.clear();
    pathIndex_.reserve(n);
    for (size_t i = 0; i < n; i++) {
        if (types_[i] == 0) continue;
        pathIndex_[pathHash(loweredPaths[i])] = static_cast<uint32_t>(i);
    }

    // Tombstone orphaned duplicates
    std::vector<bool> isWinner(n, false);
    for (const auto& [_, idx] : pathIndex_) {
        isWinner[idx] = true;
    }
    uint32_t actualLive = 0;
    for (size_t i = 0; i < n; i++) {
        if (types_[i] == 0) continue;
        if (isWinner[i]) {
            actualLive++;
        } else {
            tombstoneAt(static_cast<uint32_t>(i));
        }
    }

    // Build trigram index for fast filename search
    buildTrigramIndex();
    buildPathTrigramIndex();
    rebuildPathIdxToRecords();
    buildExtensionIndex();
    rebuildRecentCache();

    liveCount_.store(actualLive, std::memory_order_relaxed);

    // Initialize dirty page bitmap
    uint32_t pageCount = (static_cast<uint32_t>(n) + kRecordsPerPage - 1) / kRecordsPerPage;
    dirtyPages_.assign(pageCount, false);
    fullRewriteNeeded_.store(false, std::memory_order_relaxed);
}

FileRecord SearchEngine::getRecord(uint32_t index) const {
    std::shared_lock lock(mutex_);
    if (index >= types_.size()) return {};
    FileRecord r;
    r.type = types_[index];
    if (r.type == 0) return r;
    r.name = origNamePool_.str(index);
    r.path = pathPool_.str(pathIndices_[index]);
    r.size = sizes_[index];
    r.modTime = static_cast<time_t>(modTimes_[index]);
    r.inode = inodes_[index];
    r.devId = devIds_[index];
    return r;
}

uint32_t SearchEngine::recordCount() const {
    std::shared_lock lock(mutex_);
    return static_cast<uint32_t>(types_.size());
}

uint32_t SearchEngine::addRecord(FileRecord&& record) {
    std::unique_lock lock(mutex_);

    uint32_t idx = static_cast<uint32_t>(types_.size());
    std::string fullPath = makeFullPath(record.path, record.name);
    std::string lowerFull = me::toLower(fullPath);
    std::string lower = me::toLower(record.name);

    if (wal_) wal_->append(WALOp::Add, fullPath, record);

    // Tombstone existing record at same path to prevent orphaned duplicates
    uint64_t lowerHash = pathHash(lowerFull);
    auto existIt = pathIndex_.find(lowerHash);
    if (existIt != pathIndex_.end()) {
        uint32_t oldIdx = existIt->second;
        time_t oldModTime = static_cast<time_t>(modTimes_[oldIdx]);
        removeTrigramsForRecord(oldIdx);
        removePathTrigramsForRecord(oldIdx);
        tombstoneAt(oldIdx);
        pathIndex_.erase(existIt);
        liveCount_.fetch_sub(1, std::memory_order_relaxed);
        removeFromRecentCache(oldIdx, oldModTime);
    }

    // Intern path before moving record
    uint32_t pIdx = internPath(record.path);
    if (pIdx >= pathIdxToRecords_.size()) {
        ensurePathTrigramsForPathIdx(pIdx);
    }
    record.path.clear();
    record.path.shrink_to_fit();

    origNamePool_.append(record.name);
    pushRecord(std::move(record));
    uint32_t nameIdx = namePool_.append(lower);
    (void)nameIdx; // nameIdx == idx since namePool_ grows in lockstep
    pathIndices_.push_back(pIdx);
    pathIndex_[lowerHash] = idx;

    // Update trigram index (skip during Phase 2 — completePhase2 replay handles it)
    if (!phase2Pending_.load(std::memory_order_relaxed)) {
        addTrigramsForRecord(idx, namePool_.data(idx), namePool_.length(idx));
        addPathTrigramsForRecord(idx);
        addExtensionForRecord(idx);
    }

    // Dirty page tracking
    if (idx / kRecordsPerPage >= dirtyPages_.size()) {
        dirtyPages_.resize(idx / kRecordsPerPage + 1, false);
    }
    markPageDirty(idx);

    liveCount_.fetch_add(1, std::memory_order_relaxed);
    addToRecentCache(idx, static_cast<time_t>(modTimes_[idx]));

    if (shortQueryCache_.isBuilt()) {
        const char* nd = namePool_.data(idx);
        uint16_t nl = namePool_.length(idx);
        uint32_t pi = pathIndices_.back();
        uint16_t pl = lowerPathPool_.length(pi);
        shortQueryCache_.tryInsert(idx, nd, nl, static_cast<uint32_t>(pl) + 1 + nl);
    }

    return idx;
}

bool SearchEngine::removeByPathUnlocked(const std::string& fullPath) {
    auto it = pathIndex_.find(pathHash(me::toLower(fullPath)));
    if (it == pathIndex_.end()) return false;

    if (wal_) wal_->append(WALOp::Remove, fullPath);

    uint32_t idx = it->second;
    time_t oldModTime = static_cast<time_t>(modTimes_[idx]);
    removeTrigramsForRecord(idx);
    removePathTrigramsForRecord(idx);
    removeExtensionForRecord(idx);
    tombstoneAt(idx);
    pathIndex_.erase(it);

    liveCount_.fetch_sub(1, std::memory_order_relaxed);
    removeFromRecentCache(idx, oldModTime);

    return true;
}

bool SearchEngine::removeByPath(const std::string& fullPath) {
    std::unique_lock lock(mutex_);
    return removeByPathUnlocked(fullPath);
}

uint32_t SearchEngine::removeByPathPrefix(const std::string& pathPrefix) {
    std::unique_lock lock(mutex_);

    std::string lowerPrefix = me::toLower(pathPrefix);
    uint32_t removed = 0;
    for (uint32_t i = 0; i < types_.size(); i++) {
        if (types_[i] == 0) continue;
        std::string path = reconstructLowerPath(i);
        if (path.size() >= lowerPrefix.size() &&
            path.compare(0, lowerPrefix.size(), lowerPrefix) == 0 &&
            (path.size() == lowerPrefix.size() || path[lowerPrefix.size()] == '/')) {

            // H-4: Write WAL entry for each removed path
            if (wal_) {
                std::string fullPath = makeFullPath(pathPool_.str(pathIndices_[i]), origNamePool_.str(i));
                wal_->append(WALOp::Remove, fullPath);
            }

            time_t oldModTime = static_cast<time_t>(modTimes_[i]);
            removeTrigramsForRecord(i);
            removePathTrigramsForRecord(i);
            removeExtensionForRecord(i);
            tombstoneAt(i);
            liveCount_.fetch_sub(1, std::memory_order_relaxed);
            removeFromRecentCache(i, oldModTime);
            pathIndex_.erase(pathHash(path));
            removed++;
        }
    }

    return removed;
}

uint32_t SearchEngine::batchRescanPrefix(const std::string& pathPrefix,
                                         std::vector<FileRecord>&& freshRecords) {
    std::unique_lock lock(mutex_);

    // ── Phase 1: Tombstone old records matching prefix ──
    // Remove trigrams incrementally for each tombstoned record.
    std::string lowerPrefix = me::toLower(pathPrefix);
    uint32_t removed = 0;
    for (uint32_t i = 0; i < types_.size(); i++) {
        if (types_[i] == 0) continue;
        std::string path = reconstructLowerPath(i);
        if (path.size() >= lowerPrefix.size() &&
            path.compare(0, lowerPrefix.size(), lowerPrefix) == 0 &&
            (path.size() == lowerPrefix.size() || path[lowerPrefix.size()] == '/')) {

            if (wal_) {
                std::string fullPath = makeFullPath(pathPool_.str(pathIndices_[i]), origNamePool_.str(i));
                wal_->append(WALOp::Remove, fullPath);
            }

            removeTrigramsForRecord(i);
            removePathTrigramsForRecord(i);
            removeExtensionForRecord(i);
            tombstoneAt(i);
            liveCount_.fetch_sub(1, std::memory_order_relaxed);
            pathIndex_.erase(pathHash(path));
            removed++;
        }
    }

    // ── Phase 2: Add fresh records with incremental trigram insertion ──
    for (auto& record : freshRecords) {
        uint32_t newIdx = static_cast<uint32_t>(types_.size());
        std::string fullPath = makeFullPath(record.path, record.name);
        std::string lower = me::toLower(record.name);

        if (wal_) wal_->append(WALOp::Update, fullPath, record);

        uint32_t pIdx = internPath(record.path);
        if (pIdx >= pathIdxToRecords_.size()) {
            ensurePathTrigramsForPathIdx(pIdx);
        }
        record.path.clear();
        record.path.shrink_to_fit();

        origNamePool_.append(record.name);
        pushRecord(std::move(record));
        namePool_.append(lower);
        pathIndices_.push_back(pIdx);
        pathIndex_[pathHash(me::toLower(fullPath))] = newIdx;
        if (!phase2Pending_.load(std::memory_order_relaxed)) {
            addTrigramsForRecord(newIdx, namePool_.data(newIdx), namePool_.length(newIdx));
            addPathTrigramsForRecord(newIdx);
            addExtensionForRecord(newIdx);
        }
        if (newIdx / kRecordsPerPage >= dirtyPages_.size()) {
            dirtyPages_.resize(newIdx / kRecordsPerPage + 1, false);
        }
        markPageDirty(newIdx);
        liveCount_.fetch_add(1, std::memory_order_relaxed);
    }

    // ── Phase 3: Rebuild caches ──
    rebuildRecentCache();
    buildShortQueryCache();

    return removed;
}

void SearchEngine::updateByPathUnlocked(const std::string& fullPath, FileRecord&& updated) {
    if (wal_) wal_->append(WALOp::Update, fullPath, updated);

    // Remove old record if exists (case-insensitive lookup)
    auto it = pathIndex_.find(pathHash(me::toLower(fullPath)));
    if (it != pathIndex_.end()) {
        uint32_t idx = it->second;
        time_t oldModTime = static_cast<time_t>(modTimes_[idx]);
        // Clean up trigram index (must happen before tombstoning namePool_)
        removeTrigramsForRecord(idx);
        removePathTrigramsForRecord(idx);
        removeExtensionForRecord(idx);
        tombstoneAt(idx);
        pathIndex_.erase(it);
        liveCount_.fetch_sub(1, std::memory_order_relaxed);
        removeFromRecentCache(idx, oldModTime);
    }

    // Add new record
    uint32_t newIdx = static_cast<uint32_t>(types_.size());
    std::string newFullPath = makeFullPath(updated.path, updated.name);
    std::string lower = me::toLower(updated.name);

    // Intern path before moving record
    uint32_t pIdx = internPath(updated.path);
    if (pIdx >= pathIdxToRecords_.size()) {
        ensurePathTrigramsForPathIdx(pIdx);
    }
    updated.path.clear();
    updated.path.shrink_to_fit();

    origNamePool_.append(updated.name);
    pushRecord(std::move(updated));
    namePool_.append(lower);
    pathIndices_.push_back(pIdx);
    pathIndex_[pathHash(me::toLower(newFullPath))] = newIdx;
    if (!phase2Pending_.load(std::memory_order_relaxed)) {
        addTrigramsForRecord(newIdx, namePool_.data(newIdx), namePool_.length(newIdx));
        addPathTrigramsForRecord(newIdx);
        addExtensionForRecord(newIdx);
    }
    if (newIdx / kRecordsPerPage >= dirtyPages_.size()) {
        dirtyPages_.resize(newIdx / kRecordsPerPage + 1, false);
    }
    markPageDirty(newIdx);
    liveCount_.fetch_add(1, std::memory_order_relaxed);
    addToRecentCache(newIdx, static_cast<time_t>(modTimes_[newIdx]));
}

void SearchEngine::updateByPath(const std::string& fullPath, FileRecord&& updated) {
    std::unique_lock lock(mutex_);
    updateByPathUnlocked(fullPath, std::move(updated));
}

void SearchEngine::batchMutate(std::vector<MutationOp>&& ops) {
    if (ops.empty()) return;

    constexpr size_t kChunkSize = 300;
    const size_t total = ops.size();

    for (size_t offset = 0; offset < total; offset += kChunkSize) {
        const size_t end = std::min(offset + kChunkSize, total);
        std::unique_lock lock(mutex_);
        for (size_t i = offset; i < end; ++i) {
            auto& op = ops[i];
            if (op.type == MutationOp::REMOVE) {
                removeByPathUnlocked(op.path);
            } else {
                updateByPathUnlocked(op.path, std::move(op.record));
            }
        }
    }
}

std::unordered_map<uint32_t, uint32_t> SearchEngine::compactRecords() {
    // ── Phase 1: Snapshot under shared_lock ──
    // Queries continue unblocked during snapshot copy.
    std::vector<uint8_t> snapTypes;
    std::vector<uint64_t> snapSizes;
    std::vector<int64_t> snapModTimes;
    std::vector<uint64_t> snapInodes;
    std::vector<int32_t> snapDevIds;
    StringPool snapNamePool;
    StringPool snapOrigNamePool;
    std::vector<uint32_t> snapPathIndices;
    StringPool snapPathPool;
    std::unordered_map<uint64_t, uint32_t> snapPathIndex;
    uint32_t snapSize;
    {
        std::shared_lock lock(mutex_);
        uint32_t live = liveCount_.load(std::memory_order_relaxed);
        if (live == types_.size()) return {}; // nothing to compact
        snapTypes = types_;
        snapSizes = sizes_;
        snapModTimes = modTimes_;
        snapInodes = inodes_;
        snapDevIds = devIds_;
        snapNamePool = namePool_;
        snapOrigNamePool = origNamePool_;
        snapPathIndices = pathIndices_;
        snapPathPool = pathPool_;
        snapPathIndex = pathIndex_;
        snapSize = static_cast<uint32_t>(types_.size());
    }

    LOG_INFO("SearchEngine", "COW compaction Phase 2: building compacted data from "
             << snapSize << " records");

    // ── Phase 2: Build compacted data (no lock held) ──
    // Mutations (addRecord/removeByPath/updateByPath/batchRescanPrefix) continue
    // on the live data; they will be replayed in Phase 3.
    std::unordered_map<uint32_t, uint32_t> remap;
    remap.reserve(snapSize);

    std::vector<uint8_t> cdTypes;
    std::vector<uint64_t> cdSizes;
    std::vector<int64_t> cdModTimes;
    std::vector<uint64_t> cdInodes;
    std::vector<int32_t> cdDevIds;
    cdTypes.reserve(snapSize);
    cdSizes.reserve(snapSize);
    cdModTimes.reserve(snapSize);
    cdInodes.reserve(snapSize);
    cdDevIds.reserve(snapSize);
    StringPool cdNamePool;
    StringPool cdOrigNamePool;
    std::vector<uint32_t> cdPathIndices;
    cdPathIndices.reserve(snapSize);
    StringPool cdPathPool;
    StringPool cdLowerPathPool;
    std::unordered_map<uint64_t, uint32_t> cdPathLookup;
    std::unordered_map<uint64_t, uint32_t> cdLowerPathLookup;
    std::unordered_map<uint64_t, uint32_t> cdPathIndex;
    cdPathIndex.reserve(snapSize);

    for (size_t i = 0; i < snapSize; i++) {
        if (snapTypes[i] == 0) continue;
        // Skip orphaned duplicates: live records not referenced by pathIndex_
        std::string origPath = snapPathPool.str(snapPathIndices[i]);
        std::string snapName = snapOrigNamePool.str(i);
        std::string fullPathLower = me::toLower(makeFullPath(origPath, snapName));
        auto pathIt = snapPathIndex.find(pathHash(fullPathLower));
        if (pathIt == snapPathIndex.end() || pathIt->second != static_cast<uint32_t>(i)) continue;
        uint32_t newIdx = static_cast<uint32_t>(cdTypes.size());
        remap[static_cast<uint32_t>(i)] = newIdx;
        // Intern path into compacted pool (both original and lowered)
        uint32_t newPIdx;
        auto cdPlIt = cdPathLookup.find(pathHash(origPath));
        if (cdPlIt != cdPathLookup.end()) {
            newPIdx = cdPlIt->second;
        } else {
            newPIdx = cdPathPool.append(origPath);
            cdLowerPathPool.append(me::toLower(origPath));
            cdPathLookup[pathHash(origPath)] = newPIdx;
            cdLowerPathLookup[pathHash(me::toLower(origPath))] = newPIdx;
        }
        cdPathIndex[pathHash(fullPathLower)] = newIdx;
        // Copy name from snapshot pool into compacted pool
        cdOrigNamePool.append(snapOrigNamePool.data(i), snapOrigNamePool.length(i));
        cdNamePool.append(snapNamePool.data(i), snapNamePool.length(i));
        cdPathIndices.push_back(newPIdx);
        cdTypes.push_back(snapTypes[i]);
        cdSizes.push_back(snapSizes[i]);
        cdModTimes.push_back(snapModTimes[i]);
        cdInodes.push_back(snapInodes[i]);
        cdDevIds.push_back(snapDevIds[i]);
    }
    uint32_t cdLiveCount = static_cast<uint32_t>(cdTypes.size());

    // Build trigram index, path trigram index, extension index, and recent cache outside any lock
    auto cdTrigramIndex = buildTrigramIndexFromData(cdTypes, cdNamePool);
    auto cdPathTrigramIndex = buildPathTrigramIndexFromData(cdLowerPathPool);
    auto cdPathIdxToRecords = buildPathIdxToRecordsFromData(cdTypes, cdPathIndices, cdPathPool.entryCount());
    auto cdExtensionIndex = buildExtensionIndexFromData(cdTypes, cdNamePool);
    auto cdRecentCache = buildRecentCacheFromData(cdTypes, cdModTimes, kRecentCacheSize);

    LOG_INFO("SearchEngine", "COW compaction Phase 3: swapping data, compacted "
             << snapSize << " -> " << cdLiveCount << " records");

    // ── Phase 3: Swap + replay under unique_lock ──
    // This lock is held only long enough to move data and replay the small
    // number of mutations that occurred during Phase 2 (~milliseconds).
    {
        std::unique_lock lock(mutex_);

        // Move out current (mutated-during-Phase2) state
        auto oldTypes = std::move(types_);
        auto oldSizes = std::move(sizes_);
        auto oldModTimes = std::move(modTimes_);
        auto oldInodes = std::move(inodes_);
        auto oldDevIds = std::move(devIds_);
        auto oldOrigNamePool = std::move(origNamePool_);
        auto oldNamePool = std::move(namePool_);
        auto oldPathIndices = std::move(pathIndices_);
        auto oldPathPool = std::move(pathPool_);
        auto oldPathLookup = std::move(pathLookup_);
        auto oldLowerPathLookup = std::move(lowerPathLookup_);
        auto oldPathIndex = std::move(pathIndex_);

        // Install compacted data
        types_ = std::move(cdTypes);
        sizes_ = std::move(cdSizes);
        modTimes_ = std::move(cdModTimes);
        inodes_ = std::move(cdInodes);
        devIds_ = std::move(cdDevIds);
        origNamePool_ = std::move(cdOrigNamePool);
        namePool_ = std::move(cdNamePool);
        pathIndices_ = std::move(cdPathIndices);
        pathPool_ = std::move(cdPathPool);
        lowerPathPool_ = std::move(cdLowerPathPool);
        pathLookup_ = std::move(cdPathLookup);
        lowerPathLookup_ = std::move(cdLowerPathLookup);
        pathIndex_ = std::move(cdPathIndex);
        nameTrigramIndex_ = std::move(cdTrigramIndex);
        pathTrigramIndex_ = std::move(cdPathTrigramIndex);
        pathIdxToRecords_ = std::move(cdPathIdxToRecords);
        extensionIndex_ = std::move(cdExtensionIndex);
        recentCache_ = std::move(cdRecentCache);

        // Replay new records appended during Phase 2
        uint32_t replayedAdds = 0;
        for (size_t i = snapSize; i < oldTypes.size(); i++) {
            if (oldTypes[i] == 0) continue;
            uint32_t newIdx = static_cast<uint32_t>(types_.size());
            std::string origPath = oldPathPool.str(oldPathIndices[i]);
            uint32_t pIdx = internPath(origPath);
            if (pIdx >= pathIdxToRecords_.size()) {
                ensurePathTrigramsForPathIdx(pIdx);
            }
            std::string origName = oldOrigNamePool.str(i);
            std::string fullPath = me::toLower(makeFullPath(origPath, origName));
            // Copy name from old pool into current pool
            origNamePool_.append(oldOrigNamePool.data(i), oldOrigNamePool.length(i));
            namePool_.append(oldNamePool.data(i), oldNamePool.length(i));
            pathIndex_[pathHash(fullPath)] = newIdx;
            pathIndices_.push_back(pIdx);
            addTrigramsForRecord(newIdx, namePool_.data(newIdx), namePool_.length(newIdx));
            addToRecentCache(newIdx, static_cast<time_t>(oldModTimes[i]));
            // Push SoA columns for replayed record
            types_.push_back(oldTypes[i]);
            sizes_.push_back(oldSizes[i]);
            modTimes_.push_back(oldModTimes[i]);
            inodes_.push_back(oldInodes[i]);
            devIds_.push_back(oldDevIds[i]);
            addPathTrigramsForRecord(newIdx);
            addExtensionForRecord(newIdx);
            cdLiveCount++;
            replayedAdds++;
        }

        // Replay tombstones: paths in snapshot but removed during Phase 2
        uint32_t replayedDeletes = 0;
        for (auto& [hash, snapIdx] : snapPathIndex) {
            if (oldPathIndex.find(hash) != oldPathIndex.end()) continue;
            // This path was deleted during Phase 2
            auto it = remap.find(snapIdx);
            if (it == remap.end()) continue; // was already tombstoned in snapshot
            uint32_t newIdx = it->second;
            if (newIdx < types_.size() && types_[newIdx] != 0) {
                removeTrigramsForRecord(newIdx);
                removePathTrigramsForRecord(newIdx);
                removeExtensionForRecord(newIdx);
                time_t oldMod = static_cast<time_t>(modTimes_[newIdx]);
                tombstoneAt(newIdx);
                pathIndex_.erase(hash);
                removeFromRecentCache(newIdx, oldMod);
                cdLiveCount--;
                replayedDeletes++;
            }
        }

        liveCount_.store(cdLiveCount, std::memory_order_relaxed);
        compactionGen_.fetch_add(1, std::memory_order_relaxed);

        buildShortQueryCache();

        LOG_INFO("SearchEngine", "COW compaction done: replayed " << replayedAdds
                 << " adds, " << replayedDeletes << " deletes, live=" << cdLiveCount);
    }

    fullRewriteNeeded_.store(true, std::memory_order_relaxed);
    phase2Pending_.store(false, std::memory_order_release);

    return remap;
}

void SearchEngine::attachWAL(std::shared_ptr<IndexWAL> wal) {
    std::unique_lock lock(mutex_);
    wal_ = std::move(wal);
}

void SearchEngine::detachWAL() {
    std::unique_lock lock(mutex_);
    wal_.reset();
}

std::vector<uint32_t> SearchEngine::recentIndices(uint32_t count) const {
    std::shared_lock lock(mutex_);
    std::vector<uint32_t> result;
    uint32_t n = std::min(count, static_cast<uint32_t>(recentCache_.size()));
    result.reserve(n);
    auto it = recentCache_.begin();
    for (uint32_t i = 0; i < n; ++i, ++it) {
        result.push_back(it->index);
    }
    return result;
}

BoundedSortedVec<SearchEngine::RecentEntry>
SearchEngine::buildRecentCacheFromData(const std::vector<uint8_t>& types,
                                       const std::vector<int64_t>& modTimes,
                                       uint32_t cacheSize) {
    BoundedSortedVec<RecentEntry> cache(cacheSize);
    for (size_t i = 0; i < types.size(); i++) {
        if (types[i] == 0) continue;
        cache.insert({static_cast<time_t>(modTimes[i]), static_cast<uint32_t>(i)});
    }
    return cache;
}

void SearchEngine::rebuildRecentCache() {
    recentCache_ = buildRecentCacheFromData(types_, modTimes_, kRecentCacheSize);
}

void SearchEngine::addToRecentCache(uint32_t idx, time_t modTime) {
    recentCache_.insert({modTime, idx});
}

void SearchEngine::removeFromRecentCache(uint32_t idx, time_t modTime) {
    recentCache_.erase({modTime, idx});
}

uint32_t SearchEngine::indexForPath(const std::string& fullPath) const {
    std::shared_lock lock(mutex_);
    auto it = pathIndex_.find(pathHash(me::toLower(fullPath)));
    return (it != pathIndex_.end()) ? it->second : UINT32_MAX;
}

std::vector<FileRecord> SearchEngine::exportRecords() const {
    std::shared_lock lock(mutex_);
    std::vector<FileRecord> result;
    result.reserve(liveCount_.load(std::memory_order_relaxed));
    for (size_t i = 0; i < types_.size(); i++) {
        if (types_[i] == 0) continue; // skip tombstones
        FileRecord r;
        r.name = origNamePool_.str(i);
        r.type = types_[i];
        r.size = sizes_[i];
        r.modTime = static_cast<time_t>(modTimes_[i]);
        r.inode = inodes_[i];
        r.devId = devIds_[i];
        r.path = pathPool_.str(pathIndices_[i]);
        result.push_back(std::move(r));
    }
    return result;
}

void SearchEngine::replayWALEntries(std::vector<WALEntry>&& entries) {
    if (entries.empty()) return;

    std::unique_lock lock(mutex_);

    for (auto& e : entries) {
        std::string lowerFull = me::toLower(e.fullPath);

        switch (e.op) {
        case WALOp::Add: {
            // If path already exists (duplicate Add), tombstone old record first
            uint64_t walHash = pathHash(lowerFull);
            auto existIt = pathIndex_.find(walHash);
            if (existIt != pathIndex_.end()) {
                uint32_t oldIdx = existIt->second;
                removeTrigramsForRecord(oldIdx);
                removePathTrigramsForRecord(oldIdx);
                removeExtensionForRecord(oldIdx);
                tombstoneAt(oldIdx);
                pathIndex_.erase(existIt);
                liveCount_.fetch_sub(1, std::memory_order_relaxed);
            }

            uint32_t idx = static_cast<uint32_t>(types_.size());
            std::string lower = me::toLower(e.record.name);
            uint32_t pIdx = internPath(e.record.path);
            if (pIdx >= pathIdxToRecords_.size()) {
                ensurePathTrigramsForPathIdx(pIdx);
            }
            e.record.path.clear();
            e.record.path.shrink_to_fit();

            origNamePool_.append(e.record.name);
            pushRecord(std::move(e.record));
            namePool_.append(lower);
            pathIndices_.push_back(pIdx);
            pathIndex_[walHash] = idx;
            addTrigramsForRecord(idx, namePool_.data(idx), namePool_.length(idx));
            addPathTrigramsForRecord(idx);
            addExtensionForRecord(idx);
            if (idx / kRecordsPerPage >= dirtyPages_.size()) {
                dirtyPages_.resize(idx / kRecordsPerPage + 1, false);
            }
            markPageDirty(idx);
            liveCount_.fetch_add(1, std::memory_order_relaxed);
            break;
        }
        case WALOp::Remove: {
            auto it = pathIndex_.find(pathHash(lowerFull));
            if (it == pathIndex_.end()) break; // silently ignore
            uint32_t idx = it->second;
            removeTrigramsForRecord(idx);
            removePathTrigramsForRecord(idx);
            removeExtensionForRecord(idx);
            tombstoneAt(idx);
            pathIndex_.erase(it);
            liveCount_.fetch_sub(1, std::memory_order_relaxed);
            break;
        }
        case WALOp::Update: {
            // Remove old record if exists
            uint64_t walHash2 = pathHash(lowerFull);
            auto it = pathIndex_.find(walHash2);
            if (it != pathIndex_.end()) {
                uint32_t oldIdx = it->second;
                removeTrigramsForRecord(oldIdx);
                removePathTrigramsForRecord(oldIdx);
                removeExtensionForRecord(oldIdx);
                tombstoneAt(oldIdx);
                pathIndex_.erase(it);
                liveCount_.fetch_sub(1, std::memory_order_relaxed);
            }
            // Add updated record
            uint32_t newIdx = static_cast<uint32_t>(types_.size());
            std::string lower = me::toLower(e.record.name);
            uint32_t pIdx = internPath(e.record.path);
            if (pIdx >= pathIdxToRecords_.size()) {
                ensurePathTrigramsForPathIdx(pIdx);
            }
            e.record.path.clear();
            e.record.path.shrink_to_fit();

            origNamePool_.append(e.record.name);
            pushRecord(std::move(e.record));
            namePool_.append(lower);
            pathIndices_.push_back(pIdx);
            pathIndex_[walHash2] = newIdx;
            addTrigramsForRecord(newIdx, namePool_.data(newIdx), namePool_.length(newIdx));
            addPathTrigramsForRecord(newIdx);
            addExtensionForRecord(newIdx);
            if (newIdx / kRecordsPerPage >= dirtyPages_.size()) {
                dirtyPages_.resize(newIdx / kRecordsPerPage + 1, false);
            }
            markPageDirty(newIdx);
            liveCount_.fetch_add(1, std::memory_order_relaxed);
            break;
        }
        }
    }

    rebuildRecentCache();
}
