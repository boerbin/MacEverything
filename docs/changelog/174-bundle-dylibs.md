# 174 - 打包外部 dylib 使 .app 自包含（修复 issue #2）

- **类型**: bugfix
- **日期**: 2026-05-30
- **关联 issue**: [#2](https://github.com/joshua-wu/MacEverything/issues/2) — 在没有 Homebrew 的机器上启动崩溃

## 现象

用户在没有安装 Homebrew 的 Mac 上运行打包好的 `MacEverything.app`，启动即崩溃：

```
dyld: Library not loaded: /opt/homebrew/opt/re2/lib/libre2.11.dylib
  Referenced from: .../MacEverything.app/Contents/MacOS/MacEverything
  Reason: image not found
```

## 根因

`MacEverything` 主二进制链接 re2 时使用的是**绝对路径** `/opt/homebrew/opt/re2/lib/libre2.11.dylib`，而 re2 又递归依赖 64 个 abseil 动态库（同样全部是 `/opt/homebrew` 绝对路径）。这些路径只存在于装了 Homebrew 的开发机上。

应用虽然已经携带 `@executable_path/../Frameworks` 的 rpath，但 `Contents/Frameworks/` 目录是**空的**——打包流程从未把外部 dylib 复制进去，也从未重写 install name。因此最终用户机器上 dyld 找不到库，启动即失败。

这是设计缺陷（打包不完整），不是偶发问题；属于「根因修复」而非「贴膏药」。

## 方案（issue 中的方案 A：把外部 dylib 打进 .app）

新增打包脚本，在 `make dmg` 时自动执行：

1. **闭包求解（BFS）**：从 `Contents/MacOS/` 下的每个 Mach-O 二进制出发，用 `otool -L` 递归收集所有外部依赖（前缀 `/opt/homebrew/`、`/usr/local/`、`/opt/local/`）。re2 + abseil 闭包共 65 个 dylib。
2. **复制**：把闭包中每个 dylib 复制到 `Contents/Frameworks/`，并对 basename 冲突做防御性检查。
3. **重写 install name**：
   - 每个被复制的 dylib `install_name_tool -id @rpath/<base>`；
   - 把所有外部依赖引用 `-change <abs> @rpath/<base>`；
   - 主二进制若缺少 `@executable_path/../Frameworks` rpath 则补上。
4. **重新签名**：先对每个 dylib `codesign --force --sign -`，再对整个 app `codesign --force --deep --sign - --entitlements`（改写 install name 会使旧签名失效，必须重签）。

脚本幂等：对已经是 `@rpath` 相对路径的引用重复运行是 no-op。

macOS 自带 `/bin/bash` 是 3.2，缺少关联数组（依赖闭包 BFS 需要），故脚本用 `/usr/bin/python3` 实现核心逻辑（Command Line Tools 自带）。

## 测试先行（TDD）

新增 `scripts/verify-bundle.sh` 作为可失败的验收测试：扫描 `Contents/MacOS` 与 `Contents/Frameworks` 中所有 Mach-O，`otool -L`（跳过第 1 行 install-id）若仍出现 `/opt/homebrew`、`/usr/local`、`/opt/local` 绝对路径则 **exit 1 + 打印 LEAK**，否则 PASS。

- 红：未打包的 app 上运行 → 1 处 re2 泄漏，FAIL。
- 绿：执行 `bundle-dylibs.sh` 后 → 67 个 Mach-O 检查，0 泄漏，PASS。

运行时验证：把打包后的 app 复制到 `/tmp` 启动，`vmmap`/`lsof` 确认 re2、abseil、llama 模型全部从 bundle 的 `Frameworks/` 加载，无任何 dyld 错误。

## 改动文件

| 文件 | 改动 |
|------|------|
| `scripts/bundle-dylibs.sh` | 新增 — 依赖闭包 BFS + 复制 + 重写 install name + 重签名 |
| `scripts/verify-bundle.sh` | 新增 — 自包含验收测试 |
| `Makefile` | 新增 `bundle`/`verify-bundle` 目标；`dmg` 依赖改为 `bundle`；更新 help |
| `CLAUDE.md` | 打包/验收章节改为推荐 `make dmg`（自动 build→bundle→verify→package） |

## 「是否还有类似问题」检查

应用包含两个 Mach-O 二进制：

- `MacEverything`（主）—— 有此问题，已修复。
- `MacEverythingMCP` —— 只链接系统库（`/usr/lib`、`/System`），无外部绝对路径，干净。

只有主二进制受影响，已彻底覆盖。
