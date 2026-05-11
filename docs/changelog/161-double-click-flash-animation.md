# 161 - Double-Click Flash Highlight Animation

## Problem

When users double-click a search result to open a file, there is no visual feedback confirming the click was registered. The file opens in the background but the UI feels unresponsive.

## Solution

Added a brief flash highlight animation on double-click. The result row's background flashes to accent color (opacity 0.35) for 150ms with an easeOut animation, then fades back before executing the open/reveal action. This mimics the selection flash behavior seen in Finder and other native macOS apps.

## Implementation

- Added `@State var isFlashing` to both `ResultRow` and `ContentResultRow`
- Background fill now uses a ternary: `isFlashing` (0.35 opacity) > `isHovered` (0.12 opacity) > clear
- Added `.animation(.easeOut(duration: 0.15), value: isFlashing)` for smooth transition
- `flashAndRun()` helper sets `isFlashing = true`, then after 150ms resets it and executes the action
- Both `onTapGesture(count: 2)` handlers now go through `flashAndRun()`

## Files Changed

- `MacEverything/App/ResultRow.swift` — flash animation for file search results
- `MacEverything/App/ContentResultRow.swift` — flash animation for content search results
