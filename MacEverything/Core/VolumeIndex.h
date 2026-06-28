#pragma once
#include "StringPool.h"
#include <string>
#include <vector>
#include <unordered_set>
#include <unordered_map>
#include <mutex>
#include <shared_mutex>

/// Tracks known volume mount points and their online/offline state.
///
/// Design:
///   - Volume roots are stored in a StringPool for compact, deduped representation.
///   - The "offline" set is just a set of indices into that pool.
///   - Per-record volume membership is DERIVED from a record's full path
///     (longest-prefix match against volume roots). We don't maintain a
///     per-record map, which keeps volume changes cheap (O(1) toggle) and
///     avoids any SoA schema changes inside SearchEngine.
///
/// Thread-safety:
///   - Reads use a shared_lock; writes use unique_lock.
///   - Volume set and offline set mutate independently.
class VolumeIndex {
public:
    VolumeIndex() = default;

    /// Register a volume mount point. Idempotent.
    /// Returns the pool index for the volume.
    uint32_t addVolume(const std::string& mountPath);

    /// Remove a volume entirely (e.g. user unmounts permanently).
    void removeVolume(const std::string& mountPath);

    /// Mark a volume online (records under it become searchable again).
    /// No-op if the volume is not registered.
    void markOnline(const std::string& mountPath);

    /// Mark a volume offline. Records are kept in the index but queries
    /// should filter them out (or UI should dim them).
    /// No-op if the volume is not registered.
    void markOffline(const std::string& mountPath);

    /// Bulk mark online (used on startup reconcile).
    void markOnlineAll();

    /// Returns true if any registered volume is currently offline.
    bool hasOfflineVolumes() const;

    /// Returns the offline volume mount paths (for persistence).
    std::vector<std::string> offlineVolumePaths() const;

    /// Replace offline set from persistence on startup.
    void loadOfflineVolumes(const std::vector<std::string>& paths);

    /// All known volume mount paths (for diagnostics / UI).
    std::vector<std::string> allVolumePaths() const;

    /// Number of registered volumes.
    size_t volumeCount() const;

    /// Lookup a record's volume by its full path (parent + "/" + name).
    /// Returns the mount path if the record lives under a known volume,
    /// or empty string if it is on the root volume.
    /// Uses longest-prefix match.
    std::string volumeForPath(const std::string& fullPath) const;

    /// Lookup the offline state of a record given its full path.
    /// Records on the root volume are always considered online.
    bool isRecordOffline(const std::string& fullPath) const;

    /// Test helpers
    bool isVolumeOffline(const std::string& mountPath) const;

private:
    mutable std::shared_mutex mutex_;
    StringPool volumePool_;
    std::unordered_map<std::string, uint32_t> pathToIdx_;
    std::unordered_set<uint32_t> offlineIdx_;

    uint32_t internLocked(const std::string& mountPath);
    static bool isPathSubsumedBy(std::string_view child, std::string_view parent);
};
