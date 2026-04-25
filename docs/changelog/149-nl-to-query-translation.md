# #149 — NL-to-Query Translation (AI Search Mode)

## Summary
Added AI search mode to MacEverything that translates natural language queries into MacEverything query syntax using a local LLM (Qwen2.5:3b via Ollama).

## Motivation
MacEverything's powerful query syntax (ext:, path:, dm:, size:, etc.) requires users to know the exact filter names. Natural language queries like "最近下载的大PDF文件" are more intuitive but weren't supported.

## Architecture
- Python sidecar service (FastAPI on port 19861) handles translation
- Ollama backend runs Qwen2.5:3b locally (~2GB RAM, auto-unloads after 5min idle)
- Claude API available as fallback backend
- SSE streaming reduces perceived latency: ~700ms first token vs ~1.4s batch
- AI layer communicates with existing C++ engine via HTTP API — zero changes to core

## Changes

### Python AI Service (`ai_service/`)
- `config.py` — Service configuration (ports, model, timeouts, keep_alive)
- `prompt.py` — System prompt with full query syntax reference + 15 bilingual few-shot examples
- `llm_backend.py` — Backend abstraction: Ollama (batch + streaming) and Claude API
- `translator.py` — NL→Query translation with response cleaning, syntax passthrough detection
- `server.py` — FastAPI server with /translate, /translate/stream (SSE), /status, /health

### Swift UI Integration
- `AIServiceClient.swift` — Async HTTP client with batch and SSE streaming support
- `SearchViewModel.swift` — AI mode state, performAISearch() with streaming token display
- `ContentView.swift` — AI toggle button, translated query display bar
- `AISettingsView.swift` — Backend configuration panel with setup guide
- `MacEverythingApp.swift` / `AppDelegate.swift` — AI Settings menu items

### Tests
- 18 unit tests (translator, server, backend, prompt)
- 10 accuracy tests (Chinese + English parametrized, requires Ollama)
- 3 E2E integration tests (full pipeline, requires all services)
- 5 performance regression tests (latency budgets, system resource impact)
- Standalone benchmark script with JSON output

## Performance (M3 Pro, 18GB)
- Batch translate: ~1.4s (prefill 667ms + decode 750ms)
- Streaming first token: ~700ms
- Syntax passthrough: <1ms (bypasses LLM)
- Model memory: ~2GB (auto-unloads after 5min idle via keep_alive)
- All under 3s budget requirement

## How to Use
1. Install Ollama + pull qwen2.5:3b
2. Start AI service: `cd ai_service && python -m maceverything_ai.server`
3. In MacEverything app: click "AI" toggle in search bar
4. Type natural language query → see translated syntax → get results
