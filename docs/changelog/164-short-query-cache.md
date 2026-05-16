# 164 - Short Query Cache for 1-2 Character Queries

## Problem

1-2 character ASCII queries (e.g. "a", "ab") bypassed the trigram index (which requires >= 3 bytes) and did a full linear scan of ~6.4M records, taking 300-350ms while holding the shared lock. This violated the "fast & precise" principle.

## Solution

Pre-compute and cache the top 100 results for all 26 unigram + 676 bigram = **702 possible short queries**. Cached results are served in O(1), avoiding the expensive linear scan entirely.

### Performance Results

| Query | Before | After | Speedup |
|-------|--------|-------|---------|
| `a` | 303ms | 2.4ms | 126x |
| `ab` | 349ms | 0.23ms | 1517x |

### Design

- **ShortQueryCache class**: flat array of 702 entries, O(1) lookup by key index
- **Single-pass build**: scans all records once, maintains per-key bounded max-heap (top 100 by score)
- **Score computation**: identical to queryAdvanced() — exact/prefix/word-boundary/substring + path length
- **Invalidation**: `tombstoneAt()` calls `markDeleted()` to increment per-entry deletion counter
- **Rebuild threshold**: when >50% of a cache entry's results are tombstoned, `needsRebuild()` triggers
- **Persistence**: binary `.sqcache` file saved/loaded alongside V6 index
- **Memory overhead**: ~280 KB (702 entries × 100 indices × 4 bytes)

### Cache lifecycle

1. Built after Phase 2 trigram index completion
2. Invalidated incrementally on record deletion
3. Persisted on index flush, loaded on startup
4. Rebuilt when stale (>50% deletions)

## Files Changed

- `MacEverything/Core/ShortQueryCache.h` — **NEW**: class declaration
- `MacEverything/Core/ShortQueryCache.cpp` — **NEW**: implementation (rebuild, lookup, markDeleted, persistence)
- `MacEverything/Core/SearchEngine.h` — added `shortQueryCache_` member and `buildShortQueryCache()` method
- `MacEverything/Core/SearchEngineQuery.cpp` — cache lookup fast path before `queryAdvanced()`
- `MacEverything/Core/SearchEngine.cpp` — `markDeleted()` call in `tombstoneAt()`
- `MacEverything/Core/SearchEngineV6.cpp` — trigger cache build after Phase 2
- `MacEverything/Core/IndexPersistence.cpp` — load/save cache alongside V6 index
- `MacEverything.xcodeproj/project.pbxproj` — added new files to Xcode project
- `tests/test_short_query_cache.h` — **NEW**: 27 unit tests
- `test_all.cpp` — registered test suite

## Testing

- 12073 tests pass (27 new ShortQueryCache tests)
- HTTP API verified: `searchPath: "short-query-cache"` for 1-2 char queries
- 3+ char queries correctly bypass cache and use trigram path
