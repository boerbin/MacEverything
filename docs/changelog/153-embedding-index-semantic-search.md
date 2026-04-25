# #153 — Embedding Index + Semantic Search (Phase 2)

## Summary

Added semantic search as a core MacEverything capability. Files are indexed via embedding vectors (computed through LiteLLM -> Ollama), enabling search by meaning rather than keywords. Search results now include "Semantic Matches" alongside traditional results.

## Architecture

Single-process C++ AI Layer on top of existing Core:

```
MacEverything (:19860)
  +-- AI Layer (new C++) -----------------------+
  |  LiteLLMClient    -> HTTP to LiteLLM :19861 |
  |  NLTranslator     -> NL query translation   |
  |  EmbeddingIndex   -> SQLite vector storage   |
  |  VectorSearch     -> Brute-force cosine sim  |
  |  ServiceEngine+Semantic -> FSEvents indexing |
  +---------------------------------------------+
  +-- Core (minimal changes) -------------------+
  |  onFileChanged callback (~10 lines added)   |
  +---------------------------------------------+

LiteLLM Proxy (:19861) -> Ollama (:11434)
  Model gateway: routes "translate"->qwen2.5:3b, "embed"->bge-m3
```

Key design decisions:

- AI Layer calls LiteLLM for inference, never loads models itself -- thin HTTP client
- LiteLLM as model gateway: switch local/remote models via config.yaml
- EmbeddingIndex (SQLite) and VectorSearch (brute-force) are separate from ContentIndex (trigram)
- onFileChanged callback enables Core -> AI Layer notification without circular dependency
- All AI features gracefully degrade when LiteLLM is unavailable

## New C++ Components

| File | Role |
|------|------|
| `LiteLLMClient.h/.cpp` | OpenAI-compatible HTTP client for LiteLLM |
| `EmbeddingIndex.h/.cpp` | SQLite BLOB storage for embedding vectors |
| `VectorSearch.h/.cpp` | Brute-force cosine similarity search |
| `NLTranslator.h/.cpp` | NL -> query syntax translation (C++ rewrite) |
| `ServiceEngine+Semantic.cpp` | Background indexing lifecycle |

## New HTTP Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/search/semantic` | GET | Vector similarity search |
| `/api/search/similar` | GET | Find similar files by path |
| `/api/ai/translate` | POST | NL -> query translation |
| `/api/ai/status` | GET | LiteLLM status + index stats |
| `/api/semantic/config` | GET/POST | Embedding index configuration |
| `/api/semantic/rebuild` | POST | Trigger full rebuild |

## Swift UI Changes

- SemanticSettingsView: extension list, file size slider, LiteLLM status, rebuild
- SearchViewModel: parallel semantic search alongside traditional
- ContentView: "Semantic Matches" section with similarity badges
- Menu items: "Semantic Settings..." in status bar and app menu

## Third-Party Dependencies (vendored)

- hnswlib (header-only) -- vector search, for future >200K scale optimization
- cpp-httplib (header-only) -- HTTP client for LiteLLM calls
- sqlite3 (system) -- embedding vector storage

## LiteLLM Model Gateway

- Config at `litellm/config.yaml`
- Routes: translate -> qwen2.5:3b, embed -> bge-m3, summarize -> qwen2.5:3b
- Supports adding remote providers (Claude, OpenAI) as fallback
- MacEverything calls OpenAI-format API, unaware of backend

## Performance (M3 Pro, 18GB, dim=1024 bge-m3)

- EmbeddingIndex write: ~4,753 files/s
- EmbeddingIndex read: ~142,396 reads/s
- VectorSearch add: ~583,258 adds/s
- Semantic search (in-process): brute-force ~20ms at 50K scale
- Incremental update: ~17ms embedding + <1ms storage

## Tests

- LiteLLMClient: 10 unit tests (part 78)
- EmbeddingIndex: 10 unit tests (part 79)
- VectorSearch: 24 unit tests (part 80)
- NLTranslator: 11 unit tests (part 81)
- Performance benchmarks: 5 tests (part 82)
- Python E2E benchmark: benchmarks/bench_semantic.py

## How to Use

1. Install Ollama: `brew install ollama && ollama pull qwen2.5:3b && ollama pull bge-m3`
2. Install LiteLLM: `pip install litellm`
3. Start LiteLLM: `cd litellm && litellm --config config.yaml --port 19861`
4. Open MacEverything -> Semantic Settings -> configure extensions -> Apply
5. Background indexing starts automatically
6. Search normally -- semantic results appear below traditional results
