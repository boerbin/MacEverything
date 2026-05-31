# MacEverything 架构总览

> 面向架构师、长期维护者和新子系统 owner 的入口文档。
> 证据引用格式统一为“相对仓库根路径 + 行号锚点”，例如 `MacEverything/Core/ServiceEngine.cpp:141-197`。

## 1. 文档目的

本目录用于回答四类架构问题：

1. **系统由哪些运行面组成**：SwiftUI App、Objective-C++ Bridge、C++ Core、HTTP、CLI Daemon、MCP。
2. **核心状态由谁拥有**：`ServiceEngine` 统一编排生命周期，`SearchEngine` 管理文件名/路径索引，`ContentIndex` 管理全文索引。
3. **变更应该落在哪一层**：UI 状态、桥接 DTO、查询语言、索引结构、持久化格式、外部 API 各有明确边界。
4. **当前架构风险在哪里**：缓存启动一致性、v6 全量 flush、tombstone compaction 阈值、内容搜索一致性、HTTP/MCP 外部面。

架构事实以代码为准，而不是历史 changelog 或旧文档为准。当前重要事实包括：

- AI 模式是“自然语言转确定性查询语法”，不是向量语义搜索：`MacEverything/App/SearchViewModel.swift:310-388`、`MacEverything/Bridge/MacSearchBridge+Semantic.mm:8-44`。
- `infile:` 走全文内容搜索；普通查询里的 `content:` 目前不等价于 `ContentIndex` 路由：`MacEverything/App/SearchViewModel.swift:284-304`、`MacEverything/Core/QueryTokenizer.h:163-181`、`MacEverything/Core/SearchEngineAdvancedQuery.cpp:128-253`。
- 增量缓存启动会先暴露可搜索索引，再后台 replay / sync：`MacEverything/Core/ServiceEngine.cpp:221-289`、`MacEverything/Core/ServiceEngine.cpp:348-400`。
- 当前 `IndexPersistence::flush()` 交换 WAL 后执行 v6 flat full rewrite，不是 dirty-page 增量 flush：`MacEverything/Core/IndexPersistence.cpp:165-188`。
- HTTP 绑定 loopback，但 admin/prompt/rebuild 类端点当前没有本地鉴权：`MacEverything/Core/HttpServer.cpp:102-106`、`MacEverything/Core/HttpServer.cpp:339-348`。

## 2. 一句话拓扑

MacEverything 是一个本地优先的 macOS 搜索系统：SwiftUI 负责交互，Objective-C++ Bridge 负责 Swift/C++ 边界，C++20 Core 负责扫描、查询、索引、持久化和 FSEvents，HTTP/MCP/Daemon 复用同一个 `ServiceEngine` 能力面。

## 3. 架构拓扑图

<style>
.arch-readme{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;color:#111827;line-height:1.25;margin:8px 0 14px 0;page-break-inside:avoid}
.arch-wrap{border:1px solid #cbd5e1;border-radius:10px;background:#f8fafc;padding:12px;max-width:1180px}
.arch-title{font-weight:700;font-size:15px;margin-bottom:8px;color:#0f172a}
.arch-grid{display:grid;grid-template-columns:1.05fr 1.35fr 1.05fr;gap:10px}
.arch-col{display:flex;flex-direction:column;gap:8px}
.arch-layer{border:1px solid #cbd5e1;border-radius:8px;background:white;padding:8px}
.arch-layer.user{border-color:#93c5fd;background:#eff6ff}
.arch-layer.bridge{border-color:#c4b5fd;background:#f5f3ff}
.arch-layer.core{border-color:#86efac;background:#f0fdf4}
.arch-layer.data{border-color:#fcd34d;background:#fffbeb}
.arch-layer.external{border-color:#fca5a5;background:#fef2f2}
.arch-layer.ops{border-color:#94a3b8;background:#f8fafc}
.arch-h{font-size:11px;text-transform:uppercase;letter-spacing:.04em;font-weight:700;color:#334155;margin-bottom:6px}
.arch-box{border:1px solid rgba(15,23,42,.16);border-radius:6px;background:rgba(255,255,255,.78);padding:6px;margin:5px 0;font-size:12px}
.arch-box strong{display:block;font-size:12px;color:#0f172a;margin-bottom:2px}
.arch-box small{display:block;font-size:10px;color:#475569}
.arch-flow{font-size:11px;text-align:center;color:#64748b;margin:2px 0}
.arch-note{font-size:10px;color:#475569;margin-top:8px}
@media print{.arch-wrap{break-inside:avoid;background:white}.arch-box{padding:4px}.arch-grid{gap:6px}.arch-layer{padding:6px}}
</style>
<div class="arch-readme">
<div class="arch-wrap">
<div class="arch-title">MacEverything 运行时拓扑：UI / Bridge / Core / Persistence / External Surfaces</div>
<div class="arch-grid">
<div class="arch-col">
<div class="arch-layer user">
<div class="arch-h">User & App Surface</div>
<div class="arch-box"><strong>SwiftUI App</strong><small>SearchViewModel, ContentView, Settings, hotkey, menu commands</small></div>
<div class="arch-box"><strong>AppDelegate</strong><small>early engine start, termination safety net, status bar</small></div>
<div class="arch-box"><strong>AI / Content UI</strong><small>AI toggle, infile: route, content settings</small></div>
</div>
<div class="arch-layer external">
<div class="arch-h">External Clients</div>
<div class="arch-box"><strong>HTTP Clients</strong><small>127.0.0.1:19860 search/status/admin/AI routes</small></div>
<div class="arch-box"><strong>MCP Clients</strong><small>Claude Code / Cursor / Claude Desktop via stdio JSON-RPC</small></div>
<div class="arch-box"><strong>CLI Daemon</strong><small>headless ServiceEngine + HTTP lifecycle</small></div>
</div>
</div>
<div class="arch-col">
<div class="arch-layer bridge">
<div class="arch-h">Objective-C++ Boundary</div>
<div class="arch-box"><strong>MacSearchBridge</strong><small>singleton facade, DTO marshaling, main-queue callback hop</small></div>
<div class="arch-flow">Swift value models &harr; Objective-C DTO &harr; C++ records</div>
</div>
<div class="arch-layer core">
<div class="arch-h">Core Orchestration</div>
<div class="arch-box"><strong>ServiceEngine</strong><small>startup, shutdown, scanner, FSEvents, HTTP, persistence, callbacks</small></div>
<div class="arch-box"><strong>DirectoryScanner + FileSystemWatcher</strong><small>cold scan, cached replay, live FSEvents, subtree rescan</small></div>
<div class="arch-box"><strong>SearchEngine</strong><small>SoA file records, query AST, trigram/path/ext indices, short-query cache</small></div>
<div class="arch-box"><strong>ContentIndex</strong><small>trigram full-text candidate index, snippets, rich text extraction</small></div>
<div class="arch-box"><strong>NLTranslator + ModelManager</strong><small>local GGUF Llama backend translates NL to query syntax</small></div>
</div>
</div>
<div class="arch-col">
<div class="arch-layer data">
<div class="arch-h">Persistence & State</div>
<div class="arch-box"><strong>index.v6 + index WAL</strong><small>flat SoA snapshot, WAL1 replay, tombstones, full rewrite flush</small></div>
<div class="arch-box"><strong>content_index.bin + content WAL</strong><small>MECI/CWL1 content state, event-driven compaction</small></div>
<div class="arch-box"><strong>short query cache</strong><small>1-2 char ASCII precomputed hot path</small></div>
</div>
<div class="arch-layer ops">
<div class="arch-h">Operations</div>
<div class="arch-box"><strong>Build & Release</strong><small>Xcode app, Makefile tests, dylib bundling, DMG verification</small></div>
<div class="arch-box"><strong>Tests & Benchmarks</strong><small>test_all fast/slow/bench, FSEvents, MCP, content, WAL, parser</small></div>
<div class="arch-box"><strong>Performance Reports</strong><small>tombstone ratio, flush rewrite, page residency, query classes</small></div>
</div>
</div>
</div>
<div class="arch-note">Primary flow: SwiftUI input -> MacSearchBridge -> ServiceEngine -> SearchEngine / ContentIndex / NLTranslator -> persistence and callbacks; HTTP/MCP/Daemon reuse the same core services rather than owning duplicate indices.</div>
</div>
</div>

## 4. 运行时拓扑说明

### 4.1 UI 层

SwiftUI 应用入口安装 `AppDelegate`、创建 `ContentView`，并暴露搜索选项、重建、设置、AI、MCP 菜单动作：`MacEverything/App/MacEverythingApp.swift:3-64`。

`AppDelegate` 在 UI view model 之前启动日志和 C++ 引擎，这解释了 Bridge 里存在“早启动 + 后绑定回调”的状态处理：`MacEverything/App/AppDelegate.swift:11-28`、`MacEverything/Bridge/MacSearchBridge.mm:198-252`。

`SearchViewModel` 是 SwiftUI 状态协调器，集中管理扫描、搜索、内容索引、AI、分页、历史、节流和重建：`MacEverything/App/SearchViewModel.swift:23-76`、`MacEverything/App/SearchViewModel.swift:105-230`。

### 4.2 Bridge 层

`MacSearchBridge` 是 Swift 和 C++ Core 的唯一主要运行时边界，持有 `std::shared_ptr<ServiceEngine>` 和启动状态：`MacEverything/Bridge/MacSearchBridge_Internal.h:7-15`。

Bridge DTO 包括 `MEFileResult`、`MEContentResult`、`MEHighlightHint`，用于把 C++ record/snippet/highlight 信息暴露给 Swift：`MacEverything/Bridge/MacSearchBridge.h:5-43`。

Bridge 回调统一切回主队列再触达 Swift/UI 闭包，这是 UI 线程安全边界：`MacEverything/Bridge/MacSearchBridge.mm:147-185`。

普通文件查询执行 `NSString` 到 `std::string` 转换，调用 `engine->query(...)`，再映射回 Objective-C DTO；无效 UTF-8 会被跳过：`MacEverything/Bridge/MacSearchBridge.mm:411-432`。

### 4.3 Core 生命周期层

`ServiceEngine` 是纯 C++ 生命周期编排器，对外核心 API 是 `startFullScan`、`startIncremental`、`shutdown`：`MacEverything/Core/ServiceEngine.h:31-53`。

`ServiceEngine` 持有 `SearchEngine`、`FileSystemWatcher`、`ContentIndex`、模型/NL translator、持久化对象、HTTP server 和实例锁：`MacEverything/Core/ServiceEngine.h:124-133`。

冷启动 full scan 会停止 monitoring、后台扫描目录、构建新 `SearchEngine`、切换引擎、触发 completion、启动 monitoring 和内容索引：`MacEverything/Core/ServiceEngine.cpp:141-197`。

增量缓存启动优先加载持久化索引，若存在可用 `lastEventId` 和 records，会先切入 cached engine，再启动 HTTP、WAL/content 挂接、completion、phase-2 索引和后台同步：`MacEverything/Core/ServiceEngine.cpp:221-289`。

无可用 cache 时，`startIncremental` 会走 full scan fallback，然后创建持久化、挂接 WAL/content、启动 auto-compaction、写 metadata、启动 HTTP：`MacEverything/Core/ServiceEngine.cpp:292-322`。

shutdown 先停 HTTP，再取消内容索引、等待后台任务、捕获 last FSEvent id、停止 monitoring/auto-compaction、强制 compaction、释放 GCD 对象：`MacEverything/Core/ServiceEngine.cpp:537-590`。

### 4.4 Search / Query / Index 层

`SearchEngine` 使用 SoA 存储 records：name/path string pools，加 type/size/time/inode/devId 等列，并由 `std::shared_mutex` 保护：`MacEverything/Core/SearchEngine.h:379-396`。

主要索引包括 filename trigram、path trigram、path-index-to-records 反向映射、extension index、short-query cache 和 recent cache：`MacEverything/Core/SearchEngine.h:398-406`、`MacEverything/Core/SearchEngine.h:510-534`。

`SearchEngine::query()` 会预处理 query、获取 session generation、解析 slash structured query、特殊处理 `DIR_LIST`、命中 short-query cache，最后进入 unified `queryAdvanced()`：`MacEverything/Core/SearchEngineQuery.cpp:151-174`、`MacEverything/Core/SearchEngineQuery.cpp:186-271`。

`queryAdvanced()` 负责 AST parse/transform、预编译 filters/globs/regex、选择候选源、评估 records、释放锁后排序/limit：`MacEverything/Core/SearchEngineAdvancedQuery.cpp:608-649`、`MacEverything/Core/SearchEngineAdvancedQuery.cpp:651-788`、`MacEverything/Core/SearchEngineAdvancedQuery.cpp:1025-1079`。

短查询缓存只覆盖 lowercase ASCII 1-2 字符 key，最多 26 + 676 个预计算 key，每个保留 top 100：`MacEverything/Core/ShortQueryCache.h:40-43`、`MacEverything/Core/ShortQueryCache.cpp:6-19`。

### 4.5 Content Index 层

全文搜索是独立的 trigram inverted index，不是向量检索。`ContentIndex` 的结果结构只携带 `fileIndex/snippet/matchOffset`，文件内容状态保存在 `ContentFileInfo{contentHash,trigrams,lastModTime}`，核心索引是 `unordered_map<Trigram, vector<uint32_t>> invertedIndex_` 与 `unordered_map<uint32_t, ContentFileInfo> fileInfos_`：`MacEverything/Core/ContentIndex.h:27-38`、`MacEverything/Core/ContentIndex.h:132-136`。

UI 只有 `infile:` 前缀进入内容搜索分支，提取关键词后 debounce 300ms，调用 `bridge.queryContent(keyword, maxResults: 200)`：`MacEverything/App/SearchViewModel.swift:284-304`、`MacEverything/App/SearchViewModel.swift:392-398`。

`ContentIndex::query()` 小写化关键词、要求长度至少 3、抽取 query trigrams、相交 posting lists，返回候选 file indices：`MacEverything/Core/ContentIndex.cpp:568-647`。

GUI Bridge 会解析候选文件路径、并行生成 snippet，并过滤掉 snippet 查找失败的候选；这是 GUI 精确匹配验证步骤：`MacEverything/Bridge/MacSearchBridge+Content.mm:8-81`。

HTTP `/api/search/content` 当前直接序列化 `ContentIndex::query()` 的结果，存在和 GUI snippet 验证语义不完全一致的风险：`MacEverything/Core/HttpServer.cpp:432-478`。

### 4.6 AI / NL Translator 层

当前 AI 模式不是“向量语义检索”，而是 NL translator 把自然语言转成确定性文件名/路径查询语法，再调用普通搜索：`MacEverything/App/SearchViewModel.swift:310-388`。

`MacSearchBridge+Semantic.mm` 文件名仍保留 “Semantic”，但实际行为是获取 `safeNLTranslator` 并调用 `nlTranslator->translate`：`MacEverything/Bridge/MacSearchBridge+Semantic.mm:8-44`。

`ModelManager` 当前只实例化 `LlamaBackend`、扫描 `.gguf`、并偏好文件名包含 `qwen` 的模型：`MacEverything/Core/ModelManager.cpp:12-72`。

`LiteLLMBackend` 仍存在远程 chat/embedding 代码，但不是当前 `ModelManager` 选择路径：`MacEverything/Core/LiteLLMBackend.h:17-18`、`MacEverything/Core/LiteLLMBackend.cpp:146-170`。

Swift `AIServiceClient` 仍默认指向 legacy 19861 服务，而实际内置 HTTP AI 路由在 19860，形成设置面与搜索面不一致：`MacEverything/App/AIServiceClient.swift:40-105`、`MacEverything/Core/HttpServer.cpp:333-348`。

### 4.7 Persistence / WAL 层

index WAL 使用 `WAL1` header，记录 Add/Remove/Update，默认 64 entries fsync，最大 50MB：`MacEverything/Core/IndexWAL.h:9`、`MacEverything/Core/IndexWAL.h:71-74`、`MacEverything/Core/IndexWAL.cpp:135-166`。

v6 flat index 是单文件 `index.v6`：64-byte header、section table、11 个 SoA sections、metadata KV：`MacEverything/Core/FlatIndexWriter.h:8-14`、`MacEverything/Core/FlatIndexWriter.h:39-50`、`MacEverything/Core/FlatIndexWriter.cpp:201-287`。

启动加载顺序是 v6 flat、paged v5、legacy v3，后两者会迁移到 v6，然后 replay WAL：`MacEverything/Core/IndexPersistence.cpp:33-92`。

非强制 flush 会在没有 WAL、WAL 不 dirty、WAL entries 小于 100 时跳过；强制 flush 只在 header-only WAL 时跳过：`MacEverything/Core/IndexPersistence.h:36-37`、`MacEverything/Core/IndexPersistence.cpp:119-147`。

tombstone ratio 大于 25% 才触发 full compaction；近期性能证据显示这使 compaction 在高 tombstone 但低于 25% 的区间长期不可达：`MacEverything/Core/IndexPersistence.h:70-71`、`MacEverything/Core/IndexPersistence.cpp:151-162`。

### 4.8 FSEvents / 实时同步层

`FileSystemWatcher` 使用 file-level events、`NoDefer`、CF path types、`IgnoreSelf`、300ms coalescing latency、可选排除路径和 serial dispatch queue：`MacEverything/Core/FileSystemWatcher.cpp:51-83`。

后台 sync 会从持久化 event id 创建 replay watcher，等待最多 10s 的 `HistoryDone`；超时或 journal truncation 时 fallback 到 full background scan：`MacEverything/Core/ServiceEngine.cpp:348-400`。

live monitoring 会排除 app cache path、过滤 app bundle 内部路径、把 remove/update 转为 `SearchEngine::MutationOp`、更新 content index、batch mutate records、调度 subtree rescan：`MacEverything/Core/ServiceEngine+FSEvents.cpp:91-198`。

subtree rescan 使用 serial mutation queue debounce；rescan 后 `batchRescanPrefix`，必要时 compact tombstones、remap content indices 并触发 `onIndexChanged`：`MacEverything/Core/ServiceEngine+FSEvents.cpp:229-352`。

需要特别注意：由于 `IgnoreSelf`，同进程写入不会作为普通 live events 被观察到；相关测试用 child process 规避：`MacEverything/Core/FileSystemWatcher.cpp:58-62`、`tests/test_fsevents_search_latency.h:67-70`。

### 4.9 HTTP / Daemon / MCP 外部面

HTTP 由 `ServiceConfig.httpPort` 控制，GUI 默认设置 19860 并在索引可用后自动启动：`MacEverything/Core/ServiceEngine.h:21-28`、`MacEverything/Bridge/MacSearchBridge.mm:103-118`、`MacEverything/Core/ServiceEngine.cpp:254-258`。

HTTP server 只绑定 `127.0.0.1`，但 accept loop 是单线程同步处理连接：`MacEverything/Core/HttpServer.cpp:102-106`、`MacEverything/Core/HttpServer.cpp:142-145`、`MacEverything/Core/HttpServer.cpp:184-201`。

主要 HTTP routes 包括 `/api/search`、`/api/search/content`、`/api/recent`、`/api/status`、`/api/health`、content config/rebuild、index rebuild、AI status/translate/prompt：`MacEverything/Core/HttpServer.cpp:321-348`。

CLI daemon 默认 port 19860、root `/`，使用 `PathUtils` 的 cache/log，启动 `ServiceEngine` 后显式启动 HTTP，并用 `dispatch_main()` 维持生命周期：`MacEverything/CLI/daemon_main.cpp:27-69`、`MacEverything/CLI/daemon_main.cpp:181-194`。

MCP 是 stdio JSON-RPC proxy 到本机 HTTP，暴露 `search_files`、`search_content`、`recent_files`、`index_status`：`MacEverything/CLI/mcp_main.cpp:23-30`、`MacEverything/CLI/mcp_main.cpp:313-366`。

MCP 当前处理 `initialize`、`ping`、`tools/list`、`tools/call`；batch requests 被显式忽略：`MacEverything/CLI/mcp_main.cpp:387-447`、`MacEverything/CLI/mcp_main.cpp:486-521`。

### 4.10 Build / Test / Release 层

Core test/CLI 构建使用 `clang++`、C++20、macOS frameworks、Homebrew RE2、sqlite3 和 vendored llama.cpp static libs：`Makefile:1-16`。

`test_all` 链接 `test_all.cpp`、全部 `MacEverything/Core/*.cpp` 和编译后的 `.mm` objects：`Makefile:27`。

主 App 构建由 Xcode Release scheme 负责，`make dmg` 执行 build、dylib bundling、自包含校验、DMG 创建：`Makefile:67-87`、`CLAUDE.md:20-31`。

Xcode 有 `MacEverything` app、`MacEverythingUITests`、`MacEverythingMCP` 三个 native targets：`MacEverything.xcodeproj/project.pbxproj:419-470`。

bundler 会扫描 `Contents/MacOS` 下每个 Mach-O，因此 app 内复制的 `MacEverythingMCP` 也进入 dylib/rpath/signing 处理：`scripts/bundle-dylibs.sh:71-76`、`scripts/bundle-dylibs.sh:113-155`。

verify script 会扫描 `Contents/MacOS` 和 `Contents/Frameworks`，发现 `/opt/homebrew`、`/usr/local`、`/opt/local` 链接则失败：`scripts/verify-bundle.sh:22-58`。

## 5. 推荐阅读路径

| 顺序 | 读什么 | 目的 | 关键证据 |
|---:|---|---|---|
| 1 | `service-lifecycle.md` | 先建立启动、增量同步、FSEvents、shutdown 心智模型 | `MacEverything/Core/ServiceEngine.cpp:141-197`、`MacEverything/Core/ServiceEngine.cpp:204-335`、`MacEverything/Core/ServiceEngine.cpp:537-590` |
| 2 | `search-query-index.md` | 理解查询语法、AST、候选源、索引和 cancellation | `MacEverything/Core/SearchEngineQuery.cpp:151-271`、`MacEverything/Core/SearchEngineAdvancedQuery.cpp:608-1079` |
| 3 | `persistence-wal-compaction.md` | 理解 v6/WAL/flush/compaction/tombstone 风险 | `MacEverything/Core/IndexPersistence.cpp:119-188`、`MacEverything/Core/IndexPersistence.cpp:224-275` |
| 4 | `content-index.md` | 区分 `infile:` 内容搜索和普通文件名搜索 | `MacEverything/Core/ContentIndex.h:27-32`、`MacEverything/Core/ContentIndex.cpp:568-647` |
| 5 | `ai-natural-language.md` | 校正 AI 搜索定位：NL-to-query，不是向量语义检索 | `MacEverything/Core/ModelManager.cpp:12-72`、`MacEverything/Bridge/MacSearchBridge+Semantic.mm:8-44` |
| 6 | `bridge-swift-ui.md` | 理解 SwiftUI 状态、Bridge DTO、主线程回调、节流分页 | `MacEverything/Bridge/MacSearchBridge.mm:147-185`、`MacEverything/App/SearchViewModel.swift:248-371` |
| 7 | `http-mcp-daemon-cli.md` | 理解 HTTP route、daemon、MCP proxy 和外部面风险 | `MacEverything/Core/HttpServer.cpp:321-348`、`MacEverything/CLI/mcp_main.cpp:313-366` |
| 8 | `build-test-release.md` | 理解测试入口、Makefile、Xcode targets、DMG/dylib 约束 | `Makefile:55-87`、`scripts/bundle-dylibs.sh:71-157` |
| 9 | `data-backed-roadmap.md` | 用性能报告决定架构优先级，而不是凭感觉优化 | `MacEverything/Core/IndexPersistence.h:70-72`、`MacEverything/Core/IndexPersistence.cpp:165-188` |

建议第一次阅读时按顺序读，不要从 UI 或 HTTP route 直接跳到某个 bug。多数行为差异来自 `ServiceEngine` 生命周期和持久化状态，而不是单个 endpoint 或 view model。

## 6. 子系统 ownership

| 子系统 | 代码 owner 边界 | 主要职责 | 变更入口 | 不变量 / 风险 |
|---|---|---|---|---|
| App Shell | `MacEverything/App/MacEverythingApp.swift`、`MacEverything/App/AppDelegate.swift` | app 生命周期、菜单、status bar、hotkey | UI 菜单、启动、终止 | AppDelegate 会早于 view model 启动 engine：`MacEverything/App/AppDelegate.swift:11-28` |
| Search State | `MacEverything/App/SearchViewModel.swift` | 搜索状态、节流、分页、AI/content 路由、重建 | 查询行为、UI 状态机 | 文件搜索一次 marshal 最多 10000 结果后 UI 分页：`MacEverything/App/SearchViewModel.swift:337-371` |
| Bridge | `MacEverything/Bridge/MacSearchBridge.*` | Swift/C++ 边界、DTO、回调线程、HTTP admin wiring | Swift 调 C++、DTO 字段变化 | 回调必须 main queue hop：`MacEverything/Bridge/MacSearchBridge.mm:147-185` |
| Service Lifecycle | `MacEverything/Core/ServiceEngine.*` | full scan、incremental、FSEvents、content lifecycle、HTTP、shutdown | 任何跨子系统生命周期变更 | cached startup 先可搜再同步：`MacEverything/Core/ServiceEngine.cpp:249-264` |
| Scanner | `MacEverything/Core/DirectoryScanner.*` | 多线程遍历、record 生成、cross-device/.app 策略 | 扫盘范围、文件类型采集 | worker 数 4-32；`.app` 不递归：`MacEverything/Core/DirectoryScanner.cpp:42-67`、`MacEverything/Core/DirectoryScanner.cpp:259-298` |
| FSEvents | `MacEverything/Core/FileSystemWatcher.*`、`ServiceEngine+FSEvents.cpp` | replay/live events、batch mutation、subtree rescan | 实时同步、延迟、过滤 | 300ms coalescing + IgnoreSelf：`MacEverything/Core/FileSystemWatcher.cpp:51-83` |
| File Search | `MacEverything/Core/SearchEngine*` | record SoA、query parser、indices、ranking、cancel | 查询语言、性能路径 | advanced query 持 shared lock 评估，释放后排序：`MacEverything/Core/SearchEngineAdvancedQuery.cpp:651-653`、`MacEverything/Core/SearchEngineAdvancedQuery.cpp:1025-1028` |
| Query Syntax | `QueryTokenizer`、`QueryParser`、`QueryFilterParser` | terms、filters、modifiers、macros、slash syntax | 新 filter / modifier | `content:` token 存在但不是 ContentIndex route：`MacEverything/Core/QueryTokenizer.h:163-181` |
| Content Search | `ContentIndex`、`ServiceEngine+Content`、Bridge Content | `infile:` trigram 内容索引、snippet、rich text | 全文索引、内容设置 | GUI 做 snippet 验证，HTTP 语义需对齐：`MacEverything/Bridge/MacSearchBridge+Content.mm:51-62`、`MacEverything/Core/HttpServer.cpp:456-472` |
| AI Translation | `ModelManager`、`NLTranslator`、`LlamaBackend` | 本地 GGUF 模型加载、自然语言转 query | AI prompt、模型后端 | 当前只选 LlamaBackend：`MacEverything/Core/ModelManager.cpp:12-72` |
| Persistence | `IndexPersistence`、`IndexWAL`、`FlatIndexWriter` | v6 load/save、WAL replay、flush、compaction | 数据格式、crash recovery | flush 当前 full rewrite v6：`MacEverything/Core/IndexPersistence.cpp:165-188` |
| Content Persistence | `ContentIndexPersistence` | MECI/CWL1、content WAL、content compaction | 内容索引恢复/压缩 | WAL swap failure path 需审查：`MacEverything/Core/ContentIndexPersistence.cpp:326-355` |
| HTTP | `MacEverything/Core/HttpServer.cpp` | loopback REST-ish API、AI route、admin route | MCP/daemon/API client | 单线程同步处理；admin 无鉴权：`MacEverything/Core/HttpServer.cpp:184-201`、`MacEverything/Core/HttpServer.cpp:339-348` |
| Daemon | `MacEverything/CLI/daemon_main.cpp` | headless engine、HTTP、signals | 后台服务部署 | standalone daemon 不在 DMG app-copy path：`Makefile:33-36` |
| MCP | `MacEverything/CLI/mcp_main.cpp`、`MCPConfigManager.swift` | stdio JSON-RPC proxy、client config | AI 工具集成 | 当前不支持 batch：`MacEverything/CLI/mcp_main.cpp:504-510` |
| Build/Release | `Makefile`、Xcode project、scripts | test/build/dmg/dylib bundle/verify | 发布流程 | packaging 必须 bundle Homebrew dylibs：`scripts/bundle-dylibs.sh:8-14` |

## 7. 核心数据流

### 7.1 GUI 冷启动 / 增量启动

1. `AppDelegate` 调用 `MacSearchBridge.shared().startEngine()`：`MacEverything/App/AppDelegate.swift:11-13`。
2. Bridge 创建带默认 root/cache/log/http 配置的 `ServiceEngine`：`MacEverything/Bridge/MacSearchBridge.mm:66-115`。
3. UI view model 稍后调用 `startIncremental()` 安装 scan/content/index/load-error callbacks：`MacEverything/App/SearchViewModel.swift:143-208`。
4. 若已有 v6 cache，Core 先暴露 cached `SearchEngine`，再 background sync：`MacEverything/Core/ServiceEngine.cpp:221-289`。
5. 若无 cache，Core fallback 到 full scan，再建立 persistence 和 HTTP：`MacEverything/Core/ServiceEngine.cpp:292-322`。

### 7.2 普通文件搜索

1. Swift 输入走 debounce，非 `infile:`、非 AI 或 AI translation 后调用 `performSearch`：`MacEverything/App/SearchViewModel.swift:248-371`。
2. Bridge 调 `engine->query(...)` 并映射 `SearchResult` 到 `MEFileResult`：`MacEverything/Bridge/MacSearchBridge.mm:411-432`。
3. Core 解析 query，可能走 directory listing、short-query cache 或 advanced query：`MacEverything/Core/SearchEngineQuery.cpp:197-271`。
4. advanced query 选择 name trigram、path trigram、extension 或 metadata-only scan 等候选路径：`MacEverything/Core/SearchEngineAdvancedQuery.cpp:651-788`。
5. UI 只展示首批 100，后续 `loadMore()` 仅切片 cached bridge results：`MacEverything/App/SearchViewModel.swift:423-457`。

### 7.3 内容搜索

1. UI 检测 `infile:` 前缀并绕过 AI：`MacEverything/App/SearchViewModel.swift:284-304`。
2. Bridge 调 `ContentIndex::query()` 获取候选 file indices：`MacEverything/Bridge/MacSearchBridge+Content.mm:8-29`。
3. Bridge 用 `SearchEngine` 解析路径，重新读文件生成 snippet，并过滤不匹配候选：`MacEverything/Bridge/MacSearchBridge+Content.mm:41-62`。
4. Swift 渲染 `ContentFileItem` 和 snippet highlight：`MacEverything/App/ContentResultRow.swift:10-33`。

### 7.4 FSEvents 实时更新

1. live watcher 收到 events，ServiceEngine 过滤 app bundle/cache/system 噪声：`MacEverything/Core/ServiceEngine+FSEvents.cpp:91-121`。
2. remove/update 转成 mutation ops，并可能触发 content index 更新：`MacEverything/Core/ServiceEngine+FSEvents.cpp:122-183`。
3. `SearchEngine::batchMutate()` 应用变更，必要时 subtree rescan：`MacEverything/Core/ServiceEngine+FSEvents.cpp:187-198`。
4. subtree rescan debounce 后扫描路径、替换 prefix、compact/remap，并触发 UI index changed：`MacEverything/Core/ServiceEngine+FSEvents.cpp:229-352`。

### 7.5 持久化与恢复

1. 启动先尝试 v6 flat load，然后 paged v5，再 legacy v3：`MacEverything/Core/IndexPersistence.cpp:33-80`。
2. base index load 后 replay WAL：`MacEverything/Core/IndexPersistence.cpp:87-92`。
3. v6 load 后重建 path lookups/path index，并标记 Phase 2 trigram rebuild pending：`MacEverything/Core/SearchEngineV6.cpp:29-113`。
4. flush 会 swap WAL，再 full rewrite v6：`MacEverything/Core/IndexPersistence.cpp:165-188`。
5. full compaction 会 compact records、remap content index、force compact content persistence、写 v6：`MacEverything/Core/IndexPersistence.cpp:224-275`。

## 8. 关键架构约束

### 8.1 不要把 `infile:` 和 `content:` 混为一谈

`infile:` 是 Swift UI 路由到 `ContentIndex` 的前缀：`MacEverything/App/SearchViewModel.swift:284-304`。

`content:` 是 QueryTokenizer 中的 known filter，但 advanced query 当前没有真实内容索引语义：`MacEverything/Core/QueryTokenizer.h:163-181`、`MacEverything/Core/SearchEngineAdvancedQuery.cpp:24-35`、`MacEverything/Core/SearchEngineAdvancedQuery.cpp:128-253`。

### 8.2 不要把 AI 称为当前 semantic search

当前 AI path 是 local LLM 把自然语言翻译成 query syntax，再走普通文件名/路径 search：`MacEverything/App/SearchViewModel.swift:372-388`、`MacEverything/Core/NLTranslator.cpp:290-364`。

如果要恢复 向量语义检索，需要新的数据结构、索引生命周期、查询融合和 UI/API 语义，不应复用当前 “Semantic” 文件名做假设。

### 8.3 任何持久化优化都必须正视 full rewrite

虽然存在 paged writer dirty-page flushing 代码：`MacEverything/Core/PagedIndexWriter.cpp:394-481`。

但当前 orchestrator `IndexPersistence::flush()` 没有走该路径，而是 v6 full rewrite：`MacEverything/Core/IndexPersistence.cpp:165-188`。

这意味着性能 roadmap 里的 COW/no-lock snapshot flush、compaction valve、page residency 需要从 orchestrator 层设计，而不是只改 paged writer。

### 8.4 外部面默认是本机可信假设

HTTP 只绑定 loopback：`MacEverything/Core/HttpServer.cpp:102-106`。

但 rebuild、content config、AI prompt 这类 admin routes 没有鉴权：`MacEverything/Core/HttpServer.cpp:339-348`。

MCP 又把 stdio tool call 代理到这些 HTTP 能力的一部分：`MacEverything/CLI/mcp_main.cpp:326-366`。

因此新增外部 API 时必须明确：read-only、admin、prompt/model mutation 是否需要 token、用户确认或更细粒度隔离。

## 9. 当前风险清单

| 风险 | 影响 | 证据 | 建议方向 |
|---|---|---|---|
| cached startup 可搜但仍在 syncing | UI/HTTP 可能读到短暂 stale index | `MacEverything/Core/ServiceEngine.cpp:249-264`、`MacEverything/Core/ServiceEngine.cpp:387-389` | UI/API 明示 syncing 状态，关键操作读取状态 |
| `startIncrementalFrom` 参数未真正重配 engine | Swift cache/root 参数和 Core config 可能偏离 | `MacEverything/Bridge/MacSearchBridge.mm:103-119`、`MacEverything/Bridge/MacSearchBridge.mm:224-252` | 收敛配置来源或让方法名反映实际行为 |
| full rewrite flush 放大 tombstone 成本 | I/O、page eviction、query latency spikes | `MacEverything/Core/IndexPersistence.cpp:165-188` | COW/no-lock snapshot flush，先 fix compaction valve |
| tombstone 25% compaction 阈值过高 | 长期 15-16% tombstone 不回收 | `MacEverything/Core/IndexPersistence.h:70-71` | 调整到 8-10% 或改成多条件 valve |
| HTTP content search 与 GUI 验证语义不一致 | HTTP 可能返回未验证候选/空 snippet | `MacEverything/Core/ContentIndex.cpp:647`、`MacEverything/Core/HttpServer.cpp:456-472` | 共用 Bridge 级验证逻辑或在 HTTP 端补验证 |
| 新文件内容索引可能错过时序 | FSEvents 新文件先 update content 再 mutate engine | `MacEverything/Core/ServiceEngine+FSEvents.cpp:71-84`、`MacEverything/Core/ServiceEngine+FSEvents.cpp:174-187`、`MacEverything/Core/ServiceEngine+Content.cpp:235` | mutation 后再 resolve/index，或延迟 content update |
| AI 设置面和实际搜索面不一致 | 用户配置 19861 但搜索用 19860 bridge/local model | `MacEverything/App/AIServiceClient.swift:40-105`、`MacEverything/Core/HttpServer.cpp:333-348` | 统一 AI service contract |
| HTTP 单线程同步 accept loop | 长搜索/慢连接阻塞其他请求 | `MacEverything/Core/HttpServer.cpp:184-201` | worker queue 或并发连接处理 |
| MCP batch ignored | MCP client 兼容性风险 | `MacEverything/CLI/mcp_main.cpp:504-510` | 实现 batch 或明确声明并测试 non-support |
| standalone daemon packaging 不完整 | DMG 自包含保证不覆盖 daemon | `Makefile:33-36`、`scripts/bundle-dylibs.sh:21-28` | 明确 daemon 发布策略或纳入 bundle verify |

## 10. Roadmap 摘要

### P0：修复 tombstone compaction valve

当前代码阈值为 `kTombstoneCompactRatio = 0.25`：`MacEverything/Core/IndexPersistence.h:70-71`。

在高 churn workload 下，tombstone ratio 已长期低于 25% 但足以造成 rewrite bloat。优先把阈值改为更可达的 8-10%，或把绝对 tombstone 数、rewrite 成本、flush 频率纳入 valve。

验收信号：性能报告出现非零 reclaim；tombstone ratio 被限制在目标区间；每次 flush rewrite 记录量下降。

### P0：COW / no-lock snapshot flush

当前 flush 路径对 v6 做 full rewrite：`MacEverything/Core/IndexPersistence.cpp:165-188`。

目标是把 query 读锁、mutation 写锁、持久化 snapshot 写盘解耦，降低 3-4s rewrite 对 page residency 和 latency 的影响。

验收信号：flush 期间 query p95 不出现大幅尖刺；page-cold 复现概率下降；HTTP/GUI 查询不中断。

### P0：page residency 策略

SearchEngine 的 metadata 列、string pools、trigram buckets 对不同 query class 影响不同：`MacEverything/Core/SearchEngine.h:379-406`。

短查询和 ext/trigram 通常健康，string/path/content/CJK 更容易受页面驻留影响。应优先保护 hot SoA columns、常用 string pools 和高频 trigram buckets，再考虑新增 CJK 辅助索引。

### P0：FSEvents / ingest 观测和背压

当前 FSEvents 合并、batch mutation、WAL/flush 之间缺少每窗口计数：`MacEverything/Core/ServiceEngine+FSEvents.cpp:91-198`。

需要记录 events、coalesced mutations、live/tombstone delta、flush trigger reason、replay/full-sync reason，以区分自然 churn、replay churn 和自诱导 flush churn。

### P0/P1：外部面 guardrails

HTTP admin routes 无鉴权，MCP 复用 HTTP read surface，accept loop 单线程：`MacEverything/Core/HttpServer.cpp:339-348`、`MacEverything/Core/HttpServer.cpp:184-201`、`MacEverything/CLI/mcp_main.cpp:326-366`。

建议拆 read-only/admin、增加本地 token 或用户确认、并发处理请求，并明确 MCP batch 策略。

### P1：内容搜索一致性

GUI content search 有 snippet exact-match 验证，HTTP route 当前可能没有同等验证：`MacEverything/Bridge/MacSearchBridge+Content.mm:51-62`、`MacEverything/Core/HttpServer.cpp:456-472`。

建议让 GUI/HTTP 共用同一验证路径，并修复 FSEvents 新文件内容索引时序。

## 11. 变更落点速查

| 如果你要改 | 优先看 | 不要先改 |
|---|---|---|
| 搜索语法或 filter | `QueryTokenizer`、`QueryParser`、`SearchEngineAdvancedQuery` | Swift placeholder 文案 |
| 搜索性能 | `SearchEngineAdvancedQuery`、`SearchEngineIndex`、`ShortQueryCache`、性能报告 | 单独调 UI debounce |
| 启动/缓存一致性 | `ServiceEngine.cpp`、`IndexPersistence.cpp`、Bridge startup state | HTTP route |
| FSEvents 实时更新 | `FileSystemWatcher.cpp`、`ServiceEngine+FSEvents.cpp` | SearchViewModel |
| 内容搜索 | `ContentIndex.cpp`、`ServiceEngine+Content.cpp`、`MacSearchBridge+Content.mm` | 普通 query parser |
| AI 搜索 | `NLTranslator.cpp`、`ModelManager.cpp`、`MacSearchBridge+Semantic.mm` | `AIServiceClient` 旧 19861 client |
| 持久化格式 | `FlatIndexWriter`、`IndexPersistence`、`IndexWAL` | tests 之外的旧文档 |
| HTTP/MCP | `HttpServer.cpp`、`mcp_main.cpp`、`MCPConfigManager.swift` | Core query internals |
| 打包 | `Makefile`、Xcode project、`bundle-dylibs.sh`、`verify-bundle.sh` | 手工复制 dylib |

## 12. 测试与验证入口

核心测试入口是 `test_all.cpp`，测试实现应放在 `tests/test_xxx.h`：`test_all.cpp:41-132`、`CLAUDE.md:74-77`。

`make test` alias 到 `test-fast`，会构建 `test_all`、运行 bridge lint、再跑 `./test_all --fast`：`Makefile:55-58`。

慢测试包括 scan+query、FSEvents、E2E：`test_all.cpp:223-225`、`test_all.cpp:269-277`。

bench mode 覆盖 query performance 和 10M-scale benchmark：`test_all.cpp:220-222`、`test_all.cpp:319-321`。

Daemon startup test 会启动 `maceverything-daemon`、检查 `/api/health`、再 SIGTERM graceful exit：`tests/test_daemon_startup.h:78-129`。

MCP protocol test 覆盖 initialize、tools/list、ping、notifications、unknown tools、invalid JSON：`tests/test_mcp_protocol.h:45-87`、`tests/test_mcp_protocol.h:118-181`。

内容索引相关测试覆盖 trigram/persistence、modTime skip/prune、WAL tracking、compaction guard、main compaction remap、snippet、rich extraction：`test_all.cpp:278-313`。

## 13. 架构文档维护规则

1. 新增或修改架构结论时，必须同时给出代码证据行号。
2. 如果历史 changelog 与当前代码冲突，以当前代码为准，并在相关 deep dive 中标注“历史说法已过期”。
3. 涉及 AI、content、semantic、vector 的描述必须特别谨慎，避免把旧向量路径误写成现状。
4. 涉及性能 roadmap 时，必须把“代码机制”和“报告数据”分开写：代码解释为什么可能发生，报告说明是否正在发生。
5. 涉及外部服务面时，必须标注 loopback、鉴权、并发、MCP 兼容性和 packaging 影响。
6. 涉及 release 时，必须记住 Homebrew dylib bundling 是发布约束，不是可选优化：`CLAUDE.md:20-31`、`scripts/bundle-dylibs.sh:8-14`。

## 14. 最短 onboarding 路线

如果只用 30 分钟了解系统：

1. 读本页第 1-8 节，建立拓扑。
2. 读 `service-lifecycle.md` 的 startup/shutdown/FSEvents。
3. 读 `search-query-index.md` 的 query route 和 index behavior。
4. 读 `persistence-wal-compaction.md` 的 flush/compaction 风险。
5. 读 `data-backed-roadmap.md` 的 P0 roadmap。
6. 最后按你要改的模块进入对应 deep dive。

如果只要排查一个线上性能问题：

1. 先看最新 performance report。
2. 对照 `IndexPersistence.cpp:119-188` 判断是否 flush/rewrite/tombstone 相关。
3. 对照 `SearchEngineAdvancedQuery.cpp:651-1079` 判断 query route 和锁范围。
4. 对照 `ServiceEngine+FSEvents.cpp:91-198` 判断 ingest churn。
5. 再决定是否需要 UI/HTTP 层排查。

如果只要新增一个外部工具能力：

1. 先确认是否已有 HTTP route。
2. 若 MCP 暴露，检查 `mcp_main.cpp:313-366` 的 tool schema 和 HTTP proxy 映射。
3. 若涉及 mutation/admin，先设计本地鉴权/确认策略。
4. 若涉及 app bundle 内工具，确认 Xcode copy phase 和 dylib verify 覆盖：`MacEverything.xcodeproj/project.pbxproj:193-204`、`scripts/verify-bundle.sh:22-58`。