#pragma once
#include "SearchEngine.h"
#include "ContentIndex.h"
#include "ContentIndexPersistence.h"
#include "IndexPersistence.h"
#include "FileSystemWatcher.h"
#include "HttpServer.h"
#include "InstanceLock.h"
#include "RescanDebounce.h"
#include "VolumeIndex.h"
#include "VolumeWatcher.h"
#include <memory>
#include <atomic>
#include <shared_mutex>
#include <set>
#include <unordered_map>
#include <chrono>
#include <functional>
#include <string>
#include <dispatch/dispatch.h>

/// Configuration for ServiceEngine.
struct ServiceConfig {
    std::string scanRoot = "/";
    std::string cachePath;   // e.g. ~/Library/Caches/com.maceverything.app
    std::string logPath;     // e.g. ~/Library/Logs/MacEverything
    uint16_t httpPort = 0;   // 0 = no HTTP server; >0 = auto-start after index is ready
    /// Seconds to wait after a volume mount event before kicking off the
    /// debounced rescan. Lower = snappier UX; higher = more coalescing of
    /// rapid mount/unmount cycles. Default 5s.
    double mountDebounceSec = 5.0;
    /// Skip dotfiles and UF_HIDDEN-flagged entries. Default true — most
    /// users don't want .DS_Store, .git/, .Trash/ in their search.
    /// Set false only for power users doing filesystem forensics.
    bool skipHiddenFiles = true;
};

/// Pure C++ orchestration engine — owns all core objects and lifecycle.
/// Bridge (GUI) and CLI daemon both instantiate this class.
class ServiceEngine {
public:
    // ── Callbacks set by caller ──
    using IndexChangedCallback = std::function<void()>;
    using ScanProgressCallback = std::function<void(uint64_t scanned, uint64_t dirs)>;
    using ContentProgressCallback = std::function<void(uint64_t indexed, uint64_t total)>;
    using ContentCompleteCallback = std::function<void(uint32_t totalIndexed)>;
    using StartupCallback = std::function<void(uint32_t totalRecords, bool didFullScan)>;

    explicit ServiceEngine(const ServiceConfig& config);
    ~ServiceEngine();

    ServiceEngine(const ServiceEngine&) = delete;
    ServiceEngine& operator=(const ServiceEngine&) = delete;

    // ── Lifecycle ──
    void startFullScan(StartupCallback completion);
    void startIncremental(StartupCallback completion);
    void shutdown();

    // ── HTTP ──
    void startHttpServer(uint16_t port);
    void stopHttpServer();

    // ── Public operations ──
    void rescanSubtree(const std::string& dir);
    void rebuildContentIndex();
    void compactIndex();

    // ── Thread-safe accessors ──
    std::shared_ptr<SearchEngine> safeEngine();
    std::shared_ptr<ContentIndex> safeContentIndex();
    std::shared_ptr<IndexPersistence> safePersistence();
    std::shared_ptr<ContentIndexPersistence> safeContentPersistence();

    // ── State queries ──
    bool isScanning() const  { return isScanning_.load(std::memory_order_relaxed); }
    bool isMonitoring() const { return isMonitoring_.load(std::memory_order_relaxed); }
    bool isSyncing() const   { return isSyncing_.load(std::memory_order_relaxed); }

    uint32_t recordCount();
    uint32_t liveRecordCount();

    // ── Callbacks (set before starting) ──
    IndexChangedCallback onIndexChanged;
    ScanProgressCallback onScanProgress;
    ContentProgressCallback onContentIndexProgress;
    ContentCompleteCallback onContentIndexComplete;

    // ── Volume mount lifecycle (optional UI hooks) ──
    using VolumeMountCallback = std::function<void(const std::string& mountPath)>;
    using VolumeUnmountCallback = std::function<void(const std::string& mountPath)>;
    VolumeMountCallback onVolumeMounted;
    VolumeUnmountCallback onVolumeUnmounted;

    // ── Public volume accessors ──
    std::shared_ptr<VolumeIndex> safeVolumeIndex() { return volumeIndex_; }
    /// Manually trigger a rescan of a volume mount point.
    /// Skips the debounce window (useful for HTTP admin endpoints).
    void rescanVolumeNow(const std::string& mountPath);

    // ── Admin callbacks for HttpServer ──
    HttpServer::AdminCallbacks adminCallbacks;

private:
    // ── Internal methods (ServiceEngine.cpp) ──
    void setEngine(std::shared_ptr<SearchEngine> engine);
    void setPersistence(std::shared_ptr<IndexPersistence> persistence);
    void setContentPersistence(std::shared_ptr<ContentIndexPersistence> persistence);
    void backgroundSyncEngine(std::shared_ptr<SearchEngine> engine,
                              std::shared_ptr<IndexPersistence> persistence,
                              uint64_t lastEventId,
                              std::chrono::steady_clock::time_point incrementalStart,
                              std::chrono::steady_clock::time_point indexLoadDone);
    IndexMetadata buildMetadata();

    // ── FSEvents methods (ServiceEngine+FSEvents.cpp) ──
    void applyFSEvents(const std::vector<FileSystemWatcher::Event>& events,
                       std::shared_ptr<SearchEngine> engine);
    void startMonitoring();
    void stopMonitoring();
    void scheduleRescanForPaths(const std::vector<std::string>& paths);
    void flushPendingRescans();

    // ── Volume mount handling (ServiceEngine+FSEvents.cpp) ──
    void startVolumeWatcher();
    void stopVolumeWatcher();
    void handleVolumeMount(std::string mountPath);
    void handleVolumeUnmount(std::string mountPath);
    void flushPendingMounts();
    void startWatcherForVolume(const std::string& mountPath);
    void stopWatcherForVolume(const std::string& mountPath);
    void reconcileMountStateOnStartup();

    // ── Content methods (ServiceEngine+Content.cpp) ──
    void startContentIndexing();
    void setupContentPersistence();
    void updateContentForPath(const std::string& fullPath, bool removed,
                              std::shared_ptr<SearchEngine> engine);

    // ── Static helpers ──
    static bool isInsideAppBundle(const std::string& path);
    static bool pathEndsWithApp(const std::string& path);

    // ── Config ──
    ServiceConfig config_;

    // ── Core objects ──
    std::shared_ptr<SearchEngine> engine_;
    std::shared_ptr<FileSystemWatcher> watcher_;
    std::shared_ptr<ContentIndex> contentIndex_;
    std::shared_ptr<IndexPersistence> persistence_;
    std::shared_ptr<ContentIndexPersistence> contentPersistence_;
    std::shared_ptr<HttpServer> httpServer_;
    InstanceLock instanceLock_;

    // ── Volume tracking ──
    std::shared_ptr<VolumeIndex> volumeIndex_;
    std::shared_ptr<VolumeWatcher> volumeWatcher_;
    // One FileSystemWatcher per mounted volume, keyed by canonical mount path.
    std::unordered_map<std::string, std::shared_ptr<FileSystemWatcher>> volumeWatchers_;

    // ── Thread safety ──
    std::shared_mutex engineMutex_;
    std::shared_mutex contentMutex_;
    std::shared_mutex persistenceMutex_;
    std::shared_mutex contentPersistenceMutex_;

    // ── Dispatch queues & groups ──
    dispatch_queue_t mutationQueue_;
    dispatch_group_t backgroundGroup_;

    // ── Atomic flags ──
    std::atomic<bool> isScanning_{false};
    std::atomic<bool> isMonitoring_{false};
    std::atomic<bool> isContentIndexing_{false};
    std::atomic<bool> shuttingDown_{false};
    std::atomic<bool> startupCompleted_{false};
    std::atomic<bool> isSyncing_{false};
    std::atomic<bool> cancelContentIndexing_{false};
    std::atomic<uint64_t> contentIndexGeneration_{0};
    dispatch_semaphore_t contentIndexingSemaphore_;

    // ── Rescan debounce state ──
    std::mutex pendingRescanMutex_;
    std::set<std::string> pendingRescanPaths_;
    dispatch_source_t rescanDebounceTimer_ = nullptr;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> lastRescanTime_;

    // ── Volume mount debounce state ──
    std::mutex pendingMountMutex_;
    std::set<std::string> pendingMountPaths_;
    dispatch_source_t mountDebounceTimer_ = nullptr;
    std::mutex volumeWatchersMutex_;

    // ── Constants ──
    static constexpr double kRescanDebounceDelaySec = 5.0;
    static constexpr double kRescanThrottleIntervalSec = 300.0;
    /// Default mount debounce if ServiceConfig doesn't override it.
    /// Kept here for the in-process startup paths that don't go through
    /// ServiceConfig (tests, fallbacks).
    static constexpr double kMountDebounceDefaultSec = 5.0;
    static constexpr const char* kAppVersion = "1.1.0";
};
