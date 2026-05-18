# 169 - 共享 AI 翻译评估脚本

## 概述

创建了公共的 Python 评估脚本 `benchmarks/eval_ai_translation.py`，统一 AI 翻译准确率的评估逻辑。C++ 测试和 autoloop evaluator 都调用此脚本。

## 规划

- 评估逻辑分散在 C++ test（直接调用 LiteLLMBackend）和 autoloop evaluator（需要 HTTP 调用）两处
- 统一为一个 Python 脚本，通过 HTTP 接口评估，输出结构化 JSON

## 实施

### 新增文件
- `benchmarks/eval_ai_translation.py` — 核心评估脚本
  - 加载 TSV 测试数据
  - 调用 `POST /api/ai/translate` HTTP 接口
  - Token-set 匹配（顺序无关比较）
  - 输出 JSON：`{total, passed, failed, accuracy, failures}`
  - CLI: `python3 eval_ai_translation.py <tsv> [--host] [--port] [--verbose]`

### 修改文件
- `tests/test_nl_translation_data.h` — 重构为调用 Python 脚本
  - 通过 `popen()` 执行脚本
  - 解析 JSON 输出更新全局 passed/failed 计数器
  - 服务不可用时输出 `[SKIP]`

## 使用方式

```bash
# 直接评估
python3 benchmarks/eval_ai_translation.py tests/data/nl_translation_cases.tsv --verbose

# 在 autoloop evaluator 中
python3 /path/to/benchmarks/eval_ai_translation.py /path/to/nl_translation_cases.tsv

# C++ test (part 86)
./test_all --part 86
```

## 验证

- Python 脚本成功加载 TSV、调用 HTTP API、token-set 匹配、输出 JSON
- C++ test 编译通过（12,025 tests passed, 0 failed）
- 服务不可用时正确跳过
