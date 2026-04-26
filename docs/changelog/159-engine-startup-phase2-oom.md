# 159 - Engine Startup Architecture Fix & Phase 2 OOM Guard

## Problem

Two related issues caused the app to become unrecoverable after a crash:

### A. Engine startup depends on SwiftUI window state

The engine only started when `SearchViewModel.init()` was called, which requires SwiftUI to render `ContentView`. After a crash cascade, macOS suppresses the window, so:

- `ContentView` never renders
- `SearchViewModel` never initializes
- Engine never starts
- HTTP server never listens
- App appears running but does nothing

### B. Phase 2 trigram build OOM crash

The Phase 2 memory estimate was 200 bytes/record, but actual usage is ~800 bytes/record. With 4.9M records:

- Estimated: 4.9M * 200 = ~930MB
- Actual: 4.9M * 800 = ~3.7GB

The too-low estimate passed the memory check, Phase 2 started, and the process SIGABRT'd from OOM during `buildTrigramIndexFromData`.

## Root Cause Analysis

**Engine startup**: Architectural flaw — engine lifecycle was coupled to UI rendering instead of the application lifecycle. `startIncremental()` should be an `AppDelegate` responsibility, not a view model's.

**Phase 2 OOM**: The memory estimate at `SearchEngineV6.cpp:125` was `recordCount * 200` but the actual Phase 2 cost includes:
- Snapshot copies of types, modTimes, namePool, lowerPathPool, pathIndices (~100B/record)
- Trigram index (unordered_map<uint32_t, vector<uint32_t>>) (~200B/record)
- Path trigram index (~300B/record)
- pathIdx-to-records map + misc (~200B/record)

Total: ~800 bytes/record. The check threshold was also too permissive (50% of available).

## Fix

### Engine startup (5 files)

1. **`MacSearchBridge_Internal.h`**: Added `_engineStarted`, `_startupFinished`, `_startupFinishedCount`, `_startupDidFullScan`, `_startupCompletion` ivars

2. **`MacSearchBridge.h`**: Added `startEngine` and `resetEngine` methods

3. **`MacSearchBridge.mm`**:
   - `startEngine`: Idempotent method that starts the C++ engine. First call wins; subsequent calls are no-ops
   - `resetEngine`: Resets the guard flags (used by `rebuildIndex`)
   - `startIncrementalFrom:`: Guarded — if engine already started, installs UI callbacks and defers/fires completion without re-calling C++

4. **`AppDelegate.swift`**: Calls `MacSearchBridge.shared().startEngine()` right after logger init in `applicationDidFinishLaunching`

5. **`SearchViewModel.swift`**: `rebuildIndex()` calls `bridge.resetEngine()` before `startIncremental()` to allow a fresh scan

### Phase 2 OOM guard (1 file)

- **`SearchEngineV6.cpp`**: Changed memory estimate from `recordCount * 200` to `recordCount * 800`, and tightened threshold from `availableBytes / 2` to `availableBytes * 7 / 10`

## Verification

- Build succeeded on master
- App launches, engine starts from AppDelegate (logged: "startEngine: launching engine from AppDelegate")
- Incremental startup completes in 3.39s with 4.89M records
- Phase 2 correctly skipped: "need ~3737MB but only ~2686MB available"
- HTTP service responds with 200, search returns results in 2.25ms
- App remains stable (no crash)

## Files Changed

| File | Change |
|------|--------|
| `MacEverything/Bridge/MacSearchBridge_Internal.h` | Added engine state ivars |
| `MacEverything/Bridge/MacSearchBridge.h` | Added `startEngine`/`resetEngine` API |
| `MacEverything/Bridge/MacSearchBridge.mm` | Implemented idempotent startup + guard |
| `MacEverything/App/AppDelegate.swift` | Call `startEngine()` at launch |
| `MacEverything/App/SearchViewModel.swift` | Call `resetEngine()` before rebuild |
| `MacEverything/Core/SearchEngineV6.cpp` | Fixed memory estimate and threshold |
