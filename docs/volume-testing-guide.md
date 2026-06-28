# Volume Mount 测试指南

本骨架引入了"挂载后 5 秒延迟增量索引"功能（可通过 `ServiceConfig::mountDebounceSec` 调整）。验证按从快到慢分四层：

---

## 第 1 层：单元测试（无需外部资源，~5 秒）

```bash
make test-all
./test_all 77 78
```

| Part | 内容 | 状态 |
|------|------|------|
| `77` | `VolumeIndex` 纯逻辑（22 个用例：注册、offline 切换、持久化、长前缀匹配、边界） | ✅ |
| `78` | `VolumeE2E` 集成（用 `injectMountForTest` 模拟挂载，无需 NSWorkspace） | ✅ |

通过标准：所有 `✓` 出现且最后 "Tests passed: N, Tests failed: 0"。

---

## 第 2 层：注入式 e2e（无需真 USB，~10 秒）

`tests/test_volume_e2e.h` 走完这条流程：

```
构造 ServiceEngine（sandbox 模式）
  ↓
VolumeWatcher::injectMountForTest("/tmp/.../fakeusb_a")
  → 验证 mount callback 触发
  ↓
VolumeIndex::addVolume + markOffline
  → 验证 offline 状态正确
  ↓
VolumeWatcher::injectUnmountForTest
  → 验证 unmount callback 触发
  ↓
loadOfflineVolumes 模拟重启
  → 验证 offline 状态被恢复
```

运行：
```bash
./test_all 78
```

---

## 第 3 层：真 USB 端到端（需要物理 USB 盘，~1 分钟）

### 准备

```bash
# 1. 编译 daemon
make clean && make maceverything-daemon

# 2. 启动 daemon，指向沙箱缓存避免污染真实数据
./maceverything-daemon --port 19860 \
    --root / \
    --cache-dir /tmp/maceverything-cache \
    --log-dir /tmp/maceverything-logs &

DAEMON_PID=$!
echo "Daemon PID: $DAEMON_PID"

# 3. 等 daemon 完成首次扫描
sleep 5
curl -s http://127.0.0.1:19860/api/status | python3 -m json.tool
#   确认 recordCount > 0
```

### 挂载测试

```bash
# 在另一个终端：观察日志
tail -F /tmp/maceverything-logs/*.log
```

```bash
# 插上 USB 盘（macOS 自动挂载到 /Volumes/<NAME>）
# 期望日志输出（5 秒内）：
#   [INFO] [VolumeWatcher] Mount detected: /Volumes/<NAME>
#   [INFO] [VolumeWatcher] Debounce: 1 pending mount(s), scheduling 5s delay
#   [INFO] [VolumeWatcher] Rescanning mounted volume: /Volumes/<NAME>
#   [INFO] [VolumeWatcher] Started FSEvents watcher for /Volumes/<NAME>

# 验证能搜到 USB 内的文件
curl -s 'http://127.0.0.1:19860/api/search?q=myfile&maxResults=10' | python3 -m json.tool
```

### 卸载测试

```bash
diskutil unmount /Volumes/<NAME>
# 期望日志：
#   [INFO] [VolumeWatcher] Unmount detected: /Volumes/<NAME>
#   [INFO] [VolumeWatcher] Stopped FSEvents watcher for /Volumes/<NAME>

# 验证记录仍在（标记 offline）
curl -s 'http://127.0.0.1:19860/api/search?q=myfile' | python3 -m json.tool
#   → 仍返回结果（UI 层根据 isOffline 灰显）
```

### 重挂载验证

```bash
# 同一 USB 盘重新插入
# 期望日志：
#   [INFO] [VolumeWatcher] Mount detected: /Volumes/<NAME>
#   ... 5s 后 rescan
#   离线标记被自动清除
```

### 持久化验证

```bash
# 卸载后，关闭 daemon
kill $DAEMON_PID
# 等待 3s 让 flush 完成

# 重新启动
./maceverything-daemon --port 19860 \
    --cache-dir /tmp/maceverything-cache \
    --log-dir /tmp/maceverything-logs &

# 期望日志：
#   [INFO] [IndexPersistence] Loaded v6 flat index, ..., offlineVolumes=1
#   [INFO] [VolumeWatcher] Persisted offline volume is mounted again: /Volumes/<NAME>
#   [INFO] [VolumeWatcher] Debounce: 1 pending mount(s), scheduling 5s delay
```

> 关键点：上次 offline 的卷如果在重启时已挂载，会被自动标 online 并触发 rescan。
> 如果仍未挂载，保持 offline。

### 清理

```bash
kill $DAEMON_PID
rm -rf /tmp/maceverything-cache /tmp/maceverything-logs
```

---

## 第 4 层：5s 去抖验证（用 fake 抖动模拟）

```bash
# 快速插拔 3 次
diskutil unmount /Volumes/USB
diskutil mount /Volumes/USB
diskutil unmount /Volumes/USB
diskutil mount /Volumes/USB

# 期望日志：只看到一次 rescan
#   [INFO] [VolumeWatcher] Debounce: 1 pending mount(s) (合并了所有 mount)
#   [INFO] [VolumeWatcher] Rescanning mounted volume: /Volumes/USB
```

---

## 验证矩阵

| 场景 | 验证方法 | 预期结果 |
|------|----------|----------|
| 单卷挂载 | 第 3 层 | 5s 后日志出现 rescan，搜索可见 |
| 快速抖动 | 第 4 层 | 只 rescan 一次 |
| 卸载 | 第 3 层 | 记录仍在，offline 标记生效 |
| 离线状态持久化 | 第 3 层 | 重启后仍记得哪些卷 offline |
| 重挂载自动恢复 | 第 3 层 | 重启时已挂载的离线卷自动转 online |
| 单元覆盖 | 第 1 层 | 22/22 通过 |
| 注入式 e2e | 第 2 层 | 6/6 通过 |

---

## 已知限制（骨架阶段）

| 项 | 状态 | 备注 |
|----|------|------|
| SwiftUI UI 渲染 offline 样式 | ❌ 未做 | `MEFileResult.isOffline` 已暴露，UI 一行 dim 即可 |
| HTTP API 暴露 volume 列表 | ❌ 未做 | 可加 `GET /api/volumes` |
| MCP 工具 `rescan_volume` | ❌ 未做 | 可加 `maceverything-mcp` tool |
| 5s 期间用户在 UI 主动 rescan | ❌ 未做 | `rescanVolume:` Bridge 方法已就位 |
| 巨量外部卷（>20 个同时挂载） | ⚠ 未测 | VolumeIndex 内部用 `unordered_map`，O(V) 查询，V 很大时需要换 trie |

---

## 调试小贴士

```bash
# 实时观察 mount 事件（不需要 daemon）
log stream --predicate 'eventMessage CONTAINS "VolumeWatcher" OR eventMessage CONTAINS "Mount"' --info

# 看当前挂载的卷
ls -la /Volumes/

# 强制 daemon 重建索引（清掉 persisted offline 状态）
rm /tmp/maceverything-cache/index.v6
./maceverything-daemon --cache-dir /tmp/maceverything-cache ...
```
