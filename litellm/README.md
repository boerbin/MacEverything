# LiteLLM Model Gateway for MacEverything

LiteLLM acts as a unified model gateway. MacEverything calls LiteLLM's OpenAI-compatible API, and LiteLLM routes requests to the configured providers (Ollama local, Claude API, OpenAI API, etc).

## Prerequisites

1. Install Ollama and pull models:
   ```bash
   brew install ollama
   ollama pull qwen2.5:3b
   ollama pull bge-m3
   ```

2. Install LiteLLM:
   ```bash
   pip install litellm
   ```

## Start

```bash
cd litellm
litellm --config config.yaml --port 19861
```

## Verify

```bash
# List available models
curl http://localhost:19861/v1/models

# Test chat (NL translation)
curl -X POST http://localhost:19861/v1/chat/completions \
  -H "Content-Type: application/json" \
  -d '{"model":"translate","messages":[{"role":"user","content":"最近下载的PDF"}]}'

# Test embedding
curl -X POST http://localhost:19861/v1/embeddings \
  -H "Content-Type: application/json" \
  -d '{"model":"embed","input":"MacEverything file search"}'
```

## Configuration

Edit `config.yaml` to:
- Switch models (e.g., `qwen2.5:3b` -> `qwen2.5:7b`)
- Add remote providers (Claude, OpenAI) as primary or fallback
- Route different tasks to different models

MacEverything connects to `http://localhost:19861` and is unaware of which model or provider handles each request.

## Architecture

```
MacEverything (:19860)
    | OpenAI-format HTTP
LiteLLM Gateway (:19861)
    |-> Ollama (:11434) -- local models
    |-> Claude API -- remote (optional)
    +-> OpenAI API -- remote (optional)
```
