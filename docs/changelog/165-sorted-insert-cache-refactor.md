# 165 - Sorted Insert Cache Refactor with BoundedSortedVec

## Problem

The short query cache from #164 had two issues:
1. New files added after cache build never appeared in cached results (only on rebuild)
2. Deleted files were tracked by a counter but not actually removed from results
3. RecentCache used std::set (48 bytes/node overhead) for only 200 elements

## Solution

### BoundedSortedVec template
Extracted a reusable `BoundedSortedVec<T, Compare>` template — a sorted vector with bounded capacity. Insert uses binary search + eviction of the worst element when full. Both ShortQueryCache and RecentCache now share this container.

### ShortQueryCache refactoring
- `CacheEntry.results` changed from `vector<uint32_t>` to `BoundedSortedVec<ScoredResult>` where `ScoredResult = {score, idx}`
- Cache is always sorted by score — O(1) lookup returns pre-sorted results
- `tryInsert()`: computes score, binary-inserts, evicts worst if full
- `eraseRecord()`: actually removes entries (replaces old `markDeleted` counter approach)
- Wired into `addRecord()` — new files appear in cache immediately
- Wired into `compactRecords()` — full rebuild after index remap

### RecentCache refactoring
- Replaced `std::set<RecentEntry>` with `BoundedSortedVec<RecentEntry>`
- Same interface (insert/erase), 1/6 memory (1.6KB vs 9.6KB), better cache locality
- `addToRecentCache` simplified — BoundedSortedVec handles eviction internally

## Files Changed

- `MacEverything/Core/BoundedSortedVec.h` — **NEW**: generic bounded sorted container template
- `MacEverything/Core/ShortQueryCache.h` — ScoredResult struct, BoundedSortedVec-based CacheEntry
- `MacEverything/Core/ShortQueryCache.cpp` — rewritten with collectHitKeys, insertIntoKey, eraseRecord
- `MacEverything/Core/SearchEngine.h` — RecentCache uses BoundedSortedVec
- `MacEverything/Core/SearchEngine.cpp` — updated RecentCache methods, added tryInsert in addRecord, rebuild in compactRecords
- `MacEverything/Core/SearchEngineQuery.cpp` — iterate ScoredResult in cache fast path
- `tests/test_short_query_cache.h` — 38 tests including BoundedSortedVec, tryInsert, eraseRecord

## Testing

- 12083 tests pass
- HTTP API: `q=a` → 0.67ms, `q=ab` → 0.82ms (both via short-query-cache path)
- New files appear in cache after addRecord (test 6)
- Deleted files removed from cache via tombstoneAt (test 7)
