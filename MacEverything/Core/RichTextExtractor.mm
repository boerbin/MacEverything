#import "RichTextExtractor.h"
#import <CoreServices/CoreServices.h>
#import <Quartz/Quartz.h>     // PDFKit
#import <AppKit/AppKit.h>     // NSAttributedString rich text import
#include <cctype>
#include <unordered_set>

namespace me {

// ── Extension check ──────────────────────────────────────────────

static const std::unordered_set<std::string> kRichDocExtensions = {
    "pdf", "doc", "docx", "xls", "xlsx", "ppt", "pptx",
    "rtf", "odt", "ods", "odp"
};

bool isRichDocExtension(const std::string& path) {
    size_t dot = path.rfind('.');
    if (dot == std::string::npos || dot + 1 >= path.size()) return false;
    std::string ext = path.substr(dot + 1);
    for (auto& c : ext) c = std::tolower(static_cast<unsigned char>(c));
    return kRichDocExtensions.count(ext) > 0;
}

// ── Spotlight extraction (C API, works from C++) ─────────────────

static std::string extractViaSpotlight(const std::string& path) {
    @autoreleasepool {
        CFStringRef cfPath = CFStringCreateWithCString(kCFAllocatorDefault,
                                                       path.c_str(),
                                                       kCFStringEncodingUTF8);
        if (!cfPath) return {};

        MDItemRef item = MDItemCreate(kCFAllocatorDefault, cfPath);
        CFRelease(cfPath);
        if (!item) return {};

        CFTypeRef attr = MDItemCopyAttribute(item, kMDItemTextContent);
        CFRelease(item);
        if (!attr) return {};

        if (CFGetTypeID(attr) != CFStringGetTypeID()) {
            CFRelease(attr);
            return {};
        }

        CFStringRef cfText = static_cast<CFStringRef>(attr);
        CFIndex len = CFStringGetLength(cfText);
        if (len == 0) {
            CFRelease(cfText);
            return {};
        }

        CFIndex bufSize = CFStringGetMaximumSizeForEncoding(len, kCFStringEncodingUTF8) + 1;
        std::string result(static_cast<size_t>(bufSize), '\0');
        Boolean ok = CFStringGetCString(cfText, result.data(),
                                        bufSize, kCFStringEncodingUTF8);
        CFRelease(cfText);

        if (!ok) return {};
        result.resize(std::strlen(result.c_str()));
        return result;
    }
}

// ── PDFKit extraction ────────────────────────────────────────────

static std::string extractViaPDFKit(const std::string& path) {
    @autoreleasepool {
        @try {
            NSString *nsPath = [NSString stringWithUTF8String:path.c_str()];
            if (!nsPath) return {};
            NSURL *url = [NSURL fileURLWithPath:nsPath];
            PDFDocument *doc = [[PDFDocument alloc] initWithURL:url];
            if (!doc || doc.pageCount == 0) return {};

            NSMutableString *text = [NSMutableString string];
            for (NSUInteger i = 0; i < doc.pageCount; i++) {
                PDFPage *page = [doc pageAtIndex:i];
                NSString *pageText = [page string];
                if (pageText.length > 0) {
                    if (text.length > 0) [text appendString:@"\n"];
                    [text appendString:pageText];
                }
            }
            if (text.length == 0) return {};
            return std::string([text UTF8String] ?: "");
        } @catch (NSException *) {
            return {};
        }
    }
}

// ── NSAttributedString extraction (DOCX, DOC, RTF, ODT, HTML) ───

static std::string extractViaAttributedString(const std::string& path) {
    @autoreleasepool {
        @try {
            NSString *nsPath = [NSString stringWithUTF8String:path.c_str()];
            if (!nsPath) return {};
            NSURL *url = [NSURL fileURLWithPath:nsPath];
            NSError *error = nil;
            NSDictionary *attrs = nil;
            NSAttributedString *attrStr =
                [[NSAttributedString alloc] initWithURL:url
                                                options:@{}
                                     documentAttributes:&attrs
                                                  error:&error];
            if (!attrStr || attrStr.length == 0) return {};
            return std::string([[attrStr string] UTF8String] ?: "");
        } @catch (NSException *) {
            return {};
        }
    }
}

// ── Extension-to-framework routing ───────────────────────────────

static std::string extractViaFramework(const std::string& path) {
    size_t dot = path.rfind('.');
    if (dot == std::string::npos || dot + 1 >= path.size()) return {};
    std::string ext = path.substr(dot + 1);
    for (auto& c : ext) c = std::tolower(static_cast<unsigned char>(c));

    if (ext == "pdf") {
        return extractViaPDFKit(path);
    }

    // NSAttributedString handles: doc, docx, rtf, odt, html
    static const std::unordered_set<std::string> kAttrStringExts = {
        "doc", "docx", "rtf", "odt"
    };
    if (kAttrStringExts.count(ext)) {
        return extractViaAttributedString(path);
    }

    // xls, xlsx, ppt, pptx, ods, odp — no direct framework fallback
    return {};
}

// ── Public API ───────────────────────────────────────────────────

std::string extractRichDocText(const std::string& path, uint64_t maxTextBytes) {
    // Layer 1: try Spotlight metadata (fast, covers all formats Spotlight knows)
    std::string text = extractViaSpotlight(path);

    // Layer 2: framework-specific fallback
    if (text.empty()) {
        text = extractViaFramework(path);
    }

    // Truncate if requested
    if (maxTextBytes > 0 && text.size() > maxTextBytes) {
        text.resize(maxTextBytes);
    }

    return text;
}

} // namespace me
