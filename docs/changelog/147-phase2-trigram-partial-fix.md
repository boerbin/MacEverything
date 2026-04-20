# 147 - Fix: Phase 2 部分 trigram 索引导致搜索结果不完整

## 问题现象

启动 MacEverything 后，在前 ~7.5 秒内搜索 "test" 仅返回 1-2 条结果，而非完整的 ~94,245 条。约 7.5 秒后结果自动恢复正常。

## 根因分析

启动流程分两阶段：
1. **Phase 1**：从 v6 缓存加载 ~580 万条原始记录（SoA 数据），无 trigram 索引
2. **Phase 2**：后台构建 trigram 索引（耗时 ~7.5s）

问题出在 Phase 2 窗口期：
- FSEvents 增量更新通过 `addRecord()` 无条件向 `nameTrigramIndex_` 写入新记录的 trigram
- 索引从空变为"包含极少量记录"（~347 条）
- 查询引擎检测到 `!nameTrigramIndex_.empty()`，走 trigram 加速路径
- 但索引只覆盖 347/5,800,000 条记录，搜索结果严重不完整

`completePhase2()` 完成后会用完整索引替换并 replay 增量记录，所以最终结果正确。问题仅在 ~7.5 秒窗口期内。

## 修复方案

### 1. C++ 引擎层：Phase 2 期间跳过 trigram 写入

在 3 个会在 Phase 2 期间被 FSEvents 调用的函数中，用 `phase2Pending_` 原子变量守卫 trigram/extension 写入：

- `addRecord()`
- `batchRescanPrefix()`
- `updateByPathUnlocked()`

```cpp
if (!phase2Pending_.load(std::memory_order_relaxed)) {
    addTrigramsForRecord(idx, ...);
    addPathTrigramsForRecord(idx);
    addExtensionForRecord(idx);
}
```

正确性保证：`completePhase2()` 的 replay 循环会把 `snapSize..currentSize` 范围内所有增量记录重新加入完整索引。

### 2. Phase 2 完成后自动刷新

在 ServiceEngine 的 Phase 2 dispatch 块中，`completePhase2()` 完成后调用已有的 `onIndexChanged` 回调，复用现有管线自动刷新搜索结果：

```
onIndexChanged -> Bridge dispatch_async -> ViewModel.onIndexChanged() -> performIndexRefresh() -> performSearch()
```

不引入任何新的回调类型，保持引擎的前端无关性。

### 3. 状态栏显示

通过 `isPhase2Pending` 状态查询（与 `isScanning()`, `isSyncing()`, `isMonitoring()` 并列），在状态栏显示 "Building index..." 提示。当 syncing 和 building index 同时进行时显示 "Syncing & building index..."。

## 修改文件清单

| 文件 | 变更 |
|------|------|
| `MacEverything/Core/SearchEngine.cpp` | 3 处 trigram 写入加 `phase2Pending_` 守卫 |
| `MacEverything/Core/ServiceEngine.h` | 添加 `isPhase2Pending()` 声明 |
| `MacEverything/Core/ServiceEngine.cpp` | 实现 `isPhase2Pending()`；Phase 2 完成后触发 `onIndexChanged` |
| `MacEverything/Bridge/MacSearchBridge.h` | 添加 `isPhase2Pending` 只读属性 |
| `MacEverything/Bridge/MacSearchBridge.mm` | 实现 `isPhase2Pending` getter |
| `MacEverything/App/SearchViewModel.swift` | 添加 `isBuildingIndex` 状态，在 `performIndexRefresh` 中同步 |
| `MacEverything/App/ContentView.swift` | 状态栏显示 "Building index..." |

## 验证结果

- Release 构建通过
- 单元测试全部通过（11991 tests passed）
- 启动后立即搜索 "test" 返回 10000 条结果（max cap），不再出现仅 1-2 条的情况
- Phase 2 完成后结果自动刷新
