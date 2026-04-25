"""
End-to-end integration test.
Requires: Ollama running + MacEverything running on :19860 + AI service on :19861.

Run manually:
  1. Start MacEverything app
  2. ollama pull qwen2.5:3b
  3. cd ai_service && python -m maceverything_ai.server &
  4. python -m pytest tests/test_e2e.py -v -s
"""
import pytest
import httpx

AI_SERVICE = "http://localhost:19861"
ME_SERVICE = "http://localhost:19860"


def services_available() -> bool:
    try:
        r1 = httpx.get(f"{AI_SERVICE}/api/ai/health", timeout=2)
        r2 = httpx.get(f"{ME_SERVICE}/api/health", timeout=2)
        return r1.status_code == 200 and r2.status_code == 200
    except (httpx.ConnectError, httpx.ReadTimeout):
        return False


pytestmark = pytest.mark.skipif(not services_available(), reason="Services not running")


@pytest.mark.asyncio
async def test_translate_and_search():
    """Full flow: NL → translate → search MacEverything."""
    async with httpx.AsyncClient(timeout=15.0) as client:
        resp = await client.post(
            f"{AI_SERVICE}/api/ai/translate",
            json={"query": "Python脚本文件"},
        )
        assert resp.status_code == 200
        data = resp.json()
        assert data["success"] is True
        translated = data["translated_query"]
        assert "ext:py" in translated.lower(), f"Expected ext:py in: {translated}"

        resp = await client.get(
            f"{ME_SERVICE}/api/search",
            params={"q": translated, "limit": 10},
        )
        assert resp.status_code == 200
        results = resp.json()
        assert "results" in results
        if results["count"] > 0:
            assert any(r["name"].endswith(".py") for r in results["results"])


@pytest.mark.asyncio
async def test_translate_chinese_queries():
    """Test several Chinese NL queries translate correctly."""
    queries = [
        ("最近的PDF文件", "ext:pdf"),
        ("桌面上的图片", "pic:"),
        ("大文件", "size:>"),
    ]
    async with httpx.AsyncClient(timeout=15.0) as client:
        for nl, expected_fragment in queries:
            resp = await client.post(
                f"{AI_SERVICE}/api/ai/translate",
                json={"query": nl},
            )
            data = resp.json()
            assert data["success"], f"Failed for '{nl}': {data.get('error')}"
            assert expected_fragment in data["translated_query"].lower(), (
                f"Expected '{expected_fragment}' in translation of '{nl}': {data['translated_query']}"
            )


@pytest.mark.asyncio
async def test_existing_syntax_passthrough():
    """Queries that are already valid syntax should pass through unchanged."""
    async with httpx.AsyncClient(timeout=15.0) as client:
        resp = await client.post(
            f"{AI_SERVICE}/api/ai/translate",
            json={"query": "ext:py size:>1mb"},
        )
        data = resp.json()
        assert data["success"] is True
        assert data["already_syntax"] is True
        assert data["translated_query"] == "ext:py size:>1mb"
