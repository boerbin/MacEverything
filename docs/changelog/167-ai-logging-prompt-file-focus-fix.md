# 167 — AI 翻译日志 + Prompt 外部加载 + 焦点修复

## AI 翻译日志与耗时记录

NLTranslator 的 `translate()` 方法新增日志输出，记录每次翻译的输入、输出和各阶段耗时：

```
[INFO][AI] translate: "上周下载的PDF" -> "path:Downloads ext:pdf dm:last7days" | build=0.1ms infer=487.3ms clean=0.0ms total=487.5ms
[INFO][AI] translate passthrough: "ext:pdf" (already syntax)
```

四个阶段：
- **build**: prompt 组装（system prompt + few-shot + 用户输入）
- **infer**: LLM 推理
- **clean**: 响应清洗（去 markdown fences、前缀等）
- **total**: 总计

## Prompt 外部文件加载

支持从外部文件加载 NL 翻译的 system prompt 和 few-shot examples，方便迭代调优，无需重新编译。

- 默认路径: `~/Library/Application Support/MacEverything/prompt.txt`
- 文件格式: `<SYSTEM_PROMPT>...</SYSTEM_PROMPT>` + `<FEW_SHOT>user:...\nassistant:...</FEW_SHOT>`
- 文件不存在时自动使用硬编码 prompt
- HTTP API:
  - `GET /api/ai/prompt` — 查看当前 prompt 来源
  - `POST /api/ai/prompt {"path":"..."}` — 切换 prompt 文件（空字符串恢复默认）

## LLM 幻觉修复

- 新增 prompt 规则 9：纯关键词/文件名直接返回，不添加过滤器
- 新增 4 个透传 few-shot 示例（abc, readme, config.json, hello world）
- 修复输入 "abc" 被错误翻译为 `ext:doc;docx` 的问题

## AI 搜索闪烁修复

`performIndexRefresh()` 中 AI 模式不再重复触发 LLM 推理，改用缓存的翻译结果重新执行文件搜索。

## 窗口焦点修复

从最小化恢复或切换到 App 时，搜索框自动获得输入焦点。使用 Notification 直接驱动 NSTextView 的 `makeFirstResponder`，绕过 SwiftUI `@FocusState` 在窗口激活时序中的不可靠性。
