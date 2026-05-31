# 180 - 架构师子系统文档集与深度文档校准

- **类型**：docs
- **日期**：2026-05-31
- **分支**：`docs/architecture-refresh`
- **影响文件**：`docs/architecture/`、`docs/tech_sharing_architecture_deep_dive.md`、`docs/changelog/README.md`

## 背景与动机

此前 `docs/tech_sharing_architecture_deep_dive.md` 已承载大量实现细节，但单篇文档过长，架构师需要按主题快速理解现状、边界和改进优先级时，缺少可导航的分册结构。

同时，R100+ 长跑和 R116 数据暴露出更清晰的架构问题：tombstone 已达到 1,043,183（16.08%），普通 flush 仍以 v6 全量 rewrite 为主，rewrite 约 3.2–4.1s；这些问题需要在架构文档中用当前代码事实和数据支撑表达，避免继续沿用过时的“flush 全程持锁”“AI 即语义向量搜索”“MCP 暴露 AI 翻译”等不准确表述。

本次变更是纯文档更新，不修改运行时代码。

## 范围（做什么）

1. **新增 `docs/architecture/` 架构师分册**
   - `README.md`：总体导航、关键心智模型、R116 数据口径与阅读顺序。
   - `service-lifecycle.md`：`ServiceEngine` 启动、冷启动 fallback、FSEvents、内容索引与 HTTP 启动顺序。
   - `search-query-index.md`：普通搜索、QueryParser、ShortQueryCache、trigram/SIMD/full-scan 路由与性能边界。
   - `persistence-wal-compaction.md`：v6 flat index、WAL、flush、compaction、`.sqcache` 与 tombstone 风险。
   - `content-index.md`：全文 trigram inverted index、GUI `infile:` 与 HTTP `/api/search/content` 的差异。
   - `ai-natural-language.md`：自然语言到查询语法的 LLM 翻译层，区分“AI 翻译”和真正 embedding 语义搜索。
   - `bridge-swift-ui.md`：Objective-C++ Bridge、SwiftUI ViewModel、AI Settings sidecar 边界。
   - `http-mcp-daemon-cli.md`：HTTP API、CLI daemon、MCP stdio proxy 的接口边界。
   - `build-test-release.md`：Makefile、Xcode、bundle dylib、DMG、测试与 release 验收链路。
   - `data-backed-roadmap.md`：基于 R116 的架构改进路线图。

2. **回填并校准深度分享文档**
   - 新增“架构师文档导航”，把长文与 `docs/architecture/` 分册互相串联。
   - 将数据口径更新为 R116：total 6,486,890 / live 5,443,707 / tombstones 1,043,183（16.08%）。
   - 校准搜索、内容索引、AI、HTTP/MCP、持久化、发布链路等章节，使其与当前源码和最新报告一致。

3. **修正过时或易误导表述**
   - AI 当前是“自然语言 → 查询语法”的翻译层，不是 embedding/vector semantic search。
   - `IModelBackend` 当前只有 `chat()` / `isAvailable()` / `modelName()`，没有 `embed()`。
   - 当前 `ModelManager` 主路径扫描 Application Support 下本地 `.gguf` 并加载 `LlamaBackend`；不能断言具体 Qwen GGUF 已随 DMG 捆绑。
   - MCP 当前暴露 `search_files`、`search_content`、`recent_files`、`index_status`，不是 AI 翻译工具。
   - `content:` 是普通查询 filter 语法的一部分；GUI 内容搜索入口是 `infile:`，HTTP 内容搜索入口是 `/api/search/content`。
   - v6 `fullRewrite()` 先做 `snapshotForV6()`，离锁序列化/fsync/rename；问题应表述为全量 rewrite 的 IO/页驻留扰动，而不是“flush 全程持写锁”。

## 验证

- 对 `docs/architecture/` 与 `docs/tech_sharing_architecture_deep_dive.md` 执行 markdown/code-fence 完整性检查，结果：`FILES 11 / OK`。
- 对过时短语执行复扫，确认无旧图表 fence、无 fenced HTML、无绝对 worktree 路径，并移除了旧响应字段、旧索引版本、旧 AI 分发和旧 flush 锁语义等阻断项。
- 单独验证 `docs/architecture/README.md` 的 ContentIndex 描述已落到当前实现：trigram inverted index、`ContentFileInfo{contentHash,trigrams,lastModTime}`、`invertedIndex_` / `fileInfos_`。
- 纯文档变更，不涉及 C++/Swift 代码、构建产物或运行时行为，因此不执行 `make dmg` / HTTP runtime 验收。

## 影响范围

- 仅文档与 changelog。
- 无产品行为、索引格式、HTTP API 或 release 产物变更。
- 为后续架构改进提供事实基线：R67/tombstone compaction、v6 fullRewrite IO、page residency、HTTP guardrails、FSEvents ingest observability 等。
