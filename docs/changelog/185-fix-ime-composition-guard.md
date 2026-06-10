# 185 - Fix IME Composition Guard

## Summary

Fixed a bug where Chinese IME (Input Method Editor) keystrokes were lost during composition when the UI refreshed.

## Problem

When typing with a Chinese input method, uncommitted keystrokes (pinyin composition) would be randomly discarded. This happened because:

1. `textDidChange` fired during IME composition, propagating partial text to the SwiftUI binding
2. The binding update triggered `onSearchTextChanged()` → `objectWillChange.send()` → SwiftUI re-render
3. `updateNSView` compared `textView.string != text`, found a mismatch, and overwrote the text view's string with `textView.string = text`, destroying the IME composition state
4. `applyHighlighting` also modified `textStorage` during composition, disrupting marked text attributes

## Root Cause

The `HighlightedSearchField` (NSViewRepresentable) had no awareness of IME composition state. Any SwiftUI re-render — triggered by search text changes, index refresh, or status bar updates — would force `updateNSView` to overwrite the text view content, canceling the active IME session.

## Fix

Added a three-layer composition guard in `HighlightedSearchField.swift`:

1. **Explicit composition tracking** in `HighlightedNSTextView`: Override `setMarkedText`, `unmarkText`, and `insertText` to maintain an `isComposing` flag. This is more reliable than `hasMarkedText()` which can briefly return `false` between consecutive IME keystrokes.

2. **Block `textDidChange` during composition**: When `isComposing` or `hasMarkedText()` is true, skip propagating text to the SwiftUI binding. This prevents the entire SwiftUI re-render cascade.

3. **Block `updateNSView` during composition**: Early return when the text view is composing, preventing any external trigger (index refresh, status bar update) from overwriting the text view content.

4. **Block `applyHighlighting` during composition**: Skip `textStorage` modifications that would disrupt IME marked text attributes.

## Files Changed

- `MacEverything/App/HighlightedSearchField.swift` — All changes in this single file

## Testing

- Built and launched the app
- Verified Chinese IME composition is preserved during typing
- Verified normal (non-IME) search still works correctly
