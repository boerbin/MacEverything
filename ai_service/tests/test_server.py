import pytest
from httpx import AsyncClient, ASGITransport
from unittest.mock import AsyncMock, patch

from maceverything_ai.server import create_app


@pytest.fixture
def app():
    return create_app()


@pytest.fixture
async def client(app):
    transport = ASGITransport(app=app)
    async with AsyncClient(transport=transport, base_url="http://test") as c:
        yield c


@pytest.mark.asyncio
async def test_health_endpoint(client):
    resp = await client.get("/api/ai/health")
    assert resp.status_code == 200
    assert resp.json()["status"] == "ok"


@pytest.mark.asyncio
async def test_translate_endpoint(client):
    mock_result = _make_result({
        "original_query": "最近的PDF",
        "translated_query": "ext:pdf dm:last7days",
        "success": True,
        "already_syntax": False,
        "error": None,
    })
    with patch("maceverything_ai.server._get_translator") as mock_get:
        mock_translator = AsyncMock()
        mock_translator.translate = AsyncMock(return_value=mock_result)
        mock_get.return_value = mock_translator

        resp = await client.post("/api/ai/translate", json={"query": "最近的PDF"})
        assert resp.status_code == 200
        data = resp.json()
        assert data["translated_query"] == "ext:pdf dm:last7days"
        assert data["success"] is True


@pytest.mark.asyncio
async def test_translate_missing_query(client):
    resp = await client.post("/api/ai/translate", json={})
    assert resp.status_code == 422


@pytest.mark.asyncio
async def test_status_endpoint(client):
    resp = await client.get("/api/ai/status")
    assert resp.status_code == 200
    data = resp.json()
    assert "backend" in data
    assert "model" in data


def _make_result(d):
    from maceverything_ai.translator import TranslationResult
    return TranslationResult(**d)
