# 150 - DOT 语言图表替换架构深度剖析文档

## 变更概述

将 `docs/tech_sharing_architecture_deep_dive.md` 中的 ASCII 图表替换为 Graphviz DOT 语言图表，提升技术分享文档的可读性和表达力。

## 变更详情

### 替换的 ASCII 图表（8 个）

1. **Section 2 — 整体架构分层图**：用彩色 cluster subgraph 表示各层，箭头表示数据流向
2. **Section 4 — DirectoryScanner 并行模型**：展示共享队列、工作线程、work-stealing 模式
3. **Section 5 — 查询执行管线**：6 阶段流水线，含 5-Stage 竞争候选集选择
4. **Section 8 — AST 变换管线**：tokenize → parse → structured → glob → needs analysis
5. **Section 9 — 两阶段启动**：Phase 1 主线程 + Phase 2 后台线程的时序关系
6. **Section 10 — FSEvents 事件处理流程**：过滤→转换→批量写入→WAL→通知
7. **Section 11 — 锁层次**：三层锁结构的获取关系
8. **Section 11 — COW Compaction 三阶段**：shared_lock → no lock → unique_lock 的数据流

### 新增的 DOT 图表（3 个）

1. **Section 5 — 搜索路径决策树**：根据查询特征（是否含文本、高级语法、≥3字符、glob 模式等）自动选择最优搜索路径的决策流程
2. **Section 6 — Trigram 查询求值流程**：以 `*test*.cpp` 为例，展示多段字面量提取、trigram 交集、段间交集、SIMD 验证的完整流程
3. **Section 9 — 持久化生命周期**：从运行时 FSEvents 变更 → batchMutate → WAL → 自适应 Compaction → v6 落盘的完整数据流

### 保持不变的 ASCII 内容

数据结构列表、代码片段、BNF 语法、文件布局、性能表格等仍保留 ASCII 格式——这些内容更适合纯文本表示。

## 统计

- 文件变更：1 file changed, 438 insertions(+), 137 deletions(-)
- 文档总行数：从 1006 行增加到 1306 行
- DOT 图表数量：11 个
