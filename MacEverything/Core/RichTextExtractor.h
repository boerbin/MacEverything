#pragma once
#include <string>
#include <cstdint>

namespace me {

/// Extract text content from rich document files (PDF, DOCX, XLS, PPT, etc.)
/// Strategy: tries macOS Spotlight metadata first (kMDItemTextContent),
/// then falls back to native frameworks (PDFKit for PDF, NSAttributedString for DOCX/RTF/ODT).
/// Returns empty string if extraction fails or format unsupported.
/// If maxTextBytes > 0, truncates result to that many bytes.
std::string extractRichDocText(const std::string& path, uint64_t maxTextBytes = 0);

/// Returns true if the file extension indicates a binary document format
/// that extractRichDocText can potentially handle.
/// Recognized: pdf, doc, docx, xls, xlsx, ppt, pptx, rtf, odt, ods, odp
bool isRichDocExtension(const std::string& path);

} // namespace me
