# 181 - Issue #3 内存结构分析文档

- **类型**: docs
- **日期**: 2026-06-01

## 背景

Issue #3 关注 MacEverything 启动后 RSS 明显高于同类文件搜索工具。此前已完成对 `SearchEngine`、v6 加载、Phase 2 索引构建、COW compaction、Flat v6 持久化、`DirectoryScanner` 和可选 `ContentIndex` 的内存结构分析，本次变更将分析沉淀为可复核文档。

## 变更内容

- 新增 `docs/research/issue-3-memory-structures-analysis.md`。
- 按结构补充内存占用表格，覆盖：
  - `SearchEngine` canonical storage（`StringPool`、SoA、路径索引列）。
  - 路径查找与 mutation 索引（尤其是 `pathIndex_`）。
  - name/path trigram、extension、recent、short query cache 等查询加速结构。
  - 可选 `ContentIndex` 的倒排、正排、线程局部 bitmap 与 staging 结构。
  - 启动、Phase 2、compaction、full rewrite、内容索引的瞬时 RSS 峰值来源。
- 给出按 P0/P1/P2 排序的优化建议和后续测量计划。

## 设计与取舍

- 文档定位为 research note，不修改运行时代码，避免在缺少 per-structure 实测数据时直接引入复杂优化。
- 表格中的字节量为源码推导和 5M records 示例估算，明确标注不是生产测量值。
- 优化建议优先围绕 `pathIndex_`、Phase 2 guard、trigram postings 和 compaction 峰值，避免用大幅增加 CPU 或复杂度的方案简单换内存。

## 验证

- 文档包含源码证据索引，关键判断均指向具体文件与行号。
- 后续应通过 debug-only memory report 采集 `capacity()`、bucket 数、posting entries、RSS delta 等指标，把估算转为可验收数据。
