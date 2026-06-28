#include "VolumeIndex.h"
#include <algorithm>

uint32_t VolumeIndex::addVolume(const std::string& mountPath) {
    std::unique_lock lock(mutex_);
    return internLocked(mountPath);
}

void VolumeIndex::removeVolume(const std::string& mountPath) {
    std::unique_lock lock(mutex_);
    auto it = pathToIdx_.find(mountPath);
    if (it == pathToIdx_.end()) return;
    offlineIdx_.erase(it->second);
    pathToIdx_.erase(it);
    // Intentionally do NOT shrink volumePool_ — indices stay stable.
}

void VolumeIndex::markOnline(const std::string& mountPath) {
    std::unique_lock lock(mutex_);
    auto it = pathToIdx_.find(mountPath);
    if (it != pathToIdx_.end()) offlineIdx_.erase(it->second);
}

void VolumeIndex::markOffline(const std::string& mountPath) {
    std::unique_lock lock(mutex_);
    auto it = pathToIdx_.find(mountPath);
    if (it == pathToIdx_.end()) return;
    offlineIdx_.insert(it->second);
}

void VolumeIndex::markOnlineAll() {
    std::unique_lock lock(mutex_);
    offlineIdx_.clear();
}

bool VolumeIndex::hasOfflineVolumes() const {
    std::shared_lock lock(mutex_);
    return !offlineIdx_.empty();
}

std::vector<std::string> VolumeIndex::offlineVolumePaths() const {
    std::shared_lock lock(mutex_);
    std::vector<std::string> result;
    result.reserve(offlineIdx_.size());
    for (uint32_t idx : offlineIdx_) {
        result.push_back(volumePool_.str(idx));
    }
    return result;
}

void VolumeIndex::loadOfflineVolumes(const std::vector<std::string>& paths) {
    std::unique_lock lock(mutex_);
    offlineIdx_.clear();
    for (const auto& p : paths) {
        auto it = pathToIdx_.find(p);
        if (it != pathToIdx_.end()) {
            offlineIdx_.insert(it->second);
        } else {
            // Persisted offline volume that hasn't been re-mounted yet —
            // register it so the offline state survives restarts.
            uint32_t idx = internLocked(p);
            offlineIdx_.insert(idx);
        }
    }
}

std::vector<std::string> VolumeIndex::allVolumePaths() const {
    std::shared_lock lock(mutex_);
    std::vector<std::string> result;
    result.reserve(pathToIdx_.size());
    for (const auto& [path, _] : pathToIdx_) {
        result.push_back(path);
    }
    return result;
}

size_t VolumeIndex::volumeCount() const {
    std::shared_lock lock(mutex_);
    return pathToIdx_.size();
}

uint32_t VolumeIndex::internLocked(const std::string& mountPath) {
    auto it = pathToIdx_.find(mountPath);
    if (it != pathToIdx_.end()) return it->second;
    uint32_t idx = volumePool_.append(mountPath);
    pathToIdx_[mountPath] = idx;
    return idx;
}

bool VolumeIndex::isPathSubsumedBy(std::string_view child, std::string_view parent) {
    if (child == parent) return true;
    if (parent == "/") return true;
    return child.size() > parent.size()
        && child.compare(0, parent.size(), parent) == 0
        && child[parent.size()] == '/';
}

std::string VolumeIndex::volumeForPath(const std::string& fullPath) const {
    std::shared_lock lock(mutex_);
    // Find longest matching volume root.
    const std::string* best = nullptr;
    size_t bestLen = 0;
    for (const auto& [path, _] : pathToIdx_) {
        if (isPathSubsumedBy(fullPath, path) && path.size() > bestLen) {
            best = &path;
            bestLen = path.size();
        }
    }
    if (!best) return {};
    return *best;
}

bool VolumeIndex::isRecordOffline(const std::string& fullPath) const {
    std::shared_lock lock(mutex_);
    // Step 1: find longest matching volume root (regardless of online state).
    uint32_t bestIdx = UINT32_MAX;
    size_t bestLen = 0;
    for (const auto& [path, idx] : pathToIdx_) {
        if (isPathSubsumedBy(fullPath, path) && path.size() > bestLen) {
            bestIdx = idx;
            bestLen = path.size();
        }
    }
    if (bestIdx == UINT32_MAX) return false; // root volume is always online
    return offlineIdx_.count(bestIdx) > 0;
}

bool VolumeIndex::isVolumeOffline(const std::string& mountPath) const {
    std::shared_lock lock(mutex_);
    auto it = pathToIdx_.find(mountPath);
    if (it == pathToIdx_.end()) return false;
    return offlineIdx_.count(it->second) > 0;
}
