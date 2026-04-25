# MacEverything AI Service

Local AI service for natural language file search. Translates queries like "最近下载的大PDF文件" into MacEverything query syntax `path:Downloads ext:pdf size:>10mb dm:last7days`.

## Quick Start

### Prerequisites

1. Install Ollama: `brew install ollama`
2. Pull model: `ollama pull qwen2.5:3b`
3. Install Python dependencies: `cd ai_service && pip install -e ".[dev]"`

### Run

```bash
cd ai_service
python -m maceverything_ai.server
```

The service starts on `http://localhost:19861`.

### Test

```bash
# Unit tests (no Ollama needed)
python -m pytest tests/test_translator.py tests/test_server.py tests/test_llm_backend.py -v

# Accuracy tests (requires Ollama + qwen2.5:3b)
python -m pytest tests/test_translation_accuracy.py -v -s

# Performance tests (requires Ollama + qwen2.5:3b)
python -m pytest tests/test_performance.py -v -s

# E2E tests (requires Ollama + MacEverything + AI service running)
python -m pytest tests/test_e2e.py -v -s

# All tests
python -m pytest tests/ -v
```

### Benchmark

```bash
python benchmarks/bench_translate.py --rounds 5 --output results.json
```

## API

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/ai/health` | GET | Health check |
| `/api/ai/status` | GET | Backend status (model, availability) |
| `/api/ai/translate` | POST | Translate NL → query syntax (batch) |
| `/api/ai/translate/stream` | POST | Translate with SSE streaming (~700ms first token) |

### Example

```bash
# Batch translate
curl -X POST http://localhost:19861/api/ai/translate \
  -H "Content-Type: application/json" \
  -d '{"query": "最近下载的大PDF文件"}'

# Response:
# {"original_query":"最近下载的大PDF文件","translated_query":"path:Downloads ext:pdf size:>10mb dm:last7days","success":true,"already_syntax":false,"error":null}
```

## Architecture

```
SwiftUI App (AI mode toggle)
    ↓ HTTP POST
AI Service (:19861, FastAPI)
    ↓ Ollama API
LLM (qwen2.5:3b, local)
    ↓ translated query
MacEverything Core (:19860)
    ↓ search results
SwiftUI App (display)
```

## Configuration

Edit `src/maceverything_ai/config.py`:

| Setting | Default | Description |
|---------|---------|-------------|
| `ai_port` | 19861 | AI service port |
| `ollama_model` | qwen2.5:3b | Ollama model name |
| `ollama_keep_alive` | 5m | Model unload timeout (saves ~2GB RAM) |
| `llm_timeout` | 15.0 | LLM request timeout (seconds) |
| `backend` | ollama | Backend: "ollama" or "claude" |
