# Bridge 与 SwiftUI 架构说明

## 1. 文档范围

本文描述 MacEverything 的 Objective-C++ Bridge 与 SwiftUI UI 层如何协作。
重点覆盖启动生命周期、查询状态机、线程回调、结果封送、分页、高亮、快捷键、设置与权限。
本文不描述 C++ Core 的倒排索引、FSEvents、持久化细节，只说明 UI/Bridge 边界。
SwiftUI 入口是 `MacEverythingApp`，主窗口承载 `ContentView`。
证据：`MacEverything/App/MacEverythingApp.swift:2-13`
`AppDelegate` 负责早期启动 C++ 引擎、注册热键、创建状态栏入口。
证据：`MacEverything/App/AppDelegate.swift:10-27`
`MacSearchBridge` 是 Swift/ObjC 世界到 C++ `ServiceEngine` 的唯一主桥面。
证据：`MacEverything/Bridge/MacSearchBridge.h:44-72`
`SearchViewModel` 是 SwiftUI 的核心状态机，集中维护搜索、扫描、AI、内容索引和分页状态。
证据：`MacEverything/App/SearchViewModel.swift:22-76`

## 2. 分层概览

整体调用方向是 SwiftUI View → `SearchViewModel` → `MacSearchBridge` → C++ `ServiceEngine`。
UI 不直接访问 C++ 类型，而是通过 Objective-C 对象和 Foundation 容器间接交互。
Bridge 对外暴露 `MEFileResult`、`MEContentResult`、`MEHighlightHint` 三类 DTO。
证据：`MacEverything/Bridge/MacSearchBridge.h:4-42`
Swift 侧再把 DTO 复制到值类型 `FileItem` 和 `ContentFileItem`。
证据：`MacEverything/App/SearchViewModel.swift:3-20`
这种边界使 SwiftUI 不需要理解 C++ 生命周期、锁、索引记录和路径拼接策略。
但代价是查询结果会经历 C++ record → ObjC DTO → Swift struct 的多次封送。
文件名搜索和内容搜索走两条 Bridge API，不是同一个 Swift 结果模型。
文件名搜索结果进入 `displayItems`，内容搜索结果进入 `contentResults`。
证据：`MacEverything/App/SearchViewModel.swift:24-39`
AI 自然语言搜索不是第三种结果类型，而是先翻译为语法查询，再复用文件名搜索。
证据：`MacEverything/App/SearchViewModel.swift:372-390`

## 3. App 启动与主窗口

`MacEverythingApp` 使用 `@NSApplicationDelegateAdaptor` 安装 `AppDelegate`。
证据：`MacEverything/App/MacEverythingApp.swift:2-5`
主 Scene 是名为 `MacEverything` 的窗口，内容是 `ContentView`。
证据：`MacEverything/App/MacEverythingApp.swift:7-13`
菜单层提供搜索选项、重建索引、快捷键设置、内容设置、AI 设置和 MCP 开关。
证据：`MacEverything/App/MacEverythingApp.swift:14-64`
`AppDelegate.applicationDidFinishLaunching` 先初始化日志，再调用 `startEngine()`。
证据：`MacEverything/App/AppDelegate.swift:10-13`
`startEngine()` 的设计目标是让 C++ 引擎早于 SwiftUI ViewModel 启动。
当 `ContentView` 创建 `SearchViewModel` 后，ViewModel 会补装回调并等待已有启动完成。
证据：`MacEverything/Bridge/MacSearchBridge.mm:198-215`
如果以 `--minimized` 启动，`AppDelegate` 会在下一帧找到窗口并隐藏 App。
证据：`MacEverything/App/AppDelegate.swift:14-23`
启动后还会注册全局热键并安装状态栏菜单。
证据：`MacEverything/App/AppDelegate.swift:24-27`

## 4. ObjC++ Bridge 的职责

Bridge 的内部状态保存在 class extension 中，核心字段是 `std::shared_ptr<ServiceEngine>`。
证据：`MacEverything/Bridge/MacSearchBridge_Internal.h:5-14`
Bridge 初始化时构造 `ServiceConfig`，默认扫描根目录为 `/`。
证据：`MacEverything/Bridge/MacSearchBridge.mm:102-109`
Bridge 初始化还设置默认缓存路径、日志路径、HTTP 端口 19860 和 bundle prompt。
证据：`MacEverything/Bridge/MacSearchBridge.mm:105-114`
随后 Bridge 创建 `ServiceEngine` 并预安装 HTTP 管理回调。
证据：`MacEverything/Bridge/MacSearchBridge.mm:114-118`
Bridge 的公开接口把 C++ 生命周期封装为 `startEngine`、`startIncrementalFrom`、`resetEngine`、`prepareForTermination`。
证据：`MacEverything/Bridge/MacSearchBridge.h:55-74`
Bridge 也把 C++ 查询封装为 `queryResults`、`recentResults`、`queryContent`、`translateQuery` 等 Objective-C 方法；其中文件名/最近/生命周期 API 在主头文件，内容搜索与 AI 翻译分别在分类头文件中声明。
证据：`MacEverything/Bridge/MacSearchBridge.h:86-101`、`MacEverything/Bridge/MacSearchBridge+Content.h:5-18`、`MacEverything/Bridge/MacSearchBridge+Semantic.h:5-8`
`startEngine()` 是幂等的，重复调用会直接返回。
证据：`MacEverything/Bridge/MacSearchBridge.mm:198-204`
`startIncrementalFrom:cachePath:walPath:` 会处理“引擎已启动但启动尚未完成”的 UI 补订阅场景。
证据：`MacEverything/Bridge/MacSearchBridge.mm:223-252`
Bridge 暴露 `isScanning`、`isMonitoring`、`isSyncing`、`isPhase2Pending` 供 UI 显示后台状态。
证据：`MacEverything/Bridge/MacSearchBridge.mm:126-140`
Bridge 的 HTTP 管理回调会把重建索引转换成主线程通知，内容索引重建则放入全局队列。
证据：`MacEverything/Bridge/MacSearchBridge.mm:258-303`

## 5. 回调与主线程规则

Bridge 的原则是：C++ 可以在后台线程触发回调，但所有 UI 可见回调必须切回 main queue。
`_installCallbacks()` 对扫描进度、索引变化、内容索引进度、内容索引完成和加载错误都做了 `dispatch_async(dispatch_get_main_queue())`。
证据：`MacEverything/Bridge/MacSearchBridge.mm:146-185`
`startScanFrom` 的完成回调也显式回到 main queue。
证据：`MacEverything/Bridge/MacSearchBridge.mm:187-196`
`startEngine` 的启动完成回调同样在 main queue 设置 `_startupFinished` 并通知 Swift。
证据：`MacEverything/Bridge/MacSearchBridge.mm:204-214`
Swift 侧 `SearchViewModel` 标注为 `@MainActor`，因此发布属性默认由主 Actor 管理。
证据：`MacEverything/App/SearchViewModel.swift:22-76`
`startIncremental()` 安装 Bridge 回调后，又用 `Task { @MainActor in ... }` 更新 SwiftUI 状态。
证据：`MacEverything/App/SearchViewModel.swift:142-207`
查询执行不在主线程完成，而是用 `Task.detached` 调同步 Bridge API。
证据：`MacEverything/App/SearchViewModel.swift:336-370`
查询结果应用回 UI 前会回到 `MainActor.run`。
证据：`MacEverything/App/SearchViewModel.swift:361-368`
每次搜索会捕获 `searchGeneration`，回写时只有 generation 匹配才更新 UI。
证据：`MacEverything/App/SearchViewModel.swift:340-368`
这套 generation 机制是 UI 层避免旧查询覆盖新查询的主要防线。
Bridge 层另有 session cancellation，GUI 固定使用 session id `1`。
证据：`MacEverything/App/SearchViewModel.swift:67-68`
清空查询或搜索选项变化时会调用 `bridge.cancelSession(Self.guiSessionId)`。
证据：`MacEverything/App/SearchViewModel.swift:113-123`
证据：`MacEverything/App/SearchViewModel.swift:254-280`

## 6. SwiftUI 状态机

`SearchViewModel` 的状态可以分为扫描状态、普通搜索状态、内容搜索状态、AI 搜索状态和最近文件状态。
扫描状态由 `isScanning`、`scanComplete`、`scannedCount`、`totalRecords` 驱动。
证据：`MacEverything/App/SearchViewModel.swift:24-32`
后台同步和二阶段索引状态由 `isMonitoring`、`isSyncing`、`isBuildingIndex` 显示。
证据：`MacEverything/App/SearchViewModel.swift:31-42`
内容搜索状态由 `isContentSearch`、`contentResults`、`isContentIndexing`、`contentIndexProgress` 维护。
证据：`MacEverything/App/SearchViewModel.swift:35-39`
AI 状态由 `isAISearch`、`isAITranslating`、`translatedQuery` 和 `showAISetup` 维护。
证据：`MacEverything/App/SearchViewModel.swift:42-47`
最近文件状态由 `showingRecent` 和 `recentTask` 维护。
证据：`MacEverything/App/SearchViewModel.swift:34-63`
ViewModel 初始化时订阅 `SearchOptions.objectWillChange`，选项改变会触发重搜。
证据：`MacEverything/App/SearchViewModel.swift:104-123`
`startIncremental()` 完成后，如果搜索框非空则搜索，否则加载最近文件。
证据：`MacEverything/App/SearchViewModel.swift:188-205`
`rebuildIndex()` 会取消搜索任务、清空 UI 状态、删除缓存文件、reset Bridge，然后重启增量流程。
证据：`MacEverything/App/SearchViewModel.swift:209-231`
`clearCacheAndRestart()` 用于索引损坏告警后的恢复，清除索引和内容索引相关缓存，然后调用 `startIncremental()` 重新进入启动流程；它不像 `rebuildIndex()` 那样先 `resetEngine()`，因此不应把它理解为完全重建 Bridge/ServiceEngine 实例。
证据：`MacEverything/App/SearchViewModel.swift:233-245`
`ContentView` 根据这些状态在扫描页、内容搜索页、AI 翻译页、空结果页、文件列表之间切换。
证据：`MacEverything/App/ContentView.swift:146-304`
状态栏显示扫描、同步、内容索引、AI 翻译结果、匹配数和查询耗时。
证据：`MacEverything/App/ContentView.swift:72-143`
索引变化和窗口焦点变化会经 `IndexRefreshThrottle` 控制刷新节奏。
证据：`MacEverything/App/SearchViewModel.swift:483-536`

## 7. 查询输入、去抖与分支

搜索框文本变化会进入 `onSearchTextChanged()`。
证据：`MacEverything/App/ContentView.swift:25-45`
每次变化先取消旧 `searchTask` 和 `recentTask`，再递增 `searchGeneration`。
证据：`MacEverything/App/SearchViewModel.swift:247-253`
空查询会清空匹配、缓存、内容状态、AI 状态、ghost suggestion，并取消 GUI session。
证据：`MacEverything/App/SearchViewModel.swift:254-268`
空查询且扫描完成时，会延迟 20ms 再加载最近文件，避免旧查询线程池竞争。
证据：`MacEverything/App/SearchViewModel.swift:269-275`
`infile:` 前缀进入内容搜索分支，并使用 300ms 去抖。
证据：`MacEverything/App/SearchViewModel.swift:284-304`
普通文件名搜索使用 80ms 去抖。
证据：`MacEverything/App/SearchViewModel.swift:323-328`
AI 搜索默认走自然语言翻译，使用 2s 去抖。
证据：`MacEverything/App/SearchViewModel.swift:310-322`
按 Enter 会取消 AI 去抖并立即翻译。
证据：`MacEverything/App/SearchViewModel.swift:604-609`
查询变化还会更新 ghost suggestion，并安排搜索历史落盘。
证据：`MacEverything/App/SearchViewModel.swift:332-334`
ghost suggestion 优先来自历史，其次来自内置语法关键词。
证据：`MacEverything/App/SearchViewModel.swift:547-590`
搜索历史在输入稳定 2s 后记录，窗口失焦时也会记录。
证据：`MacEverything/App/SearchViewModel.swift:490-501`
证据：`MacEverything/App/SearchViewModel.swift:593-601`

## 8. 结果封送与分页

文件名搜索的 Bridge API 接收 `NSString`，转换为 `std::string` 后调用 C++ `engine->query`。
证据：`MacEverything/Bridge/MacSearchBridge.mm:387-407`
带 session 的搜索会调用 `engine->query(key, maxResults, true, sessionId)`。
证据：`MacEverything/Bridge/MacSearchBridge.mm:410-418`
Bridge 通过 `forEachRecordWithPath` 将 C++ record 和路径映射为 `MEFileResult`。
证据：`MacEverything/Bridge/MacSearchBridge.mm:420-431`
如果 C++ 字符串不能转成有效 `NSString`，该条结果会被跳过。
证据：`MacEverything/Bridge/MacSearchBridge.mm:421-425`
Swift 文件名搜索一次最多从 Bridge 取 10,000 条。
证据：`MacEverything/App/SearchViewModel.swift:70-72`
UI 首屏只把前 100 条转换成 `FileItem` 并显示。
证据：`MacEverything/App/SearchViewModel.swift:349-368`
完整的 ObjC DTO 数组被保存在 `cachedResults`。
证据：`MacEverything/App/SearchViewModel.swift:361-368`
`loadMore()` 不再调用 Bridge，只从 `cachedResults` 切下一页 100 条。
证据：`MacEverything/App/SearchViewModel.swift:422-456`
`ContentView` 用 `LazyVStack` 渲染文件结果，并通过底部 sentinel 的 `onAppear` 触发加载更多。
证据：`MacEverything/App/ContentView.swift:244-290`
内容搜索一次调用 `bridge.queryContent(keyword, maxResults: 200)`。
证据：`MacEverything/App/SearchViewModel.swift:392-420`
内容搜索 UI 没有类似 `loadMore()` 的增量分页。
证据：`MacEverything/App/ContentView.swift:200-223`
内容搜索 Bridge 先拿 `ContentIndex` 匹配，再回查 SearchEngine record，拼出完整路径。
证据：`MacEverything/Bridge/MacSearchBridge+Content.mm:7-39`
内容搜索使用 `dispatch_apply` 并行生成 snippet。
证据：`MacEverything/Bridge/MacSearchBridge+Content.mm:41-58`
snippet、路径、文件名再被封送成 `MEContentResult`。
证据：`MacEverything/Bridge/MacSearchBridge+Content.mm:60-80`

## 9. 高亮系统

搜索输入框是 `NSViewRepresentable`，内部包装自定义 `NSTextView`。
证据：`MacEverything/App/HighlightedSearchField.swift:161-216`
输入框 tokenizer 在 Swift 侧识别 filter、operator、quoted 和普通 word。
证据：`MacEverything/App/HighlightedSearchField.swift:24-159`
Coordinator 在 `textDidChange` 中同步 SwiftUI binding 并重新应用语法高亮。
证据：`MacEverything/App/HighlightedSearchField.swift:273-335`
自定义 `HighlightedNSTextView` 截获 Enter 和 Tab。
证据：`MacEverything/App/HighlightedSearchField.swift:341-362`
Tab 用于接受 ghost suggestion，Enter 用于 AI 搜索立即翻译。
证据：`MacEverything/App/ContentView.swift:25-40`
ghost suggestion 和 placeholder 都由 `draw(_:)` 自绘。
证据：`MacEverything/App/HighlightedSearchField.swift:364-409`
结果高亮不直接复用输入框 tokenizer，而是使用 C++ AST 提取出的 highlight hint。
证据：`MacEverything/App/SearchViewModel.swift:50-56`
Bridge 的 `parseHighlightHints` 调 C++ `extractHighlightHints`，再封送成 `MEHighlightHint`。
证据：`MacEverything/Bridge/MacSearchBridge.mm:461-475`
Swift 的 `HighlightHint` 表示字段、匹配模式和大小写敏感性。
证据：`MacEverything/App/HighlightHint.swift:4-43`
`TextHighlight` 支持 substring、glob、regex、whole word、whole filename。
证据：`MacEverything/App/TextHighlight.swift:239-299`
多 hint 会合并重叠范围再渲染。
证据：`MacEverything/App/TextHighlight.swift:301-335`
文件结果使用 field-aware cross-boundary 高亮，能把 `path/name` 的命中拆回路径和文件名。
证据：`MacEverything/App/TextHighlight.swift:337-420`
`ResultRow` 对文件名和路径应用结构化 hint 高亮。
证据：`MacEverything/App/ResultRow.swift:70-78`
内容搜索行只对 snippet 用关键字高亮，不使用结构化 hint。
证据：`MacEverything/App/ContentResultRow.swift:16-33`

## 10. 操作、设置、热键与权限

文件结果行支持 hover、上下文菜单、拖拽、双击打开、Command 点击 Reveal。
证据：`MacEverything/App/ResultRow.swift:57-124`
打开、Reveal、复制路径分别由 `NSWorkspace` 和 `NSPasteboard` 实现。
证据：`MacEverything/App/ResultRow.swift:150-171`
内容结果行提供相同的打开、Reveal、复制和拖拽能力。
证据：`MacEverything/App/ContentResultRow.swift:49-73`
证据：`MacEverything/App/ContentResultRow.swift:94-107`
搜索选项由 `SearchOptions.shared` 单例维护。
证据：`MacEverything/App/SearchOptions.swift:3-33`
选项会编译成 C++ 查询前缀：`regex:`、`case:`、`ww:`、`wfn:`。
证据：`MacEverything/App/SearchOptions.swift:39-48`
Regex、Whole Word、Match Filename 三者互斥。
证据：`MacEverything/App/SearchOptions.swift:7-31`
内容设置窗口从 Bridge 读取扩展名、最大文件大小和已索引文件数。
证据：`MacEverything/App/ContentSettingsView.swift:84-91`
AI 设置窗口是另一个边界：`AISettingsView` 通过 `AIServiceClient` 管理模型/prompt，默认访问 `http://127.0.0.1:19861`，它不走 `MacSearchBridge`，也不同于实际搜索翻译所用的 Core HTTP/Bridge 路径。
证据：`MacEverything/App/AISettingsView.swift:2-23`、`MacEverything/App/AIServiceClient.swift:40-105`
内容设置变更后写回 Bridge，并在全局队列重建内容索引。
证据：`MacEverything/App/ContentSettingsView.swift:105-115`
Bridge 会把内容扩展名和最大文件大小保存到 content config。
证据：`MacEverything/Bridge/MacSearchBridge+Content.mm:83-130`
热键设置窗口把 keyCode 和 modifiers 写入 `UserDefaults`，再发 `.hotkeyChanged`。
证据：`MacEverything/App/ShortcutSettingsView.swift:83-97`
`HotkeyManager` 监听 `.hotkeyChanged`，取消旧热键并重新注册。
证据：`MacEverything/App/HotkeyManager.swift:8-17`
热键底层使用 Carbon `RegisterEventHotKey`。
证据：`MacEverything/App/HotkeyManager.swift:19-41`
热键触发时，如果主窗口可见且 App 激活则隐藏，否则激活 App 并显示窗口。
证据：`MacEverything/App/HotkeyManager.swift:43-59`
权限 banner 用 `~/Library/Safari` 可读性作为 Full Disk Access 代理检测。
证据：`MacEverything/App/PermissionView.swift:3-36`
权限按钮打开 macOS 隐私设置中的 Full Disk Access 页面。
证据：`MacEverything/App/PermissionView.swift:38-43`

## 11. 关闭流程

用户退出时，`applicationShouldTerminate` 在后台队列调用 `prepareForTermination()`。
证据：`MacEverything/App/AppDelegate.swift:33-41`
后台关闭完成后回到主线程调用 `reply(toApplicationShouldTerminate:)`。
证据：`MacEverything/App/AppDelegate.swift:34-39`
`applicationWillTerminate` 再次调用 `prepareForTermination()` 作为幂等兜底。
证据：`MacEverything/App/AppDelegate.swift:43-46`
Bridge 的 `prepareForTermination()` 调 C++ shutdown，随后结束日志系统。
证据：`MacEverything/Bridge/MacSearchBridge.mm:314-318`

## 12. 主要风险与维护关注点

风险 1：`startIncrementalFrom:cachePath:walPath:` 接收 Swift 传入路径，但实现没有把这些参数传给 C++。
证据：`MacEverything/Bridge/MacSearchBridge.mm:223-252`
Swift 侧维护自己的 `cachePath`、`walPath`、`pagesPath`、`ptablePath`。
证据：`MacEverything/App/SearchViewModel.swift:77-102`
Bridge 初始化则使用 `PathUtils::getDefaultCachePath()`。
证据：`MacEverything/Bridge/MacSearchBridge.mm:102-114`
如果两套路径未来分叉，UI 删除缓存可能不会影响 C++ 实际使用的缓存。
风险 2：文件搜索不是引擎级分页。
Swift 一次从 Bridge 拉取最多 10,000 个 `MEFileResult`，首屏只显示 100 个。
证据：`MacEverything/App/SearchViewModel.swift:70-72`
证据：`MacEverything/App/SearchViewModel.swift:336-370`
宽泛查询会增加 C++→ObjC→Swift 封送成本和内存峰值。
风险 3：内容搜索没有 UI 分页，且最多只请求 200 条。
证据：`MacEverything/App/SearchViewModel.swift:392-420`
证据：`MacEverything/App/ContentView.swift:200-223`
这可能导致“命中很多但 UI 只展示一小段”的产品认知差异。
风险 4：内容 snippet 生成使用 `dispatch_apply` 并行读文件。
证据：`MacEverything/Bridge/MacSearchBridge+Content.mm:41-58`
空查询中 20ms 延迟注释已暗示旧查询线程池竞争存在。
证据：`MacEverything/App/SearchViewModel.swift:269-275`
风险 5：热键默认值不一致。
`HotkeyManager` 无保存值时默认 Command-Space。
证据：`MacEverything/App/HotkeyManager.swift:24-30`
`ShortcutSettingsView` 初始和 Reset 默认是 Option-Space。
证据：`MacEverything/App/ShortcutSettingsView.swift:3-8`
证据：`MacEverything/App/ShortcutSettingsView.swift:62-68`
这会导致首次启动默认热键和设置页显示/重置语义不一致。
风险 6：Full Disk Access 检测是启发式。
它只检查 Safari 目录可读性，不能代表所有受 TCC 保护路径。
证据：`MacEverything/App/PermissionView.swift:32-36`
检测任务只在没有权限时轮询；一旦有权限，后续撤销权限不会被持续发现。
证据：`MacEverything/App/PermissionView.swift:6-12`
风险 7：`SearchViewModel` 聚合了启动、搜索、AI、内容搜索、历史、分页和刷新节流。
证据：`MacEverything/App/SearchViewModel.swift:22-617`
这个文件是 UI 行为回归的最高风险集中点。
风险 8：输入框高亮 tokenizer 是 Swift 侧镜像实现，结果高亮 hint 来自 C++ AST。
证据：`MacEverything/App/HighlightedSearchField.swift:22-159`
证据：`MacEverything/Bridge/MacSearchBridge.mm:461-475`
两套解析规则需要同步演进，否则“输入框颜色”和“结果高亮”可能出现差异。
风险 9：AI 翻译失败时会回退到原始 query 搜索。
证据：`MacEverything/App/SearchViewModel.swift:372-390`
这对可用性友好，但 UI 只展示 `translatedQuery` 成功结果，失败原因没有直接展示在主搜索页。
风险 10：Bridge 中部分 AI 方法使用 `@try/@catch` 返回失败字典。
证据：`MacEverything/Bridge/MacSearchBridge+Semantic.mm:6-52`
如果底层抛出的是 C++ 异常而非 Objective-C exception，该保护边界不一定覆盖。

## 13. 建议的演进原则

新增 UI 状态时，优先判断它属于扫描、普通搜索、内容搜索、AI、最近文件中的哪一个子状态。
避免在 View 中直接调用 Bridge；应通过 ViewModel 集中做 generation 校验和主线程更新。
新增 Bridge API 时，应明确它是否同步、是否可取消、是否需要 main queue 回调。
查询类 API 如果可能返回大量数据，应优先设计引擎级分页或 cursor，而不是 UI 侧缓存完整结果。
修改查询语法时，要同时检查 Swift 输入框 tokenizer、C++ query parser 和 C++ highlight hint extractor。
修改内容索引设置时，要确认 Swift cache path 与 C++ `PathUtils` 默认路径是否一致。
修改热键默认值时，应同时更新 `HotkeyManager` 和 `ShortcutSettingsView`。
修改权限提示时，应注明检测的是 TCC proxy，不是完整的权限证明。
涉及 `SearchViewModel` 的改动应优先补状态机测试，特别是取消、generation、空查询、AI Enter、内容搜索切换。
涉及 Bridge 回调的改动必须验证回调线程，禁止直接从 C++ 后台线程更新 SwiftUI 状态。