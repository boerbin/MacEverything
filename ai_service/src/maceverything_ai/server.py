from __future__ import annotations

import json as json_mod

import uvicorn
from contextlib import asynccontextmanager
from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
from pydantic import BaseModel
from sse_starlette.sse import EventSourceResponse

from maceverything_ai.config import config
from maceverything_ai.llm_backend import LLMBackend, create_backend
from maceverything_ai.translator import Translator, TranslationResult

_translator: Translator | None = None
_backend: LLMBackend | None = None


def _get_translator() -> Translator:
    global _translator, _backend
    if _translator is None:
        _backend = create_backend()
        _translator = Translator(_backend)
    return _translator


class TranslateRequest(BaseModel):
    query: str


class TranslateResponse(BaseModel):
    original_query: str
    translated_query: str
    success: bool
    already_syntax: bool = False
    error: str | None = None


class StatusResponse(BaseModel):
    status: str
    backend: str
    model: str
    backend_available: bool


@asynccontextmanager
async def lifespan(app: FastAPI):
    _get_translator()
    yield


def create_app() -> FastAPI:
    app = FastAPI(title="MacEverything AI", version="0.1.0", lifespan=lifespan)
    app.add_middleware(
        CORSMiddleware,
        allow_origins=["*"],
        allow_methods=["*"],
        allow_headers=["*"],
    )

    @app.get("/api/ai/health")
    async def health():
        return {"status": "ok"}

    @app.post("/api/ai/translate", response_model=TranslateResponse)
    async def translate(req: TranslateRequest):
        translator = _get_translator()
        result = await translator.translate(req.query)
        return TranslateResponse(
            original_query=result.original_query,
            translated_query=result.translated_query,
            success=result.success,
            already_syntax=result.already_syntax,
            error=result.error,
        )

    @app.post("/api/ai/translate/stream")
    async def translate_stream(req: TranslateRequest):
        translator = _get_translator()

        async def event_generator():
            async for item in translator.translate_stream(req.query):
                if isinstance(item, TranslationResult):
                    yield {
                        "event": "result",
                        "data": json_mod.dumps({
                            "original_query": item.original_query,
                            "translated_query": item.translated_query,
                            "success": item.success,
                            "already_syntax": item.already_syntax,
                            "error": item.error,
                        }),
                    }
                else:
                    yield {
                        "event": "token",
                        "data": json_mod.dumps(item),
                    }

        return EventSourceResponse(event_generator())

    @app.get("/api/ai/status", response_model=StatusResponse)
    async def status():
        backend = _backend
        available = False
        if backend and hasattr(backend, "is_available"):
            available = await backend.is_available()
        return StatusResponse(
            status="running",
            backend=config.backend,
            model=config.ollama_model if config.backend == "ollama" else config.claude_model,
            backend_available=available,
        )

    return app


def main():
    app = create_app()
    uvicorn.run(app, host="127.0.0.1", port=config.ai_port)


if __name__ == "__main__":
    main()
