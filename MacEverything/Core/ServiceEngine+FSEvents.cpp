#include "ServiceEngine.h"
#include "DirectoryScanner.h"
#include "RescanDebounce.h"
#include "Logger.h"
#include <sys/stat.h>
#include <algorithm>

// ═══════════════════════════════════════════════════════
//  FSEvents application (replay path)
// ═══════════════════════════════════════════════════════

void ServiceEngine::applyFSEvents(
    const std::vector<FileSystemWatcher::Event>& events,
    std::shared_ptr<SearchEngine> engine)
{
    // Collect all mutation ops first, then apply in a single batch lock
    std::vector<SearchEngine::MutationOp> ops;
    ops.reserve(events.size());

    // Content index updates are collected separately (they use their own lock)
    std::vector<std::pair<std::string, bool>> contentUpdates; // path, isRemove

    for (const auto& event : events) {
        const std::string& path = event.path;
        FSEventStreamEventFlags flags = event.flags;

        if (isInsideAppBundle(path)) continue;

        bool itemRemoved = (flags & kFSEventStreamEventFlagItemRemoved) != 0;
        bool itemRenamed = (flags & kFSEventStreamEventFlagItemRenamed) != 0;

        struct stat st;
        bool exists = (lstat(path.c_str(), &st) == 0);

        if (itemRemoved || (itemRenamed && !exists)) {
            contentUpdates.push_back({path, true});
            ops.push_back({SearchEngine::MutationOp::REMOVE, path, {}});
        } else if (exists) {
            std::string dirPath, fileName;
            size_t lastSlash = path.rfind('/');
            if (lastSlash != std::string::npos) {
                dirPath = path.substr(0, lastSlash);
                fileName = path.substr(lastSlash + 1);
            } else {
                dirPath = ".";
                fileName = path;
            }
            if (fileName.empty()) continue;

            uint8_t type = 4;
            if (S_ISREG(st.st_mode))       type = 1;
            else if (S_ISDIR(st.st_mode))   type = pathEndsWithApp(path) ? 5 : 2;
            else if (S_ISLNK(st.st_mode))   type = 3;

            FileRecord record;
            record.name = fileName;
            record.path = dirPath;
            record.type = type;
            record.size = S_ISREG(st.st_mode) ? static_cast<uint64_t>(st.st_size) : 0;
            record.modTime = st.st_mtime;
            record.inode = st.st_ino;
            record.devId = static_cast<int32_t>(st.st_dev);

            ops.push_back({SearchEngine::MutationOp::UPDATE, path, std::move(record)});

            if (type == 1) {
                contentUpdates.push_back({path, false});
            }
        }
    }

    // Apply content index updates (uses its own lock)
    for (const auto& [path, isRemove] : contentUpdates) {
        updateContentForPath(path, isRemove, engine);
    }

    // Apply all search engine mutations in a single lock acquisition
    engine->batchMutate(std::move(ops));
}

// ═══════════════════════════════════════════════════════
//  Live monitoring
// ═══════════════════════════════════════════════════════

void ServiceEngine::startMonitoring() {
    if (isMonitoring_.load(std::memory_order_relaxed)) return;

    std::string root = config_.scanRoot;

    // Exclude app's own cache directory AND /Volumes (managed by per-volume watchers)
    std::string cacheExclusion = config_.cachePath;
    if (cacheExclusion.empty()) {
        const char* home = std::getenv("HOME");
        if (home) cacheExclusion = std::string(home) + "/Library/Caches/com.maceverything.app";
    }
    std::vector<std::string> exclusions;
    if (!cacheExclusion.empty()) exclusions.push_back(cacheExclusion);
    // /Volumes is managed by per-volume watchers; skip in the root watcher to
    // avoid double-processing events when a volume gets its own watcher.
    exclusions.push_back("/Volumes");
    if (!exclusions.empty()) {
        watcher_->setExclusionPaths(exclusions);
    }

    watcher_->start(root, [this](std::vector<FileSystemWatcher::Event> events) {
        if (shuttingDown_.load(std::memory_order_relaxed)) return;

        auto engine = safeEngine();
        if (!engine) return;

        std::vector<std::string> rescanDirs;
        std::vector<SearchEngine::MutationOp> ops;
        ops.reserve(events.size());
        std::vector<std::pair<std::string, bool>> contentUpdates;
        bool changed = false;

        for (const auto& event : events) {
            const std::string& path = event.path;
            FSEventStreamEventFlags flags = event.flags;

            if (isInsideAppBundle(path)) continue;

            if (flags & kFSEventStreamEventFlagMustScanSubDirs) {
                rescanDirs.push_back(path);
                continue;
            }

            bool itemRemoved = (flags & kFSEventStreamEventFlagItemRemoved) != 0;
            bool itemRenamed = (flags & kFSEventStreamEventFlagItemRenamed) != 0;

            struct stat st;
            bool exists = (lstat(path.c_str(), &st) == 0);

            if (itemRemoved || (itemRenamed && !exists)) {
                contentUpdates.push_back({path, true});
                ops.push_back({SearchEngine::MutationOp::REMOVE, path, {}});
                changed = true;
            } else if (exists) {
                std::string dirPath, fileName;
                size_t lastSlash = path.rfind('/');
                if (lastSlash != std::string::npos) {
                    dirPath = path.substr(0, lastSlash);
                    fileName = path.substr(lastSlash + 1);
                } else {
                    dirPath = ".";
                    fileName = path;
                }
                if (fileName.empty()) continue;

                uint8_t type = 4;
                if (S_ISREG(st.st_mode))       type = 1;
                else if (S_ISDIR(st.st_mode))   type = pathEndsWithApp(fileName) ? 5 : 2;
                else if (S_ISLNK(st.st_mode))   type = 3;

                FileRecord record;
                record.name = fileName;
                record.path = dirPath;
                record.type = type;
                record.size = S_ISREG(st.st_mode) ? static_cast<uint64_t>(st.st_size) : 0;
                record.modTime = st.st_mtime;
                record.inode = st.st_ino;
                record.devId = static_cast<int32_t>(st.st_dev);

                ops.push_back({SearchEngine::MutationOp::UPDATE, path, std::move(record)});
                changed = true;

                if (type == 1) {
                    contentUpdates.push_back({path, false});
                }
            }
        }

        // Apply content index updates (uses its own lock)
        for (const auto& [path, isRemove] : contentUpdates) {
            updateContentForPath(path, isRemove, engine);
        }

        // Apply all search engine mutations in a single lock acquisition
        engine->batchMutate(std::move(ops));

        if (!rescanDirs.empty()) {
            scheduleRescanForPaths(rescanDirs);
        }

        if (changed && onIndexChanged) {
            onIndexChanged();
        }
    });

    isMonitoring_.store(true, std::memory_order_relaxed);

    // Start observing volume mount/unmount events.
    startVolumeWatcher();

    // Reconcile persisted offline volumes with the current mount state.
    // Volumes that are still mounted are marked online; volumes that
    // are still missing stay offline (will be re-scanned on remount).
    reconcileMountStateOnStartup();
}

void ServiceEngine::stopMonitoring() {
    watcher_->stop();

    {
        std::lock_guard<std::mutex> lock(pendingRescanMutex_);
        if (rescanDebounceTimer_) {
            dispatch_source_cancel(rescanDebounceTimer_);
            rescanDebounceTimer_ = nullptr;
        }
        pendingRescanPaths_.clear();
        lastRescanTime_.clear();
    }

    // Stop all per-volume watchers.
    {
        std::lock_guard<std::mutex> lock(volumeWatchersMutex_);
        for (auto& [path, w] : volumeWatchers_) {
            w->stop();
        }
        volumeWatchers_.clear();
    }

    // Cancel any pending mount debounce.
    {
        std::lock_guard<std::mutex> lock(pendingMountMutex_);
        if (mountDebounceTimer_) {
            dispatch_source_cancel(mountDebounceTimer_);
            mountDebounceTimer_ = nullptr;
        }
        pendingMountPaths_.clear();
    }

    auto persistence = safePersistence();
    if (persistence) {
        persistence->stopAutoCompactionAndWait();
    }
    auto cp = safeContentPersistence();
    if (cp) {
        cp->stopAutoCompactionAndWait();
    }
    isMonitoring_.store(false, std::memory_order_relaxed);
}

// ═══════════════════════════════════════════════════════
//  Rescan debounce
// ═══════════════════════════════════════════════════════

void ServiceEngine::scheduleRescanForPaths(const std::vector<std::string>& paths) {
    std::lock_guard<std::mutex> lock(pendingRescanMutex_);

    pendingRescanPaths_ = mergeRescanPaths(pendingRescanPaths_, paths);

    LOG_INFO("FSWatcher", "Debounce: " << pendingRescanPaths_.size()
             << " pending rescan path(s), scheduling " << kRescanDebounceDelaySec << "s delay");

    if (rescanDebounceTimer_) {
        dispatch_source_cancel(rescanDebounceTimer_);
        rescanDebounceTimer_ = nullptr;
    }

    rescanDebounceTimer_ = dispatch_source_create(
        DISPATCH_SOURCE_TYPE_TIMER, 0, 0, mutationQueue_);

    uint64_t delaySec = static_cast<uint64_t>(kRescanDebounceDelaySec * NSEC_PER_SEC);
    dispatch_source_set_timer(rescanDebounceTimer_, dispatch_time(DISPATCH_TIME_NOW, delaySec),
                              DISPATCH_TIME_FOREVER, NSEC_PER_SEC / 10);

    dispatch_source_set_event_handler(rescanDebounceTimer_, ^{
        this->flushPendingRescans();
    });
    dispatch_resume(rescanDebounceTimer_);
}

void ServiceEngine::flushPendingRescans() {
    std::set<std::string> pathsToRescan;
    std::set<std::string> throttledPaths;
    {
        std::lock_guard<std::mutex> lock(pendingRescanMutex_);
        for (const auto& path : pendingRescanPaths_) {
            if (shouldThrottleRescan(path, lastRescanTime_, kRescanThrottleIntervalSec)) {
                throttledPaths.insert(path);
                LOG_INFO("FSWatcher", "Throttled rescan for: " << path
                         << " (within " << kRescanThrottleIntervalSec << "s window)");
            } else {
                pathsToRescan.insert(path);
            }
        }
        pendingRescanPaths_ = throttledPaths;

        if (rescanDebounceTimer_) {
            dispatch_source_cancel(rescanDebounceTimer_);
            rescanDebounceTimer_ = nullptr;
        }
    }

    if (pathsToRescan.empty() && throttledPaths.empty()) return;

    LOG_INFO("FSWatcher", "Flushing debounced rescan: " << pathsToRescan.size()
             << " path(s) to rescan, " << throttledPaths.size() << " throttled");

    for (const auto& path : pathsToRescan) {
        rescanSubtree(path);

        std::lock_guard<std::mutex> lock(pendingRescanMutex_);
        lastRescanTime_[path] = std::chrono::steady_clock::now();
    }

    // Clean up old entries in lastRescanTime_ (> 2x throttle interval)
    {
        std::lock_guard<std::mutex> lock(pendingRescanMutex_);
        auto now = std::chrono::steady_clock::now();
        for (auto it = lastRescanTime_.begin(); it != lastRescanTime_.end(); ) {
            auto elapsed = std::chrono::duration<double>(now - it->second).count();
            if (elapsed > kRescanThrottleIntervalSec * 2.0) {
                it = lastRescanTime_.erase(it);
            } else {
                ++it;
            }
        }
    }

    // If there are throttled paths, schedule a retry when throttle expires
    if (!throttledPaths.empty()) {
        std::lock_guard<std::mutex> lock(pendingRescanMutex_);
        if (!rescanDebounceTimer_) {
            rescanDebounceTimer_ = dispatch_source_create(
                DISPATCH_SOURCE_TYPE_TIMER, 0, 0, mutationQueue_);
            uint64_t delay = static_cast<uint64_t>(kRescanThrottleIntervalSec * NSEC_PER_SEC);
            dispatch_source_set_timer(rescanDebounceTimer_,
                                      dispatch_time(DISPATCH_TIME_NOW, delay),
                                      DISPATCH_TIME_FOREVER, NSEC_PER_SEC);
            dispatch_source_set_event_handler(rescanDebounceTimer_, ^{
                this->flushPendingRescans();
            });
            dispatch_resume(rescanDebounceTimer_);
        }
    }
}

void ServiceEngine::rescanSubtree(const std::string& dir) {
    auto engine = safeEngine();
    if (!engine) return;

    dispatch_async(mutationQueue_, ^{
        if (shuttingDown_.load(std::memory_order_relaxed)) return;

        auto scanner = std::make_shared<DirectoryScanner>();
        scanner->scan(dir);
        auto freshRecords = scanner->takeResults();

        if (shuttingDown_.load(std::memory_order_relaxed)) return;

        engine->batchRescanPrefix(dir, std::move(freshRecords));

        // Compact if tombstones exceed 30%
        uint32_t total = engine->recordCount();
        uint32_t live  = engine->liveRecordCount();
        if (total > live && (total - live) > total * 3 / 10) {
            auto remap = engine->compactRecords();
            if (!remap.empty()) {
                auto ci = safeContentIndex();
                if (ci) {
                    ci->remapFileIndices(remap);
                }
            }
        }

        if (onIndexChanged) {
            onIndexChanged();
        }
    });
}

// ═══════════════════════════════════════════════════════
//  Volume mount / unmount handling
// ═══════════════════════════════════════════════════════

void ServiceEngine::startVolumeWatcher() {
    if (!volumeWatcher_ || volumeWatcher_->isRunning()) return;
    volumeWatcher_->start(
        [this](std::string mountPath) { this->handleVolumeMount(mountPath); },
        [this](std::string mountPath) { this->handleVolumeUnmount(mountPath); }
    );
    LOG_INFO("VolumeWatcher", "Started observing /Volumes mount events");
}

void ServiceEngine::stopVolumeWatcher() {
    if (volumeWatcher_) volumeWatcher_->stop();
}

void ServiceEngine::handleVolumeMount(std::string mountPath) {
    LOG_INFO("VolumeWatcher", "Mount detected: " << mountPath);

    // Defer all work to mutationQueue_ so we don't race with FSEvents delivery.
    dispatch_async(mutationQueue_, ^{
        if (shuttingDown_.load(std::memory_order_acquire)) return;

        // Register the volume path so any existing records under it (from
        // a previous session) are now associated with this volume.
        if (volumeIndex_) volumeIndex_->addVolume(mountPath);
        // A re-mount implicitly brings the volume online.
        if (volumeIndex_) volumeIndex_->markOnline(mountPath);

        // Debounce: if multiple mount events arrive within 30s, only the
        // last firing of the timer runs a rescan. This also handles the
        // case where macOS sends a quick unmount+remount pair.
        std::lock_guard<std::mutex> lock(pendingMountMutex_);
        pendingMountPaths_.insert(mountPath);

        if (mountDebounceTimer_) {
            dispatch_source_cancel(mountDebounceTimer_);
            mountDebounceTimer_ = nullptr;
        }

        mountDebounceTimer_ = dispatch_source_create(
            DISPATCH_SOURCE_TYPE_TIMER, 0, 0, mutationQueue_);
        uint64_t delaySec = static_cast<uint64_t>(kMountDebounceDelaySec * NSEC_PER_SEC);
        dispatch_source_set_timer(mountDebounceTimer_,
                                  dispatch_time(DISPATCH_TIME_NOW, delaySec),
                                  DISPATCH_TIME_FOREVER, NSEC_PER_SEC / 10);
        dispatch_source_set_event_handler(mountDebounceTimer_, ^{
            this->flushPendingMounts();
        });
        dispatch_resume(mountDebounceTimer_);

        LOG_INFO("VolumeWatcher", "Debounce: " << pendingMountPaths_.size()
                 << " pending mount(s), scheduling " << kMountDebounceDelaySec << "s delay");
    });
}

void ServiceEngine::handleVolumeUnmount(std::string mountPath) {
    LOG_INFO("VolumeWatcher", "Unmount detected: " << mountPath);

    dispatch_async(mutationQueue_, ^{
        if (shuttingDown_.load(std::memory_order_acquire)) return;

        // Stop the per-volume watcher so we don't try to deliver events
        // for a path that no longer exists.
        stopWatcherForVolume(mountPath);

        // Mark the volume offline. Records stay in the index; queries can
        // filter them out via VolumeIndex::isRecordOffline().
        if (volumeIndex_) volumeIndex_->markOffline(mountPath);

        if (onVolumeUnmounted) onVolumeUnmounted(mountPath);
        if (onIndexChanged)   onIndexChanged();
    });
}

void ServiceEngine::flushPendingMounts() {
    std::set<std::string> paths;
    {
        std::lock_guard<std::mutex> lock(pendingMountMutex_);
        paths = pendingMountPaths_;
        pendingMountPaths_.clear();
        if (mountDebounceTimer_) {
            dispatch_source_cancel(mountDebounceTimer_);
            mountDebounceTimer_ = nullptr;
        }
    }

    if (paths.empty()) return;

    for (const auto& mountPath : paths) {
        // Verify the mount point is still there — the user might have
        // ejected the volume during the 30s debounce window.
        struct stat st;
        if (stat(mountPath.c_str(), &st) != 0) {
            LOG_INFO("VolumeWatcher", "Skipping rescan of " << mountPath
                     << " — no longer mounted");
            if (volumeIndex_) volumeIndex_->markOffline(mountPath);
            continue;
        }

        LOG_INFO("VolumeWatcher", "Rescanning mounted volume: " << mountPath);
        rescanSubtree(mountPath);
        startWatcherForVolume(mountPath);

        if (onVolumeMounted) onVolumeMounted(mountPath);
    }
}

void ServiceEngine::startWatcherForVolume(const std::string& mountPath) {
    if (!watcher_) return;
    std::lock_guard<std::mutex> lock(volumeWatchersMutex_);
    if (volumeWatchers_.count(mountPath)) return;

    auto w = std::make_shared<FileSystemWatcher>("vol-" + mountPath);
    std::string cacheExclusion = config_.cachePath;
    if (!cacheExclusion.empty()) {
        w->setExclusionPaths({cacheExclusion});
    }

    w->start(mountPath, [this](std::vector<FileSystemWatcher::Event> events) {
        if (shuttingDown_.load(std::memory_order_relaxed)) return;
        auto engine = safeEngine();
        if (!engine) return;

        // Reuse the existing FSEvents application logic — it's path-agnostic.
        this->applyFSEvents(events, engine);

        // MustScanSubDirs events for this volume still go through the
        // existing debounce, which also handles cross-volume rescan.
        std::vector<std::string> rescanDirs;
        for (const auto& e : events) {
            if (e.flags & kFSEventStreamEventFlagMustScanSubDirs) {
                rescanDirs.push_back(e.path);
            }
        }
        if (!rescanDirs.empty()) {
            this->scheduleRescanForPaths(rescanDirs);
        }
    });

    volumeWatchers_[mountPath] = w;
    LOG_INFO("VolumeWatcher", "Started FSEvents watcher for " << mountPath);
}

void ServiceEngine::stopWatcherForVolume(const std::string& mountPath) {
    std::lock_guard<std::mutex> lock(volumeWatchersMutex_);
    auto it = volumeWatchers_.find(mountPath);
    if (it == volumeWatchers_.end()) return;
    it->second->stop();
    volumeWatchers_.erase(it);
    LOG_INFO("VolumeWatcher", "Stopped FSEvents watcher for " << mountPath);
}

void ServiceEngine::reconcileMountStateOnStartup() {
    if (!volumeIndex_) return;
    auto currentMounts = VolumeWatcher::currentlyMountedLocalVolumes();
    auto persistedOffline = volumeIndex_->offlineVolumePaths();

    // First, register every currently-mounted volume.
    for (const auto& m : currentMounts) {
        volumeIndex_->addVolume(m);
    }

    // For each persisted offline volume:
    //  - If it is currently mounted, mark it online (and trigger a rescan).
    //  - If it is not mounted, keep it offline and registered.
    for (const auto& p : persistedOffline) {
        if (std::find(currentMounts.begin(), currentMounts.end(), p) != currentMounts.end()) {
            LOG_INFO("VolumeWatcher", "Persisted offline volume is mounted again: " << p);
            volumeIndex_->markOnline(p);
            handleVolumeMount(p);  // apply 30s debounce + rescan
        } else {
            LOG_INFO("VolumeWatcher", "Persisted offline volume still missing: " << p);
            volumeIndex_->addVolume(p);  // ensure it's in the path→idx map
            volumeIndex_->markOffline(p);
        }
    }
}

void ServiceEngine::rescanVolumeNow(const std::string& mountPath) {
    dispatch_async(mutationQueue_, ^{
        if (shuttingDown_.load(std::memory_order_acquire)) return;
        if (volumeIndex_) {
            volumeIndex_->addVolume(mountPath);
            volumeIndex_->markOnline(mountPath);
        }
        rescanSubtree(mountPath);
        startWatcherForVolume(mountPath);
        if (onVolumeMounted) onVolumeMounted(mountPath);
    });
}
