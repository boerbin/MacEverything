# 166 - NL Translation Data-Driven Test

## 概述

为 AI 搜索的自然语言翻译功能创建了数据驱动的端到端测试框架（part 86）。

## 规划

- 需要验证 NLTranslator 通过真实 LLM 翻译的质量
- 测试数据从 TSV 文件加载，格式为 `<原始query>\t<期望翻译结果>`
- 匹配方式：token-set 比较（忽略顺序），因为 `abc ext:h` 和 `ext:h abc` 语义等价
- LLM 不可用时优雅跳过

## 实施

### 新增文件
- `tests/data/nl_translation_cases.tsv` — 8 条初始测试用例（来自 few-shot examples 中含 filter 的用例）
- `tests/test_nl_translation_data.h` — 测试模块，包含：
  - `tokenize()` / `tokenSetMatch()` — 顺序无关的 token 集合比较
  - `loadTestCases()` — TSV 文件解析（支持注释行、空行跳过）
  - `findDataFile()` — 自动定位数据文件
  - `runNLTranslationDataTests()` — 主测试入口

### 修改文件
- `test_all.cpp` — 注册为 part 86，加入 `--fast` 和默认测试集

## 验证

- 编译通过（`make test_all`）
- `./test_all --part 86` 正确加载 8 条用例
- LiteLLM 不可用时输出 `[SKIP]` 而非失败
- 与已有 part 81 测试无冲突
