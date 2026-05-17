# 168 - LlamaBackend Token Count Logging & n_ctx Guard

## Background

MacEverything experienced 4 identical crashes at `llama_context::decode → ggml_abort +5468`. Existing logging (added in #167) records the query text and prompt source before inference, but does not log the actual token count after tokenization. Without this data, it's impossible to determine whether token overflow is a contributing factor.

## Changes

**File:** `MacEverything/Core/LlamaBackend.cpp`

1. Added `#include "Logger.h"` for LOG_INFO/LOG_ERROR macros
2. Added token count logging after tokenization: logs `nTokens`, `n_ctx`, and `n_batch` on every inference call
3. Added safety guard: rejects prompts where `nTokens > n_ctx` with an error log, preventing potential ggml_abort crashes from token overflow

## Verification

- Build: xcodebuild Release succeeded
- Runtime: Sent translate request "recent downloads" → log shows `chat: nTokens=1126 n_ctx=2048 n_batch=2048`
- Confirms 1126 tokens used out of 2048 context (55% utilization, 922 tokens headroom)

## Impact

- Every future AI inference call now has token count in the log for post-mortem analysis
- If the crash recurs, we can definitively confirm or rule out token overflow as the cause
- Prompts exceeding n_ctx are now safely rejected instead of potentially triggering ggml_abort
