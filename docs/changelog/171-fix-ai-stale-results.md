# 171 - 修复 AI 模式输入时旧搜索结果残留

## 概述

修复了 AI 搜索模式下用户输入新查询时，旧搜索结果在整个 debounce + 翻译期间仍然可见的问题。

## 问题现象

用户在 AI 模式输入 "gbk" 时，列表中显示的是上一次查询（如 "pet"）的结果，让用户误以为这些是 "gbk" 的搜索结果。

## 根因分析

`SearchViewModel.onSearchTextChanged()` 在 AI 搜索分支中：
1. 取消了旧 task，递增 generation（防止 stale 结果写入）
2. 设置了 2 秒 debounce
3. **但没有清除 `displayItems`，也没有设置 `isAITranslating = true`**

导致在 debounce 等待期间，UI 走到 `else { show displayItems }` 分支，展示陈旧结果。

## 修复

在 `isAISearch` 分支创建 debounce task 前，立即：
- 清除 `displayItems`、`cachedResults`、`loadedCount`、`totalMatches`
- 设置 `isAITranslating = true`、`translatedQuery = nil`

这样 UI 立刻进入 "正在翻译" 状态显示 spinner，而非展示旧结果。

## 修改文件
- `MacEverything/App/SearchViewModel.swift` — `onSearchTextChanged()` AI 分支增加状态清理

## 验证
- 编译通过（BUILD SUCCEEDED）
- 快速测试通过（12,025 passed, 0 failed）
