# 搜索 / 查询语言 / 索引架构

> 本文描述 MacEverything 文件名/路径搜索子系统，不包含 `infile:` 内容索引的内部实现。

## 1. 子系统定位

MacEverything 的主搜索路径由 `SearchEngine` 提供。

它负责：

- 保存扫描得到的文件/目录记录；
- 解析 Everything 风格查询语法；
- 在名称、路径、扩展名、元数据列上选择候选集；
- 使用 trigram、SIMD、短查询缓存、纯过滤扫描等路径降低延迟；
- 支持同一搜索会话内的协作式取消；
- 在查询和增量更新之间用读写锁保护核心数据结构。

核心入口是：

- `SearchEngine::query(...)`：普通查询入口，含预处理、路由、短查询缓存、DIR_LIST 特判；
- `SearchEngine::queryAdvanced(...)`：统一 AST 查询执行路径；
- `SearchEngine::recentIndices(...)`：最近文件 API，不属于普通查询路由。

证据：

- `MacEverything/Core/SearchEngine.h:160-181`
- `MacEverything/Core/SearchEngineQuery.cpp:180-271`
- `MacEverything/Core/SearchEngineAdvancedQuery.cpp:607-1090`
- `MacEverything/Core/SearchEngine.cpp:812-846`

---

## 2. 核心数据模型

`SearchEngine` 不是以 `vector<FileRecord>` 作为主存储，而是使用 SoA（Structure of Arrays）布局。

主要列包括：

- `origNamePool_`：原始大小写文件名字符串池；
- `namePool_`：小写文件名字符串池；
- `pathIndices_`：每条记录指向路径池的索引；
- `pathPool_`：原始路径字符串池；
- `lowerPathPool_`：小写路径字符串池；
- `types_`：类型列，`0=tombstone`，`1=file`，`2=directory`；
- `sizes_`：文件大小；
- `modTimes_`：修改时间；
- `inodes_` / `devIds_`：文件系统身份字段。

证据：

- `MacEverything/Core/SearchEngine.h:379-396`

核心索引包括：

- `nameTrigramIndex_`：文件名 trigram 倒排索引；
- `pathTrigramIndex_`：路径 trigram 倒排索引；
- `pathIdxToRecords_`：路径索引到记录索引的反向映射；
- `extensionIndex_`：扩展名到记录索引的倒排表；
- `ShortQueryCache shortQueryCache_`：1-2 字母短查询缓存；
- `recentCache_`：最近修改文件缓存。

证据：

- `MacEverything/Core/SearchEngine.h:398-406`
- `MacEverything/Core/SearchEngine.h:510-534`

---

## 3. 查询语言总览

查询语言是 Everything 风格的轻量表达式语言。

基础语法：

| 语法 | 含义 | 代码依据 |
|---|---|---|
| `foo` | 普通 term，默认大小写不敏感，匹配文件名或完整路径 | `MacEverything/Core/QueryAST.h:11-36` |
| `"foo bar"` | 引号短语，作为单个 term | `MacEverything/Core/QueryTokenizer.h:68-79` |
| `foo bar` | 隐式 AND | `MacEverything/Core/QueryParser.cpp:56-81` |
| `foo \| bar` | OR | `MacEverything/Core/QueryParser.cpp:30-54` |
| `!foo` | NOT | `MacEverything/Core/QueryParser.cpp:84-95` |
| `<foo \| bar>` | 分组 | `MacEverything/Core/QueryParser.cpp:97-113` |
| `*.cpp` / `test??.txt` | glob term | `MacEverything/Core/ASTGlobTransform.h:4-29` |

支持的过滤器和修饰符由 tokenizer 的 known filter 集合决定：

- `ext:`
- `size:`
- `file:`
- `folder:`
- `type:`
- `path:`
- `nopath:`
- `parent:`
- `depth:`
- `len:`
- `dm:` / `datemodified:`
- `dc:` / `datecreated:`
- `da:` / `dateaccessed:`
- `case:`
- `nocase:`
- `regex:`
- `ww:` / `wholeword:`
- `wfn:` / `wholefilename:`
- `audio:`
- `video:`
- `pic:`
- `doc:`
- `exe:`
- `zip:`
- `content:`
- `type:`

证据：

- `MacEverything/Core/QueryTokenizer.h:161-181`

过滤器解析规则：

- `ext:cpp;h;hpp` 被解析为扩展名列表；
- `size:>1mb`、`size:100kb..1mb` 使用数值比较；
- `len:`、`depth:` 复用数值比较解析；
- `path:`、`nopath:`、`parent:` 会把参数转成小写；
- `case:`、`nocase:`、`regex:`、`ww:`、`wfn:` 会从 FILTER 节点转换成 TERM 节点；
- `audio:`、`video:`、`pic:`、`doc:`、`exe:`、`zip:` 会展开成 `ext:` 过滤器。

证据：

- `MacEverything/Core/QueryFilterParser.h:21-80`
- `MacEverything/Core/QueryFilterParser.h:91-147`

日期语法由 `QueryDateParser` 负责。

证据：

- `MacEverything/Core/QueryDateParser.h:9-303`

---

## 4. 斜杠结构化查询

包含 `/` 的查询会经过结构化路径解析。

主要模式：

| 查询 | 模式 | 语义 |
|---|---|---|
| `/abc/def` | `SEGMENTS` | 名称匹配 `def`，路径约束包含 `abc` |
| `/abc/def/` | `DIR_EXACT` | 找名为 `def` 的目录，并满足路径约束 |
| `/abc/def/*` | `DIR_LIST` | 直接列出目标目录下一级子项 |
| `/abc/*/def` | `SEGMENTS` | 中间 `*` 作为邻接断开符 |
| `*.cpp` | `PLAIN` | 因 glob 字符进入普通 glob term，而不是结构化路径 |

证据：

- `MacEverything/Core/StructuredQueryParser.h:11-24`
- `MacEverything/Core/StructuredQueryParser.h:26-132`

结构化 transform 会把普通 TERM 转换成 AST 子树。

例如：

- `/usr/local/test`
- 转换为 `__pathseg` 内部过滤器 + 名称 term；
- `DIR_EXACT` 会额外加入 `folder:` 约束；
- `DIR_LIST` 不在 advanced AST 内处理，而是在 `query()` 顶层提前分流。

证据：

- `MacEverything/Core/ASTStructuredTransform.h:5-17`
- `MacEverything/Core/ASTStructuredTransform.h:31-86`
- `MacEverything/Core/SearchEngineQuery.cpp:197-246`

---

## 5. 查询预处理

`query()` 对原始输入做统一预处理：

1. 去掉首尾空白；
2. 展开开头的 `~` 到 `$HOME`；
3. 计算一份小写副本，避免后续重复 lower；
4. 获取当前 session generation，用于取消旧查询。

证据：

- `MacEverything/Core/SearchEngineQuery.cpp:151-174`
- `MacEverything/Core/SearchEngineQuery.cpp:186-190`

注意：`query()` 当前并不是“只有高级语法才进 advanced”。

实际路由是：

- `DIR_LIST` 特判；
- 短查询缓存特判；
- 其他全部进入 `queryAdvanced()`。

证据：

- `MacEverything/Core/SearchEngineQuery.cpp:197-271`

---

## 6. 路由决策树

当前主搜索路由可以概括为：

1. 空字符串：返回空结果；
2. 预处理失败或结果为空：返回空结果；
3. `parseQuery()` 判断是否为 `/path/*` 形式的 `DIR_LIST`；
4. 如果是 `DIR_LIST`：
   - 获取 shared lock；
   - 调用 `queryDirList()`；
   - 检查取消；
   - 释放锁；
   - 排序 / partial sort；
   - 返回结果。
5. 如果短查询缓存已构建，且小写 query 是合法 1-2 位 ASCII key：
   - 直接从 `shortQueryCache_` 取 top 结果；
   - 跳过 advanced AST 执行。
6. 其他所有查询：
   - 调用 `queryAdvanced()`。

证据：

- `MacEverything/Core/SearchEngineQuery.cpp:192-246`
- `MacEverything/Core/SearchEngineQuery.cpp:248-271`

`DIR_LIST` 分支在排序前释放锁。

证据：

- `MacEverything/Core/SearchEngineQuery.cpp:214-228`

短查询缓存分支在代码中没有显式获取 `shared_lock`，这一点与 advanced / DIR_LIST 主评估路径不同，属于并发边界上需要继续审查的实现细节。

证据：

- `MacEverything/Core/SearchEngineQuery.cpp:248-267`

---

## 7. Advanced 查询执行流水线

`queryAdvanced()` 的阶段如下：

1. 解析 AST；
2. 把含 `/` 的 term 转成结构化路径约束；
3. 把含 `*` / `?` 的 term 转成 glob；
4. 分析 AST 需要哪些字段；
5. 在加锁前预编译 regex；
6. 获取 shared lock；
7. 选择候选集来源；
8. 对候选或全量记录执行 AST；
9. 释放 shared lock；
10. 排序 / partial sort；
11. 填充 timing 信息。

证据：

- `MacEverything/Core/SearchEngineAdvancedQuery.cpp:619-648`
- `MacEverything/Core/SearchEngineAdvancedQuery.cpp:650-788`
- `MacEverything/Core/SearchEngineAdvancedQuery.cpp:789-1028`
- `MacEverything/Core/SearchEngineAdvancedQuery.cpp:1028-1079`

Regex 使用 RE2，并在多线程路径中为每个线程 clone 一份 regex cache，以避免共享 RE2 DFA 内部锁造成竞争。

证据：

- `MacEverything/Core/SearchEngineAdvancedQuery.cpp:93-125`
- `MacEverything/Core/SearchEngineAdvancedQuery.cpp:633-648`
- `MacEverything/Core/SearchEngineAdvancedQuery.cpp:824-827`

---

## 8. AST 求值语义

AST 节点类型包括：

- `TERM`
- `AND`
- `OR`
- `NOT`
- `FILTER`

证据：

- `MacEverything/Core/QueryAST.h:10-24`
- `MacEverything/Core/QueryAST.h:26-50`

TERM 匹配模式包括：

- `SUBSTRING`
- `GLOB`
- `REGEX`
- `WHOLEWORD`
- `WHOLEFILENAME`

证据：

- `MacEverything/Core/QueryAST.h:19-36`

默认 `SUBSTRING` 会先匹配小写文件名，再匹配小写完整路径。

如果 `nameOnly` 被结构化查询设置，则只匹配文件名，不匹配路径。

证据：

- `MacEverything/Core/SearchEngineAdvancedQuery.cpp:254-309`

FILTER 求值覆盖：

- `ext:`
- `size:`
- `file:`
- `folder:`
- `type:`
- `path:`
- `nopath:`
- `parent:`
- `depth:`
- `len:`
- `dm:` / `datemodified:`
- `dc:` / `datecreated:`
- `da:` / `dateaccessed:`
- 内部 `__pathseg`

证据：

- `MacEverything/Core/SearchEngineAdvancedQuery.cpp:127-252`

---

## 9. 候选集选择：竞争式 pre-filter

Advanced 查询不是固定使用某一种索引。

它会尝试多个候选来源，然后选最小的可用候选集。

候选来源包括：

1. 名称 trigram；
2. regex literal trigram；
3. 路径 trigram；
4. 扩展名索引；
5. 如果都不可用，则全量扫描。

证据：

- `MacEverything/Core/SearchEngineAdvancedQuery.cpp:658-788`

### 9.1 名称 trigram

名称 trigram 的 key 来自 AST 中 AND 层级下最长的可用 term。

规则：

- 只接受 `SUBSTRING` 或 glob 中提取出的最长 literal；
- key 长度必须至少 3；
- `OR` / `NOT` / 纯 FILTER 默认不能安全 pre-filter；
- 只有候选数不超过 `totalSize / 10` 才采用，否则退回更宽路径。

证据：

- `MacEverything/Core/SearchEngineAdvancedQuery.cpp:442-477`
- `MacEverything/Core/SearchEngineAdvancedQuery.cpp:663-675`

名称 trigram 索引来自小写文件名池。

证据：

- `MacEverything/Core/SearchEngineIndex.cpp:112-134`

posting list 交集会先按 posting list 长度排序，再做有序集合交集。

证据：

- `MacEverything/Core/SearchEngineIndex.cpp:9-42`

### 9.2 Regex literal trigram

当名称 trigram 不可用时，regex term 会用 RE2 `FilteredRE2` 提取长度至少为 3 的 literal atoms。

这些 atoms 用 UNION 合并候选集，因为 regex alternation 中 atoms 可能是 OR 关系。

证据：

- `MacEverything/Core/SearchEngineAdvancedQuery.cpp:479-509`
- `MacEverything/Core/SearchEngineAdvancedQuery.cpp:696-708`
- `MacEverything/Core/SearchEngineIndex.cpp:83-106`

### 9.3 路径 trigram

路径 trigram 用于结构化路径约束。

流程：

1. 从 `__pathseg` 过滤器中找最长路径 segment；
2. 在 `pathTrigramIndex_` 上查 path index 候选；
3. 通过 `pathIdxToRecords_` 展开成 record index；
4. 候选数不超过 `totalSize / 4` 才采用。

证据：

- `MacEverything/Core/SearchEngineAdvancedQuery.cpp:558-579`
- `MacEverything/Core/SearchEngineAdvancedQuery.cpp:710-740`

路径 trigram 索引建在小写路径池上。

证据：

- `MacEverything/Core/SearchEngineIndex.cpp:240-283`

### 9.4 扩展名索引

`ext:` 过滤器可以直接使用 `extensionIndex_`。

多个扩展名会 union posting lists。

扩展名索引是精确候选源；即使结果为空，也表示没有匹配。

证据：

- `MacEverything/Core/SearchEngineAdvancedQuery.cpp:581-596`
- `MacEverything/Core/SearchEngineAdvancedQuery.cpp:741-760`
- `MacEverything/Core/SearchEngineIndex.cpp:180-234`

---

## 10. 候选评估与全量扫描

如果选中了候选集：

- 候选数大于等于 10000 时使用 GCD `dispatch_apply` 并行；
- 候选数较小时单线程扫描；
- 扫描中使用软件 prefetch；
- 每 1024 个候选检查一次取消 generation。

证据：

- `MacEverything/Core/SearchEngineAdvancedQuery.cpp:799-899`

如果没有可用候选集：

- 走线性扫描；
- 线程数来自 `hardware_concurrency()`，上限 32；
- 总记录数小于 10000 时强制单线程；
- 每 4096 条记录检查一次取消 generation。

证据：

- `MacEverything/Core/SearchEngineAdvancedQuery.cpp:900-1020`

---

## 11. 纯过滤 fast path

`QueryNeedsAnalysis` 会判断查询是否需要名称或路径字符串。

如果查询只依赖类型、大小、修改时间等元数据列，则 `needs.isPureFilter()` 为真。

证据：

- `MacEverything/Core/QueryNeedsAnalysis.h:3-14`
- `MacEverything/Core/QueryNeedsAnalysis.h:16-66`

纯过滤路径特点：

- 不读取 `namePool_` / `pathPool_`；
- 直接扫描 SoA 元数据列；
- 用 `simdTypeLive16()` 一次检查 16 条记录是否 live；
- 只对 live 记录执行过滤器；
- 适合 `file:`、`folder:`、`type:`、`size:`、`dm:` 这类查询。

证据：

- `MacEverything/Core/SearchEngineAdvancedQuery.cpp:912-985`
- `MacEverything/Core/SIMDSearch.h:231-247`

不是所有 FILTER 都是纯过滤：

- `ext:` 需要文件名；
- `len:` 需要文件名；
- `path:` / `nopath:` / `parent:` / `depth:` 需要路径；
- `__pathseg` 需要完整路径；
- 普通 TERM 默认需要名称和路径。

证据：

- `MacEverything/Core/QueryNeedsAnalysis.h:21-46`

---

## 12. SIMD 在搜索中的角色

SIMD 不是单独的查询路由，而是底层加速组件。

主要用途：

- `simdFind()`：查找子串位置；
- `simdContains()`：判断子串是否存在；
- `simdFindAll()`：查找所有子串位置；
- `simdToLowerAscii()`：ASCII 小写化；
- `simdTypeLive16()`：批量检查 16 条记录是否 live。

证据：

- `MacEverything/Core/SIMDSearch.h:34-117`
- `MacEverything/Core/SIMDSearch.h:119-201`
- `MacEverything/Core/SIMDSearch.h:203-247`

在 ARM NEON 可用时走 NEON 实现，否则回退到 `memmem` / 标量循环。

证据：

- `MacEverything/Core/SIMDSearch.h:6-11`
- `MacEverything/Core/SIMDSearch.h:36-110`

普通 substring term、路径过滤器、评分逻辑都会调用 SIMD substring 工具。

证据：

- `MacEverything/Core/SearchEngineAdvancedQuery.cpp:193-206`
- `MacEverything/Core/SearchEngineAdvancedQuery.cpp:295-309`
- `MacEverything/Core/SearchEngineAdvancedQuery.cpp:529-555`

---

## 13. 短查询缓存

短查询缓存解决 1-2 字符搜索 trigram 不适用的问题。

缓存规模：

- 26 个单字母 key；
- 676 个双字母 key；
- 共 702 个 key；
- 每个 key 保存最多 100 个排序结果。

证据：

- `MacEverything/Core/ShortQueryCache.h:39-43`

合法 key 仅限：

- 长度 1：`a-z`；
- 长度 2：`aa-zz`；
- 其他字符或长度返回 miss。

证据：

- `MacEverything/Core/ShortQueryCache.cpp:5-18`

构建过程会遍历所有 live 记录，从文件名中收集出现过的单字母和双字母 key，并按质量分数保留 top 100。

证据：

- `MacEverything/Core/ShortQueryCache.cpp:41-107`

查询入口只有在 `shortQueryCache_.isBuilt()` 且 `lookup(pq.lower)` 命中时才使用该路径。

证据：

- `MacEverything/Core/SearchEngineQuery.cpp:248-267`

短查询缓存通常在 V6 Phase 2 完成后构建。

证据：

- `MacEverything/Core/SearchEngineV6.cpp:239-244`

---

## 14. Recent cache

Recent cache 不是普通 query 的候选源。

它通过独立 API `recentIndices(count)` 暴露最近修改记录。

证据：

- `MacEverything/Core/SearchEngine.h:273-275`
- `MacEverything/Core/SearchEngine.cpp:812-846`

缓存大小固定为 200。

证据：

- `MacEverything/Core/SearchEngine.h:522-534`

recent cache 在增删改和 compaction 中维护，而不是每次 recent 查询时全量排序。

证据：

- `MacEverything/Core/SearchEngine.cpp:318-400`
- `MacEverything/Core/SearchEngine.cpp:590-799`

---

## 15. 排序与评分

搜索结果先收集为 `(recordIndex, score)`。

score 越小越靠前。

评分大致由三部分组成：

- term 在文件名中缺失的数量；
- 匹配质量：exact / prefix / word-boundary / substring；
- 完整路径长度，越短越优先。

证据：

- `MacEverything/Core/SearchEngine.h:449-461`
- `MacEverything/Core/SearchEngineAdvancedQuery.cpp:512-555`

`queryAdvanced()` 会在释放 shared lock 之后排序。

如果 `maxResults` 小于命中数，则使用 `partial_sort`，否则使用完整 `sort`。

证据：

- `MacEverything/Core/SearchEngineAdvancedQuery.cpp:1024-1047`

---

## 16. 锁模型

主数据结构由 `std::shared_mutex mutex_` 保护。

证据：

- `MacEverything/Core/SearchEngine.h:396`

查询侧：

- `DIR_LIST` 分支获取 shared lock；
- `queryAdvanced()` 获取 shared lock；
- advanced 在完成记录评估后释放锁，再排序；
- regex 预编译发生在加锁前。

证据：

- `MacEverything/Core/SearchEngineQuery.cpp:200-215`
- `MacEverything/Core/SearchEngineAdvancedQuery.cpp:633-652`
- `MacEverything/Core/SearchEngineAdvancedQuery.cpp:1024-1028`

写入侧：

- load、mutation、compaction、Phase 2 swap 等路径使用 unique lock 或在独占阶段替换索引；
- Phase 2 会先在锁外构建索引，再在 unique lock 下 swap 并 replay 期间发生的变化。

证据：

- `MacEverything/Core/SearchEngine.cpp:61-184`
- `MacEverything/Core/SearchEngine.cpp:318-400`
- `MacEverything/Core/SearchEngine.cpp:590-799`
- `MacEverything/Core/SearchEngineV6.cpp:205-253`

设计后果：

- 读查询期间写入者会等待；
- advanced 的排序阶段不占用 shared lock；
- 长线性扫描仍会在评估阶段持有 shared lock；
- 取消是协作式检查，不会抢占正在执行的内层操作。

---

## 17. 取消模型

取消基于 session generation。

结构：

- `sessionId=0`：不参与取消，使用 dummy atomic；
- 非 0 session：每次查询 `fetch_add(1)` 得到新 generation；
- 同一 session 后来的查询会让前一个查询看到 generation 不一致；
- `cancelSession(sessionId)` 也会递增 generation。

证据：

- `MacEverything/Core/SearchEngine.h:96-103`
- `MacEverything/Core/SearchEngine.h:160-185`
- `MacEverything/Core/SearchEngineQuery.cpp:278-299`

取消检查点包括：

- DIR_LIST 查询后；
- path trigram 展开期间；
- 大候选并行扫描每 1024 个候选；
- 小候选扫描每 1024 个候选；
- 线性扫描每 4096 条记录；
- dispatch 完成后合并前。

证据：

- `MacEverything/Core/SearchEngineQuery.cpp:209-213`
- `MacEverything/Core/SearchEngineAdvancedQuery.cpp:725-727`
- `MacEverything/Core/SearchEngineAdvancedQuery.cpp:837-865`
- `MacEverything/Core/SearchEngineAdvancedQuery.cpp:874-876`
- `MacEverything/Core/SearchEngineAdvancedQuery.cpp:957-990`
- `MacEverything/Core/SearchEngineAdvancedQuery.cpp:1013-1019`

测试覆盖同 session 取消、不同 session 隔离、`sessionId=0` 不参与取消等行为。

证据：

- `tests/test_query_cancel.h:34-176`

---

## 18. Phase 2 与索引可用性

V6 加载采用两阶段策略。

第一阶段加载 SoA 和基础结构，并把 `phase2Pending_` 设为 true。

第二阶段后台构建：

- 名称 trigram；
- 路径 trigram；
- pathIdx 到 records 映射；
- recent cache；
- extension index；
- short query cache。

证据：

- `MacEverything/Core/SearchEngineV6.cpp:100-117`
- `MacEverything/Core/SearchEngineV6.cpp:119-253`

Phase 2 有内存保护：

- 估算约 `200B/record`；
- 如果估算占可用内存超过 70%，返回错误信息；
- 保持 `phase2Pending_=true`，避免后续增量插入构建“部分索引”。

证据：

- `MacEverything/Core/SearchEngineV6.cpp:119-145`

因此，在 Phase 2 未完成时：

- trigram / path trigram / extension / short-query cache 可能不可用；
- advanced 查询会退化到线性或纯过滤扫描；
- 搜索功能仍可用，但性能路径不同。

---

## 19. 与内容搜索的边界

普通文件名/路径查询语言中 `content:` 被 tokenizer 识别为 known filter。

证据：

- `MacEverything/Core/QueryTokenizer.h:173-176`

但是 `evalFilter()` 没有实现 `content:` 分支。

未知 filter 当前返回 true。

证据：

- `MacEverything/Core/SearchEngineAdvancedQuery.cpp:127-252`

因此：

- `content:foo` 不等价于内容索引查询；
- 当前内容搜索入口是 UI 层的 `infile:` 前缀；
- `infile:` 会绕过普通 query parser，调用 `ContentIndex::query()`。

证据：

- `MacEverything/App/SearchViewModel.swift:284-304`
- `MacEverything/Core/ContentIndex.cpp:568-647`

---

## 20. 主要限制与风险

1. `content:` 语法已被 tokenizer 接受，但普通搜索执行器没有内容搜索语义。
   证据：`MacEverything/Core/QueryTokenizer.h:173-176`，`MacEverything/Core/SearchEngineAdvancedQuery.cpp:250-251`

2. `dc:` / `da:` 当前用 `modTime` 作为 fallback，因为记录模型没有 birthtime / access time 字段。
   证据：`MacEverything/Core/SearchEngineAdvancedQuery.cpp:237-248`，`MacEverything/Core/SearchEngine.h:386-391`

3. Trigram 需要长度至少 3 的 literal，且候选集必须足够窄；过宽会主动退化到线性扫描。
   证据：`MacEverything/Core/SearchEngineAdvancedQuery.cpp:442-477`，`MacEverything/Core/SearchEngineAdvancedQuery.cpp:668-675`

4. `OR` / `NOT` 等结构通常无法安全使用单一 trigram pre-filter。
   证据：`MacEverything/Core/SearchEngineAdvancedQuery.cpp:442-477`

5. 短查询缓存只支持 1-2 位小写 ASCII 字母，并且必须已构建。
   证据：`MacEverything/Core/ShortQueryCache.cpp:5-18`，`MacEverything/Core/SearchEngineQuery.cpp:248-267`

6. 短查询缓存分支在 `query()` 代码中没有显式 shared lock，需要单独审查并发读写安全边界。
   证据：`MacEverything/Core/SearchEngineQuery.cpp:248-267`

7. 长线性查询在评估阶段持有 shared lock，写入者会等待；取消只能在检查点生效。
   证据：`MacEverything/Core/SearchEngineAdvancedQuery.cpp:650-652`，`MacEverything/Core/SearchEngineAdvancedQuery.cpp:900-1020`

8. Phase 2 可能因内存保护保持 pending，导致昂贵索引和短查询缓存暂时缺失。
   证据：`MacEverything/Core/SearchEngineV6.cpp:119-145`

9. invalid regex 不会抛给用户；对应 regex node 在 eval 时无法命中。
   证据：`MacEverything/Core/SearchEngineAdvancedQuery.cpp:633-647`，`MacEverything/Core/SearchEngineAdvancedQuery.cpp:332-347`

10. `DIR_LIST` 只在顶层 `query()` 中作为 `/path/*` 特判处理；advanced AST transform 明确不处理 `DIR_LIST`。
    证据：`MacEverything/Core/SearchEngineQuery.cpp:197-246`，`MacEverything/Core/ASTStructuredTransform.h:37-38`

---

## 21. 测试覆盖线索

查询语法测试：

- `tests/test_query_parser.h:17-180`
- `tests/test_query_modifiers.h:22-373`
- `tests/test_slash_query.h:11-273`

索引与性能路径测试：

- `tests/test_trigram_index.h:9-111`
- `tests/test_trigram_index.h:312-362`
- `tests/test_path_trigram.h:9-189`
- `tests/test_short_query_cache.h:12-244`
- `tests/test_simd_search.h:8-196`

缓存与取消测试：

- `tests/test_recent_cache.h:8-195`
- `tests/test_query_cancel.h:34-176`

---

## 22. 架构阅读要点

读代码时建议按以下顺序：

1. 先看 `SearchEngine.h` 的 SoA 字段、索引字段、API；
2. 再看 `SearchEngineQuery.cpp`，理解顶层路由；
3. 再看 `QueryTokenizer` / `QueryParser` / `QueryFilterParser`，理解语法；
4. 再看 `ASTStructuredTransform` / `ASTGlobTransform`，理解 AST 改写；
5. 再看 `SearchEngineAdvancedQuery.cpp`，理解候选选择和评估；
6. 再看 `SearchEngineIndex.cpp`，理解索引构建和 posting list；
7. 最后看 `ShortQueryCache`、`SIMDSearch`、`SearchEngineV6`，理解性能路径和启动阶段差异。

关键文件：

- `MacEverything/Core/SearchEngine.h`
- `MacEverything/Core/SearchEngineQuery.cpp`
- `MacEverything/Core/SearchEngineAdvancedQuery.cpp`
- `MacEverything/Core/SearchEngineIndex.cpp`
- `MacEverything/Core/QueryTokenizer.h`
- `MacEverything/Core/QueryParser.cpp`
- `MacEverything/Core/QueryFilterParser.h`
- `MacEverything/Core/StructuredQueryParser.h`
- `MacEverything/Core/ShortQueryCache.cpp`
- `MacEverything/Core/SIMDSearch.h`
