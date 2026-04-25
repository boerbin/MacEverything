# 155 - Rich Document Text Extraction

## Summary

Added rich document text extraction (PDF, DOCX, RTF, ODT, XLSX, PPTX, etc.) to MacEverything's content indexing system, enabling full-text search within binary document formats.

## Motivation

ContentIndex previously only handled plain text files — any file containing NUL bytes was skipped by `readFileIfText()`. This meant PDF, Word, and other office documents were invisible to content search, despite the `doc` macro already listing these extensions.

## Architecture

Two-layer extraction strategy in `RichTextExtractor.mm` (Obj-C++):

1. **Spotlight (primary):** `MDItemCopyAttribute` with `kMDItemTextContent` — fastest path, leverages macOS's pre-built Spotlight index.
2. **Framework fallback:** When Spotlight has no cached text:
   - **PDF:** PDFKit (`PDFDocument` → per-page `string`)
   - **DOCX/RTF/ODT/DOC:** `NSAttributedString` with `documentType` auto-detection
   - **XLSX/PPTX:** `NSAttributedString` (limited but functional for basic text)

All extraction runs in an `@autoreleasepool` with a configurable `maxTextBytes` cap (default 1 MB).

## Changes

| File | Action | Description |
|------|--------|-------------|
| `MacEverything/Core/RichTextExtractor.h` | Created | C++ header declaring `me::extractRichDocText()` and `me::isRichDocExtension()` |
| `MacEverything/Core/RichTextExtractor.mm` | Created | Obj-C++ implementation with Spotlight + PDFKit + NSAttributedString |
| `MacEverything/Core/ContentIndex.cpp` | Modified | `indexFile()` falls back to `extractRichDocText()` when `readFileIfText()` returns empty for rich doc extensions; `generateSnippet()` likewise uses rich text extraction |
| `MacEverything.xcodeproj/project.pbxproj` | Modified | Added RichTextExtractor.h/.mm to Xcode project, linked Quartz.framework via OTHER_LDFLAGS |
| `tests/test_rich_text_extractor.h` | Created | Unit tests for `isRichDocExtension`, `extractRichDocText` (PDF/DOCX/RTF), truncation, error cases, and ContentIndex integration |
| `test_all.cpp` | Modified | Added part 83 (`runRichTextExtractorTests`) to test runner |

## Testing

- **Part 83** (`--part 83`): Tests `isRichDocExtension` for all supported/unsupported extensions, PDF text extraction via minimal in-memory PDF, DOCX extraction via `textutil` conversion, RTF extraction, `maxTextBytes` truncation, error paths (missing file, unsupported extension), and ContentIndex integration (indexing, querying, snippet generation, persistence roundtrip).
- All tests included in `--fast` suite.

## Supported Extensions

pdf, doc, docx, xls, xlsx, ppt, pptx, rtf, odt, ods, odp

## Commits

- `42dd5fc` — feat(content): add RichTextExtractor header
- `59f09d4` — feat(content): implement RichTextExtractor with Spotlight + PDFKit + NSAttributedString
- `40e7326` — build: add RichTextExtractor to Xcode project, link Quartz.framework
- `b45007c` — feat(content): integrate RichTextExtractor into indexFile and generateSnippet
- `c9fc2d4` — test: add failing tests for RichTextExtractor
- `4fe3d2e` — test: add ContentIndex integration tests for rich document indexing
- `4914a88` — merge: integrate rich document text extraction feature
- `89f25c0` — fix: add missing source files to Xcode project
