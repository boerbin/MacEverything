# 内容索引架构说明：`infile:` 路由、三元组倒排与持久化
> 适用范围：MacEverything 的全文内容索引与内容搜索链路。
> 本文只描述当前代码实现，不描述目标态。

## 1. 总览

MacEverything 的内容搜索与文件名搜索是两套索引体系。
文件名搜索由 `SearchEngine` 维护路径、名称、过滤器和主索引。
内容搜索由 `ContentIndex` 维护文件内容的三元组倒排索引。
两者通过同一个 `fileIndex` 关联。
`ContentMatch.fileIndex` 明确表示它是 `SearchEngine` SoA 列中的索引：
`MacEverything/Core/ContentIndex.h:15`。
`ContentIndex` 的注释说明它把三元组映射到包含该三元组的 `fileIndex` 集合：
`MacEverything/Core/ContentIndex.h:28`。
内部结构是 `invertedIndex_ : trigram -> sorted list of fileIndices`：
`MacEverything/Core/ContentIndex.h:135`。
每个文件还保存 `contentHash`、三元组集合和 `lastModTime`：
`MacEverything/Core/ContentIndex.h:21`。

## 2. 核心数据流

用户在 GUI 输入 `infile:关键词`。
`SearchViewModel` 识别 `infile:` 前缀并切换到内容搜索：
`MacEverything/App/SearchViewModel.swift:285`。
它截取前缀之后的关键词：
`MacEverything/App/SearchViewModel.swift:292`。
GUI 内容搜索有 300ms debounce：
`MacEverything/App/SearchViewModel.swift:301`。
最终调用 `bridge.queryContent(keyword, maxResults: 200)`：
`MacEverything/App/SearchViewModel.swift:393`。

HTTP 则不使用 `infile:` 前缀。
HTTP 内容搜索入口是 `GET /api/search/content?q=...&limit=...`：
`MacEverything/Core/HttpServer.cpp:323`。
它直接读取 query 参数 `q`：
`MacEverything/Core/HttpServer.cpp:433`。
然后调用 `contentIndex->query(keyword, limit)`：
`MacEverything/Core/HttpServer.cpp:456`。

## 3. 组件职责

`ServiceEngine` 持有 `SearchEngine`、`FileSystemWatcher` 与 `ContentIndex`。
构造函数中创建 `ContentIndex`：
`MacEverything/Core/ServiceEngine.cpp:14`。
具体初始化行是：
`MacEverything/Core/ServiceEngine.cpp:19`。

`ContentIndex` 只知道文件内容与 `fileIndex`。
它不知道如何从 `fileIndex` 解析完整路径。
`ContentIndex::query()` 中的注释明确说路径解析由桥接层完成：
`MacEverything/Core/ContentIndex.cpp:643`。
因此内容搜索分两段：
第一段在 C++ 内容索引中返回候选 `fileIndex`。
第二段由 Bridge 或 HTTP 使用 `SearchEngine` 解析路径。

`MacSearchBridge+Content` 是 GUI 内容搜索的适配层。
它取出 `SearchEngine` 与 `ContentIndex`：
`MacEverything/Bridge/MacSearchBridge+Content.mm:8`。
然后调用 `contentIndex->query()`：
`MacEverything/Bridge/MacSearchBridge+Content.mm:17`。
再用 `engine->getRecord()` 与 `SearchEngine::makeFullPath()` 还原路径：
`MacEverything/Bridge/MacSearchBridge+Content.mm:29`。

## 4. `infile:` 路由语义

`infile:` 只在 SwiftUI 搜索框中有特殊语义。
输入文本小写后以 `infile:` 开头时，GUI 设置 `isContentSearch = true`：
`MacEverything/App/SearchViewModel.swift:285`。
这条分支会清空文件名搜索结果：
`MacEverything/App/SearchViewModel.swift:287`。
关键词为空时不查询，并清空结果计数：
`MacEverything/App/SearchViewModel.swift:294`。
非内容搜索时，GUI 会回到普通 `performSearch()` 或 AI 翻译后的文件名搜索：
`MacEverything/App/SearchViewModel.swift:306`。

这个设计意味着：
`infile:` 不是通用查询语法的一部分。
它是 GUI ViewModel 的路由前缀。
HTTP 内容搜索使用单独 endpoint。
普通 `/api/search` 不会因为 `q=infile:xxx` 自动转入内容索引。
普通搜索 endpoint 在 `/api/search` 分支中调用 `engine->query()`：
`MacEverything/Core/HttpServer.cpp:321`。
内容搜索 endpoint 在 `/api/search/content` 分支中调用 `handleContentSearch()`：
`MacEverything/Core/HttpServer.cpp:323`。

## 5. 三元组内容索引

三元组类型是连续 3 个小写 ASCII 字节打包到 `uint32_t` 低 24 位：
`MacEverything/Core/ContentIndex.h:11`。
`makeTrigram()` 把 3 个 byte 打包为 `a << 16 | b << 8 | c`：
`MacEverything/Core/ContentIndex.h:120`。
`extractTrigrams()` 对文本逐字节滑窗并小写化：
`MacEverything/Core/ContentIndex.cpp:170`。
小于 3 字节的文本没有三元组：
`MacEverything/Core/ContentIndex.cpp:171`。

实现使用 `thread_local` bitmap 去重，避免每次分配 2MB bitmap：
`MacEverything/Core/ContentIndex.cpp:173`。
每次调用只清理上次置位过的 dirty bits：
`MacEverything/Core/ContentIndex.cpp:179`。
索引更新时，每个三元组的 posting list 保持排序插入：
`MacEverything/Core/ContentIndex.cpp:431`。
排序 posting list 的目的，是支持查询时用 `std::set_intersection`：
`MacEverything/Core/ContentIndex.cpp:613`。

## 6. 文件内容抽取

内容索引默认不索引任何扩展名。
构造函数注释写明：没有默认扩展名，内容索引是 opt-in：
`MacEverything/Core/ContentIndex.cpp:20`。
虽然头文件的 `setExtensions()` 注释写着空列表等于索引全部文本文件：
`MacEverything/Core/ContentIndex.h:59`。
但实际 `indexFile()` 中 `extensions_.empty()` 会直接返回 false：
`MacEverything/Core/ContentIndex.cpp:337`。
因此当前实现应按“扩展名白名单必填”理解。

普通文本读取走 `readFileIfText()`。
它用 `fopen()` 打开文件：
`MacEverything/Core/ContentIndex.cpp:138`。
文件大小小于等于 0 或超过 `maxFileSize_` 时跳过：
`MacEverything/Core/ContentIndex.cpp:142`。
读取后检查前 8KB 是否包含 NUL 字节，包含则视为二进制并跳过：
`MacEverything/Core/ContentIndex.cpp:159`。

富文档抽取由 `RichTextExtractor` 负责。
它声明支持 PDF、DOC、DOCX、XLS、PPT、RTF、ODT 等扩展名识别：
`MacEverything/Core/RichTextExtractor.h:14`。
公开 API 是 `extractRichDocText(path, maxTextBytes)`：
`MacEverything/Core/RichTextExtractor.h:12`。
实际策略先尝试 Spotlight metadata：
`MacEverything/Core/RichTextExtractor.mm:143`。
Spotlight 失败后再走框架 fallback：
`MacEverything/Core/RichTextExtractor.mm:147`。
PDF 走 PDFKit：
`MacEverything/Core/RichTextExtractor.mm:125`。
DOC、DOCX、RTF、ODT 走 `NSAttributedString`：
`MacEverything/Core/RichTextExtractor.mm:129`。
XLS、XLSX、PPT、PPTX、ODS、ODP 当前没有框架 fallback：
`MacEverything/Core/RichTextExtractor.mm:137`。

## 7. 单文件索引流程

`indexFile(fileIndex, fullPath, modTime)` 是最小索引单元。
第一步检查扩展名：
`MacEverything/Core/ContentIndex.cpp:337`。
扩展名不匹配时直接返回 false：
`MacEverything/Core/ContentIndex.cpp:344`。
如果传入 `modTime > 0` 且与已保存 `lastModTime` 相同，则跳过 I/O：
`MacEverything/Core/ContentIndex.cpp:347`。
读取普通文本失败时，如果是富文档扩展名，会尝试富文档抽取：
`MacEverything/Core/ContentIndex.cpp:366`。
内容为空则返回 false，或仅更新已索引项的 `lastModTime`：
`MacEverything/Core/ContentIndex.cpp:371`。

非空内容会计算 FNV-1a hash：
`MacEverything/Core/ContentIndex.cpp:110`。
然后提取三元组：
`MacEverything/Core/ContentIndex.cpp:390`。
如果 hash 未变化，只在必要时更新 `lastModTime` 并返回：
`MacEverything/Core/ContentIndex.cpp:393`。
如果内容变化，会先从旧 posting list 删除旧三元组：
`MacEverything/Core/ContentIndex.cpp:415`。
再把新三元组插入倒排索引：
`MacEverything/Core/ContentIndex.cpp:431`。
最后写入新的 `ContentFileInfo`：
`MacEverything/Core/ContentIndex.cpp:440`。

## 8. 启动与后台索引

全量扫描完成后，`ServiceEngine` 会在后台设置内容持久化并启动内容索引：
`MacEverything/Core/ServiceEngine.cpp:187`。
增量启动且 FSEvents replay 成功后，也会启动内容持久化和内容索引：
`MacEverything/Core/ServiceEngine.cpp:392`。
后台全量同步后同样会启动：
`MacEverything/Core/ServiceEngine.cpp:462`。

`setupContentPersistence()` 会先加载内容配置：
`MacEverything/Core/ServiceEngine+Content.cpp:22`。
内容 base 文件是 `content_index.bin`：
`MacEverything/Core/ServiceEngine+Content.cpp:24`。
内容 WAL 文件是 `content_index.wal`：
`MacEverything/Core/ServiceEngine+Content.cpp:25`。
加载后会根据当前 `SearchEngine` 的 regular file 集合修剪 stale content entries：
`MacEverything/Core/ServiceEngine+Content.cpp:31`。
随后 attach WAL 并启动 event-driven compaction：
`MacEverything/Core/ServiceEngine+Content.cpp:49`。

## 9. 全量模式与增量模式

`startContentIndexing()` 会先判断内容索引是否已有条目。
如果已有条目，进入 incremental mode：
`MacEverything/Core/ServiceEngine+Content.cpp:87`。
增量模式只遍历已经内容索引过的 file indices：
`MacEverything/Core/ServiceEngine+Content.cpp:91`。
如果没有内容条目，进入 full scan mode：
`MacEverything/Core/ServiceEngine+Content.cpp:100`。
全量模式遍历所有 regular files：
`MacEverything/Core/ServiceEngine+Content.cpp:107`。

真正的索引执行使用 `dispatch_apply` 并行处理：
`MacEverything/Core/ServiceEngine+Content.cpp:125`。
每个文件调用 `contentIndex->indexFile()`：
`MacEverything/Core/ServiceEngine+Content.cpp:131`。
若索引发生变化，则取出 `ContentFileInfo` 并追加 WAL add 记录：
`MacEverything/Core/ServiceEngine+Content.cpp:133`。
进度每处理 500 个条目回调一次：
`MacEverything/Core/ServiceEngine+Content.cpp:144`。
完成回调报告的是当前已索引文件总数：
`MacEverything/Core/ServiceEngine+Content.cpp:155`。

## 10. 查询流程

`ContentIndex::query()` 先拒绝空关键词：
`MacEverything/Core/ContentIndex.cpp:568`。
然后把关键词小写：
`MacEverything/Core/ContentIndex.cpp:571`。
长度大于等于 3 时提取关键词三元组：
`MacEverything/Core/ContentIndex.cpp:577`。
如果任一关键词三元组不存在于倒排索引，直接无结果：
`MacEverything/Core/ContentIndex.cpp:584`。
它会按 posting list 大小排序，从最小集合开始相交：
`MacEverything/Core/ContentIndex.cpp:597`。
集合相交发生在：
`MacEverything/Core/ContentIndex.cpp:613`。
长度小于 3 的关键词当前直接返回空：
`MacEverything/Core/ContentIndex.cpp:623`。

关键点：`ContentIndex::query()` 当前不自己生成 snippet。
它返回带空 snippet 的候选 `ContentMatch`：
`MacEverything/Core/ContentIndex.cpp:648`。
`fileIndex` 被写入结果：
`MacEverything/Core/ContentIndex.cpp:654`。
`matchOffset` 初始为 0：
`MacEverything/Core/ContentIndex.cpp:656`。

## 11. Snippet 生成与精确验证

GUI Bridge 会对候选结果生成 snippet。
它先预解析候选路径：
`MacEverything/Bridge/MacSearchBridge+Content.mm:20`。
然后用 `dispatch_apply` 并行生成 snippet：
`MacEverything/Bridge/MacSearchBridge+Content.mm:50`。
每个候选调用 `ContentIndex::generateSnippet(fullPath, key, offset)`：
`MacEverything/Bridge/MacSearchBridge+Content.mm:52`。
只有 snippet 非空才标记为 valid：
`MacEverything/Bridge/MacSearchBridge+Content.mm:54`。
最终 GUI 只返回 `valid` 的结果：
`MacEverything/Bridge/MacSearchBridge+Content.mm:61`。

这一步是 GUI 的精确匹配验证。
原因是三元组相交只能产生候选。
候选文件可能包含所有关键词三元组，但不一定包含连续关键词。
`generateSnippet()` 会重新读取文件并查找完整 keyword。
富文档 snippet 会重新抽取富文档文本：
`MacEverything/Core/ContentIndex.cpp:207`。
普通文件 snippet 用 64KB chunk 扫描，最多读 1MB：
`MacEverything/Core/ContentIndex.cpp:240`。
chunk 之间保留 `keyword.size() - 1` 的 overlap，避免跨块漏匹配：
`MacEverything/Core/ContentIndex.cpp:281`。
找到后返回上下文并把换行替换成空格：
`MacEverything/Core/ContentIndex.cpp:307`。

## 12. GUI 与 HTTP 的差异

GUI 内容搜索返回的是经过 snippet 验证的结果。
没有 snippet 的候选会被过滤：
`MacEverything/Bridge/MacSearchBridge+Content.mm:63`。
因此 GUI 结果更接近“确实包含关键词”的语义。

HTTP 内容搜索当前直接序列化 `ContentIndex::query()` 的 `match.snippet`。
序列化位置是：
`MacEverything/Core/HttpServer.cpp:470`。
但 `ContentIndex::query()` 返回的 snippet 默认为空：
`MacEverything/Core/ContentIndex.cpp:648`。
所以 HTTP 结果可能包含空 snippet。
HTTP 也没有像 GUI 那样调用 `generateSnippet()` 做精确验证。
这意味着 HTTP `/api/search/content` 可能返回三元组候选，而不是 GUI 语义下的验证结果。

配置更新也存在 GUI 与 HTTP 差异。
GUI 设置最大文件大小会调用 bridge setter 并保存 config：
`MacEverything/Bridge/MacSearchBridge+Content.mm:97`。
GUI 设置扩展名也会保存 config：
`MacEverything/Bridge/MacSearchBridge+Content.mm:84`。
设置页 Apply 后会触发后台 rebuild：
`MacEverything/App/ContentSettingsView.swift:106`。
HTTP `POST /api/content/config` 调用 admin callback：
`MacEverything/Core/HttpServer.cpp:571`。
callback 只在内存中 `setExtensions` 和 `setMaxFileSize`：
`MacEverything/Bridge/MacSearchBridge.mm:278`。
该 callback 没有保存 config，也没有自动 rebuild。

## 13. 内容索引持久化

内容索引 base 文件格式 magic 是 `MECI`：
`MacEverything/Core/ContentIndex.cpp:16`。
当前内容索引格式版本是 2：
`MacEverything/Core/ContentIndex.cpp:18`。
保存时每个文件写入 `fileIndex`、`contentHash`、三元组数量、三元组数组、`lastModTime`：
`MacEverything/Core/ContentIndex.cpp:711`。
base save 使用临时文件：
`MacEverything/Core/ContentIndex.cpp:693`。
写完后 `fsync`：
`MacEverything/Core/ContentIndex.cpp:726`。
最后 `rename` 原子替换：
`MacEverything/Core/ContentIndex.cpp:735`。

加载 base 时会校验 magic 和版本：
`MacEverything/Core/ContentIndex.cpp:746`。
它限制 `fileCount` 最大合理值：
`MacEverything/Core/ContentIndex.cpp:765`。
也限制单文件三元组数量：
`MacEverything/Core/ContentIndex.cpp:787`。
批量加载时先 `push_back` posting list，最后统一排序：
`MacEverything/Core/ContentIndex.cpp:810`。

## 14. 内容 WAL 与 compact

内容 WAL 的 magic 是 `CWL1`：
`MacEverything/Core/ContentIndexPersistence.h:56`。
最大 WAL 大小是 20MB：
`MacEverything/Core/ContentIndexPersistence.h:53`。
Add entry 包含 `fileIndex`、`contentHash`、三元组、`lastModTime`：
`MacEverything/Core/ContentIndexPersistence.h:23`。
Remove entry 只包含 `fileIndex`：
`MacEverything/Core/ContentIndexPersistence.h:26`。
每条 WAL entry 写 CRC32：
`MacEverything/Core/ContentIndexPersistence.cpp:70`。
读取时会验证 CRC，失败则停止 replay：
`MacEverything/Core/ContentIndexPersistence.cpp:184`。

`ContentIndexPersistence::load()` 先加载 base：
`MacEverything/Core/ContentIndexPersistence.cpp:253`。
再读取并 replay WAL：
`MacEverything/Core/ContentIndexPersistence.cpp:263`。
Add 通过 `insertFileInfo()` 放回内存索引：
`MacEverything/Core/ContentIndexPersistence.cpp:269`。
Remove 通过 `removeFile()` 应用：
`MacEverything/Core/ContentIndexPersistence.cpp:272`。

自动 compact 是事件驱动，不是固定周期。
`startAutoCompaction()` 创建串行 compaction queue：
`MacEverything/Core/ContentIndexPersistence.cpp:380`。
WAL append 后会 `scheduleCompaction()`：
`MacEverything/Core/ContentIndexPersistence.cpp:419`。
延迟时间是 60 秒：
`MacEverything/Core/ContentIndexPersistence.h:115`。
非强制 compact 会在 WAL 不 dirty 或 entry 数少于 50 时跳过：
`MacEverything/Core/ContentIndexPersistence.cpp:315`。
强制 compact 用于退出或主索引 compact 后同步落盘：
`MacEverything/Core/ServiceEngine.cpp:571`。

## 15. 主索引 compact 对内容索引的影响

内容索引依赖 `SearchEngine` 的 `fileIndex`。
主索引 compact 会删除 tombstone 并重排 record。
因此主索引 compact 后必须 remap 内容索引的 file indices。
`IndexPersistence::compact()` 中调用 `engine_->compactRecords()` 得到 remap：
`MacEverything/Core/IndexPersistence.cpp:252`。
如果 remap 非空且存在内容索引，则调用 `contentIndex_->remapFileIndices(remap)`：
`MacEverything/Core/IndexPersistence.cpp:263`。
随后强制内容 compact：
`MacEverything/Core/IndexPersistence.cpp:266`。
`ContentIndex::remapFileIndices()` 会重建 `fileInfos_` 和 `invertedIndex_`：
`MacEverything/Core/ContentIndex.cpp:477`。
未出现在 remap 中的旧索引会被视为 tombstoned 并丢弃：
`MacEverything/Core/ContentIndex.cpp:487`。

## 16. FSEvents 更新路径与风险

FSEvents 更新路径会收集普通 `SearchEngine` mutation ops。
同时收集内容索引更新列表 `contentUpdates`：
`MacEverything/Core/ServiceEngine+FSEvents.cpp:19`。
删除事件会加入 remove content update：
`MacEverything/Core/ServiceEngine+FSEvents.cpp:34`。
regular file 更新会加入 content update：
`MacEverything/Core/ServiceEngine+FSEvents.cpp:65`。
风险点是：内容索引更新发生在 `engine->batchMutate()` 之前。
replay 路径中先调用 `updateContentForPath()`：
`MacEverything/Core/ServiceEngine+FSEvents.cpp:71`。
之后才 `engine->batchMutate()`：
`MacEverything/Core/ServiceEngine+FSEvents.cpp:83`。
live monitoring 路径也是同样顺序：
`MacEverything/Core/ServiceEngine+FSEvents.cpp:174`。
之后才批量写入 SearchEngine：
`MacEverything/Core/ServiceEngine+FSEvents.cpp:186`。

`updateContentForPath()` 对新增或更新文件先用旧 engine 查 `indexForPath(fullPath)`：
`MacEverything/Core/ServiceEngine+Content.cpp:235`。
如果这个文件尚未进入 `SearchEngine`，则 `fileIndex == UINT32_MAX`，内容索引不会写入。
因此新增文件可能在 FSEvents 到达时被主索引稍后接收，但内容索引提前查不到 fileIndex。
这是当前代码中的更新顺序风险。

另一个风险来自启动增量内容索引。
当内容索引非空时，它只重查已索引文件：
`MacEverything/Core/ServiceEngine+Content.cpp:91`。
离线期间新增且符合扩展名的文件，如果没有通过 FSEvents 成功进入内容索引，可能要等用户 rebuild。
设置页可以触发 rebuild：
`MacEverything/App/ContentSettingsView.swift:111`。
HTTP 也提供 `/api/content/rebuild`：
`MacEverything/Core/HttpServer.cpp:341`。

## 17. 进度与 UI 表现

Bridge 把内容索引进度转发到 Swift 主线程：
`MacEverything/App/SearchViewModel.swift:153`。
Swift 设置 `isContentIndexing = true` 并记录 `(indexed,total)`：
`MacEverything/App/SearchViewModel.swift:155`。
完成时清空进度并更新 `contentIndexedCount`：
`MacEverything/App/SearchViewModel.swift:161`。
如果当前正处在内容搜索模式，完成后自动刷新内容搜索结果：
`MacEverything/App/SearchViewModel.swift:167`。
注意 `startContentIndexing()` 的 `indexed` 计数实际上是处理过的条目数：
`MacEverything/Core/ServiceEngine+Content.cpp:142`。
完成回调报告的是 `contentIndex->indexedFileCount()`：
`MacEverything/Core/ServiceEngine+Content.cpp:155`。
因此进度条的 current 不等于成功索引文件数。

## 18. 测试覆盖

`test_all.cpp` 注册了内容索引基础测试：
`test_all.cpp:283`。
也注册了 trigram index 测试：
`test_all.cpp:284`。
内容查询 benchmark 注册在：
`test_all.cpp:285`。
并行 snippet 测试注册在：
`test_all.cpp:288`。
内容 WAL tracking、compact threshold、compaction guard、modTime 测试分别注册在：
`test_all.cpp:309`。
`test_all.cpp:310`。
`test_all.cpp:311`。
`test_all.cpp:313`。
富文档抽取测试注册在：
`test_all.cpp:355`。

基础测试覆盖三元组提取、二进制文件拒绝、查询、保存与加载：
`tests/test_content_index.h:7`。
它验证二进制文件不会被索引：
`tests/test_content_index.h:41`。
它验证保存后重新加载仍可查询：
`tests/test_content_index.h:52`。

`test_content_modtime.h` 覆盖 `modTime` 相同跳过 reindex：
`tests/test_content_modtime.h:28`。
覆盖 `modTime` 变化触发 reindex：
`tests/test_content_modtime.h:44`。
覆盖 WAL replay 插入时保留 `lastModTime`：
`tests/test_content_modtime.h:96`。
覆盖 prune stale entries：
`tests/test_content_modtime.h:180`。

`test_parallel_snippets.h` 覆盖 snippet 正确性和并行生成：
`tests/test_parallel_snippets.h:25`。
它验证所有并行 snippet 都有效：
`tests/test_parallel_snippets.h:77`。
`test_rich_text_extractor.h` 覆盖 PDF、DOCX、RTF 抽取和内容索引集成：
`tests/test_rich_text_extractor.h:88`。
它验证 ContentIndex 可以索引 PDF：
`tests/test_rich_text_extractor.h:39`。
`test_content_compaction_guard.h` 覆盖非内容索引文件删除时不污染 WAL：
`tests/test_content_compaction_guard.h:15`。
它也覆盖强制 compact 处理 replay 后但本 session 不 dirty 的 WAL：
`tests/test_content_compaction_guard.h:101`。

## 19. 维护建议

修改 GUI 内容搜索语义时，应同时检查 `SearchViewModel` 的 `infile:` 分支和 `MacSearchBridge+Content` 的 snippet 验证。
修改 HTTP 内容搜索时，应注意它当前没有 GUI 那样的 snippet 验证。
修改 `ContentIndex::query()` 时，应保持 posting list 排序和集合相交语义。
修改主索引 compact 时，应保留内容索引 remap 与强制 content compact。
修改 FSEvents 更新顺序时，应重点验证新文件进入主索引和内容索引的一致性。
修改内容配置时，应明确区分“仅内存变更”“保存 config”“触发 rebuild”三件事。
测试入口应继续只在 `test_all.cpp` 注册，不在入口文件中实现测试函数。
