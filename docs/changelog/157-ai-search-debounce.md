# 157 - AI Search Debounce: Enter or 2s Pause

## Summary

In AI search mode, queries no longer fire on every keystroke. The user must either press Enter to trigger an immediate search, or pause typing for 2+ seconds to auto-trigger. Normal (non-AI) search retains the existing 80ms debounce behavior.

## Changes

### HighlightedSearchField.swift
- Added `onEnter: (() -> Void)?` callback parameter to `HighlightedSearchField`
- Added `onEnterKey: (() -> Void)?` handler to `HighlightedNSTextView`
- Overrode `keyDown` to intercept Return (keyCode 36) and numpad Enter (keyCode 76), calling the `onEnterKey` handler
- Wired coordinator's `handleEnter()` to the NSTextView callback

### SearchViewModel.swift
- Added `onEnterPressed()` method: cancels any pending debounce, routes AI search immediately (NL translate for default, semantic search for `infile:` prefix)
- Modified `onSearchTextChanged()`: AI mode paths now use 2-second debounce instead of immediate execution; normal mode unchanged (80ms debounce)
- AI search only triggers on Enter press or after 2s typing pause

### ContentView.swift
- Wired `onEnter: { viewModel.onEnterPressed() }` to `HighlightedSearchField` initializer

## Rationale

AI searches involve expensive operations (LLM translation or vector embedding). Firing on every keystroke wastes resources and creates a poor UX with flickering intermediate results. Requiring explicit intent (Enter or pause) matches user expectations for AI-powered queries.

## Verification

- Build: `xcodebuild` succeeded on master
- Tests: All C++ tests passed (pre-commit hook)
- DMG: Packaged successfully
- HTTP: `/api/search?q=test` returns results confirming app runs correctly
