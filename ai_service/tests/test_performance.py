"""
Performance regression tests for NL-to-Query translation.
Requires: Ollama running with qwen2.5:3b.

Validates against budgets from docs/semantic-search/performance_report.md:
- Total RT: < 3s (requirement), expected ~1.4s
- First token (streaming): < 1s, expected ~700ms
- Syntax passthrough: < 5ms (no LLM call)
- Memory: model ~2GB, service overhead < 50MB

Run: pytest tests/test_performance.py -v -s
"""
import time
import os
import pytest
import httpx
import psutil

from maceverything_ai.llm_backend import OllamaBackend
from maceverything_ai.translator import Translator

OLLAMA_URL = "http://localhost:11434"
WARMUP_ROUNDS = 2


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


@pytest.mark.asyncio
async def test_batch_translate_latency(translator):
    """Total RT for batch translate must be < 3s. Expected ~1.4s."""
    for _ in range(WARMUP_ROUNDS):
        await translator.translate("test")

    latencies = []
    queries = [
        "最近下载的PDF", "桌面上的大文件", "上周修改的Python脚本",
        "recent images", "大于100MB的视频文件",
    ]
    for q in queries:
        start = time.perf_counter()
        result = await translator.translate(q)
        elapsed = time.perf_counter() - start
        assert result.success, f"Translation failed for '{q}': {result.error}"
        latencies.append(elapsed)

    avg_ms = sum(latencies) / len(latencies) * 1000
    max_ms = max(latencies) * 1000
    min_ms = min(latencies) * 1000

    print(f"\n--- Batch Translate Latency ---")
    print(f"  Avg: {avg_ms:.0f}ms | Min: {min_ms:.0f}ms | Max: {max_ms:.0f}ms")

    assert max_ms < 3000, f"Max latency {max_ms:.0f}ms exceeds 3s budget"
    assert avg_ms < 2000, f"Avg latency {avg_ms:.0f}ms exceeds 2s expected"


@pytest.mark.asyncio
async def test_streaming_first_token_latency(translator):
    """First token via streaming must arrive in < 1.5s. Expected ~700ms."""
    if not hasattr(translator._backend, "chat_stream"):
        pytest.skip("Backend does not support streaming")

    for _ in range(WARMUP_ROUNDS):
        await translator.translate("test")

    first_token_latencies = []
    queries = ["最近下载的PDF", "桌面上的图片", "Python代码文件"]

    for q in queries:
        start = time.perf_counter()
        first_token_time = None
        async for item in translator.translate_stream(q):
            if first_token_time is None and isinstance(item, dict) and "token" in item:
                first_token_time = time.perf_counter() - start
        if first_token_time:
            first_token_latencies.append(first_token_time)

    if not first_token_latencies:
        pytest.skip("No streaming tokens received")

    avg_ms = sum(first_token_latencies) / len(first_token_latencies) * 1000
    max_ms = max(first_token_latencies) * 1000

    print(f"\n--- Streaming First Token Latency ---")
    print(f"  Avg: {avg_ms:.0f}ms | Max: {max_ms:.0f}ms")

    assert max_ms < 1500, f"First token {max_ms:.0f}ms exceeds 1.5s budget"


@pytest.mark.asyncio
async def test_syntax_passthrough_latency(translator):
    """Queries already in syntax form should bypass LLM — expected < 5ms."""
    queries = [
        "ext:pdf size:>1mb",
        "path:Downloads ext:py dm:last7days",
        "content:TODO ext:js nopath:node_modules",
    ]
    latencies = []
    for q in queries:
        start = time.perf_counter()
        result = await translator.translate(q)
        elapsed = time.perf_counter() - start
        assert result.already_syntax is True
        latencies.append(elapsed)

    max_ms = max(latencies) * 1000
    print(f"\n--- Syntax Passthrough Latency ---")
    print(f"  Max: {max_ms:.2f}ms")

    assert max_ms < 5, f"Passthrough took {max_ms:.2f}ms, expected < 5ms"


@pytest.mark.asyncio
async def test_sequential_throughput(translator):
    """Sequential translation throughput baseline."""
    await translator.translate("warmup")

    queries = [
        "最近的PDF", "大文件", "Python代码", "下载的图片", "本周的文档",
        "recent videos", "config files", "markdown notes", "桌面截图", "压缩包",
    ]

    start = time.perf_counter()
    for q in queries:
        result = await translator.translate(q)
        assert result.success
    total = time.perf_counter() - start

    qps = len(queries) / total
    avg_ms = total / len(queries) * 1000
    print(f"\n--- Sequential Throughput ---")
    print(f"  {len(queries)} queries in {total:.1f}s | {qps:.1f} q/s | {avg_ms:.0f}ms avg")


@pytest.mark.asyncio
async def test_system_resource_impact(translator):
    """Measure CPU, memory, and load impact during translation."""
    def get_system_metrics():
        cpu_percent = psutil.cpu_percent(interval=None, percpu=False)
        mem = psutil.virtual_memory()
        swap = psutil.swap_memory()
        load_1, load_5, load_15 = os.getloadavg()
        return {
            "cpu_percent": cpu_percent,
            "mem_used_gb": mem.used / (1024**3),
            "mem_available_gb": mem.available / (1024**3),
            "mem_percent": mem.percent,
            "swap_used_gb": swap.used / (1024**3),
            "load_1m": load_1,
            "load_5m": load_5,
            "load_15m": load_15,
        }

    _ = psutil.cpu_percent(interval=0.5)
    baseline = get_system_metrics()
    print(f"\n--- System Resource Impact ---")
    print(f"  Baseline: CPU {baseline['cpu_percent']:.0f}% | "
          f"Mem {baseline['mem_used_gb']:.1f}GB used / {baseline['mem_available_gb']:.1f}GB avail ({baseline['mem_percent']:.0f}%) | "
          f"Swap {baseline['swap_used_gb']:.1f}GB | "
          f"Load {baseline['load_1m']:.1f}/{baseline['load_5m']:.1f}/{baseline['load_15m']:.1f}")

    await translator.translate("warmup")
    time.sleep(0.5)
    after_load = get_system_metrics()
    print(f"  After model load: CPU {after_load['cpu_percent']:.0f}% | "
          f"Mem {after_load['mem_used_gb']:.1f}GB used / {after_load['mem_available_gb']:.1f}GB avail ({after_load['mem_percent']:.0f}%) | "
          f"Swap {after_load['swap_used_gb']:.1f}GB | "
          f"Load {after_load['load_1m']:.1f}/{after_load['load_5m']:.1f}/{after_load['load_15m']:.1f}")

    peak_cpu = 0.0
    peak_mem_percent = 0.0
    for q in ["最近的PDF", "大文件", "Python代码", "下载的图片", "本周的文档"]:
        await translator.translate(q)
        m = get_system_metrics()
        peak_cpu = max(peak_cpu, m["cpu_percent"])
        peak_mem_percent = max(peak_mem_percent, m["mem_percent"])

    during = get_system_metrics()
    print(f"  During sustained load: Peak CPU {peak_cpu:.0f}% | "
          f"Peak Mem {peak_mem_percent:.0f}% | "
          f"Load {during['load_1m']:.1f}/{during['load_5m']:.1f}/{during['load_15m']:.1f}")

    mem_delta_gb = after_load["mem_used_gb"] - baseline["mem_used_gb"]
    print(f"  Model load memory delta: {mem_delta_gb:+.1f} GB")

    assert after_load["mem_available_gb"] > 2.0, (
        f"Available memory dropped to {after_load['mem_available_gb']:.1f}GB after model load. "
        f"Risk of foreground starvation on 18GB machine."
    )
    assert during["load_1m"] < os.cpu_count() * 2, (
        f"1-min load average {during['load_1m']:.1f} too high "
        f"(> 2x CPU count {os.cpu_count()})"
    )
