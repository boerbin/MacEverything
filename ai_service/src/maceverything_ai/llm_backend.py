from abc import ABC, abstractmethod

import httpx

from maceverything_ai.config import config


class LLMBackend(ABC):
    @abstractmethod
    async def chat(self, messages: list[dict[str, str]]) -> str:
        """Send messages to LLM and return the response text."""


class OllamaBackend(LLMBackend):
    def __init__(self, base_url: str | None = None, model: str | None = None):
        self.base_url = base_url or config.ollama_url
        self.model = model or config.ollama_model
        self.keep_alive = config.ollama_keep_alive
        self._client = httpx.AsyncClient(timeout=config.llm_timeout)

    async def chat(self, messages: list[dict[str, str]]) -> str:
        resp = await self._client.post(
            f"{self.base_url}/api/chat",
            json={
                "model": self.model,
                "messages": messages,
                "stream": False,
                "keep_alive": self.keep_alive,
                "options": {"temperature": 0.1, "num_predict": 200},
            },
        )
        resp.raise_for_status()
        return resp.json()["message"]["content"].strip()

    async def chat_stream(self, messages: list[dict[str, str]]):
        """Yield tokens as they arrive. ~700ms to first token vs ~1.4s batch."""
        import json as json_mod

        async with self._client.stream(
            "POST",
            f"{self.base_url}/api/chat",
            json={
                "model": self.model,
                "messages": messages,
                "stream": True,
                "keep_alive": self.keep_alive,
                "options": {"temperature": 0.1, "num_predict": 200},
            },
        ) as resp:
            resp.raise_for_status()
            async for line in resp.aiter_lines():
                if line:
                    chunk = json_mod.loads(line)
                    token = chunk.get("message", {}).get("content", "")
                    if token:
                        yield token
                    if chunk.get("done"):
                        break

    async def is_available(self) -> bool:
        try:
            resp = await self._client.get(f"{self.base_url}/api/tags")
            return resp.status_code == 200
        except httpx.ConnectError:
            return False


class ClaudeBackend(LLMBackend):
    def __init__(self, model: str | None = None):
        self.model = model or config.claude_model
        self._client = None

    def _get_client(self):
        if self._client is None:
            import anthropic

            self._client = anthropic.AsyncAnthropic()
        return self._client

    async def chat(self, messages: list[dict[str, str]]) -> str:
        client = self._get_client()
        system_msg = ""
        chat_messages = []
        for m in messages:
            if m["role"] == "system":
                system_msg = m["content"]
            else:
                chat_messages.append(m)
        resp = await client.messages.create(
            model=self.model,
            max_tokens=200,
            system=system_msg,
            messages=chat_messages,
        )
        return resp.content[0].text.strip()

    async def is_available(self) -> bool:
        try:
            import anthropic  # noqa: F401
            import os

            return bool(os.environ.get("ANTHROPIC_API_KEY"))
        except ImportError:
            return False


def create_backend(name: str | None = None) -> LLMBackend:
    name = name or config.backend
    if name == "ollama":
        return OllamaBackend()
    elif name == "claude":
        return ClaudeBackend()
    else:
        raise ValueError(f"Unknown backend: {name!r}. Use 'ollama' or 'claude'.")
