# 173 - 性能分析审计报告 R_2605231804

## 概述

对 MacEverything 搜索服务进行全面性能审计，产出报告 `docs/performance_ana/R_2605231804.md`。

## 方法

- 注入 20 类不同长度/类型查询 + AI 翻译 5 类自然语言 + 重复采样稳定性
- 分析最近一次启动（2026-05-22 10:43）以来 31.3h、5308 行日志

## 核心发现

### 严重问题（4 项）

1. **墓碑占比 22.9%**：31h 内从 6.2% 涨到 23%，COW compaction 从未完成
2. **Flush 风暴**：2481 次 flush / 31h，平均 45s（配置 300s），89% 在 60s 内
3. **7 次 `Failed to flush paged index` ERROR** 无根因现场
4. **括号字面量不可搜**：`test (1)` 返回 0 结果（macOS 自动重命名大量使用）

### 中等问题（3 项）

- 扩展名查询 HTTP 端到端 1~3.5s（内部计时 200~472ms + 锁等待放大）
- AI 翻译质量：`图片→pic:` 残缺、`项目中cpp→content:cpp` 语义错误
- Content 索引从未构建，内容搜索全量不可用

## 优化建议

| 优先级 | 行动项 | 预期收益 |
|---|---|---|
| P0 | flush 限流（min_interval=300s token-bucket）| 写盘降 5×，锁竞争消除 |
| P0 | ERROR 补全 errno/path/stage | 让 #3 可定位 |
| P0 | compaction 按墓碑比例 ≥15% 触发 | 线性查询提速 1.25× |
| P0 | QueryParser 括号当字面量 | 修一类文件搜不到 |
| P1 | 扩展名倒排索引 | `.h` 类查询 300ms→<10ms |
| P1 | HTTP 访问日志 | 可观测性 |
| P1 | shared_lock 分离读写 | 时延抖动归零 |
| P1 | AI prompt 精简 | infer p95 降 ~30% |
