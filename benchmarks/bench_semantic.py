"""
Benchmark semantic search end-to-end via HTTP API.

Usage:
  python benchmarks/bench_semantic.py
  python benchmarks/bench_semantic.py --output results.json

Requires: MacEverything (:19860) + LiteLLM (:19861) + Ollama running
"""
import argparse
import json
import time
import sys
import os

try:
    import httpx
    import psutil
except ImportError:
    print("Install dependencies: pip install httpx psutil")
    sys.exit(1)

ME_URL = "http://localhost:19860"

def check_services():
    try:
        r = httpx.get(f"{ME_URL}/api/health", timeout=2)
        return r.status_code == 200
    except:
        return False

def bench_translate(rounds=5):
    queries = [
        ("最近下载的PDF", "Chinese + filters"),
        ("recent large files", "English + filters"),
        ("Python代码文件", "Chinese + extension"),
        ("桌面上的图片", "Chinese + path + type"),
        ("今天修改的文档", "Chinese + date + type"),
    ]

    results = []
    for query, desc in queries:
        latencies = []
        for _ in range(rounds):
            start = time.perf_counter()
            r = httpx.post(f"{ME_URL}/api/ai/translate",
                          json={"query": query}, timeout=30)
            elapsed = (time.perf_counter() - start) * 1000
            latencies.append(elapsed)

        data = r.json()
        results.append({
            "query": query,
            "description": desc,
            "translated": data.get("translated_query", ""),
            "success": data.get("success", False),
            "avg_ms": sum(latencies) / len(latencies),
            "min_ms": min(latencies),
            "max_ms": max(latencies),
        })
        print(f"  Translate: {query:20s} -> {data.get('translated_query', 'ERR'):30s}  avg={results[-1]['avg_ms']:.0f}ms")

    return results

def bench_semantic_search(rounds=3):
    queries = ["machine learning", "推荐系统", "configuration file", "test script"]
    results = []

    for query in queries:
        latencies = []
        for _ in range(rounds):
            start = time.perf_counter()
            r = httpx.get(f"{ME_URL}/api/search/semantic",
                         params={"q": query, "limit": 10}, timeout=30)
            elapsed = (time.perf_counter() - start) * 1000
            latencies.append(elapsed)

        data = r.json()
        results.append({
            "query": query,
            "count": data.get("count", 0),
            "avg_ms": sum(latencies) / len(latencies),
        })
        print(f"  Semantic:  {query:20s}  count={data.get('count', 0):3d}  avg={results[-1]['avg_ms']:.0f}ms")

    return results

def get_system_metrics():
    mem = psutil.virtual_memory()
    load_1, load_5, load_15 = os.getloadavg()
    return {
        "cpu_percent": psutil.cpu_percent(interval=1.0),
        "mem_used_gb": round(mem.used / (1024**3), 1),
        "mem_available_gb": round(mem.available / (1024**3), 1),
        "mem_percent": mem.percent,
        "load_avg": [round(load_1, 2), round(load_5, 2), round(load_15, 2)],
    }

def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--rounds", type=int, default=5)
    parser.add_argument("--output", type=str)
    args = parser.parse_args()

    if not check_services():
        print("MacEverything not running on :19860")
        sys.exit(1)

    # Check AI status
    r = httpx.get(f"{ME_URL}/api/ai/status", timeout=5)
    status = r.json()
    print(f"AI Status: {json.dumps(status, indent=2)}")

    print(f"\n--- Translation Benchmark ({args.rounds} rounds) ---")
    translate_results = bench_translate(args.rounds)

    print(f"\n--- Semantic Search Benchmark ({args.rounds} rounds) ---")
    search_results = bench_semantic_search(args.rounds)

    print(f"\n--- System Metrics ---")
    metrics = get_system_metrics()
    print(f"  CPU: {metrics['cpu_percent']:.0f}%  Mem: {metrics['mem_used_gb']}GB/{metrics['mem_available_gb']}GB avail  Load: {metrics['load_avg']}")

    output = {
        "translate": translate_results,
        "semantic_search": search_results,
        "system": metrics,
    }

    if args.output:
        with open(args.output, "w") as f:
            json.dump(output, f, indent=2, ensure_ascii=False)
        print(f"\nSaved to {args.output}")

if __name__ == "__main__":
    main()
