# 154 - AI Search Toggle

## Summary

Added an AI search toggle to the search bar's magnifying glass icon, enabling users to switch between normal file search and AI-powered semantic search.

## Changes

### UI Changes (ContentView.swift)
- Converted the static magnifying glass icon into a clickable toggle button
- In AI mode: icon changes to "sparkles" with purple color, search bar border turns purple
- Added `SemanticResultRow` view displaying file name, path, and similarity percentage badge
- Added semantic search results section with loading, empty, and results states
- Status bar shows "AI" indicator and vector count when AI mode is active
- Semantic indexing progress displayed in status bar

### ViewModel Changes (SearchViewModel.swift)
- Added `isAISearch` toggle, `semanticResults`, `isSemanticSearching` properties
- Added `toggleAISearch()` method that checks LiteLLM availability before enabling
- If LiteLLM unavailable, shows AI Setup dialog instead of toggling
- Added `performSemanticSearch()` using `bridge.semanticSearch()` for vector similarity queries
- Wired `onSemanticIndexProgress` and `onSemanticIndexComplete` callbacks
- 300ms debounce for AI search (vs 80ms for normal search)

### Xcode Project Fixes
- Registered all Phase 2 AI layer source files that were missing from the build target:
  LiteLLMClient, EmbeddingIndex, VectorSearch, NLTranslator, ServiceEngine+Semantic,
  MacSearchBridge+Semantic, AISettingsView, SemanticSettingsView, AIServiceClient,
  RichTextExtractor
- Added `third_party` and `third_party/hnswlib` to header search paths
- Added `-lsqlite3`, Quartz.framework, and AppKit.framework to linker flags
- Added `AISettingsWindowController` and `SemanticSettingsWindowController` classes

## How It Works

1. User clicks the magnifying glass icon in the search bar
2. If LiteLLM is not running, the AI Setup dialog appears
3. If LiteLLM is available, the icon switches to sparkles (purple)
4. Queries now go through `bridge.semanticSearch()` which embeds the query via LiteLLM and performs cosine similarity against the vector index
5. Results show similarity percentages instead of traditional match counts
6. Clicking the sparkles icon again returns to normal search mode

## Verification

- Build succeeds on master with `xcodebuild`
- Normal search still works as before
- AI toggle correctly checks LiteLLM availability
- `infile:` content search is unaffected by AI mode
