# 186 - Fix App Display Name Search

## Summary

Fixed app bundle search so Finder display names such as `Focus To-Do` are searchable while results still preserve and return the filesystem bundle name such as `WebPomodoro.app`.

## Problem

macOS app bundles can have a filesystem name that differs from the Finder-facing display name. MacEverything indexed only the canonical filesystem name in `namePool_`, so `WebPomodoro.app` could be found by `WebPomodoro`, but not by the display name from `CFBundleDisplayName` / `CFBundleName`.

## Root Cause

`namePool_` served two different roles: canonical filename storage for paths, extensions, persistence, and returned records; and searchable filename text for query indices and verification. Adding display aliases directly to `namePool_` would corrupt canonical path construction and result names, so the missing abstraction was a separate search-only name pool.

## Fix

- Added `searchableNamePool_` as a search-only lowercase name pool containing the canonical filename plus delimiter-separated app display-name aliases.
- Added `originalSearchableNamePool_` so `case:` queries can match original-case display aliases without changing canonical filenames.
- Read `.app` display metadata through CoreFoundation bundle APIs, preferring `CFBundleDisplayName` and falling back to `CFBundleName`, while ignoring non-string plist values safely.
- Switched filename trigram indexing, advanced-query verification, structured-query final-name verification, and short-query cache membership to use search-only aliases.
- Inserted updated and WAL-replayed app aliases into already-built `ShortQueryCache` instances so incremental mutations match full rebuild behavior.
- Scored each alias independently in `ShortQueryCache`, advanced-query ranking, and structured-query ranking so a display-name exact/prefix match is not penalized as a late substring after the filesystem bundle name.
- Evaluated glob, regex, whole-word, and whole-filename checks per alias so synthetic alias boundaries do not create false phrase matches.
- Kept `namePool_` as the canonical filename source for paths, extensions, persistence, and returned record names.
- Stopped loading or saving `IndexPersistence` `.sqcache` sidecar files so display-name aliases are rebuilt from current bundle metadata instead of reusing stale caches after `Info.plist` changes.
- Bumped `ShortQueryCache` serialization version for standalone cache round-trips.

## Testing

- Added regression coverage in `tests/test_path_search.h` for:
  - filesystem bundle-name search still returning `WebPomodoro.app`
  - display-name phrase search for `Focus To-Do`
  - short-query cache (`fo`)
  - glob, prefix glob, regex, case-sensitive, whole-word, whole-filename, and structured path query behavior through display-name aliases
  - quoted phrases do not match across canonical-name/display-name alias boundaries
  - malformed `Info.plist` values do not crash indexing or create bogus aliases
- Verified focused rebuild and regressions from the intended worktree: `make -C . -B test_all LLAMA_DIR=/Users/wujian/data/project/mac_everything/vendor/llama.cpp && ./test_all --part 87 && ./test_all --part 42`.
  - Part 87: `updateByPath` inserts replacement display aliases into `ShortQueryCache`; display-name prefix ranking outranks canonical substrings; 51 passed / 0 failed.
  - Part 42: WAL replay inserts display aliases into `ShortQueryCache`; 31 passed / 0 failed.
- Verified review-followup regressions from the intended worktree: `make -C . -B test_all LLAMA_DIR=/Users/wujian/data/project/mac_everything/vendor/llama.cpp && ./test_all --part 30 --part 52`.
  - Part 30: `IndexPersistence` no longer writes stale-prone `.sqcache` sidecars; 13 passed / 0 failed.
  - Part 52: app display-name matching and structured display-alias ranking; 39 passed / 0 failed.
- Re-ran the focused display-name/cache/WAL/persistence gate after changelog updates: `./test_all --part 30 --part 42 --part 52 --part 87`; 134 passed / 0 failed.
- Fixed the ranking test fixture to avoid reading real `/Applications/Alfred 5.app` bundle metadata on the host machine, then rebuilt and ran `./test_all --part 3e`; 17 passed / 0 failed.
- Re-ran the exact pre-commit fast gate: `arch -arm64 make test-fast`; 12099 passed / 0 failed.
- Independent `code-reviewer` pass returned `[]` with no blocking findings for correctness, persistence/cache lifecycle, ranking regressions, path/name corruption, or tests.
- Verified broader targeted search/mutation/persistence suites from the intended worktree: parts 3, 8, 30, 45, 52, 58, 59, 60, 53, 71, 87, and 42 all ended with `Tests failed: 0`.
- Part 52 specifically covers filesystem bundle-name search, Finder display-name search, short-query cache, glob, prefix glob, regex, `case:`, `ww:`, `wfn:`, structured path queries, alias-boundary phrase rejection, and malformed plist handling.
- Full suite was attempted earlier; FSEvents-dependent parts 4 and 6 failed because the watcher received 0 events in this environment, while unrelated later search/persistence suites passed.
