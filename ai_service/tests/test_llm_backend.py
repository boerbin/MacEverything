import pytest
from maceverything_ai.llm_backend import create_backend, OllamaBackend, ClaudeBackend


def test_create_backend_ollama():
    backend = create_backend("ollama")
    assert isinstance(backend, OllamaBackend)


def test_create_backend_claude():
    backend = create_backend("claude")
    assert isinstance(backend, ClaudeBackend)


def test_create_backend_unknown():
    with pytest.raises(ValueError, match="Unknown backend"):
        create_backend("unknown_backend")


def test_ollama_backend_has_keep_alive():
    backend = OllamaBackend()
    assert backend.keep_alive == "5m"


def test_ollama_backend_has_stream_method():
    backend = OllamaBackend()
    assert hasattr(backend, "chat_stream")
    assert callable(getattr(backend, "chat_stream"))
