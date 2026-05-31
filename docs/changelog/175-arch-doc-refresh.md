# 175 - 架构深度文档刷新（ShortQueryCache + 内置 LLM + R108 性能/稳定性）

- **类型**：docs
- **日期**：2026-05-31
- **分支**：`docs/architecture-refresh`
- **影响文件**：`docs/tech_sharing_architecture_deep_dive.md`（+456 / −96）、`docs/changelog/readme.md`

## 背景与动机

`docs/tech_sharing_architecture_deep_dive.md` 是面向架构师的深度技术文档，但内容停留在
早期版本：性能数据引用 R29（4.86M 记录、平均 10.5ms），且**完全缺失**两个近期落地的核心子系统——
ShortQueryCache（短查询缓存，changelog 162/165/170）与内置 LLM 自然语言搜索（changelog 164/166–169/171）。
架构师据此无法准确了解现有实现，也无法基于真实瓶颈规划改进。

本次变更不涉及任何代码或行为改动，**纯文档刷新**，目标是让文档与当前 master 实现（R108 长跑观测）严格对齐。

## 范围（做什么）

1. **新增 §9「短查询缓存：ShortQueryCache」**（全新章节）
   - 数据结构：702 key（26 unigram + 676 bigram ASCII），每 key Top-100 `BoundedSortedVec<ScoredResult>`
   - keyIndex 编码、`computeScore = (0<<16)|(quality<<8)|min(fullPathLen,255)`
   - 单遍 `rebuild()` 全量扫描伪代码
   - **增量维护**（`tryInsert`/`eraseRecord` + 查询期 tombstone 跳过兜底）——明确标注此处与原 plan 的
     「deletedCount + 50% 重建」设计已被取代（changelog 165/170）
   - 查询快路径代码（`SearchEngineQuery.cpp:248-268`，`searchPath="short-query-cache"` 0.37ms）
   - `.sqcache` 文件格式（magic `0x56435153` "SQCS"，version 2）
   - 收益对比表 + ```dot 生命周期图

2. **新增 §14「内置 LLM 自然语言搜索」**（全新章节）
   - ```dot AI 栈分层图
   - `IModelBackend` 接口；`LlamaBackend`（vendored llama.cpp + Qwen2.5-0.5B GGUF，Metal n_gpu_layers=99，n_ctx=2048）；
     `LiteLLMBackend`（远程 OpenAI 兼容，手写 JSON，5s/30s 超时）；`ModelManager`（扫描 *.gguf、loadAsync、prefer "qwen"）
   - `NLTranslator` 翻译管线 ```dot 图：`looksLikeQuerySyntax()` 语法透传 → system+few-shot → `chat()` temp=0 贪心 →
     `cleanLLMResponse()` 去围栏/前缀/引号/取首行
   - 集成路径：ServiceEngine 异步初始化、HttpServer `/api/ai/{status,prompt,translate}`、MCP

3. **刷新性能数据（R29 → R108）**
   - §1 核心指标：数据集 4.86M → 6.18M（总 6,183,351 / 存活 5,412,386）；延迟从「平均 10.5ms」改为按路径分层
     （1-2 字符 0.4ms 缓存 / 常规词 ~10-20ms trigram）
   - §5 搜索路径延迟表全部替换为 R108 实测值，决策树改写为 ```dot 并加入 IsShort→short-query-cache 快路径优先级
   - §15 性能演进：旧图标注为「历史轨迹」，新增「运行时稳定性观测（R100+ 长跑）」子节

4. **运行时稳定性 P0 议题（架构师重点）**
   - **R67**：tombstone 单调增长至 12.47%（R108），压缩比阀门 25% 因崩溃参考 ~12.97% 而不可达 →
     full compaction 永不自动触发 → flush 重写膨胀（每次 flush 重写 771K 死记录）
   - **Flush 写锁**：flush 持写锁 3-30s
   - 写入 §15 根因分析与 §17 已知问题表（P0）

5. **附录刷新**
   - 附录 A：新增 kTotalKeys 702、kMaxResults 100、.sqcache magic/version、n_gpu_layers 99、n_ctx 2048、
     NLTranslator temp 0.0、LiteLLM 超时 5s/30s 等常量
   - 附录 B：Core 目录树补齐 ShortQueryCache/BoundedSortedVec/AI 栈全部文件；
     `SearchEngineAdvancedQuery.cpp` 标注「⚠ 1091 行，超规范待拆」；App 增 AIServiceClient/AISettingsView，
     CLI 增 mcp_main.cpp，新增 ai_service/、prompt.txt、vendor/llama.cpp/
   - 附录 C：HTTP API 增补 `/api/ai/{status,prompt,translate}` 三个端点示例

## 章节编号变更

文档从 16 节扩为 18 节 + 附录 A/B/C。新增 §9（ShortQueryCache）与 §14（AI）后，
持久化→10、FSEvents→11、并发→12、内容搜索→13、性能演进→15、竞品→16、设计得失→17、演化方向→18。
目录与正文标题、所有 ```dot 新增图表（§5 决策树、§9 生命周期、§14 AI 栈/翻译管线）已校验一致；
既有 ```graphviz 图表按「仅新增图用 dot」原则保留原样。

## 验证

- 文档结构核对：18 节 + 附录 A/B/C，标题与目录编号一一对应（`grep '^## '` 校验通过）
- 新增图表统一使用 ```dot（行 340/741/1190/1260），无遗漏 ```graphviz 误用
- 所有代码片段、常量、R108 指标均从 master 源码（`ShortQueryCache.cpp`、`LiteLLMBackend.cpp`、
  `SearchEngineQuery.cpp`、`SearchEngineV6.cpp`）与 `R_2605302211.md` 取证，未臆测
- 纯文档变更，不涉及 C++/Swift 代码、构建产物或运行时行为，故无需 `make dmg` / HTTP 功能验收

## 备注

- 本变更明确记录了 plan→实现的演进差异（ShortQueryCache 增量维护取代 deletedCount/50% 重建），
  避免后人误读历史 plan 为现状。
- R67 与 flush 重写膨胀被列为架构师 P0 改进目标，配套 §18 短期路线图（R67 修复、COW flush、P24、大文件拆分）。
