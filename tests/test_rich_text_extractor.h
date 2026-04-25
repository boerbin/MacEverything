#pragma once
// Tests for RichTextExtractor: rich document text extraction

#include "RichTextExtractor.h"
#include "ContentIndex.h"
#include <fstream>
#include <cstdlib>
#include <filesystem>

namespace fs = std::filesystem;

static void runContentIndexRichDocTests() {
    std::cout << "── ContentIndex Rich Doc Integration ──\n";

    std::string tmpDir = "/tmp/maceverything_ci_rich_" + std::to_string(getpid());
    fs::create_directories(tmpDir);

    // Create a minimal PDF
    {
        std::string pdf =
            "%PDF-1.4\n"
            "1 0 obj\n<< /Type /Catalog /Pages 2 0 R >>\nendobj\n"
            "2 0 obj\n<< /Type /Pages /Kids [3 0 R] /Count 1 >>\nendobj\n"
            "3 0 obj\n<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792]\n"
            "   /Contents 4 0 R /Resources << /Font << /F1 5 0 R >> >> >>\nendobj\n"
            "4 0 obj\n<< /Length 52 >>\nstream\n"
            "BT /F1 12 Tf 100 700 Td (ContentIndexRichDoc) Tj ET\n"
            "endstream\nendobj\n"
            "5 0 obj\n<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>\nendobj\n"
            "xref\n0 6\n"
            "0000000000 65535 f \n0000000009 00000 n \n0000000058 00000 n \n"
            "0000000115 00000 n \n0000000266 00000 n \n0000000368 00000 n \n"
            "trailer\n<< /Size 6 /Root 1 0 R >>\nstartxref\n441\n%%EOF\n";
        std::ofstream ofs(tmpDir + "/rich.pdf", std::ios::binary);
        ofs << pdf;
    }

    // ContentIndex should index the PDF via RichTextExtractor
    ContentIndex ci;
    ci.setExtensions({"pdf"});

    bool indexed = ci.indexFile(0, tmpDir + "/rich.pdf");
    check(indexed, "ContentIndex: indexFile succeeds for PDF");
    check(ci.indexedFileCount() == 1, "ContentIndex: PDF is counted as indexed");

    // Query for text within the PDF
    auto matches = ci.query("contentindexrichdoc");
    check(!matches.empty(), "ContentIndex: query finds match in indexed PDF");

    // Snippet generation for PDF
    uint32_t offset = 0;
    std::string snippet = ContentIndex::generateSnippet(
        tmpDir + "/rich.pdf", "contentindexrichdoc", offset);
    if (!snippet.empty()) {
        std::string lower = snippet;
        for (auto& c : lower) c = std::tolower(static_cast<unsigned char>(c));
        check(lower.find("contentindexrichdoc") != std::string::npos,
              "ContentIndex: snippet contains keyword from PDF");
    }

    // Test RTF via ContentIndex
    {
        std::ofstream ofs(tmpDir + "/rich.rtf");
        ofs << "{\\rtf1\\ansi {\\fonttbl {\\f0 Helvetica;}}\n"
            << "\\f0\\fs24 UniqueRTFSearchTerm document.\\par\n}";
    }
    ci.setExtensions({"pdf", "rtf"});
    bool rtfIndexed = ci.indexFile(1, tmpDir + "/rich.rtf");
    check(rtfIndexed, "ContentIndex: indexFile succeeds for RTF");

    auto rtfMatches = ci.query("uniquertfsearchterm");
    check(!rtfMatches.empty(), "ContentIndex: query finds match in indexed RTF");

    // Persistence roundtrip: save and reload should preserve rich doc entries
    std::string savePath = tmpDir + "/ci_rich.bin";
    check(ci.saveToFile(savePath), "ContentIndex: saveToFile with rich docs");

    ContentIndex ci2;
    check(ci2.loadFromFile(savePath), "ContentIndex: loadFromFile with rich docs");
    check(ci2.indexedFileCount() == 2, "ContentIndex: loaded index has 2 files (PDF + RTF)");

    auto reloadMatches = ci2.query("contentindexrichdoc");
    check(!reloadMatches.empty(), "ContentIndex: reloaded index can query PDF content");

    fs::remove_all(tmpDir);
}

static void runRichTextExtractorTests() {
    std::cout << "═══ RichTextExtractor Tests ═══\n\n";

    // --- isRichDocExtension ---

    check(me::isRichDocExtension("/path/to/file.pdf"), "isRichDocExtension: .pdf");
    check(me::isRichDocExtension("/path/to/file.PDF"), "isRichDocExtension: .PDF (case insensitive)");
    check(me::isRichDocExtension("/path/to/file.docx"), "isRichDocExtension: .docx");
    check(me::isRichDocExtension("/path/to/file.doc"), "isRichDocExtension: .doc");
    check(me::isRichDocExtension("/path/to/file.xlsx"), "isRichDocExtension: .xlsx");
    check(me::isRichDocExtension("/path/to/file.pptx"), "isRichDocExtension: .pptx");
    check(me::isRichDocExtension("/path/to/file.rtf"), "isRichDocExtension: .rtf");
    check(me::isRichDocExtension("/path/to/file.odt"), "isRichDocExtension: .odt");
    check(!me::isRichDocExtension("/path/to/file.txt"), "isRichDocExtension: .txt is NOT rich doc");
    check(!me::isRichDocExtension("/path/to/file.cpp"), "isRichDocExtension: .cpp is NOT rich doc");
    check(!me::isRichDocExtension("/path/to/file.md"), "isRichDocExtension: .md is NOT rich doc");
    check(!me::isRichDocExtension("/path/to/file"), "isRichDocExtension: no extension is NOT rich doc");
    check(!me::isRichDocExtension("/path/to/file."), "isRichDocExtension: trailing dot is NOT rich doc");

    // --- extractRichDocText with a minimal PDF ---

    std::string tmpDir = "/tmp/maceverything_rich_test_" + std::to_string(getpid());
    fs::create_directories(tmpDir);

    // Create a minimal valid PDF with extractable text
    {
        std::string pdfContent =
            "%PDF-1.4\n"
            "1 0 obj\n"
            "<< /Type /Catalog /Pages 2 0 R >>\n"
            "endobj\n"
            "2 0 obj\n"
            "<< /Type /Pages /Kids [3 0 R] /Count 1 >>\n"
            "endobj\n"
            "3 0 obj\n"
            "<< /Type /Page /Parent 2 0 R /MediaBox [0 0 612 792]\n"
            "   /Contents 4 0 R /Resources << /Font << /F1 5 0 R >> >> >>\n"
            "endobj\n"
            "4 0 obj\n"
            "<< /Length 44 >>\n"
            "stream\n"
            "BT /F1 12 Tf 100 700 Td (Hello RichTest) Tj ET\n"
            "endstream\n"
            "endobj\n"
            "5 0 obj\n"
            "<< /Type /Font /Subtype /Type1 /BaseFont /Helvetica >>\n"
            "endobj\n"
            "xref\n"
            "0 6\n"
            "0000000000 65535 f \n"
            "0000000009 00000 n \n"
            "0000000058 00000 n \n"
            "0000000115 00000 n \n"
            "0000000266 00000 n \n"
            "0000000360 00000 n \n"
            "trailer\n"
            "<< /Size 6 /Root 1 0 R >>\n"
            "startxref\n"
            "441\n"
            "%%EOF\n";
        std::ofstream ofs(tmpDir + "/test.pdf", std::ios::binary);
        ofs << pdfContent;
    }

    std::string pdfText = me::extractRichDocText(tmpDir + "/test.pdf");
    check(!pdfText.empty(), "extractRichDocText: PDF returns non-empty text");
    // PDFKit should extract "Hello RichTest" from the minimal PDF
    std::string pdfLower = pdfText;
    for (auto& c : pdfLower) c = std::tolower(static_cast<unsigned char>(c));
    check(pdfLower.find("hello") != std::string::npos ||
          pdfLower.find("richtest") != std::string::npos,
          "extractRichDocText: PDF text contains expected content");

    // --- extractRichDocText with DOCX created via textutil ---

    // Create a plain text file, then convert to DOCX using macOS textutil
    {
        std::ofstream ofs(tmpDir + "/source.txt");
        ofs << "MacEverything document extraction test content for DOCX format.";
    }
    int ret = std::system(("textutil -convert docx -output " + tmpDir + "/test.docx " + tmpDir + "/source.txt 2>/dev/null").c_str());
    if (ret == 0 && fs::exists(tmpDir + "/test.docx")) {
        std::string docxText = me::extractRichDocText(tmpDir + "/test.docx");
        check(!docxText.empty(), "extractRichDocText: DOCX returns non-empty text");
        std::string docxLower = docxText;
        for (auto& c : docxLower) c = std::tolower(static_cast<unsigned char>(c));
        check(docxLower.find("maceverything") != std::string::npos,
              "extractRichDocText: DOCX text contains expected content");
    } else {
        std::cout << "  [SKIP] DOCX test: textutil not available\n";
    }

    // --- extractRichDocText with RTF ---

    {
        std::ofstream ofs(tmpDir + "/test.rtf");
        ofs << "{\\rtf1\\ansi\\deff0 {\\fonttbl {\\f0 Helvetica;}}\n"
            << "\\f0\\fs24 RichTextFormat extraction test.\\par\n"
            << "}";
    }

    std::string rtfText = me::extractRichDocText(tmpDir + "/test.rtf");
    check(!rtfText.empty(), "extractRichDocText: RTF returns non-empty text");
    std::string rtfLower = rtfText;
    for (auto& c : rtfLower) c = std::tolower(static_cast<unsigned char>(c));
    check(rtfLower.find("richtextformat") != std::string::npos ||
          rtfLower.find("extraction") != std::string::npos,
          "extractRichDocText: RTF text contains expected content");

    // --- maxTextBytes truncation ---

    std::string truncated = me::extractRichDocText(tmpDir + "/test.pdf", 5);
    check(truncated.size() <= 5, "extractRichDocText: maxTextBytes truncates result");

    // --- Non-existent file ---

    std::string missing = me::extractRichDocText("/nonexistent/file.pdf");
    check(missing.empty(), "extractRichDocText: returns empty for non-existent file");

    // --- Unsupported extension ---

    {
        std::ofstream ofs(tmpDir + "/test.xyz");
        ofs << "plain text in unknown extension";
    }
    std::string unsupported = me::extractRichDocText(tmpDir + "/test.xyz");

    fs::remove_all(tmpDir);
    runContentIndexRichDocTests();
    std::cout << "\n";
}
