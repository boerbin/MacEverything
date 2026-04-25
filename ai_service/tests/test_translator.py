from maceverything_ai.prompt import build_messages, FEW_SHOT_EXAMPLES


def test_build_messages_structure():
    messages = build_messages("最近的PDF")
    assert messages[0]["role"] == "system"
    assert "ext:" in messages[0]["content"]
    expected_len = 1 + 2 * len(FEW_SHOT_EXAMPLES) + 1
    assert len(messages) == expected_len
    assert messages[-1] == {"role": "user", "content": "最近的PDF"}


def test_few_shot_examples_are_valid_queries():
    known_prefixes = [
        "ext:", "size:", "path:", "nopath:", "dm:", "dc:", "file:", "folder:",
        "content:", "regex:", "ww:", "parent:", "depth:", "pic:", "video:",
        "audio:", "doc:", "zip:", "exe:", "case:",
    ]
    for nl, query in FEW_SHOT_EXAMPLES:
        has_filter = any(p in query for p in known_prefixes)
        has_keyword = len(query.split()) > 0
        assert has_filter or has_keyword, f"Example '{nl}' → '{query}' has no filter or keyword"


import pytest
from unittest.mock import AsyncMock
from maceverything_ai.translator import Translator


class FakeLLM:
    def __init__(self, response: str):
        self.response = response
        self.chat = AsyncMock(return_value=response)

    async def is_available(self):
        return True


@pytest.mark.asyncio
async def test_translator_returns_structured_result():
    llm = FakeLLM("path:Downloads ext:pdf dm:last7days")
    translator = Translator(llm)
    result = await translator.translate("最近下载的PDF")
    assert result.translated_query == "path:Downloads ext:pdf dm:last7days"
    assert result.original_query == "最近下载的PDF"
    assert result.success is True


@pytest.mark.asyncio
async def test_translator_cleans_markdown_fences():
    llm = FakeLLM("```\npath:Downloads ext:pdf\n```")
    translator = Translator(llm)
    result = await translator.translate("下载的PDF")
    assert result.translated_query == "path:Downloads ext:pdf"


@pytest.mark.asyncio
async def test_translator_strips_explanation_prefix():
    llm = FakeLLM("Query: path:Downloads ext:pdf dm:last7days")
    translator = Translator(llm)
    result = await translator.translate("最近下载的PDF")
    assert result.translated_query == "path:Downloads ext:pdf dm:last7days"


@pytest.mark.asyncio
async def test_translator_handles_llm_error():
    llm = FakeLLM("")
    llm.chat = AsyncMock(side_effect=Exception("connection refused"))
    translator = Translator(llm)
    result = await translator.translate("test query")
    assert result.success is False
    assert result.translated_query == "test query"
    assert "connection refused" in result.error


@pytest.mark.asyncio
async def test_translator_detects_existing_query_syntax():
    llm = FakeLLM("ext:py")
    translator = Translator(llm)
    result = await translator.translate("ext:pdf size:>1mb")
    assert result.translated_query == "ext:pdf size:>1mb"
    assert result.already_syntax is True
    llm.chat.assert_not_called()


@pytest.mark.asyncio
async def test_translator_handles_empty_query():
    llm = FakeLLM("")
    translator = Translator(llm)
    result = await translator.translate("")
    assert result.success is False
    assert "Empty query" in result.error


@pytest.mark.asyncio
async def test_translator_handles_empty_llm_response():
    llm = FakeLLM("")
    translator = Translator(llm)
    result = await translator.translate("some query")
    assert result.success is False
    assert result.translated_query == "some query"
