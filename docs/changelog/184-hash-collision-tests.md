# 184 — Hash Collision & Correctness Test Suite

## Background

After replacing `pathIndex_`, `pathLookup_`, and `lowerPathLookup_` from `unordered_map<string, uint32_t>` to `unordered_map<uint64_t, uint32_t>` (FNV-1a hash keys) in changelog 183, all mutation paths use hash-only lookups with no string verification. This creates a silent data corruption risk on hash collision. We needed tests to document and verify this behavior.

## Plan

Add 4 categories of tests covering:
1. **Hash collision handling (pathIndex_)** — Inject colliding hash entries via `friend class HashCollisionTestHelper`, verify that collisions cause dangling references and tombstoned wrong records.
2. **Cross-operation mutation sequences** — Black-box tests for add/remove/update cycles, interleaved operations, mass add/remove (1000 records), and duplicate-path last-wins semantics.
3. **pathLookup_ collision cascading corruption** — Inject colliding pathLookup_ entries, verify that multiple records get the wrong directory path (cascading corruption).
4. **compactRecords correctness under hash keys** — Verify compaction preserves all live records with correct fields, handles dedup correctly, rebuilds pathLookup_, and supports add-after-compact.

## Implementation

- Added `friend class HashCollisionTestHelper;` to `SearchEngine` class for test seam access to private `pathIndex_` and `pathLookup_`.
- Created 3 new test files:
  - `tests/test_hash_collision.h` (Part 90) — 5 tests, categories 1 & 3
  - `tests/test_mutation_sequences.h` (Part 91) — 6 tests, category 2
  - `tests/test_compact_hash.h` (Part 92) — 5 tests, category 4
- Registered Parts 90, 91, 92 in `test_all.cpp` (includes, --fast set, runner calls).

## Results

- 16 new test functions, 45 new assertions
- Total test count: 12,143 → 12,188
- All tests pass
- Collision tests confirm the documented vulnerability: hash collisions cause dangling pathIndex_ references and cascading path corruption through pathLookup_.

## Files Changed

| File | Change |
|------|--------|
| `MacEverything/Core/SearchEngine.h` | Added `friend class HashCollisionTestHelper;` |
| `tests/test_hash_collision.h` | New — Part 90 |
| `tests/test_mutation_sequences.h` | New — Part 91 |
| `tests/test_compact_hash.h` | New — Part 92 |
| `test_all.cpp` | Registered Parts 90-92 |
