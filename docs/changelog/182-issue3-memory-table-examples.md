# 182 - Issue #3 内存结构表格增加内容示例列

## 变更类型
docs

## 变更内容

为 `docs/research/issue-3-memory-structures-analysis.md` 中的全部 5 张内存结构表格增加"内容示例"列：

1. **4.1 Canonical storage**（11 行）：为 `origNamePool_`、`namePool_`、`pathIndices_`、`pathPool_`、`lowerPathPool_`、`types_`、`sizes_`、`modTimes_`、`inodes_`、`devIds_`、`dirtyPages_` 各补充示例
2. **4.2 路径查找与 mutation 索引**（5 行）：为 `pathLookup_`、`lowerPathLookup_`、`pathIndex_`、`sessionGenerations_`、`wal_/compactionGen_` 各补充示例
3. **4.3 查询加速索引**（6 行）：为 `nameTrigramIndex_`、`pathTrigramIndex_`、`pathIdxToRecords_`、`extensionIndex_`、`recentCache_`、`ShortQueryCache` 各补充示例
4. **5. ContentIndex 结构表**（5 行）：为 `invertedIndex_`、`fileInfos_`、`extensions_`、`thread_local seen bitmap`、`readFileIfText content string` 各补充示例
5. **6. 瞬时峰值表**（11 行）：为所有临时结构（DirectoryScanner、v6 load、Phase 2、COW compaction、full rewrite、content prune/indexing）各补充示例

示例内容基于项目实际文件路径和数据格式，帮助读者直观理解每个数据结构存储了什么。

## 关联
- Issue #3
- docs/research/issue-3-memory-structures-analysis.md
