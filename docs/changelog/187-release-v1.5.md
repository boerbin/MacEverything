# 187 — Release v1.5

- **类型**: release
- **日期**: 2026-06-14

## 概述

发布 MacEverything v1.5 版本，重点修复中文输入法组合态丢键和 macOS app bundle Finder 显示名不可搜索问题，并纳入 v1.4 之后的性能观测报告。

## 变更内容

### 输入体验 — 中文 IME 组合态保护

根因：`HighlightedSearchField` 在中文输入法 marked text 组合期间仍会通过 `textDidChange` 推送 SwiftUI binding，随后 `updateNSView` 和 `applyHighlighting` 可能在界面刷新中覆盖 NSTextView 内容或修改 textStorage，导致尚未上屏的拼音按键被取消。

修复：

- 在 `HighlightedNSTextView` 中显式跟踪 IME 组合态
- 组合态期间阻断 `textDidChange` 向 SwiftUI binding 传播半成品文本
- 组合态期间阻断 `updateNSView` 覆盖文本内容
- 组合态期间跳过 `applyHighlighting` 对 marked text 属性的干扰

### 搜索正确性 — app bundle Finder 显示名

根因：搜索索引只使用文件系统 bundle 名，例如 `WebPomodoro.app`，没有索引 `CFBundleDisplayName` / `CFBundleName` 暴露给 Finder 的显示名，例如 `Focus To-Do`。

修复：

- 新增 search-only alias pool，把 app display name 作为搜索别名索引
- 保持 canonical `namePool_` 不变，结果名称、路径、扩展名和持久化仍使用真实文件名
- filename trigram、短查询缓存、结构化查询、高级查询验证和排序均支持 display-name alias
- alias 之间用边界分隔，避免 phrase、regex、glob、whole-word 匹配跨越真实文件名与显示名
- 停止持久化 stale-prone `.sqcache` sidecar，display alias 从当前 `Info.plist` 重新构建

### 文档与观测

- 新增 IME 组合态修复 changelog
- 新增 app display-name 搜索修复 changelog
- 纳入 v1.4 之后的多轮性能分析报告，覆盖 R260610 之后的运行状态、I/O 抖动、tombstone 比例和检索耗时趋势

## 测试与验收

- IME 修复已构建启动并手工验证中文输入法组合态保留，普通搜索路径保持可用
- app display-name 修复新增并通过覆盖 filesystem bundle name、Finder display name、short-query cache、glob、regex、case、whole-word、whole-filename、structured query、alias-boundary phrase rejection、malformed plist、WAL replay、updateByPath 和 stale `.sqcache` 的回归测试
- app display-name 修复的 pre-commit fast gate：`arch -arm64 make test-fast`，12099 passed / 0 failed
- 发布打包使用 `make dmg` 生成自包含 DMG，并通过 bundle dylib leak 校验

## 发布信息

- 版本: 1.5 (build 6)
- GitHub Release: https://github.com/joshua-wu/MacEverything/releases/tag/v1.5
- 打包格式: DMG（自包含，0 个外部 dylib 泄漏）
