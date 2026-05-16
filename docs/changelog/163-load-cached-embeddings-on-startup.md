# #163 fix: load cached embeddings from SQLite into VectorSearch on startup

## 问题

重启应用后，VectorSearch 内存索引为空，语义搜索只能找到本次 session 新索引的文件，之前已索引的文件在语义搜索中不可见。

## 根因分析

`startSemanticIndexing()` 遍历所有文件时，对于内容未变化的文件（SQLite 中已有 embedding），`needsUpdate()` 返回 false 后直接 `continue` 跳过。而 `vecSearch->addVector()` 只在重新调用 LLM 获取 embedding 后才执行。这意味着 SQLite 中已持久化的 embedding 从未被加载到内存中的 VectorSearch。

`EmbeddingIndex::getAllEmbeddings()` 方法已经实现，但启动时从未被调用。

## 修复方案

1. 在 `ServiceEngine+Semantic.cpp` 中新增 `loadCachedEmbeddings()` 方法：
   - 调用 `getAllEmbeddings()` 从 SQLite 读取全部 embedding
   - 通过 `indexForPath()` 将文件路径映射为 fileIndex
   - 调用 `addVector()` 加载到 VectorSearch 内存索引

2. 在所有 4 个启动/重建路径中，在 `embIdx->open(dbPath)` 之后、`startSemanticIndexing()` 之前调用 `loadCachedEmbeddings()`：
   - `startFullScan()` 路径 (ServiceEngine.cpp:168)
   - `backgroundSyncEngine()` FSEvents replay 成功路径 (ServiceEngine.cpp:381)
   - `backgroundSyncEngine()` 全量扫描回退路径 (ServiceEngine.cpp:461)
   - `rebuildSemanticIndex()` 路径 (ServiceEngine+Semantic.cpp:280)

3. 在 `ServiceEngine.h` 中声明 `loadCachedEmbeddings()` 方法

## 修改文件

- `MacEverything/Core/ServiceEngine.h` — 新增方法声明
- `MacEverything/Core/ServiceEngine.cpp` — 3 处启动路径插入调用
- `MacEverything/Core/ServiceEngine+Semantic.cpp` — 实现方法 + rebuildSemanticIndex 路径插入调用
- `tests/test_embedding_cache_load.h` — 新增 3 个测试用例
- `test_all.cpp` — 注册测试 (part 79b)

## 测试

- test 79b: Embedding Cache Load Tests (3 cases ALL PASSED)
  - 缓存 embedding 从 SQLite 重新加载到 VectorSearch
  - 空数据库不产生向量
  - 缓存向量与新索引向量共存
