# 183 - Fix Search Input Lag (P0-P3)

## Problem

Users reported keystroke stuttering in the search field — keys pressed but not displayed, then appearing after a brief pause. Investigation identified the root cause as a **main-thread dispatch storm**: multiple `@Published` property updates from search results and FSEvents-driven `onIndexChanged` fire simultaneously on the main thread, each triggering separate SwiftUI view invalidation cycles.

No single code path exceeded the 16ms frame budget individually — the issue was the aggregate effect of concurrent notifications.

## Root Cause Analysis

1. **`highlightHints` computed property** (P0): Evaluated once per `ResultRow` in `ForEach` — ~100 calls per render cycle, each invoking `bridge.parseHighlightHints()` with identical input.
2. **Per-property `@Published` notifications** (P1): 8 high-frequency properties each fired a separate `objectWillChange` notification. A single `performSearch` result delivery triggered 3+ SwiftUI view invalidation cycles instead of 1.
3. **FSEvents competing with typing** (P2): `onIndexChanged()` triggered `performIndexRefresh()` which re-ran the search query during active typing, competing with the user's debounced search.
4. **Synchronous syntax highlighting** (P3): `applyHighlighting()` ran synchronously in `textDidChange()`, blocking the run loop iteration and preventing the typed character from rendering.

## Fixes Implemented

### P0: Cache `highlightHints` (SearchViewModel.swift)
- Converted computed property to stored property (`private(set) var highlightHints`)
- Added `updateHighlightHints()` method called from `onSearchTextChanged()` and `onSearchOptionsChanged()`
- Eliminates ~100x redundant `parseHighlightHints()` calls per render cycle

### P1: Batch `@Published` Updates (SearchViewModel.swift)
- Removed `@Published` from 8 properties: `displayItems`, `totalMatches`, `queryTimeMs`, `totalRecords`, `isMonitoring`, `isSyncing`, `isBuildingIndex`, `contentIndexedCount`
- Added single `objectWillChange.send()` call at each of 12 batch assignment sites
- Coalesces multiple per-property SwiftUI invalidation cycles into one per batch

### P2: Typing Guard for `onIndexChanged` (SearchViewModel.swift, IndexRefreshThrottle.swift)
- Added `lastKeystrokeTime` tracking in `onSearchTextChanged()`
- Added guard in `onIndexChanged()`: if a keystroke occurred within the last 500ms, defers refresh via `refreshThrottle.markPending()` instead of executing immediately
- Added `markPending()` method to `IndexRefreshThrottle`
- Pending refreshes fire on next cooldown expiry or focus regain

### P3: Defer `applyHighlighting` (HighlightedSearchField.swift)
- Replaced synchronous `applyHighlighting()` call in `textDidChange()` with `DispatchWorkItem` dispatched to next run loop iteration
- Typed character renders immediately; syntax highlighting applies on the next iteration
- `updateNSView` path remains synchronous for programmatic text changes

## Files Changed

| File | Changes |
|------|---------|
| `MacEverything/App/SearchViewModel.swift` | P0, P1, P2 |
| `MacEverything/App/HighlightedSearchField.swift` | P3 |
| `MacEverything/App/IndexRefreshThrottle.swift` | P2 (markPending) |
| `tests/test_index_refresh_throttle.swift` | P2 (testMarkPending) |

## Verification

- All 12068 C++ tests pass (`make test`)
- IndexRefreshThrottle unit tests pass (49/49 including new `testMarkPending`)
- App builds successfully (`make app` — BUILD SUCCEEDED)
