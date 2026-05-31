# 持久化、WAL、Flush 与 Compaction 架构说明

目标文件：`docs/architecture/persistence-wal-compaction.md`

## 1. 结论摘要

MacEverything 当前持久化层已经从 legacy v3、paged v5 演进到 v6 flat SoA 主路径。

启动路径优先加载单文件 `index.v6`，失败后才 fallback 到 paged v5 或 legacy v3，并在 fallback 成功后自动迁移回 v6。

运行期所有索引变更先写 Index WAL，随后由 `IndexPersistence::flush()` 将内存索引 checkpoint 到 v6 base file。

需要特别注意：当前代码注释仍称 flush 为“dirty pages incremental flush”，但实际主路径已经不是 paged dirty-page flush，而是每次 flush 执行完整 v6 flat rewrite。

这个差异是性能报告里“flush rewrite bloat”和 tombstone 持续累积的核心背景。

证据：`MacEverything/Core/IndexPersistence.cpp:186-188`

证据：`MacEverything/Core/PagedIndexWriter.cpp:394-481`

最新性能报告显示，系统在 tombstone 16.08% 时仍稳定运行，但 25% compaction 阀门长期不可达，导致 flush 只做全量重写、没有回收。

报告证据：`R_2605311211`，tombstones `1,043,183`、ratio `16.08%`、flush `162` 次、compaction `0`、reclaimed `0`。

报告锚点：`docs/performance_ana/R_2605311211.md:81-97`

---

## 2. 组件边界

| 组件 | 职责 | 当前状态 |
|---|---|---|
| `IndexWAL` | 文件名/路径索引 mutation 日志 | 主路径使用 |
| `FlatIndexWriter` | v6 flat SoA base file 读写 | 主路径使用 |
| `PagedIndexWriter` | paged v5 dirty-page 持久化 | fallback/遗留能力 |
| `SearchEnginePersistence` | legacy v1-v3 index.bin | fallback/迁移输入 |
| `ContentIndexPersistence` | 内容索引 base + WAL + compact | 内容搜索侧使用 |
| `SearchEngine` | tombstone、dirty page、COW compaction、WAL replay | 所有持久化动作的内存模型 |

---

## 3. 文件格式总览

### 3.1 Index WAL

Index WAL 文件头为 `WAL1` + version。

mutation op 包括 `Add`、`Remove`、`Update`。

证据：`MacEverything/Core/IndexWAL.h:10-15`

每条 WAL entry 写入 op、full path、可选 `FileRecord` 字段，末尾追加 CRC32。

证据：`MacEverything/Core/IndexWAL.cpp:136-166`

WAL 最大大小为 50 MB。

证据：`MacEverything/Core/IndexWAL.h:70-75`

默认每 64 条 entry 做一次 fsync。

证据：`MacEverything/Core/IndexWAL.h:80-83`

append 时先 `fflush`，再按 sync interval 批量 `fsync`。

证据：`MacEverything/Core/IndexWAL.cpp:170-179`

WAL replay 读取时校验 magic/version；如果不匹配，会退回 legacy 无头 WAL 读取模式。

证据：`MacEverything/Core/IndexWAL.cpp:184-197`

读取 entry 时校验 CRC，遇到损坏 entry 后停止。

证据：`MacEverything/Core/IndexWAL.cpp:221-240`

### 3.2 v6 flat index

v6 flat index 是当前主 base file，文件名通常为 `index.v6`。

格式是单文件 flat SoA：

| 区域 | 内容 |
|---|---|
| Header | 64 bytes，magic/version/recordCount/liveCount/timestamp/lastEventId/sectionCount/headerCRC |
| Section Table | 11 个 section，每个 24 bytes |
| Sections | StringPool 与 SoA columns |

格式定义证据：`MacEverything/Core/FlatIndexWriter.h:7-15`

Header 结构证据：`MacEverything/Core/FlatIndexWriter.h:66-78`

Section ID 共 11 个：

| ID | Section |
|---:|---|
| 1 | original names |
| 2 | lower names |
| 3 | path pool |
| 4 | lower path pool |
| 5 | path indices |
| 6 | types |
| 7 | sizes |
| 8 | mod times |
| 9 | inodes |
| 10 | dev ids |
| 11 | metadata KV |

证据：`MacEverything/Core/FlatIndexWriter.h:39-51`

v6 rewrite 先对 `SearchEngine` 做 snapshot，然后写临时文件、fsync、rename。

证据：`MacEverything/Core/FlatIndexWriter.cpp:156-179`

v6 writer 写入 11 个 section，并为每个 section 记录 offset、byteLength、CRC32。

证据：`MacEverything/Core/FlatIndexWriter.cpp:202-287`

v6 load 会逐 section 读回，并逐 section 校验 CRC。

证据：`MacEverything/Core/FlatIndexWriter.cpp:333-531`

注意：header CRC 注释写“first 60 bytes excluding headerCRC”，但实现实际计算前 36 bytes。

证据：`MacEverything/Core/FlatIndexWriter.cpp:300-310`

这是文档和实现不一致点，建议作为低风险清理项单独修正注释或扩展 CRC 范围。

### 3.3 Paged v5 index

Paged format 是两文件格式：

| 文件 | 作用 |
|---|---|
| `index.pages` | append-only page blobs |
| `index.ptable` | atomic page table，包含 page offset/length/CRC |

证据：`MacEverything/Core/PagedIndexWriter.h:8-15`

Paged flush 可以只追加 dirty pages，并重写 ptable。

证据：`MacEverything/Core/PagedIndexWriter.h:24-30`

dirty page append 后，旧 page blob 变成 dead space。

证据：`MacEverything/Core/PagedIndexWriter.cpp:441-481`

Paged writer 可计算 dead space ratio。

证据：`MacEverything/Core/PagedIndexWriter.cpp:563-573`

但当前 `IndexPersistence::flush()` 没有调用 `flushDirtyPages()`，所以 paged dirty-page 能力不是运行期主路径。

证据：`MacEverything/Core/IndexPersistence.cpp:186-188`

证据：`MacEverything/Core/PagedIndexWriter.cpp:394-481`

Paged v4 已不再支持，加载 v4 ptable 会返回 false，触发 rebuild/fallback。

证据：`MacEverything/Core/PagedIndexWriter.cpp:271-275`

### 3.4 Legacy v3 index

Legacy base file magic 为 `MEID`，支持 v1-v3。

证据：`MacEverything/Core/SearchEnginePersistence.cpp:9-15`

v3 save 写入 metadata KV、live record count，然后只写 live records。

证据：`MacEverything/Core/SearchEnginePersistence.cpp:64-138`

`tombstone` 不会写入 legacy v3 base file，因为保存时显式跳过 `types_[i] == 0`。

证据：`MacEverything/Core/SearchEnginePersistence.cpp:97-126`

legacy load 只作为 fallback 输入，成功后由 `IndexPersistence` 自动迁移到 v6。

证据：`MacEverything/Core/IndexPersistence.cpp:71-80`

### 3.5 Content WAL 与内容索引持久化

Content WAL 文件头为 `CWL1` + version。

证据：`MacEverything/Core/ContentIndexPersistence.h:53-58`

Content WAL 最大大小为 20 MB。

证据：`MacEverything/Core/ContentIndexPersistence.h:53-54`

Add entry 写入 `fileIndex`、`contentHash`、trigrams、`lastModTime`。

证据：`MacEverything/Core/ContentIndexPersistence.cpp:45-89`

Remove entry 只写入 `fileIndex`。

证据：`MacEverything/Core/ContentIndexPersistence.cpp:91-123`

Content WAL replay 读取 base 后应用 WAL entries。

证据：`MacEverything/Core/ContentIndexPersistence.cpp:253-281`

Content compaction 是事件驱动：WAL threshold 为 50，mutation 后延迟 60 秒触发。

证据：`MacEverything/Core/ContentIndexPersistence.h:98-116`

证据：`MacEverything/Core/ContentIndexPersistence.cpp:397-438`

---

## 4. 启动加载流程

### 4.1 主索引启动流程

启动时 `IndexPersistence::load()` 按以下顺序尝试：

1. 加载 v6 flat index。
2. 如果 v6 成功，同时尝试加载 `.sqcache`。
3. 如果 v6 不存在或损坏，fallback 到 paged v5。
4. paged v5 成功后，立即 full rewrite 迁移到 v6。
5. 如果 paged v5 不可用，fallback 到 legacy v3。
6. legacy v3 成功后，立即 full rewrite 迁移到 v6。
7. base index 加载后，读取并 replay Index WAL。

证据：`MacEverything/Core/IndexPersistence.cpp:31-96`

这意味着 v6 是启动快路径，paged/legacy 是兼容与修复路径。

### 4.2 v6 load 的 SearchEngine 安装语义

v6 load 将 StringPool 与 SoA arrays 直接安装到 `SearchEngine`。

证据：`MacEverything/Core/SearchEngineV6.cpp:15-39`

随后重建 path lookup、lower path lookup、pathIndex。

证据：`MacEverything/Core/SearchEngineV6.cpp:41-84`

如果同一 full path 有重复记录，pathIndex 采用 last-wins，非 winner 会被 tombstone。

证据：`MacEverything/Core/SearchEngineV6.cpp:86-104`

v6 load 后 Phase 2 trigram rebuild 会被标记为 pending。

证据：`MacEverything/Core/SearchEngineV6.cpp:106-108`

v6 load 最后初始化 dirty page bitmap，并清除 full rewrite needed 标志。

证据：`MacEverything/Core/SearchEngineV6.cpp:110-113`

---

## 5. Mutation、Tombstone 与 WAL 写入

### 5.1 Tombstone 的内存语义

`tombstoneAt(idx)` 将 `types_[idx]` 置为 `0`。

同时它会标记对应 page 为 dirty，清理 size/modTime/inode/devId，并 tombstone name pools。

证据：`MacEverything/Core/SearchEngine.cpp:37-51`

因此当前系统的“删除”不是立即压缩数组，而是在 SoA 中留下 dead slot。

### 5.2 Add

`addRecord()` 在持有 write lock 后先写 WAL Add entry。

证据：`MacEverything/Core/SearchEngine.cpp:318-327`

如果同 path 已存在，旧 record 会被 tombstone，以防出现 orphan duplicate。

证据：`MacEverything/Core/SearchEngine.cpp:328-339`

新 record append 到 SoA 后，标记其 page dirty。

证据：`MacEverything/Core/SearchEngine.cpp:363-368`

### 5.3 Remove

`removeByPathUnlocked()` 先写 WAL Remove entry，再 tombstone record，并从 pathIndex 删除。

证据：`MacEverything/Core/SearchEngine.cpp:383-400`

prefix remove 会对每个被删除 path 写 WAL Remove entry。

证据：`MacEverything/Core/SearchEngine.cpp:408-441`

### 5.4 Update 与 batch mutation

`updateByPathUnlocked()` 写 WAL Update entry，若旧记录存在则 tombstone，再 append 新记录。

证据：`MacEverything/Core/SearchEngine.cpp:516-563`

`batchMutate()` 每 300 个 mutation 分块持锁执行，降低单次写锁长度。

证据：`MacEverything/Core/SearchEngine.cpp:570-588`

### 5.5 WAL replay

WAL replay 在一个 write lock 内顺序应用 Add/Remove/Update。

证据：`MacEverything/Core/SearchEngine.cpp:873-972`

replay Add/Update 同样遵守 last-wins：旧 path winner 先 tombstone，再 append 新记录。

证据：`MacEverything/Core/SearchEngine.cpp:881-966`

---

## 6. Dirty Pages：已有机制与当前落差

SearchEngine 有 dirty page bitmap。

新增 record、tombstone、update 都会 mark dirty page。

证据：`MacEverything/Core/SearchEngine.cpp:37-51`

证据：`MacEverything/Core/SearchEngine.cpp:363-368`

Paged writer 可以读取 `engine.getDirtyPageNumbers()`，只 append dirty pages，然后清除 dirty bitmap。

证据：`MacEverything/Core/PagedIndexWriter.cpp:394-481`

但是当前 `IndexPersistence::flush()` 主路径没有调用 paged dirty-page flush，而是调用 `flatWriter_->fullRewrite()`。

证据：`MacEverything/Core/IndexPersistence.cpp:186-188`

因此 dirty page bitmap 当前主要影响 adaptive timer，而不是影响实际写盘粒度。

adaptive timer 根据 dirty page ratio 决定下次 flush 间隔：

| dirty ratio | interval |
|---:|---:|
| `> 0.3` | 30s |
| `> 0.1` | 150s |
| `> 0.01` | 300s |
| otherwise | 600s |

证据：`MacEverything/Core/IndexPersistence.cpp:306-337`

WAL size 超过 2 MB 时，会把 interval 压到最小值。

证据：`MacEverything/Core/IndexPersistence.h:66-72`

证据：`MacEverything/Core/IndexPersistence.cpp:326-334`

---

## 7. Flush 主路径

### 7.1 Skip gate

非强制 flush 会跳过以下场景：

1. 没有 WAL。
2. WAL 没有 dirty。
3. WAL entry count 小于 100。

证据：`MacEverything/Core/IndexPersistence.cpp:119-147`

强制 flush 只在 WAL header-only 时跳过。

证据：`MacEverything/Core/IndexPersistence.cpp:132-136`

entry threshold 定义为 `kCompactThreshold = 100`。

证据：`MacEverything/Core/IndexPersistence.h:36-40`

### 7.2 Tombstone ratio gate

flush skip gate 之后，代码计算 tombstone ratio。

如果 tombstone ratio 大于 25%，触发 full compaction。

证据：`MacEverything/Core/IndexPersistence.cpp:152-163`

阈值定义为 `kTombstoneCompactRatio = 0.25`。

证据：`MacEverything/Core/IndexPersistence.h:70-72`

这正是当前长期不触发 compaction 的结构性原因。

报告 `R_2605311211` 显示 tombstone ratio 已到 16.08%，但 compaction 仍为 0。

报告锚点：`docs/performance_ana/R_2605311211.md:81-97`

### 7.3 WAL swap 与 v6 rewrite

普通 flush 会：

1. 打开 `walPath + ".new"`。
2. 在 `walMutex_` 下用 new WAL 替换 current WAL。
3. 将 new WAL attach 到 SearchEngine。
4. 调用 `flatWriter_->fullRewrite()` 写 v6。
5. 保存 short query cache。
6. rename `.new` WAL 覆盖正式 WAL。
7. 关闭 old WAL。

证据：`MacEverything/Core/IndexPersistence.cpp:166-223`

这条路径的实际效果是“WAL checkpoint + v6 全量 base rewrite”。

它不是 dirty-page 增量 flush。

报告 `R_2605302211` 显示 16 次 flush 全为纯重写，0 compaction / 0 reclaim。

报告锚点：`docs/performance_ana/R_2605302211.md:97-123`

报告 `R_2605311011` 显示 session flush counter 114、compaction 0、reclaim 0，每次重写约 6.39M records，其中约 975K tombstones。

报告锚点：`docs/performance_ana/R_2605311011.md:108-118`

报告 `R_2605311211` 显示 flush 162 次，rewrite 3210-4116ms，compaction/reclaimed 仍为 0。

报告锚点：`docs/performance_ana/R_2605311211.md:81-87`

---

## 8. Full Compaction 主路径

full compaction 会：

1. 打开新 WAL。
2. swap WAL。
3. 调用 `engine_->compactRecords()` 压缩内存记录。
4. 生成 old index 到 new index 的 remap。
5. remap content index。
6. force compact content persistence。
7. full rewrite v6。
8. rename 新 WAL。
9. close old WAL。

证据：`MacEverything/Core/IndexPersistence.cpp:225-304`

`compactRecords()` 使用 COW 三阶段：

1. shared lock 下 snapshot 当前 SoA 和索引结构。
2. 无锁构建 compacted data。
3. unique lock 下 swap，并 replay Phase 2 期间发生的 adds/deletes。

证据：`MacEverything/Core/SearchEngine.cpp:590-799`

compaction 完成后会设置 `fullRewriteNeeded_ = true`。

证据：`MacEverything/Core/SearchEngine.cpp:796-799`

由于当前 full compaction 后立即写 v6 flat，`fullRewriteNeeded_` 对 paged writer 的意义在主路径上基本没有发挥。

---

## 9. Tombstone 现状与风险

### 9.1 tombstone 增长事实

`R_2605302211`：tombstones `770,965`，ratio `12.47%`。

报告锚点：`docs/performance_ana/R_2605302211.md:2-5`

`R_2605311011`：tombstones `975,333`，ratio `15.25%`。

报告锚点：`docs/performance_ana/R_2605311011.md:121-133`

`R_2605311211`：tombstones `1,043,183`，ratio `16.08%`。

报告锚点：`docs/performance_ana/R_2605311211.md:89-97`

### 9.2 12.97% 不是硬崩溃线

近期报告反复显示系统能稳定运行在 14.7%-16.08% tombstone ratio。

因此旧的 12.97% 更像相关性参考线，不是硬崩溃阈值。

报告 `R_2605311011` 明确指出连续三轮越过 12.97% 参考线而 app 稳定。

报告锚点：`docs/performance_ana/R_2605311011.md:121-133`

但这不降低 tombstone 风险。

因为 tombstone 仍会增加 v6 rewrite 体积、扩大 full scan 边界，并增加 page residency 压力。

### 9.3 当前真实退化轴

性能图谱给出的主链路是：

FSEvents / ingest churn → tombstones 增长 → v6 flush 重写 full index including tombstones → I/O 与 page eviction → string/content/path/CJK 查询冷态变慢，偶尔影响 numeric `dm:`。

报告 `R_2605311211` 显示本轮 record +19,292 / live +3,293，新增 tombstones +15,999，入墓比 83.0%。

报告锚点：`docs/performance_ana/R_2605311211.md:89-98`

报告 `R_2605311011` 的双窗口分析显示一次全局冷事件会按内存散布度放大：size 1.59x、content 2.83x、dm 3.65x、path 3.92x、中文 5.71x。

报告锚点：`docs/performance_ana/R_2605311011.md:45-72`

这说明 page residency 是当前性能风险的直接机制之一。

---

## 10. 已观察风险清单

### 10.1 compaction ratio valve 过高

当前阀门为 25%。

证据：`MacEverything/Core/IndexPersistence.h:70-72`

最新 tombstone ratio 为 16.08%，但已经产生 1M+ tombstones 与 3-4s rewrite。

报告锚点：`docs/performance_ana/R_2605311211.md:81-97`

因此 25% 阀门在实际压力出现后仍无法触发 reclaim。

建议将 ratio valve 下调到 8%-10%，并增加绝对 tombstone 数和 rewrite cost 触发条件。

### 10.2 flush 名称和行为不一致

`IndexPersistence.h` 注释仍写“Incremental flush: write only dirty pages, swap WAL”。

证据：`MacEverything/Core/IndexPersistence.h:39-45`

实际 `IndexPersistence::flush()` 调用 v6 full rewrite。

证据：`MacEverything/Core/IndexPersistence.cpp:186-188`

这会误导后续维护者低估 flush 成本。

### 10.3 dead-space threshold 未接入当前主路径

`kDeadSpaceRewriteRatio = 0.5` 已定义。

证据：`MacEverything/Core/IndexPersistence.h:70-72`

Paged writer 也能计算 dead space。

证据：`MacEverything/Core/PagedIndexWriter.cpp:563-573`

但主路径已转向 v6 full rewrite，不使用 paged dead-space reclaim。

### 10.4 WAL swap 失败路径需审计

普通 flush 在 v6 rewrite 前已经把 `wal_` 替换为 `.new` WAL。

证据：`MacEverything/Core/IndexPersistence.cpp:166-185`

如果 v6 rewrite 失败，代码日志写“keeping old WAL for recovery”，但实际只 close old WAL 并 return，没有把 `wal_` 恢复到 old WAL。

证据：`MacEverything/Core/IndexPersistence.cpp:186-209`

启动 replay 只读取正式 `walPath_`。

证据：`MacEverything/Core/IndexPersistence.cpp:88-92`

因此 `.new` WAL 的 crash/restart 语义需要补强。

Content compaction 也有类似“先 swap 到 `.new`，再写 base”的路径，需要同类审计。

证据：`MacEverything/Core/ContentIndexPersistence.cpp:327-378`

### 10.5 F_NOCACHE 已使用但不足以消除 page-residency 风险

v6 writer 在写临时文件时调用 `F_NOCACHE`。

证据：`MacEverything/Core/FlatIndexWriter.cpp:166-167`

legacy save 和 paged writer 也使用 `F_NOCACHE`。

证据：`MacEverything/Core/SearchEnginePersistence.cpp:72-73`

证据：`MacEverything/Core/PagedIndexWriter.cpp:425-426`

但报告仍观测到 flush/page eviction 相关冷态波动。

报告锚点：`docs/performance_ana/R_2605311011.md:45-72`

结论是：`F_NOCACHE` 是必要但不充分的缓解，仍需减少 rewrite 体积、缩短锁区间、钉住热页。

---

## 11. 改进挂钩

### 11.1 P0：修正 compaction 阀门

建议：

1. 将 `kTombstoneCompactRatio` 从 25% 下调到 8%-10%。
2. 增加绝对 tombstone count 触发，例如超过 300K 或 500K。
3. 增加 rewrite waste 触发，例如 tombstone slots / total rewrite slots 超过固定预算。
4. 在 flush 前输出 trigger reason。

验收指标：

1. 性能报告出现非零 `triggering full compaction`。
2. 性能报告出现非零 `Reclaimed`。
3. tombstone ratio 被压在目标区间内。
4. v6 rewrite record count 下降。
5. flush rewrite 时间下降或稳定不增长。

### 11.2 P0：v6 no-lock snapshot flush

当前 `FlatIndexWriter::fullRewrite()` 已以 `engine.snapshotForV6()` 为入口。

证据：`MacEverything/Core/FlatIndexWriter.cpp:156-157`

改进方向：

1. 明确 snapshot 阶段、serialize 阶段、rename 阶段的锁边界。
2. 避免长时间持有 `SearchEngine` write lock。
3. 参考 `SearchEngine::compactRecords()` 的 COW 三阶段模型。
4. 在 flush 日志中拆出 snapshotMs、serializeMs、fsyncMs、renameMs。
5. 将 query lock wait 与 flush 阶段关联起来。

报告依据：`R_2605311011` 和 `R_2605311211` 都把 flush rewrite 与 page/cold event 关联为 P0。

报告锚点：`docs/performance_ana/R_2605311011.md:147-153`

报告锚点：`docs/performance_ana/R_2605311211.md:107-112`

### 11.3 P0：让 dirty pages 重新产生实际收益

可选路线：

1. 恢复 paged v5 dirty-page 主路径。
2. 设计 v6 segmented format，把大 section 拆成可增量替换的 segment。
3. 保持 v6 flat 作为 periodic checkpoint，但将普通 flush 变成 WAL checkpoint 或 segment-level checkpoint。
4. 将 dirty page ratio 从“timer 输入”升级为“写入粒度输入”。

关键验收指标：

1. 普通 flush 不再重写全部 record slots。
2. dirty page count 与 write bytes 正相关。
3. tombstone reclaim 与 ordinary flush 分离。
4. `.sqcache` 保存不成为 flush 尾部抖动源。

### 11.4 P0：WAL generation 与 crash recovery 协议

建议引入 WAL generation manifest 或 checkpoint marker：

1. 当前活跃 WAL generation。
2. checkpoint 开始状态。
3. checkpoint base file 路径。
4. `.new` WAL 是否可恢复。
5. rename 后 fsync parent directory。

失败恢复规则应覆盖：

1. base rewrite 失败。
2. WAL rename 失败。
3. 进程在 `.new` WAL 已接管但未 rename 时崩溃。
4. old WAL fd 仍存在但目录项已被 rename 替换。
5. content WAL 与 index WAL generation 不一致。

### 11.5 P0：page residency 保护

报告显示字符串路径、content/path/CJK 与 `dm:` 瞬时升档都受 page residency 影响。

报告锚点：`docs/performance_ana/R_2605311211.md:107-112`

建议：

1. 对 hot SoA columns 使用 `mlock` 或 `madvise`。
2. 优先保护 `types_`、`sizes_`、`modTimes_`、path/name string pools、trigram buckets。
3. flush 前后采样 resident set、page faults、query floor。
4. 如果系统内存不足，降级为只保护 numeric columns 与 path/name hot pools。
5. 将 page residency 指标写入性能报告。

### 11.6 P0：FSEvents/ingest 观测与背压

报告显示新增长期以 tombstone 为主，`R_2605311211` 入墓比 83.0%。

报告锚点：`docs/performance_ana/R_2605311211.md:89-98`

建议新增 per-window counters：

1. FSEvents raw event count。
2. coalesced mutation count。
3. batchMutate op count。
4. add/update/remove 分布。
5. tombstone/live delta。
6. WAL entries 和 WAL bytes。
7. flush trigger reason。
8. compaction skipped reason。
9. query cold event correlation。

### 11.7 P1：Content index remap 与 compact 观测

Index full compaction 会 remap content index 并 force compact content persistence。

证据：`MacEverything/Core/IndexPersistence.cpp:262-271`

建议：

1. 输出 remap size。
2. 输出 content compact 耗时。
3. 输出 content WAL entries/bytes。
4. 校验 remap 前后 indexed file count。
5. 在 index compaction 失败时避免 content index 已部分推进。

---

## 12. 建议新增日志字段

| 日志点 | 字段 |
|---|---|
| flush skip | reason, walDirty, walEntries, walBytes, force |
| flush start | totalRecords, liveRecords, tombstones, tombRatio, dirtyPages, dirtyRatio |
| v6 rewrite | snapshotMs, serializeMs, fsyncMs, renameMs, bytesWritten |
| compaction start | threshold, tombRatio, absoluteTombstones, triggerReason |
| compaction done | reclaimed, beforeTotal, afterTotal, remapSize, contentRemapMs |
| WAL swap | oldPath, newPath, generation, renameResult |
| startup replay | baseFormat, baseLastEventId, walEntries, walBytes, corruptOffset |
| content compact | entries, bytes, indexedFiles, writeMs, renameMs |

---

## 13. 架构判断

当前持久化层的正确性基础较完整：WAL、有 CRC、tmp+fsync+rename、fallback/migration、content remap 都已存在。

当前最大问题不是“没有持久化机制”，而是“机制组合后的运行期策略不匹配当前数据规模”。

最关键的不匹配有三点：

1. tombstone reclaim 阀门过高。
2. ordinary flush 实际是 full v6 rewrite。
3. dirty-page 与 dead-space 机制没有接入当前 v6 主路径。

因此优化优先级应先修正策略与观测，而不是先重写文件格式。

推荐路线：

1. 先修 compaction 阀门和日志。
2. 再实现 no-lock snapshot flush 与 WAL generation recovery。
3. 然后决定是 revive paged dirty-page，还是设计 v6 segmented checkpoint。
4. 最后做更细的 content/CJK 辅助索引。

这样能最短路径降低风险，并保留当前 v6 flat 启动快路径。