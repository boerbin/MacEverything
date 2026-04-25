# 155 - Index Corruption Error Handling

## Problem

When index files were corrupted (e.g., from disk-full during write), the app crashed with SIGABRT on startup. The crash occurred because:
1. GCD dispatch blocks in ServiceEngine had no try-catch — uncaught C++ exceptions called std::terminate
2. StringPool methods (view, data, length, str) had no bounds checking — corrupted offsets read invalid memory
3. No error propagation from C++ to Swift UI for load failures

## Solution

### Defense in depth:

**StringPool.h** — Added bounds checking to all accessor methods (view, data, length, str). Out-of-bounds indices or offsets now return empty/zero instead of reading invalid memory.

**ServiceEngine.cpp** — Wrapped `startIncremental()` dispatch block in try-catch. On exception: logs error, deletes corrupt cache files, notifies UI via `onLoadError` callback.

**ServiceEngine+Content.cpp** — Wrapped `setupContentPersistence()` in try-catch. On failure, continues with empty content index.

**ServiceEngine.cpp (completePhase2)** — Wrapped Phase 2 trigram build in try-catch. On failure, logs and continues (search works without trigram acceleration).

**Bridge layer** — Added `onLoadError` callback property on MacSearchBridge, wired through to ServiceEngine.

**Swift UI** — SearchViewModel receives load errors and shows an alert dialog with two options:
- "Clear Cache & Retry" — deletes all index files and restarts indexing from scratch
- "Quit" — terminates the app

## Also Fixed

- Replaced `.contentTransition(.symbolEffect(.replace))` (macOS 15+ API) with `.animation()` to prevent crash on macOS 14 deployment target
