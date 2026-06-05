# 184 — Release v1.4

- **类型**: release
- **日期**: 2026-06-05

## 概述

发布 MacEverything v1.4 版本，包含搜索输入延迟修复、内存优化和文档完善。

## 变更内容

### 性能 — 搜索输入卡顿修复 (P0-P3)

根因：多个 `@Published` 属性并发更新引发 SwiftUI 视图无效化风暴，叠加 FSEvents 触发的 `onIndexChanged` 与用户输入竞争主线程。

四项修复：

- **P0**: 将 `highlightHints` 从计算属性改为存储属性，消除每次渲染 ~100x 冗余计算
- **P1**: 移除 8 个属性的 `@Published`，在 12 个批量赋值点手动调用 `objectWillChange.send()` 合并视图更新
- **P2**: 在 `onIndexChanged()` 中添加打字守卫，500ms 内抑制 FSEvents 触发的索引刷新
- **P3**: 将 `applyHighlighting` 延迟到下一个 run loop 迭代执行

### 内存优化 (issue #3)

- 用 `uint64_t` 哈希键替换 `pathIndex_`、`pathLookup_`/`lowerPathLookup_` 的字符串键，减少每条记录内存占用
- 将 Phase 2 OOM 保护估算从 200 提升至 800 bytes/record

### 文档

- 架构深度文档刷新（ShortQueryCache、内置 LLM、R108 数据）
- 发布级配图和信息图
- 竞品分析和 CTR 文档更新
- Issue #3 内存结构分析文档

### 测试

- 输入延迟修复验证测试套件（8 个场景，24 个断言）
- 哈希碰撞与正确性测试套件（4 个分类）

## 发布信息

- 版本: 1.4 (build 5)
- GitHub Release: https://github.com/joshua-wu/MacEverything/releases/tag/v1.4
- 打包格式: DMG（自包含，0 个外部 dylib 泄漏）
