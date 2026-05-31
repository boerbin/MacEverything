# ServiceEngine 生命周期架构

## 1. 范围

本文说明 MacEverything 核心服务 `ServiceEngine` 的生命周期边界：

- 启动入口：GUI、Bridge、CLI daemon
- 冷启动全量扫描
- 缓存索引增量启动
- FSEvents replay 与实时监听
- 内容索引联动
- 关闭与持久化落盘
- 线程、GCD 队列、锁与回调所有权
- 当前架构风险

证据引用均使用相对路径与行号，例如 `MacEverything/Core/ServiceEngine.cpp:204-335`。

---

## 2. 总体定位

`ServiceEngine` 是核心 C++ 编排器，位于 SwiftUI/Objective-C++ Bridge 与底层索引、扫描、监听、持久化模块之间。

它的公开生命周期 API 很小：

| API | 作用 |
|---|---|
| `startFullScan(completion)` | 从扫描根目录重新建立内存索引 |
| `startIncremental(completion)` | 优先加载持久化索引，再后台同步 |
| `shutdown()` | 停止服务、等待后台任务、强制落盘 |

证据：`MacEverything/Core/ServiceEngine.h:31-53`

`ServiceEngine` 同时被 GUI Bridge 和 CLI daemon 使用，因此它不能依赖 SwiftUI 生命周期。

证据：`MacEverything/Core/ServiceEngine.h:31-32`、`MacEverything/Bridge/MacSearchBridge.mm:66-115`、`MacEverything/CLI/daemon_main.cpp:107-193`

---

## 3. 核心拥有关系

`ServiceEngine` 持有以下核心对象：

| 对象 | 责任 |
|---|---|
| `SearchEngine` | 文件名、路径、过滤器查询与记录突变 |
| `FileSystemWatcher` | FSEvents replay 与 live monitoring |
| `ContentIndex` | 全文 trigram 内容索引 |
| `IndexPersistence` | 文件索引 base/WAL/compaction |
| `ContentIndexPersistence` | 内容索引 base/WAL/compaction |
| `HttpServer` | 本地 HTTP API |
| `ModelManager` / `NLTranslator` | AI 查询翻译相关能力 |
| `InstanceLock` | 缓存目录实例锁 |

证据：`MacEverything/Core/ServiceEngine.h:124-133`

这些对象不是裸指针所有权，而是 `shared_ptr` 或成员对象。

其中 `SearchEngine`、`ContentIndex`、`IndexPersistence`、`ContentIndexPersistence` 都通过 mutex 保护访问。

证据：`MacEverything/Core/ServiceEngine.h:135-139`

---

## 4. 状态机概览

`ServiceEngine` 维护几个关键原子状态：

| 状态 | 含义 |
|---|---|
| `isScanning_` | 正在全量扫描或启动扫描 |
| `isMonitoring_` | FSEvents live watcher 已启动 |
| `isContentIndexing_` | 内容索引后台任务进行中 |
| `shuttingDown_` | 正在关闭，后台任务应尽快退出 |
| `startupCompleted_` | 启动 completion 是否已经对外触发 |
| `isSyncing_` | 缓存索引已可用，但后台 replay/full-sync 尚未完成 |
| `cancelContentIndexing_` | 请求取消内容索引任务 |

证据：`MacEverything/Core/ServiceEngine.h:146-155`

其中最容易误解的是 `isSyncing_`。

缓存增量启动会先让索引可查询，再后台同步 FSEvents 或 full scan。

因此系统存在一个“可搜索但仍在同步”的中间态。

证据：`MacEverything/Core/ServiceEngine.cpp:249-264`、`MacEverything/Core/ServiceEngine.cpp:387-389`、`MacEverything/Core/ServiceEngine.cpp:439-456`

---

## 5. 入口：GUI 生命周期

GUI 入口由 SwiftUI app 与 `AppDelegate` 启动。

`AppDelegate.applicationDidFinishLaunching` 初始化日志并调用 `MacSearchBridge.shared().startEngine()`。

证据：`MacEverything/App/AppDelegate.swift:10-28`

`SearchViewModel` 稍后调用 `startIncremental()`，安装 UI 关心的扫描、内容索引、索引变化、加载错误回调。

证据：`MacEverything/App/SearchViewModel.swift:105-207`

Bridge 内部持有 `std::shared_ptr<ServiceEngine>`，是 Swift/ObjC 世界和 C++ Core 的所有权边界。

证据：`MacEverything/Bridge/MacSearchBridge_Internal.h:5-14`、`MacEverything/Bridge/MacSearchBridge.mm:66-115`

---

## 6. 入口：CLI daemon 生命周期

CLI daemon 解析 `--port`、`--root`、`--cache-dir`、`--log-dir` 后构造栈上 `ServiceEngine`。

证据：`MacEverything/CLI/daemon_main.cpp:27-69`、`MacEverything/CLI/daemon_main.cpp:107-193`

daemon 安装 SIGINT/SIGTERM dispatch source，收到信号后调用 `ServiceEngine::shutdown()` 并关闭 logger。

证据：`MacEverything/CLI/daemon_main.cpp:76-100`

daemon 通过 `dispatch_main()` 维持 GCD/FSEvents 生命周期。

证据：`MacEverything/CLI/daemon_main.cpp:192-194`

---

## 7. HTTP 生命周期

HTTP 是否自动启动由 `ServiceConfig.httpPort` 决定。

`httpPort = 0` 表示不自动启动；大于 0 表示索引可用后自动启动。

证据：`MacEverything/Core/ServiceEngine.h:22-29`

GUI Bridge 默认配置 `httpPort = 19860`。

证据：`MacEverything/Bridge/MacSearchBridge.mm:103-118`

缓存增量启动在 `SearchEngine` 被设置后启动 HTTP。

证据：`MacEverything/Core/ServiceEngine.cpp:255-258`

冷启动全量扫描 fallback 在首次持久化 flush 后启动 HTTP。

证据：`MacEverything/Core/ServiceEngine.cpp:317-320`

daemon 则显式调用 `startHttpServer(port)`。

证据：`MacEverything/CLI/daemon_main.cpp:181-190`

---

## 8. 冷启动全量扫描路径

全量扫描由 `startFullScan()` 实现。

基本顺序：

1. 设置 `isScanning_ = true`
2. 停止现有 FSEvents monitoring
3. 在后台 GCD group 中创建 `DirectoryScanner`
4. 每 200ms 轮询 scanner progress 并触发 `onScanProgress`
5. 扫描 `config_.scanRoot`
6. 将扫描结果加载进新的 `SearchEngine`
7. 原子替换 `engine_`
8. 设置 `isScanning_ = false`
9. 调用 startup completion，`didFullScan = true`
10. 启动 live monitoring
11. 后台启动内容索引持久化与内容索引任务

证据：`MacEverything/Core/ServiceEngine.cpp:141-197`

注意：`startFullScan()` 本身主要是“内存索引重建原语”。

它会替换内存 `SearchEngine` 并启动 monitoring/content indexing。

但索引持久化对象创建、WAL attach、metadata flush 发生在 `startIncremental()` 的冷启动 fallback 包装层里。

证据：`MacEverything/Core/ServiceEngine.cpp:170-195`、`MacEverything/Core/ServiceEngine.cpp:292-322`

---

## 9. DirectoryScanner 边界

`DirectoryScanner` 每次扫描会重置可复用状态。

它按硬件线程数选择 worker，但限制在 4 到 32 个线程之间。

证据：`MacEverything/Core/DirectoryScanner.cpp:15-68`

扫描实现使用 macOS `getattrlistbulk` 批量读取目录项。

证据：`MacEverything/Core/DirectoryScanner.cpp:144-164`

当根设备号已知时，scanner 会跳过跨设备目录，避免误扫其它挂载卷、autofs、devfs 等。

证据：`MacEverything/Core/DirectoryScanner.cpp:259-265`

`.app` 目录被作为 app bundle 记录处理，不继续递归其内部文件。

证据：`MacEverything/Core/DirectoryScanner.cpp:259-298`

scanner 结束后将每个 worker 的结果合并为 `FileRecord` 列表，并交给 `SearchEngine::loadRecords()`。

证据：`MacEverything/Core/DirectoryScanner.cpp:42-67`、`MacEverything/Core/ServiceEngine.cpp:168-174`

---

## 10. 缓存增量启动路径

增量启动由 `startIncremental()` 实现。

它是 GUI/daemon 正常启动的主路径。

基本顺序：

1. 设置 `isScanning_ = true`
2. 清空 `startupCompleted_`
3. 设置 `isSyncing_ = false`
4. 停止现有 monitoring
5. 在 cache 目录获取 `.instance.lock`
6. 创建新的空 `SearchEngine`
7. 创建 `IndexPersistence`
8. 加载持久化索引
9. 根据 `lastEventId` 和记录数决定缓存命中还是冷启动 fallback

证据：`MacEverything/Core/ServiceEngine.cpp:204-240`

---

## 11. 缓存命中：先可用，后同步

当 `lastEventId > 0` 且缓存记录数大于 0 时，启动进入缓存命中路径。

该路径不会等待 FSEvents replay 完成才通知 UI。

它会：

1. 将 `IndexPersistence` 提升为 shared ownership
2. 通过 `startupCompleted_` CAS 保证 completion 只触发一次
3. 设置新的 `SearchEngine`
4. 设置新的 persistence
5. 设置 `isScanning_ = false`
6. 设置 `isSyncing_ = true`
7. 自动启动 HTTP
8. attach WAL
9. 关联 content index
10. 调用 completion，`didFullScan = false`
11. 后台执行 phase-2 trigram build
12. 后台执行 FSEvents replay 或 fallback full scan

证据：`MacEverything/Core/ServiceEngine.cpp:240-289`

这意味着用户可以很快搜索到缓存结果。

但结果可能短暂落后于真实文件系统，直到 `isSyncing_` 变回 false。

证据：`MacEverything/Core/ServiceEngine.cpp:249-264`、`MacEverything/Core/ServiceEngine.cpp:387-389`

---

## 12. 冷启动 fallback：无可用缓存

当没有可用缓存时，`startIncremental()` 会委托 `startFullScan()`。

全量扫描 completion 返回后，wrapper 继续完成持久化启动：

1. 通过 CAS 标记 startup completion
2. 创建新的 `IndexPersistence`
3. 设置 persistence
4. attach WAL
5. 关联 content index
6. 启动 auto compaction
7. 构造 metadata
8. 强制 flush base index
9. 自动启动 HTTP
10. 对外 completion，`didFullScan = true`

证据：`MacEverything/Core/ServiceEngine.cpp:292-322`

如果加载索引时抛异常，会删除 index/cache 文件，并通过 `onLoadError` 通知调用方。

证据：`MacEverything/Core/ServiceEngine.cpp:324-334`

---

## 13. FSEvents replay 同步

缓存命中后，`backgroundSyncEngine()` 会尝试从持久化的 `lastEventId` 开始 replay FSEvents。

它创建一个独立的 `FileSystemWatcher("replay")`，不是 live watcher。

证据：`MacEverything/Core/ServiceEngine.cpp:342-371`

replay watcher 的事件回调调用 `applyFSEvents(events, engine)`，把历史事件应用到缓存加载出的 engine 上。

证据：`MacEverything/Core/ServiceEngine.cpp:360-365`、`MacEverything/Core/ServiceEngine+FSEvents.cpp:11-85`

replay 等待 `HistoryDone`，最多等待 10 秒。

证据：`MacEverything/Core/ServiceEngine.cpp:373-376`

若 10 秒内收到 `HistoryDone` 且 journal 未截断，则认为 replay 成功。

成功后：

1. 设置 `isSyncing_ = false`
2. 启动 live monitoring
3. 启动 index auto compaction
4. 后台 setup content persistence
5. 后台 start content indexing
6. 触发 `onIndexChanged`

证据：`MacEverything/Core/ServiceEngine.cpp:376-398`

---

## 14. FSEvents replay fallback full scan

如果 replay 超时、未完成、或检测到 journal truncation，则进入后台 full scan。

证据：`MacEverything/Core/ServiceEngine.cpp:401-471`

这个路径不会撤回已经对外可用的缓存索引。

它在后台扫描完整文件系统，然后将 fresh records 加载回同一个 engine。

证据：`MacEverything/Core/ServiceEngine.cpp:423-428`

扫描完成后：

1. 设置 `isSyncing_ = false`
2. 停止旧 persistence auto compaction
3. 创建新的 `IndexPersistence`
4. attach WAL
5. 关联 content index
6. 启动 live monitoring
7. 启动 auto compaction
8. 强制 flush
9. 后台 setup content persistence
10. 后台 start content indexing
11. 触发 `onIndexChanged`

证据：`MacEverything/Core/ServiceEngine.cpp:440-471`

---

## 15. FSEvents stream 配置

`FileSystemWatcher` 使用 macOS FSEvents。

stream 创建参数包括：

- file-level events
- `NoDefer`
- CF path types
- `IgnoreSelf`
- 300ms coalescing latency

证据：`MacEverything/Core/FileSystemWatcher.cpp:52-63`

FSEvents 使用一个 serial dispatch queue：`com.maceverything.fswatcher`。

证据：`MacEverything/Core/FileSystemWatcher.cpp:81-83`

`IgnoreSelf` 表示本进程写入通常不会作为普通 live event 反馈回来。

测试中为了验证延迟，会用 child process 创建文件。

证据：`MacEverything/Core/FileSystemWatcher.cpp:58-62`、`tests/test_fsevents_search_latency.h:7-8`、`tests/test_fsevents_search_latency.h:67-70`

---

## 16. FSEvents 过滤规则

FSEvents callback 会过滤 root changed、unmount、系统噪声路径和显式 exclusion path。

证据：`MacEverything/Core/FileSystemWatcher.cpp:136-174`

噪声路径包括：

- `/.Spotlight-V100/`
- `/.fseventsd/`
- `/.Trashes/`
- 部分 `/private/var/folders/.../com.apple...`

证据：`MacEverything/Core/FileSystemWatcher.cpp:158-164`

`ServiceEngine::startMonitoring()` 会把 app cache 目录加入 exclusion，避免持久化写入反向触发索引循环。

证据：`MacEverything/Core/ServiceEngine+FSEvents.cpp:91-104`

---

## 17. live monitoring 启动

`startMonitoring()` 只在 `isMonitoring_` 为 false 时启动。

证据：`MacEverything/Core/ServiceEngine+FSEvents.cpp:91-99`

它读取 `config_.scanRoot` 作为监听根目录。

证据：`MacEverything/Core/ServiceEngine+FSEvents.cpp:94-106`

watcher callback 内部首先检查 `shuttingDown_`，然后取 `safeEngine()`。

证据：`MacEverything/Core/ServiceEngine+FSEvents.cpp:106-110`

这保证关闭期间的新事件不会继续修改索引。

---

## 18. live event 到 SearchEngine mutation

live monitoring 对每个事件执行以下分类：

| 条件 | 动作 |
|---|---|
| `.app` 内部路径 | 跳过 |
| `MustScanSubDirs` | 加入 subtree rescan |
| removed 或 renamed 且 path 不存在 | 生成 REMOVE mutation |
| path 存在 | stat 文件，生成 UPDATE mutation |
| regular file | 加入 content index update |

证据：`MacEverything/Core/ServiceEngine+FSEvents.cpp:118-171`

所有 search mutations 会通过一次 `engine->batchMutate()` 应用，减少锁粒度。

证据：`MacEverything/Core/ServiceEngine+FSEvents.cpp:186-188`

如果有记录变化，会触发 `onIndexChanged()`。

证据：`MacEverything/Core/ServiceEngine+FSEvents.cpp:193-195`

---

## 19. replay event 到 SearchEngine mutation

replay 路径使用 `applyFSEvents()`。

它与 live monitoring 类似，也先收集 mutation ops，再批量应用。

证据：`MacEverything/Core/ServiceEngine+FSEvents.cpp:11-85`

区别是 replay 路径没有 live watcher callback 中的 rescan debounce 调度。

`MustScanSubDirs` 的 replay 语义主要在 `FileSystemWatcher` 层通过 journal truncation 检测影响 replay 成败。

证据：`MacEverything/Core/FileSystemWatcher.cpp:176-197`、`MacEverything/Core/ServiceEngine.cpp:373-401`

---

## 20. MustScanSubDirs 与 subtree rescan

live monitoring 遇到 `kFSEventStreamEventFlagMustScanSubDirs` 时，不直接将该事件转成单个文件 mutation。

它会将路径加入 `rescanDirs`，之后调用 `scheduleRescanForPaths()`。

证据：`MacEverything/Core/ServiceEngine+FSEvents.cpp:124-127`、`MacEverything/Core/ServiceEngine+FSEvents.cpp:189-191`

rescan 调度会合并 pending paths，并在 `mutationQueue_` 上创建 debounce timer。

默认 debounce delay 是 5 秒。

证据：`MacEverything/Core/ServiceEngine+FSEvents.cpp:229-252`、`MacEverything/Core/ServiceEngine.h:163-165`

flush 时会对重复路径做 throttle，默认 300 秒窗口。

证据：`MacEverything/Core/ServiceEngine+FSEvents.cpp:255-319`、`MacEverything/Core/ServiceEngine.h:164-165`

---

## 21. subtree rescan 执行

`rescanSubtree(dir)` 先获取当前 `SearchEngine`。

证据：`MacEverything/Core/ServiceEngine+FSEvents.cpp:321-323`

实际扫描被派发到 `mutationQueue_`，使 subtree rescan 与其它 mutation 串行化。

证据：`MacEverything/Core/ServiceEngine+FSEvents.cpp:325-334`

执行步骤：

1. 检查是否 shutting down
2. 创建 `DirectoryScanner`
3. 扫描子树
4. 取 fresh records
5. 再次检查 shutting down
6. 调用 `engine->batchRescanPrefix(dir, freshRecords)`
7. tombstone 超过 30% 时 compact records
8. remap content index file indices
9. 触发 `onIndexChanged`

证据：`MacEverything/Core/ServiceEngine+FSEvents.cpp:325-352`

---

## 22. 内容索引生命周期联动

`ServiceEngine` 构造后持有 `ContentIndex`。

证据：`MacEverything/Core/ServiceEngine.cpp:19`、`MacEverything/Core/ServiceEngine.h:127`

内容持久化通常在以下时机 setup：

- 全量扫描完成后后台执行
- FSEvents replay 成功后后台执行
- replay fallback full scan 完成后后台执行

证据：`MacEverything/Core/ServiceEngine.cpp:187-196`、`MacEverything/Core/ServiceEngine.cpp:392-396`、`MacEverything/Core/ServiceEngine.cpp:462-470`

`setupContentPersistence()` 会加载内容配置、base index、WAL，prune stale entries，attach WAL，并启动内容 compaction。

证据：`MacEverything/Core/ServiceEngine+Content.cpp:13-48`

`startContentIndexing()` 根据已有内容索引状态选择增量或全量内容索引。

证据：`MacEverything/Core/ServiceEngine+Content.cpp:87-112`

实际索引使用 `dispatch_apply` 并行处理文件。

证据：`MacEverything/Core/ServiceEngine+Content.cpp:125-136`

---

## 23. live event 与内容索引更新

FSEvents 对 regular file 的 update/remove 路径会加入 `contentUpdates`；remove 会以 `isRemove=true` 传入，普通文件 update 则尝试按路径更新内容索引。新增文件如果在 search mutation 前尚未拥有 fileIndex，存在漏收风险，见本节风险说明。

证据：`MacEverything/Core/ServiceEngine+FSEvents.cpp:135-170`

随后调用 `updateContentForPath(path, isRemove, engine)`。

证据：`MacEverything/Core/ServiceEngine+FSEvents.cpp:174-177`

如果调用方设置了 `onFileChanged`，Core 会通过 `onFileChanged(path, action)` 发出通知；它不是默认 AI 管线，也不应假定 Bridge/UI 已自动消费该 callback。

证据：`MacEverything/Core/ServiceEngine+FSEvents.cpp:179-183`

最后才执行 `engine->batchMutate()`。

证据：`MacEverything/Core/ServiceEngine+FSEvents.cpp:186-188`

这个顺序是一个重要风险点：新文件的 content update 可能在 search mutation 之前解析 fileIndex，导致内容索引漏收新文件。

证据：`MacEverything/Core/ServiceEngine+FSEvents.cpp:174-187`、`MacEverything/Core/ServiceEngine+Content.cpp:235`

---

## 24. 关闭流程

`shutdown()` 使用 `shuttingDown_` 的 CAS 保证幂等。

证据：`MacEverything/Core/ServiceEngine.cpp:538-542`

关闭顺序：

1. 停止 HTTP
2. 记录 shutdown started
3. 取消内容索引
4. 增加 content index generation
5. 等待 background group，最多 3 秒
6. 读取 watcher last event id
7. 停止 monitoring
8. 强制 compact index persistence
9. 强制 compact content persistence
10. release mutation queue
11. release background group
12. release content indexing semaphore
13. 记录 shutdown completed

证据：`MacEverything/Core/ServiceEngine.cpp:544-590`

3 秒超时是有意设计：优先保证 macOS 终止 watchdog 前还有机会执行最终 flush，而不是无限等待卡住的 I/O。

证据：`MacEverything/Core/ServiceEngine.cpp:550-557`

---

## 25. stopMonitoring 的职责

`stopMonitoring()` 不只是停止 FSEvents watcher。

它还会：

- 停止 watcher
- 取消 pending rescan debounce timer
- 清空 pending rescan paths
- 清空 rescan throttle state
- 停止 index persistence auto compaction
- 停止 content persistence auto compaction
- 设置 `isMonitoring_ = false`

证据：`MacEverything/Core/ServiceEngine+FSEvents.cpp:201-223`

因此在启动全量扫描、增量启动、shutdown 前调用 `stopMonitoring()` 是必要的生命周期边界。

证据：`MacEverything/Core/ServiceEngine.cpp:141-144`、`MacEverything/Core/ServiceEngine.cpp:204-209`、`MacEverything/Core/ServiceEngine.cpp:560-563`

---

## 26. 线程与队列边界

| 边界 | 机制 | 说明 |
|---|---|---|
| 启动扫描与后台同步 | `backgroundGroup_` + global GCD queue | full scan、incremental load、replay/full-sync |
| scanner 内部 | `std::thread` workers | 4-32 worker 扫描文件系统 |
| FSEvents watcher | serial dispatch queue | FSEvents callback 串行进入 |
| subtree rescan mutation | `mutationQueue_` serial queue | debounce flush 与 rescan 串行执行 |
| 内容索引 | `dispatch_apply` | 并行索引候选文件 |
| Bridge 到 Swift | main queue | 回调进入 UI 前 hop 到主线程 |
| Swift 搜索执行 | `Task.detached` | 查询 off-main，结果回主线程 |

证据：`MacEverything/Core/ServiceEngine.h:142-145`、`MacEverything/Core/DirectoryScanner.cpp:42-67`、`MacEverything/Core/FileSystemWatcher.cpp:81-83`、`MacEverything/Core/ServiceEngine+FSEvents.cpp:241-252`、`MacEverything/Core/ServiceEngine+Content.cpp:125-136`、`MacEverything/Bridge/MacSearchBridge.mm:146-185`、`MacEverything/App/SearchViewModel.swift:337-371`

---

## 27. 锁边界

`ServiceEngine` 使用多个 mutex，而不是一个大锁：

| 锁 | 保护对象 |
|---|---|
| `engineMutex_` | `engine_`、`httpServer_` 等 engine 相关对象 |
| `contentMutex_` | `contentIndex_` |
| `persistenceMutex_` | `persistence_` |
| `contentPersistenceMutex_` | `contentPersistence_` |
| `pendingRescanMutex_` | debounce paths、timer、throttle map |

证据：`MacEverything/Core/ServiceEngine.h:135-139`、`MacEverything/Core/ServiceEngine.h:157-161`

外部访问通过 `safeEngine()`、`safeContentIndex()`、`safePersistence()`、`safeContentPersistence()` 等 shared pointer getter。

证据：`MacEverything/Core/ServiceEngine.h:63-69`

设计意图是让后台任务先复制 shared pointer，再在对象自己的方法内部做更细粒度同步。

---

## 28. 回调所有权

`ServiceEngine` 的回调是调用方设置的 `std::function` 字段。

包括：

- `onIndexChanged`
- `onScanProgress`
- `onContentIndexProgress`
- `onContentIndexComplete`
- `onFileChanged`
- `onLoadError`

证据：`MacEverything/Core/ServiceEngine.h:35-42`、`MacEverything/Core/ServiceEngine.h:80-86`

Bridge 安装这些回调后，会显式 dispatch 到 main queue 再调用 Swift closure。

证据：`MacEverything/Bridge/MacSearchBridge.mm:146-185`

`SearchViewModel` 是 `@MainActor`，因此 UI 状态更新应发生在主线程。

证据：`MacEverything/App/SearchViewModel.swift:22-48`、`MacEverything/App/SearchViewModel.swift:146-206`

---

## 29. startup completion 所有权

`StartupCallback` 是 `startFullScan()` 和 `startIncremental()` 的 completion。

证据：`MacEverything/Core/ServiceEngine.h:40-53`

在缓存命中路径中，`startupCompleted_` CAS 防止多次 completion。

证据：`MacEverything/Core/ServiceEngine.cpp:244-248`

在冷启动 fallback 中，也使用同一个 CAS 防止重复 completion。

证据：`MacEverything/Core/ServiceEngine.cpp:294-299`

Bridge 还处理 GUI 早期 `startEngine()` 与稍后 ViewModel callback attachment 之间的 race。

证据：`MacEverything/Bridge/MacSearchBridge.mm:198-252`

---

## 30. FileSystemWatcher 回调所有权

`FileSystemWatcher::stop()` 会先 stop/invalidate/release FSEvents stream。

证据：`MacEverything/Core/FileSystemWatcher.cpp:87-95`

如果 watcher queue 存在，它会 `dispatch_sync(queue_, ^{ callback_ = nullptr; onReplayDone_ = nullptr; })`。

证据：`MacEverything/Core/FileSystemWatcher.cpp:96-104`

这避免 callback 字段在 FSEvents serial queue 上被读取时，与 stop 线程并发置空产生 data race。

证据：`MacEverything/Core/FileSystemWatcher.cpp:97-103`

---

## 31. 持久化与 event id

缓存启动依赖 `IndexPersistence::load()` 返回的 `lastEventId`。

证据：`MacEverything/Core/ServiceEngine.cpp:233-240`

shutdown 时会从 live watcher 读取最后 event id，并用于最终 compact。

证据：`MacEverything/Core/ServiceEngine.cpp:560-569`

成功 live monitoring 后，auto compaction 会拿 watcher 作为 event id 来源。

证据：`MacEverything/Core/ServiceEngine.cpp:388-390`、`MacEverything/Core/ServiceEngine.cpp:456-457`

这条链路保证下次启动可以从上次确认的 FSEvents 位置 replay。

---

## 32. 查询可用性边界

缓存命中时：

- `SearchEngine` 先可用
- HTTP 可先启动
- completion 先触发
- replay/background sync 后完成

证据：`MacEverything/Core/ServiceEngine.cpp:250-264`、`MacEverything/Core/ServiceEngine.cpp:289-290`

因此 API/UI 需要理解三种状态：

| 状态 | 搜索 | 同步 |
|---|---|---|
| cold scanning | 可能不可用或记录为空 | 正在 full scan |
| cached searchable | 可用 | `isSyncing_ = true` |
| settled | 可用 | `isSyncing_ = false` |

证据：`MacEverything/Core/ServiceEngine.h:71-75`、`MacEverything/Core/ServiceEngine.cpp:249-264`

---

## 33. 已覆盖测试

当前测试覆盖了以下生命周期关键路径：

- `ServiceEngine` 构造、full scan、query、shutdown、cold incremental
- FSEvents create/modify/rename/delete/batch
- FSEvents 到搜索延迟
- watcher event id 状态
- scanner cancel/reentry
- daemon startup/shutdown
- logger 并发与轮转

证据：`tests/test_service_engine.h:24-112`、`tests/test_fsevents.h:39-132`、`tests/test_fsevents_search_latency.h:44-177`、`tests/test_fswatcher_eventid.h:7-32`、`tests/test_scanner_cancel.h:16-61`、`tests/test_scanner_reentry.h:20-50`、`tests/test_daemon_startup.h:46-129`、`tests/test_logger.h:18-237`

---

## 34. 架构风险：缓存路径参数未真正生效

`MacSearchBridge.startIncrementalFrom(rootPath:cachePath:walPath:)` 暴露 root/cache/WAL 参数。

但实现中使用的是 Bridge 初始化时写入 `ServiceEngine` config 的路径，而不是方法参数重新配置 engine。

证据：`MacEverything/Bridge/MacSearchBridge.mm:102-115`、`MacEverything/Bridge/MacSearchBridge.mm:223-252`

如果 Swift 层传入路径与 Bridge 默认路径不一致，可能出现“API 看似支持自定义路径，但 Core 实际仍用默认路径”的错觉。

---

## 35. 架构风险：standalone startFullScan 持久化语义不完整

`startFullScan()` 会重建内存索引并启动 monitoring/content indexing。

证据：`MacEverything/Core/ServiceEngine.cpp:141-197`

但 persistence attach、auto compaction、metadata flush 是 `startIncremental()` cold fallback 做的。

证据：`MacEverything/Core/ServiceEngine.cpp:292-322`

因此如果 admin callback 或未来代码直接调用 `startFullScan()`，需要确认是否还需要手动处理持久化落盘。

daemon admin callback 直接运行 full scan，是需要重点审视的路径。

证据：`MacEverything/CLI/daemon_main.cpp:132-140`

---

## 36. 架构风险：FSEvents replay 固定 10 秒等待

replay 等待上限固定为 10 秒。

证据：`MacEverything/Core/ServiceEngine.cpp:373-376`

如果历史事件很多、系统延迟较高、或 FSEvents 回放慢，可能触发 expensive background full scan。

证据：`MacEverything/Core/ServiceEngine.cpp:401-471`

这是正确性优先的策略，但对大盘扫描性能有影响。

---

## 37. 架构风险：内容索引新文件漏收

live monitoring 中，内容索引更新发生在 search engine mutation 之前。

证据：`MacEverything/Core/ServiceEngine+FSEvents.cpp:174-187`

如果 `updateContentForPath()` 需要依赖已有 `SearchEngine` fileIndex，新文件可能还不存在于 engine 中。

证据：`MacEverything/Core/ServiceEngine+Content.cpp:235`

这可能导致新建文件已进入文件名搜索，但内容索引没有及时加入。

---

## 38. 架构风险：本进程写入被 IgnoreSelf 过滤

FSEvents stream 使用 `kFSEventStreamCreateFlagIgnoreSelf`。

证据：`MacEverything/Core/FileSystemWatcher.cpp:58-62`

这避免持久化/cache 写入造成自激循环。

但代价是同进程内部创建或修改的文件不一定通过普通 live event 被索引。

测试已经通过 child process 绕开该语义。

证据：`tests/test_fsevents_search_latency.h:7-8`、`tests/test_fsevents_search_latency.h:67-70`

---

## 39. 架构风险：关闭等待有界

shutdown 只等待 background group 3 秒。

证据：`MacEverything/Core/ServiceEngine.cpp:550-557`

这防止 app 退出被无限阻塞。

但如果后台 I/O 真的超过 3 秒，shutdown 会继续 final compaction。

因此后台任务必须尊重 `shuttingDown_` 和 cancellation flag，避免关闭阶段仍大量占用 I/O。

证据：`MacEverything/Core/ServiceEngine.cpp:547-557`、`MacEverything/Core/ServiceEngine.cpp:564-573`

---

## 40. 架构风险：onFileChanged 消费者边界不清

`onFileChanged` 是 core callback，FSEvents/content-update 路径会触发它。

证据：`MacEverything/Core/ServiceEngine.h:35-42`、`MacEverything/Core/ServiceEngine.h:80-86`、`MacEverything/Core/ServiceEngine+FSEvents.cpp:76-80`、`MacEverything/Core/ServiceEngine+FSEvents.cpp:179-183`

Bridge 当前清晰转发了 scan/index/content/load-error 相关回调到主线程。

证据：`MacEverything/Bridge/MacSearchBridge.mm:146-185`

但文档和接口层应明确 `onFileChanged` 的目标消费者、线程要求和生命周期所有权，避免未来在非主线程直接触碰 UI 或 AI 状态。

---

## 41. 设计准则

未来修改 `ServiceEngine` 生命周期时应遵守以下约束：

1. 启动 completion 必须只触发一次。
2. 缓存命中路径必须明确暴露“可搜索但仍同步”状态。
3. `startFullScan()` 与 `startIncremental()` 的持久化语义不能混淆。
4. FSEvents callback 不应直接做长耗时工作。
5. subtree rescan 必须经过 debounce/throttle。
6. Bridge 回调进入 Swift 前必须 hop 到 main queue。
7. shutdown 必须幂等。
8. shutdown 必须优先停止 HTTP 和 live monitoring。
9. final compaction 必须携带最后 FSEvents event id。
10. 内容索引更新必须考虑 fileIndex remap 与新文件时序。

---

## 42. 生命周期摘要

正常 GUI 缓存启动：

1. `AppDelegate` 启动 Bridge。
2. Bridge 创建并持有 `ServiceEngine`。
3. `SearchViewModel` 安装回调。
4. `startIncremental()` 加载持久化索引。
5. 缓存命中后立即设置 `SearchEngine`。
6. HTTP 启动。
7. UI 收到 completion，可查询。
8. 后台 replay FSEvents。
9. replay 成功后启动 live monitoring。
10. 内容索引后台恢复。
11. app 退出时 shutdown 强制落盘。

证据：`MacEverything/App/AppDelegate.swift:10-46`、`MacEverything/Bridge/MacSearchBridge.mm:198-252`、`MacEverything/Core/ServiceEngine.cpp:204-335`、`MacEverything/Core/ServiceEngine.cpp:342-481`、`MacEverything/Core/ServiceEngine.cpp:537-590`

正常冷启动（`startIncremental()` 无缓存 → fallback full scan）：

1. `startIncremental()` 发现无可用缓存。
2. 委托 `startFullScan()`。
3. scanner 多线程扫盘。
4. `startFullScan()` 加载 fresh records 到新 `SearchEngine`，先触发 startup completion，并在该函数自身尾部安排 live monitoring 与内容索引启动。
5. `startIncremental()` 包装层的 completion 随后创建 `IndexPersistence`。
6. attach WAL，关联 `ContentIndex`，启动 auto-compaction。
7. force flush base index（写入 v6）。
8. HTTP 启动。
9. 外层 completion 通知 UI/API cold fallback 完成。
10. `startFullScan()` 安排的 live monitoring 与内容索引后台流程恢复/开始。

证据：`MacEverything/Core/ServiceEngine.cpp:141-197`、`MacEverything/Core/ServiceEngine.cpp:292-322`

正常关闭：

1. CAS 设置 `shuttingDown_`。
2. 停 HTTP。
3. 取消内容索引。
4. 等后台任务最多 3 秒。
5. 读取 last event id。
6. 停 monitoring 与 compaction timers。
7. 强制 compact 文件索引。
8. 强制 compact 内容索引。
9. release GCD 对象。

证据：`MacEverything/Core/ServiceEngine.cpp:538-590`