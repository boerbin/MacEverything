#import "VolumeWatcher.h"
#import <AppKit/AppKit.h>
#import <Foundation/Foundation.h>
#include <dispatch/dispatch.h>

// ═══════════════════════════════════════════════════════════════
//  VolumeWatcher — bridges NSWorkspaceDidMount/WillUnmount notifications
//  into C++ callbacks.
//
//  Filter: only paths under /Volumes/ are reported (skips root pseudo-volumes,
//  mobile backups, etc.).
//
//  Callbacks are invoked synchronously on NSWorkspace's notification thread.
//  The caller is responsible for dispatching to a serial queue if it needs
//  ordering with other engine operations.
// ═══════════════════════════════════════════════════════════════

namespace {
    static std::string canonicalizeMountPath(NSString *raw) {
        if (!raw) return {};
        std::string s = [raw UTF8String];
        while (s.size() > 1 && s.back() == '/') s.pop_back();
        return s;
    }

    static bool isUnderVolumes(const std::string& path) {
        if (path.empty()) return false;
        static const std::string kPrefix = "/Volumes";
        if (path == kPrefix) return true;
        return path.size() > kPrefix.size()
            && path.compare(0, kPrefix.size(), kPrefix) == 0
            && path[kPrefix.size()] == '/';
    }

    static NSString* extractPathFromNote(NSNotification *note) {
        NSDictionary *info = [note userInfo];
        id urlOrPath = [info objectForKey:NSWorkspaceVolumeURLKey];
        if (!urlOrPath) urlOrPath = [note object];
        if (!urlOrPath) return nil;
        if ([urlOrPath isKindOfClass:[NSURL class]]) {
            return [(NSURL*)urlOrPath path];
        }
        if ([urlOrPath isKindOfClass:[NSString class]]) {
            return (NSString *)urlOrPath;
        }
        return nil;
    }
}

VolumeWatcher::VolumeWatcher() = default;

VolumeWatcher::~VolumeWatcher() {
    stop();
}

void VolumeWatcher::start(MountCallback onMount, UnmountCallback onUnmount) {
    bool expected = false;
    if (!running_.compare_exchange_strong(expected, true, std::memory_order_acq_rel)) {
        return; // already running
    }
    onMount_ = std::move(onMount);
    onUnmount_ = std::move(onUnmount);

    NSNotificationCenter *nc = [NSNotificationCenter defaultCenter];
    __block VolumeWatcher *weakSelf = this;

    auto wrapMount = ^(NSNotification *note) {
        VolumeWatcher *self = weakSelf;
        if (!self || !self->running_.load(std::memory_order_acquire)) return;
        NSString *mp = extractPathFromNote(note);
        std::string path = canonicalizeMountPath(mp);
        if (!isUnderVolumes(path)) return;
        if (self->onMount_) self->onMount_(path);
    };

    auto wrapUnmount = ^(NSNotification *note) {
        VolumeWatcher *self = weakSelf;
        if (!self || !self->running_.load(std::memory_order_acquire)) return;
        NSString *mp = extractPathFromNote(note);
        std::string path = canonicalizeMountPath(mp);
        if (!isUnderVolumes(path)) return;
        if (self->onUnmount_) self->onUnmount_(path);
    };

    id mObs = [nc addObserverForName:NSWorkspaceDidMountNotification
                              object:nil
                               queue:nil
                          usingBlock:wrapMount];
    id uObs = [nc addObserverForName:NSWorkspaceDidUnmountNotification
                              object:nil
                               queue:nil
                          usingBlock:wrapUnmount];
    id pObs = [nc addObserverForName:NSWorkspaceWillUnmountNotification
                              object:nil
                               queue:nil
                          usingBlock:wrapUnmount];

    // addObserverForName: returns a retained object; CFBridgingRetain moves
    // ownership into a CF-style +1 ref, balanced by CFRelease in stop().
    mountObserver_ = (void*)CFBridgingRetain(mObs);
    unmountObserver_ = (void*)CFBridgingRetain(uObs);
    preUnmountObserver_ = (void*)CFBridgingRetain(pObs);
}

void VolumeWatcher::stop() {
    bool expected = true;
    if (!running_.compare_exchange_strong(expected, false, std::memory_order_acq_rel)) {
        return;
    }
    NSNotificationCenter *nc = [NSNotificationCenter defaultCenter];
    if (mountObserver_) {
        [nc removeObserver:(__bridge id)mountObserver_];
        CFRelease(mountObserver_);
        mountObserver_ = nullptr;
    }
    if (unmountObserver_) {
        [nc removeObserver:(__bridge id)unmountObserver_];
        CFRelease(unmountObserver_);
        unmountObserver_ = nullptr;
    }
    if (preUnmountObserver_) {
        [nc removeObserver:(__bridge id)preUnmountObserver_];
        CFRelease(preUnmountObserver_);
        preUnmountObserver_ = nullptr;
    }
    onMount_ = nullptr;
    onUnmount_ = nullptr;
}

std::vector<std::string> VolumeWatcher::currentlyMountedLocalVolumes() {
    std::vector<std::string> result;
    NSFileManager *fm = [NSFileManager defaultManager];
    NSArray<NSURL*> *urls = [fm mountedVolumeURLsIncludingResourceValuesForKeys:nil
                                                                       options:NSVolumeEnumerationSkipHiddenVolumes];
    for (NSURL *u in urls) {
        std::string p = canonicalizeMountPath([u path]);
        if (isUnderVolumes(p)) result.push_back(std::move(p));
    }
    return result;
}

void VolumeWatcher::injectMountForTest(const std::string& mountPath) {
    if (onMount_) onMount_(mountPath);
}

void VolumeWatcher::injectUnmountForTest(const std::string& mountPath) {
    if (onUnmount_) onUnmount_(mountPath);
}
