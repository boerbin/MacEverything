# 170: Fix ShortQueryCache stale indices after COW compaction

## Problem

Searching for single characters (e.g., "a") returned files that didn't contain that character in their filename. For example, querying "a" returned hex-named files like `23be2e2189353b57beb04ebff941d1fdee5dcc` which have no 'a' anywhere in the name. 30 out of 93 cached results were incorrect.

## Root Cause

`compactRecords()` performs COW (copy-on-write) compaction that completely remaps all record indices — old index N becomes new index M. However, it did not rebuild the `ShortQueryCache` after the remap. The cache still held `ScoredResult.idx` values pointing to pre-compaction indices, which now reference completely different records in the compacted data.

The stale cache was also persisted to disk via `saveTo()`, propagating the corruption across app restarts.

## Fix

Added `buildShortQueryCache()` call inside `compactRecords()` (within the unique_lock scope) after the data swap completes. This is consistent with how `batchRescanPrefix()` and `completePhase2()` both rebuild the cache after modifying indexed data.

**Changed file:** `MacEverything/Core/SearchEngine.cpp`  
**One line added:** `buildShortQueryCache();` after compaction gen increment, before the LOG_INFO.

## Testing

- Added Test 8 ("compactRecords rebuilds cache") in `tests/test_short_query_cache.h`:
  - Creates records with/without 'a' in filename
  - Tombstones records and runs compactRecords()
  - Verifies all results from the cache query contain 'a' in name
  - Verifies the cache is used (searchPath == "short-query-cache")
- Registered as Part 87 in `test_all.cpp` (fast + default sets)
- Full test suite: 12,068 tests, 0 failures
- Live verification: query "a" returns 100/100 correct results (was 63/93 before fix)

## Verification

Before fix: `curl localhost:19860/api/search?q=a` → 30/93 results had no 'a' in filename  
After fix: `curl localhost:19860/api/search?q=a` → 0/100 results incorrect, all contain 'a'
