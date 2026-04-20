# v146 Ghost Suggestion Cursor Position Fix

## Problem
When using ghost text completion (inline autocomplete), pressing Tab to accept the suggestion applied the text correctly but left the cursor at the old position instead of moving it to the end of the completed text.

For example: typing "ex" would show ghost suggestion "ext:", pressing Tab would set text to "ext:" but cursor stayed at position 2 (after "ex") instead of position 4 (end of "ext:").

## Root Cause
In `HighlightedSearchField.swift`, the `updateNSView` method saved cursor position before replacing text, then blindly restored the old position:

```swift
let selectedRanges = textView.selectedRanges  // saves old cursor (pos 2)
textView.string = text                         // sets "ext:"
textView.selectedRanges = selectedRanges       // restores cursor to pos 2
```

This pattern made sense for cases like programmatic text clearing, but was wrong for ghost suggestion acceptance where the text grows.

## Fix
Compare old vs new text length to determine the appropriate cursor behavior:
- **Text grew** (ghost suggestion accepted): move cursor to end of new text
- **Text shrank** (e.g., clear button): clamp cursor to new text bounds

## Files Changed
- `MacEverything/App/HighlightedSearchField.swift` — `updateNSView` cursor logic

## Verification
- Build succeeded
- Manual test: type partial text, accept ghost suggestion via Tab, confirm cursor is at end
