# 159 - Fix: Phase 2 Skip Leaves Partial Trigram Index Corrupting Search

## Summary

Fixed a critical search regression where queries like "test" returned only 4-5 results instead of thousands. The trigram index was partially populated, causing the search engine to use it (finding almost nothing) instead of falling back to linear scan.

## Root Cause

When `completePhase2()` was skipped due to insufficient memory (commit `2569d33`), it set `phase2Pending_ = false`. This told `addRecord()` that the trigram index was active, so new records from FSEvents were incrementally added to an otherwise empty index. Queries then used this tiny partial index (5 candidates) instead of falling back to the correct linear scan path.

## Changes

### SearchEngineV6.cpp
- **Keep `phase2Pending_=true` on memory skip**: When Phase 2 is skipped due to insufficient memory, no longer set `phase2Pending_` to false. This ensures `addRecord()` continues to skip trigram insertion, keeping the index empty so queries fall back to linear scan.
- **Reduce memory estimate from 800B to 200B/record**: The 800B estimate was overly conservative (4× actual measured peak), causing Phase 2 to be unnecessarily skipped on machines with moderate available memory. With 200B/record, a 5M file index needs ~1GB — well within typical free memory.

## Verification

- Build: `xcodebuild` succeeded on master
- Tests: All C++ tests passed (pre-commit hook)
- DMG: Packaged successfully
- HTTP: `test` query now returns 100 results with 115,776 trigram candidates (was 5)
