# 158 - Uniform Extension Badge Width for Rapid Removal

## Summary

Extension badges in ContentSettingsView now have uniform width (72pt), enabling Chrome-like tab-close UX: users can click the X button repeatedly in the same spot to remove multiple extensions without moving the mouse.

## Changes

### ContentSettingsView.swift
- Added `.lineLimit(1)` to extension text to prevent wrapping
- Added `Spacer(minLength: 0)` between text and close button to push X to the right edge
- Added `.frame(width: 72)` to each badge HStack for uniform sizing
- X buttons now align vertically across all badges regardless of extension name length

## Rationale

Previously, each badge sized to fit its content (e.g., `.c` was narrower than `.docx`), causing the X button to shift position with each removal. This forced users to re-aim the mouse for every click. With uniform width, the X button stays in the same horizontal position within each column, matching the Chrome tab-close pattern where rapid sequential closes work without mouse movement.

## Verification

- Build: `xcodebuild` succeeded on master
- Tests: All C++ tests passed (pre-commit hook)
- DMG: Packaged successfully
