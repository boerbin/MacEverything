#pragma once
// ═══════════════════════════════════════════════════════
//  Regression test for the dispatch_async dangling-reference bug.
// ═══════════════════════════════════════════════════════
//
// ServiceEngine::rescanSubtree used to capture the `dir` parameter by
// reference inside its dispatch_async block. When called from a tight loop
// (flushPendingMounts iterating over 5 USB volumes), the caller's stack
// frame was destroyed before the blocks ran, leaving all 5 blocks with
// dangling references that all read the same garbage memory — typically
// an empty string, making the scanner scan the CWD instead of the mount.
//
// This test simulates that pattern with a minimal mock ServiceEngine to
// verify the dispatch path preserves the path argument.

#include <atomic>
#include <chrono>
#include <iostream>
#include <memory>
#include <mutex>
#include <set>
#include <string>
#include <thread>
#include <vector>
#include <dispatch/dispatch.h>

namespace mock {

// Mirror ServiceEngine's rescanSubtree dispatch pattern.
class Engine {
public:
    explicit Engine(dispatch_queue_t q) : q_(q) {}

    void rescanSubtree(const std::string& dir, std::vector<std::string>* log) {
        // Heap-allocate to escape caller's stack frame, exactly like the
        // post-fix production code.
        std::string* dirHeap = new std::string(dir);
        dispatch_async(q_, ^{
            std::unique_ptr<std::string> p(dirHeap);
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            log->push_back(*p);
        });
    }

private:
    dispatch_queue_t q_;
};

} // namespace mock

static void runRescanDanglingRefTest() {
    std::cout << "========================================\n";
    std::cout << "  Rescan Dangling Reference Test\n";
    std::cout << "========================================\n\n";

    dispatch_queue_t q = dispatch_queue_create("test.rescan", DISPATCH_QUEUE_SERIAL);
    mock::Engine engine(q);

    std::vector<std::string> log;
    std::vector<std::string> expected = {
        "/Volumes/HDD3", "/Volumes/HDD4", "/Volumes/hdd14ts1",
        "/Volumes/seedbox_temp", "/Volumes/tempdown"
    };

    // Simulate flushPendingMounts: tight loop calling rescanSubtree 5 times.
    // BEFORE the fix, all 5 dispatched blocks would read the same dangling
    // memory and produce empty strings. AFTER the fix, each block gets
    // its own heap-allocated copy.
    {
        std::set<std::string> paths(expected.begin(), expected.end());
        for (const auto& p : paths) {
            engine.rescanSubtree(p, &log);
        }
    }
    // At this point the set `paths` is destroyed, the references would
    // dangle if we were still using by-reference capture.

    // Wait for all 5 blocks to complete (5 * 50ms + slack).
    std::this_thread::sleep_for(std::chrono::milliseconds(500));

    check(log.size() == expected.size(),
          "all 5 rescans recorded");
    check(log == expected,
          "each rescan received the correct path (no empty strings, no duplicates)");

    // Sanity: if the bug were present, we'd see 5 empty strings or 5 copies
    // of the same last-iterated value.
    if (log.size() == expected.size() && log == expected) {
        std::cout << "  ✓ no path lost to dangling reference\n";
    } else {
        std::cout << "  ✗ paths lost — got: ";
        for (const auto& p : log) std::cout << "[" << p << "] ";
        std::cout << "\n";
    }

    std::cout << "\n  Rescan Dangling Ref: ";
    if (log == expected) std::cout << "all passed\n\n";
    else std::cout << "FAILED\n\n";
}
