#include "SearchEngine.h"
#include "StringUtils.h"
#include "Logger.h"
#include <algorithm>
#include <chrono>
#include <thread>
#ifdef MACEVERYTHING_TESTING
#include <atomic>
#endif
#ifdef __APPLE__
#include <mach/mach.h>
#endif

// ---------------------------------------------------------------------------
// v6 Flat SoA: loadRecordsV6, completePhase2, snapshotForV6
// ---------------------------------------------------------------------------

#ifdef MACEVERYTHING_TESTING
namespace {
std::atomic<void (*)()> gPhase2PostSnapshotHook{nullptr};
}

void SearchEngine::setPhase2PostSnapshotHook(void (*hook)()) {
    gPhase2PostSnapshotHook.store(hook, std::memory_order_release);
}
#endif

void SearchEngine::loadRecordsV6(StringPool&& origNamePool,
                                  StringPool&& namePool,
                                  std::vector<uint32_t>&& pathIndices,
                                  StringPool&& pathPool,
                                  StringPool&& lowerPathPool,
                                  std::vector<uint8_t>&& types,
                                  std::vector<uint64_t>&& sizes,
                                  std::vector<int64_t>&& modTimes,
                                  std::vector<uint64_t>&& inodes,
                                  std::vector<int32_t>&& devIds) {
    std::unique_lock lock(mutex_);

    uint32_t n = origNamePool.entryCount();

    // Install SoA columns directly (zero-copy from v6 file)
    origNamePool_ = std::move(origNamePool);
    namePool_ = std::move(namePool);
    pathIndices_ = std::move(pathIndices);
    pathPool_ = std::move(pathPool);
    lowerPathPool_ = std::move(lowerPathPool);
    types_ = std::move(types);
    sizes_ = std::move(sizes);
    modTimes_ = std::move(modTimes);
    inodes_ = std::move(inodes);
    devIds_ = std::move(devIds);

    // Rebuild pathLookup_ and lowerPathLookup_ from pathPool_ entries
    pathLookup_.clear();
    lowerPathLookup_.clear();
    pathLookup_.reserve(pathPool_.entryCount());
    lowerPathLookup_.reserve(pathPool_.entryCount());
    for (uint32_t i = 0; i < pathPool_.entryCount(); i++) {
        if (pathPool_.isLive(i)) {
            pathLookup_[pathPool_.str(i)] = i;
            lowerPathLookup_[lowerPathPool_.str(i)] = i;
        }
    }

    // Build pathIndex_ (lowercase full path -> record index)
    // Parallelize full-path construction using string_view to avoid allocations
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
            size_t end = std::min(start + chunkSize, static_cast<size_t>(n));
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

    // Insert into pathIndex_ and merge tombstone dedup (last-wins overwrites)
    pathIndex_.clear();
    pathIndex_.reserve(n);
    for (uint32_t i = 0; i < n; i++) {
        if (types_[i] == 0) continue;
        pathIndex_[std::move(loweredPaths[i])] = i;
    }

    // Tombstone orphaned duplicates: records not in pathIndex_ as winners
    uint32_t actualLive = 0;
    {
        // Collect winner indices from pathIndex_ values
        std::vector<bool> isWinner(n, false);
        for (const auto& [_, idx] : pathIndex_) {
            isWinner[idx] = true;
        }
        for (uint32_t i = 0; i < n; i++) {
            if (types_[i] == 0) continue;
            if (isWinner[i]) {
                actualLive++;
            } else {
                tombstoneAt(i);
            }
        }
    }

    rebuildSearchableNamePools(false);

    liveCount_.store(actualLive, std::memory_order_relaxed);

    // Phase 2: defer trigram index building to background
    phase2Pending_.store(true, std::memory_order_release);
    phase2StartRecordCount_ = static_cast<uint32_t>(types_.size());

    // Initialize dirty page bitmap
    uint32_t pageCount = (n + kRecordsPerPage - 1) / kRecordsPerPage;
    dirtyPages_.assign(pageCount, false);
    fullRewriteNeeded_.store(false, std::memory_order_relaxed);

    LOG_INFO("SearchEngine", "loadRecordsV6: loaded " << n << " records (" << actualLive
             << " live), Phase 2 pending");
}

std::string SearchEngine::completePhase2() {
    if (!phase2Pending_.load(std::memory_order_acquire)) return {};

    // Check available memory before snapshot+build
    // Measured peak: ~200B/record (snapshots ~60B + indices ~140B)
    {
        std::shared_lock lock(mutex_);
        uint64_t recordCount = types_.size();
        uint64_t estimatedBytes = recordCount * 200;
        uint64_t availablePages = 0;
#ifdef __APPLE__
        vm_statistics64_data_t vmstat;
        mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
        if (host_statistics64(mach_host_self(), HOST_VM_INFO64,
                              (host_info64_t)&vmstat, &count) == KERN_SUCCESS) {
            availablePages = vmstat.free_count + vmstat.inactive_count;
        }
        uint64_t availableBytes = availablePages * vm_page_size;
        if (availableBytes > 0 && estimatedBytes > availableBytes * 7 / 10) {
            std::string msg = "Insufficient memory for trigram index: need ~"
                + std::to_string(estimatedBytes >> 20) + "MB but only ~"
                + std::to_string(availableBytes >> 20) + "MB available. Close other applications to free memory.";
            LOG_WARN("SearchEngine", msg);
            // Keep phase2Pending_=true so addRecord() skips trigram insertion.
            // Otherwise new records build a tiny partial index that corrupts search.
            return msg;
        }
#endif
    }

    auto phase2Start = std::chrono::steady_clock::now();
    LOG_INFO("SearchEngine", "Phase 2: building trigram indices in background...");

    // Snapshot data needed for building indices (under shared lock)
    std::vector<uint8_t> snapTypes;
    std::vector<int64_t> snapModTimes;
    StringPool snapOrigNamePool;
    StringPool snapNamePool;
    StringPool snapPathPool;
    StringPool snapLowerPathPool;
    std::vector<uint32_t> snapPathIndices;
    uint32_t snapPathPoolSize;
    uint32_t snapSize;
    auto snapStart = std::chrono::steady_clock::now();
    {
        std::shared_lock lock(mutex_);
        snapTypes = types_;
        snapModTimes = modTimes_;
        snapOrigNamePool = origNamePool_;
        snapNamePool = namePool_;
        snapPathPool = pathPool_;
        snapLowerPathPool = lowerPathPool_;
        snapPathIndices = pathIndices_;
        snapPathPoolSize = pathPool_.entryCount();
        snapSize = static_cast<uint32_t>(types_.size());
    }
#ifdef MACEVERYTHING_TESTING
    if (auto hook = gPhase2PostSnapshotHook.load(std::memory_order_acquire)) {
        hook();
    }
#endif
    auto snapMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - snapStart).count();

    // Build all indices without holding any lock
    auto t0 = std::chrono::steady_clock::now();
    StringPool snapSearchableNamePool;
    StringPool snapOriginalSearchableNamePool;
    {
        std::vector<std::string> searchableNames(snapTypes.size());
        std::vector<std::string> originalSearchableNames(snapTypes.size());
        for (uint32_t i = 0; i < snapTypes.size(); i++) {
            if (snapTypes[i] == 0) continue;
            searchableNames[i] = buildSearchableNameFromParts(snapNamePool.view(i),
                                                               snapPathPool.view(snapPathIndices[i]),
                                                               snapOrigNamePool.view(i));
            originalSearchableNames[i] = buildOriginalSearchableNameFromParts(snapNamePool.view(i),
                                                                              snapPathPool.view(snapPathIndices[i]),
                                                                              snapOrigNamePool.view(i));
        }
        snapSearchableNamePool.loadBulk(searchableNames);
        snapOriginalSearchableNamePool.loadBulk(originalSearchableNames);
        for (uint32_t i = 0; i < snapTypes.size(); i++) {
            if (snapTypes[i] == 0) {
                snapSearchableNamePool.tombstone(i);
                snapOriginalSearchableNamePool.tombstone(i);
            }
        }
    }
    auto aliasMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();

    t0 = std::chrono::steady_clock::now();
    auto trigramIndex = buildTrigramIndexFromData(snapTypes, snapSearchableNamePool);
    auto trigramMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();

    t0 = std::chrono::steady_clock::now();
    auto pathTrigramIndex = buildPathTrigramIndexFromData(snapLowerPathPool);
    auto pathTrigramMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();

    t0 = std::chrono::steady_clock::now();
    auto pathIdxToRecords = buildPathIdxToRecordsFromData(snapTypes, snapPathIndices, snapPathPoolSize);
    auto pathIdxMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();

    t0 = std::chrono::steady_clock::now();
    auto recentCache = buildRecentCacheFromData(snapTypes, snapModTimes, kRecentCacheSize);
    auto recentMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();

    t0 = std::chrono::steady_clock::now();
    auto extensionIndex = buildExtensionIndexFromData(snapTypes, snapNamePool);
    auto extMs = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - t0).count();

    LOG_INFO("SearchEngine", "Phase 2 build done: snap=" << snapMs
             << "ms alias=" << aliasMs << "ms trigram=" << trigramMs << "ms pathTrigram=" << pathTrigramMs
             << "ms pathIdx=" << pathIdxMs << "ms recent=" << recentMs
             << "ms ext=" << extMs << "ms");

    // Swap under unique lock and replay mutations that occurred during build
    auto swapStart = std::chrono::steady_clock::now();
    {
        std::unique_lock lock(mutex_);

        StringPool liveSearchableNamePool = std::move(searchableNamePool_);
        StringPool liveOriginalSearchableNamePool = std::move(originalSearchableNamePool_);
        searchableNamePool_ = std::move(snapSearchableNamePool);
        originalSearchableNamePool_ = std::move(snapOriginalSearchableNamePool);
        nameTrigramIndex_ = std::move(trigramIndex);
        pathTrigramIndex_ = std::move(pathTrigramIndex);
        pathIdxToRecords_ = std::move(pathIdxToRecords);
        recentCache_ = std::move(recentCache);
        extensionIndex_ = std::move(extensionIndex);

        // Replay records added during Phase 2 build
        uint32_t currentSize = static_cast<uint32_t>(types_.size());
        uint32_t replayCount = 0;
        for (uint32_t i = snapSize; i < currentSize; i++) {
            if (i < liveSearchableNamePool.entryCount()) {
                searchableNamePool_.append(liveSearchableNamePool.data(i), liveSearchableNamePool.length(i));
            } else {
                searchableNamePool_.append(namePool_.data(i), namePool_.length(i));
            }
            if (i < liveOriginalSearchableNamePool.entryCount()) {
                originalSearchableNamePool_.append(liveOriginalSearchableNamePool.data(i), liveOriginalSearchableNamePool.length(i));
            } else {
                originalSearchableNamePool_.append(origNamePool_.data(i), origNamePool_.length(i));
            }
            if (types_[i] == 0) {
                searchableNamePool_.tombstone(i);
                originalSearchableNamePool_.tombstone(i);
                continue;
            }
            addTrigramsForRecord(i, searchableNamePool_.data(i), searchableNamePool_.length(i));
            ensurePathTrigramsForPathIdx(pathIndices_[i]);
            addPathTrigramsForRecord(i);
            addExtensionForRecord(i);
            addToRecentCache(i, static_cast<time_t>(modTimes_[i]));
            replayCount++;
        }

        // Replay tombstones: records that were live in snapshot but deleted during build
        bool replayedSnapshotTombstones = false;
        for (uint32_t i = 0; i < snapSize; i++) {
            if (i >= types_.size()) break;
            if (snapTypes[i] != 0 && types_[i] == 0) {
                replayedSnapshotTombstones = true;
                removeTrigramsForRecord(i);
                removePathTrigramsForRecord(i);
                if (i < snapNamePool.entryCount() && snapNamePool.isLive(i)) {
                    std::string ext;
                    std::string_view name = snapNamePool.view(i);
                    size_t dot = name.rfind('.');
                    if (dot != std::string_view::npos && dot + 1 < name.size()) {
                        ext = std::string(name.substr(dot + 1));
                    }
                    auto it = extensionIndex_.find(ext);
                    if (it != extensionIndex_.end()) {
                        auto& list = it->second;
                        auto pos = std::lower_bound(list.begin(), list.end(), i);
                        if (pos != list.end() && *pos == i) {
                            list.erase(pos);
                        }
                        if (list.empty()) {
                            extensionIndex_.erase(it);
                        }
                    }
                }
            }
        }
        if (replayedSnapshotTombstones) {
            rebuildRecentCache();
        }

        auto replayMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - swapStart).count();

        phase2Pending_.store(false, std::memory_order_release);

        t0 = std::chrono::steady_clock::now();
        buildShortQueryCache();
        auto sqcMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - t0).count();

        auto totalMs = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - phase2Start).count();
        LOG_INFO("SearchEngine", "Phase 2 complete: replayed " << replayCount
                 << " mutations | timing: swap+replay=" << replayMs
                 << "ms sqcache=" << sqcMs << "ms total=" << totalMs << "ms");
    }
    return {};
}

SearchEngine::V6Snapshot SearchEngine::snapshotForV6() const {
    std::shared_lock lock(mutex_);
    V6Snapshot snap;
    snap.origNamePool = origNamePool_;
    snap.namePool = namePool_;
    snap.pathIndices = pathIndices_;
    snap.pathPool = pathPool_;
    snap.lowerPathPool = lowerPathPool_;
    snap.types = types_;
    snap.sizes = sizes_;
    snap.modTimes = modTimes_;
    snap.inodes = inodes_;
    snap.devIds = devIds_;
    snap.liveCount = liveCount_.load(std::memory_order_relaxed);
    return snap;
}
