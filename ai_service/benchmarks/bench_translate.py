"""
Standalone benchmark script for NL-to-Query translation performance.

Usage:
  python benchmarks/bench_translate.py
  python benchmarks/bench_translate.py --rounds 10 --output results.json
"""
import argparse
import asyncio
import json
import time
import sys
import os

import psutil

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "src"))

from maceverything_ai.llm_backend import OllamaBackend
from maceverything_ai.translator import Translator

BENCH_QUERIES = [
    ("最近下载的PDF", "Chinese + path + ext + date"),
    ("上个月修改的Word文档", "Chinese + ext + date"),
    ("大于500MB的视频文件", "Chinese + type + size"),
    ("除了node_modules以外的JS文件", "Chinese + ext + exclusion"),
    ("recent large PDF files", "English + ext + size + date"),
    ("Python scripts modified this week", "English + ext + date"),
    ("桌面上的截图", "Chinese + path + image"),
    ("config files in my project", "English + ext category"),
    ("今天创建的文件", "Chinese + date created"),
    ("3天内修改的Markdown笔记", "Chinese + ext + relative date"),
]


async def run_benchmark(rounds: int = 5, warmup: int = 2):
    backend = OllamaBackend()
    translator = Translator(backend)

    print(f"Warming up ({warmup} rounds)...")
    for _ in range(warmup):
        await translator.translate("warmup query")

    results = []
    for query, description in BENCH_QUERIES:
        latencies = []
        outputs = []
        for _ in range(rounds):
            start = time.perf_counter()
            result = await translator.translate(query)
            elapsed = time.perf_counter() - start
            latencies.append(elapsed * 1000)
            outputs.append(result.translated_query)

        entry = {
            "query": query,
            "description": description,
            "translated": outputs[-1],
            "success": result.success,
            "rounds": rounds,
            "avg_ms": sum(latencies) / len(latencies),
            "min_ms": min(latencies),
            "max_ms": max(latencies),
            "p50_ms": sorted(latencies)[len(latencies) // 2],
            "p95_ms": sorted(latencies)[int(len(latencies) * 0.95)],
        }
        results.append(entry)
        print(f"  {query:30s} -> {entry['translated']:40s}  "
              f"avg={entry['avg_ms']:.0f}ms  p95={entry['p95_ms']:.0f}ms")

    mem = psutil.virtual_memory()
    swap = psutil.swap_memory()
    load_1, load_5, load_15 = os.getloadavg()
    cpu_percent = psutil.cpu_percent(interval=1.0)

    system_metrics = {
        "cpu_percent": cpu_percent,
        "cpu_count": os.cpu_count(),
        "mem_total_gb": round(mem.total / (1024**3), 1),
        "mem_used_gb": round(mem.used / (1024**3), 1),
        "mem_available_gb": round(mem.available / (1024**3), 1),
        "mem_percent": mem.percent,
        "swap_used_gb": round(swap.used / (1024**3), 1),
        "load_avg_1m": round(load_1, 2),
        "load_avg_5m": round(load_5, 2),
        "load_avg_15m": round(load_15, 2),
    }

    print(f"\n--- System Resources ---")
    print(f"  CPU: {cpu_percent:.0f}% ({os.cpu_count()} cores) | "
          f"Load: {load_1:.1f}/{load_5:.1f}/{load_15:.1f}")
    print(f"  Mem: {system_metrics['mem_used_gb']}GB / {system_metrics['mem_total_gb']}GB "
          f"({mem.percent:.0f}%) | Avail: {system_metrics['mem_available_gb']}GB | "
          f"Swap: {system_metrics['swap_used_gb']}GB")

    all_avgs = [r["avg_ms"] for r in results]
    all_p95s = [r["p95_ms"] for r in results]
    summary = {
        "total_queries": len(results),
        "rounds_per_query": rounds,
        "overall_avg_ms": sum(all_avgs) / len(all_avgs),
        "overall_p95_ms": max(all_p95s),
        "success_rate": sum(1 for r in results if r["success"]) / len(results),
        "budget_3s_pass": all(r["max_ms"] < 3000 for r in results),
    }
    print(f"\n--- Summary ---")
    print(f"  Overall avg: {summary['overall_avg_ms']:.0f}ms | "
          f"P95: {summary['overall_p95_ms']:.0f}ms | "
          f"Success: {summary['success_rate']:.0%} | "
          f"<3s budget: {'PASS' if summary['budget_3s_pass'] else 'FAIL'}")

    return {"results": results, "summary": summary, "system": system_metrics}


def main():
    parser = argparse.ArgumentParser(description="Benchmark NL-to-Query translation")
    parser.add_argument("--rounds", type=int, default=5, help="Rounds per query")
    parser.add_argument("--warmup", type=int, default=2, help="Warmup rounds")
    parser.add_argument("--output", type=str, help="Output JSON file")
    args = parser.parse_args()

    data = asyncio.run(run_benchmark(rounds=args.rounds, warmup=args.warmup))

    if args.output:
        with open(args.output, "w") as f:
            json.dump(data, f, indent=2, ensure_ascii=False)
        print(f"\nResults saved to {args.output}")


if __name__ == "__main__":
    main()
