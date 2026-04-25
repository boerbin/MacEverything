"""
Integration tests that require a running Ollama instance with qwen2.5:3b.
Skipped automatically if Ollama is not available.

Run: pytest tests/test_translation_accuracy.py -v -s
"""
import pytest
import httpx

from maceverything_ai.llm_backend import OllamaBackend
from maceverything_ai.translator import Translator

OLLAMA_URL = "http://localhost:11434"


def ollama_available() -> bool:
    try:
        resp = httpx.get(f"{OLLAMA_URL}/api/tags", timeout=2.0)
        return resp.status_code == 200
    except (httpx.ConnectError, httpx.ReadTimeout):
        return False


pytestmark = pytest.mark.skipif(not ollama_available(), reason="Ollama not running")


@pytest.fixture
def translator():
    backend = OllamaBackend()
    return Translator(backend)


ACCURACY_CASES = [
    ("最近下载的PDF", [["ext:pdf"], ["dm:"]]),
    ("上个月修改的Word文档", [["ext:doc", "doc:"], ["dm:lastmonth", "dm:last"]]),
    ("桌面上的图片", [["desktop"], ["pic:"]]),
    ("大于500MB的视频", [["video:"], ["size:>500mb", "size:>500"]]),
    ("今天创建的文件", [["dc:today"]]),
    ("Python代码文件", [["ext:py"]]),
    ("除了build目录的cpp文件", [["ext:cpp"], ["nopath:build", "nopath:"]]),
    ("recent PDF files", [["ext:pdf"], ["dm:"]]),
    ("large downloads", [["downloads"], ["size:>"]]),
    ("markdown files modified this week", [["ext:md"], ["dm:thisweek", "dm:last7days"]]),
]


@pytest.mark.asyncio
@pytest.mark.parametrize("nl_input,expected_alternatives", ACCURACY_CASES)
async def test_translation_accuracy(translator, nl_input, expected_alternatives):
    result = await translator.translate(nl_input)
    assert result.success, f"Translation failed for '{nl_input}': {result.error}"
    query = result.translated_query.lower()
    for alternatives in expected_alternatives:
        matched = any(alt.lower() in query for alt in alternatives)
        assert matched, (
            f"Expected one of {alternatives} in translation of '{nl_input}', got: '{result.translated_query}'"
        )
