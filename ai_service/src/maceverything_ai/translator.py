import re
from dataclasses import dataclass, field
from typing import Optional

from maceverything_ai.llm_backend import LLMBackend
from maceverything_ai.prompt import build_messages

KNOWN_FILTERS = [
    "ext:", "size:", "path:", "nopath:", "dm:", "dc:", "da:", "file:", "folder:",
    "content:", "regex:", "ww:", "wholeword:", "wfn:", "wholefilename:", "parent:",
    "depth:", "len:", "case:", "nocase:", "type:", "pic:", "video:", "audio:",
    "doc:", "exe:", "zip:", "datemodified:", "datecreated:", "dateaccessed:",
]


@dataclass
class TranslationResult:
    original_query: str
    translated_query: str
    success: bool
    already_syntax: bool = False
    error: Optional[str] = None


def _looks_like_query_syntax(text: str) -> bool:
    return any(f in text.lower() for f in KNOWN_FILTERS)


def _clean_llm_response(raw: str) -> str:
    text = raw.strip()
    text = re.sub(r"^```[\w]*\n?", "", text)
    text = re.sub(r"\n?```$", "", text)
    text = re.sub(r"^(?:Query|Result|Output|Translation|查询)[:\s：]+", "", text, flags=re.IGNORECASE)
    text = text.strip().strip('"').strip("'")
    first_line = text.split("\n")[0].strip()
    return first_line


class Translator:
    def __init__(self, backend: LLMBackend):
        self._backend = backend

    async def translate(self, user_query: str) -> TranslationResult:
        user_query = user_query.strip()
        if not user_query:
            return TranslationResult(
                original_query=user_query,
                translated_query=user_query,
                success=False,
                error="Empty query",
            )

        if _looks_like_query_syntax(user_query):
            return TranslationResult(
                original_query=user_query,
                translated_query=user_query,
                success=True,
                already_syntax=True,
            )

        try:
            messages = build_messages(user_query)
            raw_response = await self._backend.chat(messages)
            translated = _clean_llm_response(raw_response)

            if not translated:
                return TranslationResult(
                    original_query=user_query,
                    translated_query=user_query,
                    success=False,
                    error="LLM returned empty response",
                )

            return TranslationResult(
                original_query=user_query,
                translated_query=translated,
                success=True,
            )
        except Exception as e:
            return TranslationResult(
                original_query=user_query,
                translated_query=user_query,
                success=False,
                error=str(e),
            )

    async def translate_stream(self, user_query: str):
        """Yield partial tokens as SSE events. Final event is the complete TranslationResult."""
        user_query = user_query.strip()
        if not user_query:
            yield TranslationResult(user_query, user_query, False, error="Empty query")
            return

        if _looks_like_query_syntax(user_query):
            yield TranslationResult(user_query, user_query, True, already_syntax=True)
            return

        if not hasattr(self._backend, "chat_stream"):
            result = await self.translate(user_query)
            yield result
            return

        try:
            messages = build_messages(user_query)
            accumulated = ""
            async for token in self._backend.chat_stream(messages):
                accumulated += token
                yield {"token": token, "partial": accumulated}

            translated = _clean_llm_response(accumulated)
            yield TranslationResult(
                original_query=user_query,
                translated_query=translated if translated else user_query,
                success=bool(translated),
                error="LLM returned empty response" if not translated else None,
            )
        except Exception as e:
            yield TranslationResult(user_query, user_query, False, error=str(e))
