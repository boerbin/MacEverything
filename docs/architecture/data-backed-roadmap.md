# MacEverything 数据驱动架构路线图

> 目标：用最近性能报告和代码锚点定义下一阶段 P0/P1 优化顺序、验收标准与取舍。

## 1. 核心结论

当前主要风险不是 trigram/ext/短查询路径，而是长跑运维退化链：

FSEvents/ingest churn → tombstone 单调增长 → v6 flush 全量重写含 tombstone 的索引 → I/O 与页驻留压力 → 字符串/content/path/CJK 变慢，偶发扩散到 `dm:` 数值列。

关键数据：

| 报告 | tombstones | ratio | flush/compaction | 关键现象 |
|---|---:|---:|---|---|
| `R_2605302211` | 770,965 | 12.47% | 16 flush / 0 compaction / 0 reclaim | 每次重写约 771K 死记录，暖态 rewrite 3–7s |
| `R_2605311011` | 975,333 | 15.25% | 114 flush / 0 compaction / 0 reclaim | W2 全局冷事件：size 1.59x < CJK 5.71x |
| `R_2605311141` | 1,027,184 | 15.88% | 150 flush / 0 compaction / 0 reclaim | `dm:` 对抗口径首次交叉，0.86x |
| `R_2605311211` | 1,043,183 | 16.08% | 162 flush / 0 compaction / 0 reclaim | `dm:` 回退，对抗口径恢复 1.60x |

## 2. 证据来源

| 类型 | 路径 |
|---|---|
| 综合输入 | `/tmp/maceverything-arch-maps/perf-reports-roadmap.md` |
| 最新报告 | `docs/performance_ana/R_2605311211.md` |
| `dm:` 裂纹报告 | `docs/performance_ana/R_2605311141.md` |
| page-residency 强证据报告 | `docs/performance_ana/R_2605311011.md` |
| 暖态基线报告 | `docs/performance_ana/R_2605302211.md` |
| 架构深潜 | `docs/tech_sharing_architecture_deep_dive.md` |

## 3. 代码锚点

| 子系统 | 代码锚点 | 含义 |
|---|---|---|
| compaction 阈值 | `MacEverything/Core/IndexPersistence.h:37-45` | `kCompactThreshold`、`flush()`、`fullCompact()` |
| tombstone 比例阀 | `MacEverything/Core/IndexPersistence.h:67-72` | `kTombstoneCompactRatio = 0.25` |
| flush skip 与比例检查 | `MacEverything/Core/IndexPersistence.cpp:119-164` | skip-gate 后才检查 tombstone ratio |
| v6 全量重写 | `MacEverything/Core/IndexPersistence.cpp:166-188` | WAL swap 后调用 `flatWriter_->fullRewrite()` |
| flush 日志 | `MacEverything/Core/IndexPersistence.cpp:202-205` | `Flushed v6 flat index ... rewrite=...` |
| reclaim 日志 | `MacEverything/Core/IndexPersistence.cpp:225-280` | `fullCompact()` 与 `Reclaimed ... tombstones` |
| COW compaction 模型 | `MacEverything/Core/SearchEngine.cpp:590-789` | snapshot → no-lock build → unique_lock swap/replay |
| FSEvents 300ms | `MacEverything/Core/FileSystemWatcher.cpp:52-62` | 300ms coalesce + FileEvents/NoDefer |
| FSEvents 计数 | `MacEverything/Core/FileSystemWatcher.cpp:122-128` | `lastEventId` 与 raw event counter |
| batch mutation | `MacEverything/Core/SearchEngine.cpp:570-587` | 每 300 个 mutation 持 unique lock |
| 短查询快路径 | `MacEverything/Core/SearchEngineQuery.cpp:248-271` | `short-query-cache` 优先于 Advanced |
| Advanced 日志 | `MacEverything/Core/SearchEngineAdvancedQuery.cpp:1050-1088` | path/timing/candidates 记录 |
| HTTP accept loop | `MacEverything/Core/HttpServer.cpp:185-202` | 单线程 accept 后同步 `handleConnection()` |

## 4. 优先级总览

| 优先级 | 事项 | 为什么现在做 | 验收核心 |
|---|---|---|---|
| P0-1 | 修复 tombstone compaction 阀门 | 16.08% 仍 0 reclaim，flush 重写 1.043M 死记录 | `Reclaimed > 0`，ratio 被压住，rewrite 体积下降 |
| P0-2 | COW/no-lock snapshot flush | 162 次 flush，rewrite 3210–4116ms | flush 期间查询无明显尾延迟 |
| P0-3 | page residency 策略 | R112 冷事件按内存散布度放大，R115 `dm:` 裂纹 | floor-swing 收窄，`dm:` 不再交叉 |
| P0-4 | FSEvents/ingest 可观测与背压 | R116 增量 83.0% 入墓，缺少直接事件链路指标 | 每窗口有 raw/coalesced/tomb/live/flush reason |
| P0-5 | 查询与 HTTP 护栏 | 仍有 2.3s 历史尾延迟，accept loop 单线程 | deadline、取消、并发连接不互相阻塞 |
| P1-1 | CJK/content 辅助索引 | CJK/content 慢，但当前主要受页驻留影响 | P0 稳定后再评估收益 |
| P1-2 | Advanced 查询拆分 | `SearchEngineAdvancedQuery.cpp` 超 1000 行 | 按 trigram/linear/pure-filter 拆分 |
| P1-3 | mmap/zero-copy 与 posting 压缩 | v6 已接近 mmap-ready | 启动和内存收益实测成立 |

## 5. P0-1：修复 tombstone compaction 阀门

### 问题

当前 `kTombstoneCompactRatio = 0.25`。

最新报告 `R_2605311211` 已到：

| 指标 | 值 |
|---|---:|
| total records | 6,486,890 |
| live records | 5,443,707 |
| tombstones | 1,043,183 |
| tombRatio | 16.08% |
| 本轮 record 增量 | +19,292 |
| 本轮 live 增量 | +3,293 |
| 本轮新增 tombstone | +15,999 |
| 入墓比 | 83.0% |

但同一报告仍是：

| 指标 | 值 |
|---|---:|
| flush 次数 | 162 |
| compaction | 0 |
| Reclaimed | 0 |
| rewrite | 3210–4116ms |

### 判断

12.97% 旧参考线不是硬崩溃阈值，因为 `R_2605311211` 已稳定运行到 16.08%。

但这不削弱 P0，反而说明 25% 阀门不是健康阀门。

系统已经在 16.08% 承担 1.043M tombstone 的 I/O 和页驻留债务，却仍没有自动 reclaim。

### 设计方向

1. 将比例阀从 25% 下调到 8–10%。
2. 增加 hysteresis，避免刚压到阈值附近就频繁 fullCompact。
3. flush 前先评估 tombstone health，不要让普通 flush 在高 tombstone 状态继续重写死记录。
4. `fullCompact()` 保持复用现有 COW 三阶段模型。
5. 对 content index remap 做集成验证，避免文件内容索引错位。

### 验收标准

| 验收项 | 目标 |
|---|---|
| 日志 | 至少出现一次 `triggering full compaction` |
| 日志 | 至少出现一次 `Reclaimed N tombstones`，且 `N > 0` |
| 比例 | compaction 后 tombRatio 降到目标阀值以下 |
| rewrite | 后续 `Flushed v6` 的 recordCount/rewrite 体积低于 compaction 前 |
| 正确性 | 搜索结果、content index、`lastEventId` 在 compaction 前后保持一致 |
| 长跑 | 24h 内 tombRatio 不再单调无界增长 |

### 测试清单

- 构造 100K+ record 的 synthetic index，制造 12% tombstone，验证自动触发 compaction。
- 构造低于阀值的 5% tombstone，验证不会误触发。
- compaction 期间持续 add/remove/update，验证 Phase 3 replay 后无丢失。
- content index remap 后通过 GUI `infile:import` 或 HTTP `/api/search/content?q=import` 验证结果不串位；普通 `content:` 仅是查询 filter 语法，不作为 ContentIndex 验收入口。
- 强制 kill 后 WAL replay，验证 `lastEventId` 和 live count 正确。

### 取舍

| 取舍 | 说明 |
|---|---|
| 更低阀值 | 提前 reclaim，减少 rewrite-bloat；但 compaction 更频繁 |
| hysteresis | 降低抖动；但实现和诊断复杂度增加 |
| COW 内存峰值 | compaction 需要额外 snapshot 内存；需在大索引上测峰值 |
| content remap | 正确性风险高于普通 flush；必须用集成测试覆盖 |

## 6. P0-2：COW/no-lock snapshot flush

### 问题

`IndexPersistence::flush()` 当前路径是：

1. 检查 WAL。
2. 计算 tombstone ratio。
3. swap WAL。
4. 调用 `flatWriter_->fullRewrite(*engine_, metadata)`。
5. 保存 `.sqcache`。
6. rename WAL。

`FlatIndexWriter::fullRewrite()` 会写 v6 的所有 sections：

- `NAMES_ORIG`
- `NAMES_LOWER`
- `PATH_POOL`
- `LOWER_PATH_POOL`
- `PATH_INDICES`
- `TYPES`
- `SIZES`
- `MOD_TIMES`
- `INODES`
- `DEV_IDS`
- `METADATA_KV`

虽然 `MacEverything/Core/FlatIndexWriter.cpp:166-167` 已使用 `F_NOCACHE`，报告仍显示 flush 与页驻留压力相关。

### 数据证据

| 报告 | flush 证据 |
|---|---|
| `R_2605302211` | 16 次 flush，0 compaction；暖态 rewrite 3010/3585/4620/5562/6200/6784ms |
| `R_2605311011` | 114 次 flush，重写 6.39M records，含 975K tombstone |
| `R_2605311141` | 150 次 flush，rewrite 3293–4323ms |
| `R_2605311211` | 162 次 flush，rewrite 3210–4116ms |

### 设计方向

1. 复用 `SearchEngine::compactRecords()` 的三阶段思想。
2. Phase A：短 shared_lock 下获取可序列化 snapshot。
3. Phase B：无锁写 v6 临时文件。
4. Phase C：短锁更新 flush 状态、WAL 状态和 metadata。
5. flush reason、snapshot recordCount、liveCount、tombCount 必须进入日志。
6. 保留崩溃恢复：新 WAL、旧 WAL、tmp v6 的 rename 顺序要可证明。

### 验收标准

| 验收项 | 目标 |
|---|---|
| 查询尾延迟 | flush 期间组织流量无新增 ≥500ms 查询 |
| 锁时间 | query `lockHeld` 不随 flush rewrite 进入秒级 |
| flush 正确性 | WAL swap 后 crash/restart 不丢 mutation |
| 日志 | 每次 flush 记录 reason、snapshot total/live/tomb、rewriteMs |
| 性能 | rewrite 仍可耗时，但不制造读者长时间阻塞 |
| 稳定性 | 24h 内 `Failed to flush paged index` 为 0 |

### 取舍

| 取舍 | 说明 |
|---|---|
| snapshot 内存 | 可增加峰值内存；需按 6.5M records 测量 |
| 复杂度 | WAL 与 v6 rename 顺序更敏感 |
| 一致性 | snapshot 与 live mutation 并发，需要明确 replay/metadata 边界 |
| 收益 | 即使 rewrite 本身耗时不降，也能隔离读路径尾延迟 |

## 7. P0-3：page residency 策略

### 问题

报告连续显示字符串路径和部分数值路径的波动更像 page-residency 问题，而不是算法永久退化。

`R_2605311011` 的 W2 全局冷事件给出强证据：

| query | 类型 | W1 floor | W2 floor | 摆动 |
|---|---|---:|---:|---:|
| `size:>100m` | 数值连续列 | 8.78ms | 13.93ms | 1.59x |
| `content:import` | 字符串偏移/池 | 51.23ms | 144.75ms | 2.83x |
| `dm:today` | 时间列 | 16.85ms | 61.43ms | 3.65x |
| `path:/Users` | path 串池 | 49.37ms | 193.51ms | 3.92x |
| `中文测试` | UTF-8 串池 | 42.39ms | 242.08ms | 5.71x |

`R_2605311141` 出现 `dm:` 裂纹：

| 指标 | 值 |
|---|---:|
| `dm:today` 电池 | 107.75ms |
| `dm:today` 再探 | 78.56 / 81.74 / 112.53 / 84.76ms |
| 对抗口径 | 最差 dm 112.53 > 最佳 path 96.75 |
| 比值 | 0.86x，首次交叉 |

`R_2605311211` 又恢复：

| 指标 | 值 |
|---|---:|
| `dm:today` 再探 | 65.30 / 39.27 / 35.61 / 38.67 / 62.67ms |
| `dm` global floor | 37.11ms |
| `content` global floor | 115.48ms |
| 对抗口径 | 最差 dm 72.06 < 最佳 content 115.48 |
| 比值 | 1.60x，恢复 |

### 判断

如果 `dm:` 是算法或路径永久劣化，不应在 R115→R116 中回退。

可逆升档更符合页驻留和 flush 驱逐解释。

### 设计方向

1. 先做诊断型 page residency：记录关键 SoA 列、string pool、trigram buckets 的访问热度和缺页代理指标。
2. 优先保护小而热的连续列：`types_`、`sizes_`、`modTimes_`、`pathIndices_`。
3. 再保护高散布字符串池：`namePool_`、`origNamePool_`、`pathPool_`、`lowerPathPool_`。
4. macOS 上优先评估 `madvise`/访问预热；`mlock` 作为有上限的可选策略。
5. 避免无限制 pin memory，必须有内存预算和降级路径。

### 验收标准

| 验收项 | 目标 |
|---|---|
| `dm:` | 不再出现对抗口径交叉，即 worst dm < best string |
| floor-swing | R112 类跨窗 swing 明显收窄 |
| 字符串层 | `content/path/CJK` ns/slot 不再 sustained-hot 到 17–21ns 长期区间 |
| 数值层 | `size` 与 `dm` floor 回到稳定数值带 |
| 内存 | pin/prewarm 额外内存有上限，有日志 |
| 降级 | 内存压力下自动关闭或缩小 residency 策略 |

### 取舍

| 取舍 | 说明 |
|---|---|
| `mlock` | 最强但可能影响系统内存和权限策略 |
| `madvise`/预热 | 风险较低但收益不确定 |
| pin 字符串池 | 直接命中痛点，但内存占用大 |
| pin 数值列 | 成本小，能防止 `dm:` 裂纹扩散 |
| 先观测后固定 | 多一轮实现，但避免盲目 pin 错对象 |

## 8. P0-4：FSEvents/ingest 可观测与背压

### 问题

当前报告能看到 tombstone/live 结果，但看不到完整原因链：

- raw FSEvents 有多少？
- coalesced 后 mutation 有多少？
- remove/update/rescan 各多少？
- 哪些 mutation 造成 tombstone？
- flush 是被 WAL entry、WAL size、timer 还是 force 触发？
- page-cold 事件是否紧跟 flush 或 ingest burst？

### 已有代码基础

`FileSystemWatcher` 已有：

- 300ms coalesce latency。
- `lastEventId_`。
- `totalEventsReceived_`。
- noisy system path filters。
- `MustScanSubDirs` 检测。
- callback 传递 filtered events。

`SearchEngine::batchMutate()` 已有：

- 每 300 个 mutation 分块。
- 每块持 unique lock。
- remove/update 写 WAL 并产生 tombstone。

### 数据证据

| 报告 | ingest/tombstone 现象 |
|---|---|
| `R_2605311011` | 窗口 +2,013 tombstone，99.6% 入墓 |
| `R_2605311141` | record +17,574 / live +3,225 / tomb +14,349，81.6% 入墓 |
| `R_2605311211` | record +19,292 / live +3,293 / tomb +15,999，83.0% 入墓 |

### 设计方向

1. 增加 ingest window metrics，窗口建议 60s 或跟 flush 周期对齐。
2. 记录 raw events、filtered events、coalesced paths。
3. 记录 mutation 类型：remove、update、rescanPrefix、ignored。
4. 记录 liveDelta、tombDelta、recordDelta。
5. 记录 flush trigger reason。
6. 将 page-cold 查询与最近 flush/ingest burst 做时间相关日志。
7. 增加 backpressure：同一路径短时间多次事件合并，只保留最终状态。
8. 对自写缓存目录和 app 内部路径持续审计，防止 self-induced churn。

### 验收标准

| 验收项 | 目标 |
|---|---|
| 日志 | 每窗口输出 raw/filtered/coalesced/mutation/tomb/live |
| 归因 | 每次 flush 有 trigger reason |
| 告警 | 连续窗口入墓比 >80% 时有明确诊断 |
| 降噪 | 同一路径 storm 被合并，mutation 数显著低于 raw events |
| 正确性 | 合并后不丢最终文件状态 |
| 关联 | 慢查询报告能标注最近 flush/ingest burst 距离 |

### 取舍

| 取舍 | 说明 |
|---|---|
| 可观测性 | 多日志可能影响性能，需要采样或汇总 |
| 背压 | 会牺牲极端情况下的实时性，但换来稳定性 |
| 合并策略 | 需要处理 rename/delete/create 顺序 |
| 告警阈值 | 80% 入墓来自当前报告，后续可按数据调整 |

## 9. P0-5：查询与 HTTP 护栏

### 问题

核心查询路径整体健康，但仍需要系统级护栏。

`R_2605311211` 显示：

| 指标 | 值 |
|---|---:|
| QueryAdvanced | 353 次 |
| 100–200ms | 163 |
| 200–500ms | 187 |
| ≥500ms | 3 |
| WARN | 0 |
| ERROR | 0 |

≥500ms 中两条是 R110 残留 `size:>100m` 2354/1902ms，一条是探针 `中文测试` 517ms。

这说明组织流量没有持续恶化，但系统仍存在尾延迟形态。

HTTP 侧 `MacEverything/Core/HttpServer.cpp:185-202` 是 accept 后同步 `handleConnection()`，慢客户端或慢查询可能阻塞后续连接。

### 设计方向

1. 给查询增加 deadline/timeout。
2. 复用已有 session generation cancellation 机制。
3. HTTP accept loop 改为轻量 accept + worker pool。
4. 慢客户端读写不占用 accept loop。
5. 慢查询返回结构化错误或 partial response。
6. QueryAdvanced 日志增加 deadline、cancelled、timeout reason。

### 验收标准

| 验收项 | 目标 |
|---|---|
| HTTP 并发 | 多个并发 curl 不被单个慢请求 head-of-line blocking |
| timeout | 超时查询返回明确 JSON，不挂死 |
| cancellation | 新一轮 session 查询能取消旧查询 |
| 日志 | 慢查询能区分正常慢、timeout、cancelled |
| 尾延迟 | 压测下组织查询无新增秒级尾延迟 |
| 兼容性 | SwiftUI、CLI、MCP 调用语义不破坏 |

### 取舍

| 取舍 | 说明 |
|---|---|
| timeout | 保护系统，但可能截断用户期望的重查询 |
| worker pool | 提高并发，但引入线程资源和调度复杂度 |
| partial result | UX 更友好，但排序和一致性更难解释 |
| cancel | 可减少浪费，但需保证锁和临时对象安全释放 |

## 10. P1-1：CJK/content/path 辅助索引

### 为什么不是 P0

最新数据说明，CJK/content/path 慢并不只是算法缺索引。

`R_2605311211` 的全局地板：

| query | floor | ns/slot |
|---|---:|---:|
| `content:import` | 115.48ms | 17.80 |
| `path:/Users` | 124.51ms | 19.19 |
| `中文测试` | 137.73ms | 21.23 |

这些值已经处于 sustained-hot 字符串层。

若先上 CJK 2-gram/trigram，而 flush/page residency 未修复，新增索引也可能被同样的页驻留问题影响。

### 设计方向

1. P0 稳定后重新跑同一电池，确认残余慢点。
2. CJK 短词优先考虑 2-gram，而非直接套 ASCII trigram。
3. content 查询区分文件名/路径 content 与正文 content。
4. path 子串可考虑 path token / directory segment index。
5. 所有辅助索引必须支持 tombstone skip 和 compaction remap。

### 验收标准

| 验收项 | 目标 |
|---|---|
| CJK | `中文测试` 不再走大范围 `advanced-linear-gcd` |
| path | `path:/Users` 有可解释候选集缩小 |
| content | `content:import` 不再全量扫所有 slot |
| 内存 | 新索引内存占用有上限和报告 |
| 更新 | FSEvents 增量更新能维护索引 |
| compaction | remap 后 posting list 不错位 |

## 11. P1-2：Advanced 查询拆分

### 问题

架构文档指出 `SearchEngineAdvancedQuery.cpp` 约 1091 行，超过项目单文件 1000 行规范。

它同时承载：

- `advanced-trigram`
- `advanced-ext-index`
- `advanced-linear-gcd`
- `advanced-pure-filter-soa-gcd`
- regex/path/filter 选择
- timing/logging

### 设计方向

1. 先不改变行为，只做无行为拆分。
2. 按候选集生成、filter eval、linear scan、timing 四类拆。
3. 保留原 `queryAdvanced()` 作为 façade。
4. 拆分后再做 P1 辅助索引，降低改动风险。

### 验收标准

| 验收项 | 目标 |
|---|---|
| 行数 | 单文件低于 1000 行 |
| 行为 | 固定查询电池输出路径不变 |
| 性能 | 关键查询无回退 |
| 测试 | trigram/ext/linear/pure-filter 覆盖 |
| 可维护性 | 每条热路径有独立文件和注释 |

## 12. P1-3：mmap/zero-copy 与 posting 压缩

### 背景

v6 flat index 已按 sections 存储，接近 mmap-ready。

架构文档中已有中期方向：

- zero-copy index load。
- delta compressed posting list。
- 更低内存占用与更好缓存命中率。

### 排序理由

这类优化有潜在收益，但不应早于 P0：

1. tombstone 不回收会污染 mmap 载入体积。
2. flush 全量重写会继续制造 I/O。
3. page residency 未稳定时，mmap 收益难以归因。

### 验收标准

| 验收项 | 目标 |
|---|---|
| 启动 | Phase 1 load 有明确下降 |
| 内存 | RSS 和 dirty pages 下降 |
| 查询 | trigram/ext 不回退 |
| flush | 与 COW flush 兼容 |
| 回滚 | 可切换回现有 v6 load |

## 13. 不建议优先投入的方向

| 方向 | 原因 |
|---|---|
| 继续优化 ASCII 1–2 字符查询 | `short-query-cache` 已从 57.8ms 到 R108 0.37ms、R116 0.44ms |
| 优先重写 trigram/ext | `R_2605311211` trigram/ext 多数 18–46ms，非当前主风险 |
| 先做 CJK 索引替代 P0 | CJK 慢很大部分来自 page residency，先做索引可能误判收益 |
| 只加 try/catch 或吞异常 | 当前是健康度和调度问题，不是异常处理问题 |
| 等 tombstone 到 25% | 16.08% 已产生 1.043M 死记录重写债务，25% 不是健康阀门 |

## 14. 分阶段实施顺序

| 阶段 | 内容 | 依赖 |
|---|---|---|
| Phase 0 | 增加 ingest/flush/query 观测字段 | 无 |
| Phase 1 | 修复 compaction 阀门，证明 `Reclaimed > 0` | Phase 0 可并行 |
| Phase 2 | COW/no-lock snapshot flush | Phase 1 的健康指标更利于验收 |
| Phase 3 | page residency 策略 | Phase 0 数据用于选 pin 对象 |
| Phase 4 | HTTP/query guardrails | 可与 Phase 1–3 并行 |
| Phase 5 | CJK/content/path 辅助索引 | P0 稳定后 |
| Phase 6 | Advanced 拆分、mmap、posting 压缩 | P0 稳定后 |

## 15. 总体验收矩阵

| 指标 | 当前基线 | 目标 |
|---|---:|---:|
| tombstones | 1,043,183 | compaction 后下降 |
| tombRatio | 16.08% | 被阀值和 hysteresis 控住 |
| compaction | 0 | 非 0 |
| Reclaimed | 0 | 非 0 |
| flush rewrite | 3210–4116ms | 可耗时但不阻塞读者 |
| `dm:` floor | 37.11ms / 5.72ns | 稳定，不再升入字符串带 |
| `content` floor | 115.48ms / 17.80ns | P0 后回落或波动收窄 |
| `path` floor | 124.51ms / 19.19ns | P0 后回落或波动收窄 |
| `CJK` floor | 137.73ms / 21.23ns | P0 后回落，P1 再索引化 |
| WARN/ERROR | 0/0 | 继续为 0 |
| 组织流量 ≥500ms | 0 新增 | 继续 0 新增 |
| flush reason | 不完整 | 每次可归因 |
| ingest 入墓比 | 83.0% | 可解释、可告警、可背压 |

## 16. 风险与回滚

| 改动 | 主要风险 | 回滚策略 |
|---|---|---|
| compaction 阀门 | 过度 compaction、内存峰值、remap bug | 配置开关恢复旧阀值，保留强制 flush |
| COW flush | WAL/v6 一致性错误 | crash-injection 测试，保留旧 flush 路径开关 |
| page residency | 内存压力、系统策略限制 | 预算上限，运行时关闭 |
| ingest 背压 | 实时性下降、事件顺序错误 | 只先观测，再逐步启用 coalescing |
| HTTP worker pool | 线程资源和并发 bug | 固定小池，可回退单线程 |
| CJK/content 索引 | 内存增大、compaction remap 复杂 | feature flag，保留 linear fallback |

## 17. 最终判断

P0 的核心不是“让某个查询再快一点”，而是恢复索引健康度闭环。

必须先让系统做到：

1. tombstone 能被回收。
2. flush 不再把读路径拖进秒级尾延迟。
3. 热数据页驻留可控。
4. ingest churn 可解释、可背压。
5. HTTP/query 有明确超时和并发边界。

在这些成立后，再投入 CJK/content/path 辅助索引、mmap、posting 压缩，收益才可归因且不容易被长跑退化吞掉。