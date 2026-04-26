# 156 - AI Search Routing: NL Translate + Vector Search

## Summary

Rerouted AI search mode to use two distinct paths for easier independent testing:

1. **AI default (NL translate)**: Natural language query is translated to structured MacEverything syntax via `NLTranslator` (LiteLLM/qwen2.5:3b), then results are fetched via the standard file name/directory search engine (`performSearch()`).

2. **AI + `infile:` prefix (vector search)**: Query after `infile:` is used for embedding-based semantic search via `performSemanticSearch()`, returning cosine similarity ranked results.

## Changes

### SearchViewModel.swift
- Added state variables: `isAITranslating`, `translatedQuery`, `isVectorSearch`
- New method `performAITranslatedSearch()`: calls `bridge.translateQuery()` on a background thread, feeds the translated result to `performSearch()`
- Updated `onSearchTextChanged()` routing:
  - `isAISearch && infile:` → vector search (`performSemanticSearch`)
  - `isAISearch` (no prefix) → NL translate → file search
  - Normal mode unchanged
- Updated `onSearchOptionsChanged()` and `performIndexRefresh()` to match new routing
- Updated `toggleAISearch()` to reset new state vars

### ContentView.swift
- AI vector search (`isVectorSearch`) shows semantic result rows with similarity badges
- AI NL translate shows translating spinner during translation phase
- Status bar shows `AI·NL` or `AI·Vec` to indicate active AI sub-mode
- Translated query displayed in status bar when available (e.g., `→ ext:swift dm:today`)

## Rationale

AI search is in early stages. Keeping file name search (via NL translation) and vector search separate allows independent testing and iteration of each path. Once both paths are mature, they can be unified into a single entry point.

## Verification

- Build: `xcodebuild` succeeded on master
- Tests: 31/31 C++ tests passed
- DMG: packaged successfully
- HTTP: `/api/search?q=test` returns results confirming app runs correctly
