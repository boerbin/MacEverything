#pragma once
#include <string>
#include <functional>
#include <atomic>

namespace objc_object_forward {
    // Forward declarations to keep this header pure C++.
    // The .mm file pulls in AppKit/Foundation and uses these types.
    struct NSWorkspaceOpaque;
    struct NSObjectOpaque;
}

/// Observes macOS volume mount/unmount events via NSWorkspace notifications.
/// Exposes a C++ callback API usable from any thread.
///
/// Filters:
///   - Only emits events whose mount path is under "/Volumes/".
///   - Strips trailing slashes for normalization.
class VolumeWatcher {
public:
    using MountCallback   = std::function<void(std::string mountPath)>;
    using UnmountCallback = std::function<void(std::string mountPath)>;

    VolumeWatcher();
    ~VolumeWatcher();

    VolumeWatcher(const VolumeWatcher&) = delete;
    VolumeWatcher& operator=(const VolumeWatcher&) = delete;

    /// Start observing. Safe to call multiple times — second call is no-op
    /// until stop() is invoked.
    void start(MountCallback onMount, UnmountCallback onUnmount);

    /// Stop observing and release observer tokens.
    void stop();

    bool isRunning() const { return running_.load(std::memory_order_acquire); }

    /// Snapshot the current set of mounted local volumes.
    /// Used at startup to reconcile persisted offline state.
    /// Returns canonical mount paths (e.g. "/Volumes/USBDRIVE").
    static std::vector<std::string> currentlyMountedLocalVolumes();

    /// Test helper — synthesize a mount event. Only intended for unit tests
    /// where we can't easily trigger real NSWorkspace events.
    void injectMountForTest(const std::string& mountPath);
    void injectUnmountForTest(const std::string& mountPath);

private:
    std::atomic<bool> running_{false};
    MountCallback onMount_;
    UnmountCallback onUnmount_;

    // Held as void* to keep this header free of Objective-C types.
    // The .mm file knows the actual types (id).
    void* mountObserver_ = nullptr;
    void* unmountObserver_ = nullptr;
    void* preUnmountObserver_ = nullptr;
};
