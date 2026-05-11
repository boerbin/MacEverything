# 162 - Multi-Term Scoring with Name Hit-Rate and Word-Boundary Alignment

## Problem

When searching multi-word queries like "activity app", files matching ALL query terms in the filename (e.g. "Activity Monitor.app") were not prioritized over files matching only one term (e.g. "activity" folder). The old scoring system only used the first query term for priority computation, ignoring the rest.

## Root Cause

`extractScoringTerm()` returned only the first SUBSTRING TERM from the AST. For "activity app" (AND("activity", "app")), only "activity" was used for scoring. Both "Activity Monitor.app" and "activity" folder got the same priority (2 = name contains), so they tied and sorted by path length.

## Solution

Replaced the coarse 4-level `uint8_t priority` with a composite `uint32_t score` encoding three signals:

- **Bits 16-23: Name miss count** — how many query terms are NOT found in the filename. 0 = all terms match (best). This is the strongest signal.
- **Bits 8-15: Match quality sum** — per-term quality scores summed: 0=exact, 1=prefix, 2=word-boundary, 3=substring. Lower is better.
- **Bits 0-7: Path length** — `min(fullPathLen, 255)` as tiebreaker. Shorter paths first.

Word-boundary detection reuses existing `isWordBoundaryChar()` to give bonus to matches at word starts (e.g. "Activity" matching at '.' boundary vs "interactivity" matching mid-word).

### Example: "activity app"

| Result | Miss Count | Quality | Ranking |
|--------|-----------|---------|---------|
| Activity Monitor.app | 0 (both terms in name) | 4 | 1st |
| activity (folder) | 1 ("app" not in name) | 2 | lower |

## Files Changed

- `MacEverything/Core/SearchEngine.h` — Changed `Match` struct from `{idx, priority, pathLen}` to `{idx, score}`, added `encodeScore()` helper
- `MacEverything/Core/SearchEngineAdvancedQuery.cpp` — Replaced `extractScoringTerm()` with `extractScoringTerms()`, added `computeTermQuality()` and `computeMultiTermScore()`, updated all 3 scoring code paths and sort comparator
- `MacEverything/Core/SearchEngineQuery.cpp` — Updated Match construction and sort comparator
- `MacEverything/Core/SearchEngineStructuredQuery.cpp` — Updated 4 Match construction sites
- `tests/test_ranking.h` — Updated test expectations for multi-term ranking

## Testing

- All 12046 tests pass
- HTTP API verification: `curl 'http://localhost:19860/api/search?q=activity+app&limit=5'` returns "Activity Monitor.app" as first result
