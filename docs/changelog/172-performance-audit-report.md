# 172 - 性能分析审计报告 R_2605231404

## 概述

对 MacEverything 搜索服务进行了全面的性能与日志审计，产出报告 `docs/performance_ana/R_2605231404.md`。

## 方法

- 注入 20 类不同长度/类型的查询（单字符 → 短语 → 中文 → 扩展名），测量端到端时延
- 注入 5 类自然语言 AI 翻译请求
- 重复采样稳定性测试
- 分析最近一次启动（2026-05-22 10:43）以来 27.4h、4240 行日志

## 核心发现

### 严重问题（4 项）

1. **墓碑堆积 20.6%**：27h 内 tombstone 从 342K 涨到 1.36M，占比 6.2% → 20.6%，但 COW compaction 从未触发
2. **Flush 风暴**：2006 次 flush / 27h，平均 49s（配置 300s），86% 在 60s 内，单次写 6.6M 记录
3. **5 次 `Failed to flush paged index` ERROR** 无根因现场，无法定位
4. **括号字面量不可搜**：`test (1)` / `test(1)` / `(test)` 全部返回 0 结果（macOS 自动重命名大量使用此格式）

### 中等问题（2 项）

- 扩展名 `.h/.cpp/.md` 永远走 linear-gcd（trigram 不支持 ≤2 字符），200~470ms
- AI prompt 占 68% 上下文（1392/2048 tokens），翻译质量出错（`图片→pic:`、`项目中cpp→content:cpp`）

## 优化建议

| 优先级 | 行动项 | 预期收益 |
|---|---|---|
| P0 | flush 限流（min_interval=300s token-bucket）| 写盘降 5×，时延抖动消除 |
| P0 | ERROR 补全 errno/path/stage | 让 #3 可定位 |
| P0 | QueryParser 括号当字面量 | 修一类文件搜不到的 P0 bug |
| P0 | compaction 按墓碑比例 ≥15% 触发 | 控制膨胀 + linear-gcd 提速 1.25× |
| P1 | 扩展名倒排索引 | `.h` 类查询 300ms → <10ms |
| P1 | HTTP 访问日志 | 可观测性 |
| P1 | shared_lock 分离读写 | 抖动归零 |
| P1 | AI prompt 精简 | infer p95 降 ~30% |
