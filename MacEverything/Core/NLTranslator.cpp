#include "NLTranslator.h"
#include "IModelBackend.h"
#include "Logger.h"
#include <algorithm>
#include <cctype>
#include <regex>
#include <fstream>
#include <sstream>
#include <chrono>
#include <iomanip>

// ── Known filter prefixes (ported from Python KNOWN_FILTERS) ──

static const std::vector<std::string>& knownFilters() {
    static const std::vector<std::string> filters = {
        "ext:", "size:", "path:", "nopath:", "dm:", "dc:", "da:", "file:", "folder:",
        "content:", "regex:", "ww:", "wholeword:", "wfn:", "wholefilename:", "parent:",
        "depth:", "len:", "case:", "nocase:", "type:", "pic:", "video:", "audio:",
        "doc:", "exe:", "zip:", "datemodified:", "datecreated:", "dateaccessed:",
    };
    return filters;
}

// ── Helpers ──

static std::string toLower(const std::string& s) {
    std::string out;
    out.reserve(s.size());
    for (unsigned char c : s) {
        out.push_back(static_cast<char>(std::tolower(c)));
    }
    return out;
}

static std::string trim(const std::string& s) {
    auto start = s.find_first_not_of(" \t\n\r");
    if (start == std::string::npos) return "";
    auto end = s.find_last_not_of(" \t\n\r");
    return s.substr(start, end - start + 1);
}

// ── Constructor ──

NLTranslator::NLTranslator(std::shared_ptr<IModelBackend> backend)
    : backend_(std::move(backend)) {
    resetToDefaults();
}

// ── looksLikeQuerySyntax ──

bool NLTranslator::looksLikeQuerySyntax(const std::string& text) {
    std::string lower = toLower(text);
    for (const auto& filter : knownFilters()) {
        if (lower.find(filter) != std::string::npos) {
            return true;
        }
    }
    return false;
}

// ── cleanLLMResponse ──

std::string NLTranslator::cleanLLMResponse(const std::string& raw) {
    std::string text = trim(raw);
    if (text.empty()) return "";

    // Strip markdown code fences: ```lang\n...\n```
    static const std::regex fenceStart(R"(^```[\w]*\n?)");
    static const std::regex fenceEnd(R"(\n?```$)");
    text = std::regex_replace(text, fenceStart, "");
    text = std::regex_replace(text, fenceEnd, "");

    // Strip explanation prefixes: Query:, Result:, Output:, Translation:, 查询:
    static const std::regex prefixRe(
        R"(^(?:Query|Result|Output|Translation|查询)[:\s：]+)",
        std::regex::icase);
    text = std::regex_replace(text, prefixRe, "");

    // Trim again after stripping
    text = trim(text);

    // Strip surrounding quotes
    if (text.size() >= 2) {
        char first = text.front();
        char last = text.back();
        if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
            text = text.substr(1, text.size() - 2);
        }
    }

    // Take first line only
    auto newlinePos = text.find('\n');
    if (newlinePos != std::string::npos) {
        text = text.substr(0, newlinePos);
    }

    return trim(text);
}

// ── getDefaultSystemPrompt ──

std::string NLTranslator::getDefaultSystemPrompt() {
    return R"(You are a query translator for MacEverything, a macOS file search tool.
Your job: convert the user's natural language description into MacEverything query syntax.

## Query Syntax Reference

Basic: keywords separated by spaces (implicit AND). Use | for OR, ! for NOT.
Quoted: "exact phrase" for exact filename match.
Grouping: <expr1 | expr2> for grouping.

## Filters

ext:py              — Extension filter (multiple: ext:py;js;ts)
size:>1mb           — File size (units: b, kb, mb, gb, tb)
size:100kb..1mb     — Size range
file:               — Files only
folder:             — Directories only
path:keyword        — Path contains keyword
nopath:keyword      — Path does NOT contain keyword
parent:dirname      — Immediate parent directory name
depth:<3            — Directory depth
dm:today            — Date modified (today, yesterday, thisweek, lastweek, thismonth, lastmonth, thisyear, lastyear)
dm:last7days        — Relative date (last2days, last3days, last7days, last30days, last3months, last6months)
dm:>2024-01-01      — Date comparison
dm:2024-01..2024-06 — Date range
dc:                 — Date created (same syntax as dm:)
content:keyword     — Search inside file content
regex:pattern       — ECMAScript regex
ww:word             — Whole word match
case:term           — Case sensitive
audio:              — Audio files (mp3, wav, flac, aac, ogg, m4a, wma)
video:              — Video files (mp4, avi, mkv, mov, wmv, flv, webm)
pic:                — Image files (jpg, jpeg, png, gif, bmp, tiff, svg, webp, ico, heic)
doc:                — Document files (pdf, doc, docx, xls, xlsx, ppt, pptx, txt, md, rtf, csv, pages, numbers, keynote)
exe:                — Executable files
zip:                — Archive files (zip, rar, 7z, tar, gz, bz2, xz, dmg, iso)

## Path Queries

/abc/def            — Name matches "def", path contains "abc"
~/Downloads         — Expands ~ to home directory

## Rules

1. Output ONLY the translated query string, nothing else.
2. Use the most specific filters available. Prefer filters over keywords when possible.
3. For ambiguous time references like "最近" (recent), use dm:last7days.
4. For "大文件" (large files), use size:>100mb unless context suggests otherwise.
5. Recognize common file type descriptions:
   - "Word文档" → ext:doc;docx
   - "Excel表格" → ext:xls;xlsx
   - "PPT/幻灯片" → ext:ppt;pptx
   - "代码" → ext:py;js;ts;go;rs;cpp;h;java;swift
   - "配置文件" → ext:json;yaml;yml;toml;ini;conf;cfg;env
   - "图片/照片" → pic:
   - "视频" → video:
   - "音乐/音频" → audio:
   - "文档" → doc:
   - "压缩包" → zip:
6. "下载" refers to path:Downloads, "桌面" to path:Desktop, "文档" directory to path:Documents.
7. If the input is already valid query syntax, return it unchanged.
8. For Chinese input, translate the intent — not the words literally.
9. If the input is just a filename, keyword, or plain text that does NOT describe any file property (type, size, date, location), return it unchanged as a keyword search. Do NOT invent filters that the user did not ask for.)";
}

// ── getDefaultFewShotExamples ──

std::vector<std::pair<std::string, std::string>> NLTranslator::getDefaultFewShotExamples() {
    return {
        {"最近下载的PDF", "path:Downloads ext:pdf dm:last7days"},
        {"上个月修改的Word文档", "ext:doc;docx dm:lastmonth"},
        {"除了node_modules以外的JS文件", "ext:js nopath:node_modules"},
        {"大于100MB的视频文件", "video: size:>100mb"},
        {"桌面上的截图", "path:Desktop pic:"},
        {"今天创建的Python脚本", "ext:py dc:today"},
        {"recent large PDF files", "ext:pdf size:>10mb dm:last7days"},
        {"3天内修改的Markdown笔记", "ext:md dm:last3days"},
        {"abc", "abc"},
        {"readme", "readme"},
        {"config.json", "config.json"},
        {"hello world", "hello world"},
    };
}

// ── Prompt management ──

void NLTranslator::resetToDefaults() {
    systemPrompt_ = getDefaultSystemPrompt();
    fewShotExamples_ = getDefaultFewShotExamples();
    usingFilePrompt_ = false;
    promptFilePath_.clear();
}

bool NLTranslator::parsePromptFile(const std::string& content) {
    const std::string sysOpen = "<SYSTEM_PROMPT>";
    const std::string sysClose = "</SYSTEM_PROMPT>";
    const std::string fewOpen = "<FEW_SHOT>";
    const std::string fewClose = "</FEW_SHOT>";

    auto sysStart = content.find(sysOpen);
    auto sysEnd = content.find(sysClose);
    if (sysStart == std::string::npos || sysEnd == std::string::npos || sysEnd <= sysStart)
        return false;

    std::string sysPrompt = content.substr(sysStart + sysOpen.size(), sysEnd - sysStart - sysOpen.size());
    // trim
    while (!sysPrompt.empty() && (sysPrompt.front() == '\n' || sysPrompt.front() == '\r'))
        sysPrompt.erase(sysPrompt.begin());
    while (!sysPrompt.empty() && (sysPrompt.back() == '\n' || sysPrompt.back() == '\r'))
        sysPrompt.pop_back();

    if (sysPrompt.empty()) return false;

    auto fewStart = content.find(fewOpen);
    auto fewEnd = content.find(fewClose);
    std::vector<std::pair<std::string, std::string>> examples;

    if (fewStart != std::string::npos && fewEnd != std::string::npos && fewEnd > fewStart) {
        std::string fewSection = content.substr(fewStart + fewOpen.size(), fewEnd - fewStart - fewOpen.size());
        std::istringstream iss(fewSection);
        std::string line;
        std::string pendingUser;
        while (std::getline(iss, line)) {
            // trim line
            while (!line.empty() && (line.back() == '\r' || line.back() == '\n'))
                line.pop_back();
            if (line.empty()) continue;

            if (line.substr(0, 5) == "user:" || line.substr(0, 6) == "user: ") {
                auto colonPos = line.find(':');
                pendingUser = line.substr(colonPos + 1);
                while (!pendingUser.empty() && pendingUser.front() == ' ')
                    pendingUser.erase(pendingUser.begin());
            } else if ((line.substr(0, 10) == "assistant:" || line.substr(0, 11) == "assistant: ") && !pendingUser.empty()) {
                auto colonPos = line.find(':');
                std::string assistantVal = line.substr(colonPos + 1);
                while (!assistantVal.empty() && assistantVal.front() == ' ')
                    assistantVal.erase(assistantVal.begin());
                examples.emplace_back(pendingUser, assistantVal);
                pendingUser.clear();
            }
        }
    }

    systemPrompt_ = sysPrompt;
    fewShotExamples_ = examples; // can be empty if no FEW_SHOT section
    return true;
}

bool NLTranslator::loadPromptFromFile(const std::string& path) {
    std::lock_guard<std::mutex> lock(promptMutex_);
    std::ifstream file(path);
    if (!file.is_open()) {
        resetToDefaults();
        return false;
    }
    std::ostringstream ss;
    ss << file.rdbuf();
    std::string content = ss.str();
    if (content.empty() || !parsePromptFile(content)) {
        resetToDefaults();
        return false;
    }
    usingFilePrompt_ = true;
    promptFilePath_ = path;
    return true;
}

void NLTranslator::setPromptFile(const std::string& path) {
    if (path.empty()) {
        std::lock_guard<std::mutex> lock(promptMutex_);
        resetToDefaults();
        return;
    }
    loadPromptFromFile(path);
}

std::string NLTranslator::promptSource() const {
    std::lock_guard<std::mutex> lock(promptMutex_);
    return usingFilePrompt_ ? promptFilePath_ : "builtin";
}

// ── buildMessages ──

std::vector<std::pair<std::string, std::string>> NLTranslator::buildMessages(const std::string& userQuery) {
    std::lock_guard<std::mutex> lock(promptMutex_);
    std::vector<std::pair<std::string, std::string>> messages;

    // System prompt
    messages.emplace_back("system", systemPrompt_);

    // Few-shot examples as user/assistant pairs
    for (const auto& [nl, query] : fewShotExamples_) {
        messages.emplace_back("user", nl);
        messages.emplace_back("assistant", query);
    }

    // User query
    messages.emplace_back("user", userQuery);

    return messages;
}

// ── translate ──

TranslationResult NLTranslator::translate(const std::string& query) {
    TranslationResult result;
    result.originalQuery = query;

    // Trim input
    std::string trimmed = trim(query);

    // Empty query
    if (trimmed.empty()) {
        result.translatedQuery = trimmed;
        result.success = false;
        result.error = "Empty query";
        return result;
    }

    using Clock = std::chrono::steady_clock;
    auto totalStart = Clock::now();

    // Syntax passthrough
    if (looksLikeQuerySyntax(trimmed)) {
        result.translatedQuery = trimmed;
        result.success = true;
        result.alreadySyntax = true;
        LOG_INFO("AI", "translate passthrough: \"" << trimmed << "\" (already syntax)");
        return result;
    }

    // Need LLM client
    if (!backend_) {
        result.translatedQuery = trimmed;
        result.success = false;
        result.error = "No AI backend available";
        LOG_ERROR("AI", "translate failed: no backend available");
        return result;
    }

    try {
        auto msgStart = Clock::now();
        auto messages = buildMessages(trimmed);
        auto msgMs = std::chrono::duration<double, std::milli>(Clock::now() - msgStart).count();

        auto inferStart = Clock::now();
        std::string rawResponse = backend_->chat(messages);
        auto inferMs = std::chrono::duration<double, std::milli>(Clock::now() - inferStart).count();

        auto cleanStart = Clock::now();
        std::string translated = cleanLLMResponse(rawResponse);
        auto cleanMs = std::chrono::duration<double, std::milli>(Clock::now() - cleanStart).count();

        auto totalMs = std::chrono::duration<double, std::milli>(Clock::now() - totalStart).count();

        if (translated.empty()) {
            result.translatedQuery = trimmed;
            result.success = false;
            result.error = "LLM returned empty response";
            LOG_ERROR("AI", "translate failed: empty response for \"" << trimmed
                      << "\" | infer=" << std::fixed << std::setprecision(1) << inferMs << "ms");
            return result;
        }

        result.translatedQuery = translated;
        result.success = true;

        LOG_INFO("AI", "translate: \"" << trimmed << "\" -> \"" << translated
                 << "\" | build=" << std::fixed << std::setprecision(1) << msgMs
                 << "ms infer=" << inferMs << "ms clean=" << cleanMs
                 << "ms total=" << totalMs << "ms");
    } catch (const std::exception& e) {
        result.translatedQuery = trimmed;
        result.success = false;
        result.error = e.what();
        if (result.error.empty()) {
            result.error = "LLM request failed";
        }
        LOG_ERROR("AI", "translate exception: \"" << trimmed << "\" | error=" << result.error);
    }

    return result;
}
