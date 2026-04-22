# 148 — batchMutate 写锁拆分

## 问题

R71 性能报告发现 FSEvents MEGA-STORM（Spotlight 重建索引触发 2.4M 事件）期间，`batchMutate()` 持有 `unique_lock` 长达 71,500ms，导致所有查询（需要 `shared_lock`）完全阻塞。

**根因**：`SearchEngine::batchMutate()` 对整个 ops 向量加一次 `unique_lock`，零让步点。batch 大小与锁持有时间线性增长。

## 修复

将 `batchMutate()` 的单次全量加锁改为分块加锁：

- `kChunkSize = 300`：单 op 约 0.01-0.05ms，300 ops ≈ 3-15ms 持锁时间
- chunk 之间自动释放 `unique_lock`，等待中的 `shared_lock` 查询可以执行
- 无 WAL 一致性问题（batchMutate 内无 WAL 写入，纯内存 SoA 操作）

**预期效果**：lockWait 从 71s 降至 <500ms。

## 变更文件

| 文件 | 变更 |
|------|------|
| `MacEverything/Core/SearchEngine.cpp` | batchMutate 分块实现 |
| `tests/test_batch_split.h` | 新增 Part 77 测试（5 个用例） |
| `test_all.cpp` | 注册 Part 77 |

## 测试

Part 77 — batchMutate Lock Split Tests：
1. 空 ops 不死锁
2. 小 batch (<300) 正确执行
3. 大 batch (1000 ops) 正确执行，验证分块
4. 并发测试：2000 ops writer + timed reader，验证查询不被长期阻塞 (<100ms)
5. 混合 REMOVE + UPDATE 600 ops，验证数据一致性

全量测试 11,962 项 PASS。
