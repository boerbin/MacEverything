# 183 — Issue #3 内存优化：pathIndex/pathLookup 哈希键替换 + OOM 守卫修复

**类型:** performance  
**日期:** 2026-06-03  
**关联:** Issue #3 (高内存占用)

## 背景

在 5M 记录规模下，`SearchEngine` 的三个 `unordered_map<string, uint32_t>` 成员（`pathIndex_`、`pathLookup_`、`lowerPathLookup_`）各自存储完整路径字符串作为键，导致大量内存被重复字符串占用。Phase 2 OOM 守卫的每记录估算值（200B）也远低于实测值（~800B），存在内存不足风险。

## 规划

基于 `docs/superpowers/plans/2026-06-02-memory-optimization-issue3.md` 制定的 6 任务方案：

1. 先写失败测试（TDD）
2. 添加 `pathHash()`（FNV-1a 64-bit）及 `reconstructLowerPath()` 辅助函数
3. 将 `pathIndex_` 键类型从 `string` 替换为 `uint64_t`
4. 将 `pathLookup_`/`lowerPathLookup_` 键类型同样替换
5. 修正 Phase 2 OOM 守卫估算值
6. 验证 + 变更文档 + 合并

## 实施

### P0 — pathIndex_ 哈希键替换

- **pathHash()**: 静态 FNV-1a 64-bit 哈希函数（offset basis `0xcbf29ce484222325`，prime `0x100000001b3`）
- **pathIndex_**: `unordered_map<string, uint32_t>` → `unordered_map<uint64_t, uint32_t>`
- 约 20 处访问站点全部更新（`loadRecords`、`loadRecordsV5`、`loadRecordsV6`、`addRecord`、`removeByPath`、`removeByPathPrefix`、`batchRescanPrefix`、`compactRecords`、`indexForPath` 等）
- `removeByPathPrefix()` 和 `batchRescanPrefix()` 从迭代 map 字符串键改为遍历 SoA 数组 + `reconstructLowerPath()` 重建路径，仍为 O(N)
- **新增辅助函数**: `reconstructLowerPath(idx)` 从 `lowerPathPool_` + `namePool_` 拼接小写全路径；`verifyPathIndex()` 验证哈希映射正确性

### P1 — pathLookup_/lowerPathLookup_ 哈希键替换

- 两个 map 均从 `unordered_map<string, uint32_t>` → `unordered_map<uint64_t, uint32_t>`
- 更新站点：`internPath()`、`loadRecordsV5` 重建循环、`loadRecordsV6` 重建循环、`SearchEngineQuery.cpp` 目录查找、`SearchEngineStructuredQuery.cpp` 目录查找、`compactRecords()` 局部变量
- 额外发现并修复了计划未列出的 `SearchEngineStructuredQuery.cpp` 中的 `lowerPathLookup_` 使用站点

### P1 — Phase 2 OOM 守卫修复

- `completePhase2()` 中每记录估算从 `200` 提升至 `800`（匹配实测：快照 ~200B + trigram 索引 ~400B + 路径 trigram ~200B）
- 新增 `LOG_INFO` 日志行，记录记录数、估算内存、可用内存，便于生产环境追踪

## 预期内存节省（5M 记录）

| 结构 | 旧键类型 | 新键类型 | 估算节省 |
|------|----------|----------|----------|
| `pathIndex_` | string (~80B avg) | uint64_t (8B) | ~360MB |
| `pathLookup_` | string (~60B avg) | uint64_t (8B) | ~60-100MB |
| `lowerPathLookup_` | string (~60B avg) | uint64_t (8B) | ~60-100MB |
| **合计** | | | **~480-560MB** |

## 测试

- **Part 88** (`tests/test_pathindex_hash.h`): 7 个测试函数覆盖哈希确定性、区分度、增删查、前缀删除、去重、万级碰撞安全
- **Part 89** (`tests/test_path_lookup_pool.h`): 4 个测试函数覆盖路径去重、internPath 复用、删除后持久性、compaction 正确性
- 全量测试：12,114 通过，0 失败

## 变更文件

| 文件 | 操作 |
|------|------|
| `MacEverything/Core/SearchEngine.h` | 修改：类型声明 + 新增辅助方法 |
| `MacEverything/Core/SearchEngine.cpp` | 修改：pathHash/reconstructLowerPath/verifyPathIndex 实现 + ~20 处 pathIndex_ 站点 + internPath/compactRecords |
| `MacEverything/Core/SearchEngineV6.cpp` | 修改：loadRecordsV6 重建 + OOM 守卫 |
| `MacEverything/Core/SearchEngineQuery.cpp` | 修改：pathLookup_ 查找 |
| `MacEverything/Core/SearchEngineStructuredQuery.cpp` | 修改：lowerPathLookup_ 查找 |
| `test_all.cpp` | 修改：注册 Part 88 + 89 |
| `tests/test_pathindex_hash.h` | 新增 |
| `tests/test_path_lookup_pool.h` | 新增 |

## 提交历史

1. `dae2225` — test: add failing tests for hash-keyed pathIndex
2. `0c5dbb8` — feat: add pathHash, reconstructLowerPath, verifyPathIndex helpers
3. `0855afc` — feat: replace pathIndex_ string keys with uint64_t hash keys
4. `a7ae301` — feat: replace pathLookup_/lowerPathLookup_ string keys with hash keys
5. `8ec26d5` — fix: raise Phase 2 OOM guard estimate from 200 to 800 bytes/record
