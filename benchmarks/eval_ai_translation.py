#!/usr/bin/env python3
"""
AI Translation Accuracy Evaluator

Loads test cases from a TSV file, calls the MacEverything translate API,
and compares results using token-set matching (order-independent).

Usage:
    python3 eval_ai_translation.py <tsv_path> [--host HOST] [--port PORT] [--verbose] [--json]

Output (JSON to stdout):
    {"total": N, "passed": N, "failed": N, "accuracy": 0.XX, "failures": [...]}
"""

import argparse
import json
import sys
import urllib.request
import urllib.error


def load_test_cases(tsv_path):
    cases = []
    with open(tsv_path, "r", encoding="utf-8") as f:
        for line_num, line in enumerate(f, 1):
            line = line.rstrip("\n\r")
            if not line or line.startswith("#"):
                continue
            parts = line.split("\t")
            if len(parts) != 2:
                print(f"  [WARN] Line {line_num}: expected 2 tab-separated columns, got {len(parts)}", file=sys.stderr)
                continue
            cases.append({"line": line_num, "input": parts[0], "expected": parts[1]})
    return cases


def token_set_match(a, b):
    return set(a.split()) == set(b.split())


def translate(query, host, port):
    url = f"http://{host}:{port}/api/ai/translate"
    data = json.dumps({"query": query}).encode("utf-8")
    req = urllib.request.Request(url, data=data, headers={"Content-Type": "application/json"})
    try:
        with urllib.request.urlopen(req, timeout=30) as resp:
            result = json.loads(resp.read().decode("utf-8"))
            if result.get("success"):
                return result.get("translated_query", ""), None
            else:
                return "", result.get("error", "Translation failed")
    except urllib.error.URLError as e:
        return "", f"Connection error: {e}"
    except Exception as e:
        return "", str(e)


def check_service(host, port):
    url = f"http://{host}:{port}/api/ai/status"
    try:
        with urllib.request.urlopen(url, timeout=5) as resp:
            result = json.loads(resp.read().decode("utf-8"))
            return result.get("translator_available", False)
    except Exception:
        return False


def run_evaluation(tsv_path, host="127.0.0.1", port=19860, verbose=False):
    cases = load_test_cases(tsv_path)
    if not cases:
        return {"total": 0, "passed": 0, "failed": 0, "accuracy": 0.0, "failures": [], "error": "No test cases loaded"}

    if not check_service(host, port):
        return {"total": len(cases), "passed": 0, "failed": 0, "accuracy": 0.0, "failures": [], "error": f"Service not available at {host}:{port}"}

    passed = 0
    failed = 0
    failures = []

    for case in cases:
        actual, error = translate(case["input"], host, port)
        if error:
            failed += 1
            failures.append({
                "line": case["line"],
                "input": case["input"],
                "expected": case["expected"],
                "actual": "",
                "error": error,
            })
            if verbose:
                print(f"  [ERROR] Line {case['line']} \"{case['input']}\" -> {error}", file=sys.stderr)
        elif token_set_match(actual, case["expected"]):
            passed += 1
            if verbose:
                print(f"  [PASS] \"{case['input']}\" -> \"{actual}\"", file=sys.stderr)
        else:
            failed += 1
            failures.append({
                "line": case["line"],
                "input": case["input"],
                "expected": case["expected"],
                "actual": actual,
                "error": None,
            })
            if verbose:
                print(f"  [FAIL] Line {case['line']} \"{case['input']}\"", file=sys.stderr)
                print(f"         Expected: \"{case['expected']}\"", file=sys.stderr)
                print(f"         Got:      \"{actual}\"", file=sys.stderr)

    total = passed + failed
    accuracy = passed / total if total > 0 else 0.0

    return {
        "total": total,
        "passed": passed,
        "failed": failed,
        "accuracy": round(accuracy, 4),
        "failures": failures,
    }


def main():
    parser = argparse.ArgumentParser(description="Evaluate AI translation accuracy")
    parser.add_argument("tsv_path", help="Path to TSV test cases file")
    parser.add_argument("--host", default="127.0.0.1", help="MacEverything host (default: 127.0.0.1)")
    parser.add_argument("--port", type=int, default=19860, help="MacEverything port (default: 19860)")
    parser.add_argument("--verbose", action="store_true", help="Print per-case results to stderr")
    args = parser.parse_args()

    result = run_evaluation(args.tsv_path, args.host, args.port, args.verbose)
    print(json.dumps(result, ensure_ascii=False, indent=2))

    if "error" in result:
        sys.exit(2)
    elif result["failed"] > 0:
        sys.exit(1)
    else:
        sys.exit(0)


if __name__ == "__main__":
    main()
