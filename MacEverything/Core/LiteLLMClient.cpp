#include "LiteLLMClient.h"
#include "httplib.h"
#include <sstream>
#include <cstdlib>

// ---------------------------------------------------------------------------
// Construction
// ---------------------------------------------------------------------------

LiteLLMClient::LiteLLMClient(const std::string& host, int port)
    : host_(host), port_(port) {}

// ---------------------------------------------------------------------------
// JSON helpers (manual — no JSON library, matches project pattern)
// ---------------------------------------------------------------------------

std::string LiteLLMClient::jsonEscape(const std::string& s) {
    std::string out;
    out.reserve(s.size() + 8);
    for (unsigned char c : s) {
        switch (c) {
            case '"':  out += "\\\""; break;
            case '\\': out += "\\\\"; break;
            case '\b': out += "\\b";  break;
            case '\f': out += "\\f";  break;
            case '\n': out += "\\n";  break;
            case '\r': out += "\\r";  break;
            case '\t': out += "\\t";  break;
            default:
                if (c < 0x20) {
                    char buf[8];
                    snprintf(buf, sizeof(buf), "\\u%04x", c);
                    out += buf;
                } else {
                    out += static_cast<char>(c);
                }
                break;
        }
    }
    return out;
}

std::string LiteLLMClient::trim(const std::string& s) {
    size_t start = 0;
    while (start < s.size() && (s[start] == ' ' || s[start] == '\t' ||
                                 s[start] == '\n' || s[start] == '\r'))
        ++start;
    size_t end = s.size();
    while (end > start && (s[end - 1] == ' ' || s[end - 1] == '\t' ||
                            s[end - 1] == '\n' || s[end - 1] == '\r'))
        --end;
    return s.substr(start, end - start);
}

// ---------------------------------------------------------------------------
// Request body builders
// ---------------------------------------------------------------------------

std::string LiteLLMClient::buildChatRequestBody(
    const std::string& model,
    const std::vector<std::pair<std::string, std::string>>& messages,
    float temperature, int maxTokens)
{
    std::ostringstream os;
    os << "{\"model\":\"" << jsonEscape(model) << "\",\"messages\":[";
    for (size_t i = 0; i < messages.size(); ++i) {
        if (i > 0) os << ",";
        os << "{\"role\":\"" << jsonEscape(messages[i].first)
           << "\",\"content\":\"" << jsonEscape(messages[i].second) << "\"}";
    }
    os << "],\"temperature\":" << temperature
       << ",\"max_tokens\":" << maxTokens << "}";
    return os.str();
}

std::string LiteLLMClient::buildEmbedRequestBody(
    const std::string& model, const std::string& text)
{
    std::ostringstream os;
    os << "{\"model\":\"" << jsonEscape(model)
       << "\",\"input\":\"" << jsonEscape(text) << "\"}";
    return os.str();
}

// ---------------------------------------------------------------------------
// Response parsers
// ---------------------------------------------------------------------------

std::string LiteLLMClient::parseChatResponse(const std::string& json) {
    // Find "content":"..." in the response.
    // Handles the standard OpenAI chat completion format.
    const std::string key = "\"content\":\"";
    auto pos = json.find(key);
    if (pos == std::string::npos) return "";
    pos += key.size();

    std::string result;
    while (pos < json.size() && json[pos] != '"') {
        if (json[pos] == '\\' && pos + 1 < json.size()) {
            char next = json[pos + 1];
            switch (next) {
                case '"':  result += '"';  break;
                case '\\': result += '\\'; break;
                case 'n':  result += '\n'; break;
                case 'r':  result += '\r'; break;
                case 't':  result += '\t'; break;
                case 'b':  result += '\b'; break;
                case 'f':  result += '\f'; break;
                default:   result += next; break;
            }
            pos += 2;
        } else {
            result += json[pos];
            ++pos;
        }
    }
    return trim(result);
}

std::vector<float> LiteLLMClient::parseEmbedResponse(const std::string& json) {
    // Find "embedding":[...] and extract comma-separated floats.
    std::vector<float> result;
    const std::string key = "\"embedding\":[";
    auto pos = json.find(key);
    if (pos == std::string::npos) return result;
    pos += key.size();

    while (pos < json.size() && json[pos] != ']') {
        // skip whitespace/commas
        while (pos < json.size() && (json[pos] == ' ' || json[pos] == ','))
            ++pos;
        if (pos >= json.size() || json[pos] == ']') break;

        // parse float
        char* end = nullptr;
        float val = std::strtof(json.c_str() + pos, &end);
        if (end == json.c_str() + pos) break; // no progress
        result.push_back(val);
        pos = static_cast<size_t>(end - json.c_str());
    }
    return result;
}

// ---------------------------------------------------------------------------
// High-level API
// ---------------------------------------------------------------------------

std::string LiteLLMClient::chat(
    const std::string& model,
    const std::vector<std::pair<std::string, std::string>>& messages,
    float temperature, int maxTokens)
{
    httplib::Client cli(host_, port_);
    cli.set_connection_timeout(5);
    cli.set_read_timeout(30);

    auto body = buildChatRequestBody(model, messages, temperature, maxTokens);
    auto res = cli.Post("/v1/chat/completions", body, "application/json");
    if (!res || res->status != 200) return "";
    return parseChatResponse(res->body);
}

std::vector<float> LiteLLMClient::embed(
    const std::string& model, const std::string& text)
{
    httplib::Client cli(host_, port_);
    cli.set_connection_timeout(5);
    cli.set_read_timeout(30);

    auto body = buildEmbedRequestBody(model, text);
    auto res = cli.Post("/v1/embeddings", body, "application/json");
    if (!res || res->status != 200) return {};
    return parseEmbedResponse(res->body);
}

bool LiteLLMClient::isAvailable() {
    httplib::Client cli(host_, port_);
    cli.set_connection_timeout(2);
    cli.set_read_timeout(2);

    auto res = cli.Get("/v1/models");
    return res && res->status == 200;
}
