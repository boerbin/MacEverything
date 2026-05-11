# 160 - Fix IME Return Key Not Committing Composed Text

## Problem

When using Chinese input methods (e.g. Sogou Input Method) in MacEverything's search field, pressing Return/Enter to commit the composed pinyin text did not work. The composed text was swallowed instead of being inserted into the search field. This worked correctly in all other macOS apps.

## Root Cause

In `MacEverything/App/HighlightedSearchField.swift`, the `HighlightedNSTextView.keyDown(with:)` override unconditionally intercepted Return (keyCode 36) and Enter (keyCode 76) to trigger the `onEnterKey` callback. It did not check whether the NSTextView had active marked text (IME composing state via `hasMarkedText()`).

When an IME is composing, pressing Return should be forwarded to the input method framework via `super.keyDown(with:)` so that `NSTextInputClient` protocol methods (`insertText`, `unmarkText`) can commit the composed text. By returning early, the IME composition was never committed.

## Fix

Added `hasMarkedText()` guard before intercepting Return/Enter and Tab keys. When the text view has marked text (IME is actively composing), the event falls through to `super.keyDown(with:)`, allowing the input method to handle it normally.

## Files Changed

- `MacEverything/App/HighlightedSearchField.swift` — `keyDown(with:)` in `HighlightedNSTextView` (1 file, 5 lines changed)

## Testing

- Build succeeded on Release configuration
- App launched and running
