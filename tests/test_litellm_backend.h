#pragma once
#include "LiteLLMBackend.h"
#include <cassert>
#include <iostream>
#include <memory>

inline void runLiteLLMBackendTests() {
    std::cout << "=== LiteLLMBackend Tests ===" << std::endl;

    // Test 1: Implements IModelBackend (shared_ptr cast)
    {
        auto backend = std::make_shared<LiteLLMBackend>();
        std::shared_ptr<IModelBackend> iface = backend;
        assert(iface != nullptr);
        std::cout << "  [PASS] Implements IModelBackend" << std::endl;
    }

    // Test 2: Default construction
    {
        LiteLLMBackend backend;
        assert(backend.getHost() == "127.0.0.1");
        assert(backend.getPort() == 19861);
        std::cout << "  [PASS] Default construction" << std::endl;
    }

    // Test 3: Custom host/port
    {
        LiteLLMBackend backend("localhost", 9999);
        assert(backend.getHost() == "localhost");
        assert(backend.getPort() == 9999);
        std::cout << "  [PASS] Custom host/port" << std::endl;
    }

    // Test 4: modelName returns "remote:<host>:<port>"
    {
        LiteLLMBackend backend("10.0.0.1", 8080);
        assert(backend.modelName() == "remote:10.0.0.1:8080");
        std::cout << "  [PASS] modelName()" << std::endl;
    }

    // Test 5: buildChatRequestBody builds correct JSON (no model param)
    {
        LiteLLMBackend backend("127.0.0.1", 19861, "my-model");
        std::vector<std::pair<std::string, std::string>> messages = {
            {"system", "You are a translator."},
            {"user", "hello world"}
        };
        auto body = backend.buildChatRequestBody(messages, 0.1f, 200);
        assert(body.find("\"model\"") != std::string::npos);
        assert(body.find("my-model") != std::string::npos);
        assert(body.find("\"messages\"") != std::string::npos);
        assert(body.find("system") != std::string::npos);
        assert(body.find("hello world") != std::string::npos);
        assert(body.find("\"temperature\"") != std::string::npos);
        assert(body.find("\"max_tokens\"") != std::string::npos);
        std::cout << "  [PASS] buildChatRequestBody JSON format" << std::endl;
    }

    // Test 6: parseChatResponse extracts content
    {
        LiteLLMBackend backend;
        std::string json = R"({"id":"x","choices":[{"index":0,"message":{"role":"assistant","content":"ext:pdf dm:last7days"},"finish_reason":"stop"}]})";
        auto content = backend.parseChatResponse(json);
        assert(content == "ext:pdf dm:last7days");
        std::cout << "  [PASS] parseChatResponse" << std::endl;
    }

    // Test 7: isAvailable returns false for unreachable service
    {
        LiteLLMBackend backend("127.0.0.1", 59999);
        assert(!backend.isAvailable());
        std::cout << "  [PASS] isAvailable false for unreachable" << std::endl;
    }

    // Test 8: JSON escaping in request body
    {
        LiteLLMBackend backend;
        std::vector<std::pair<std::string, std::string>> messages = {
            {"user", "quote \"test\" and backslash \\ end"}
        };
        auto body = backend.buildChatRequestBody(messages, 0.1f, 100);
        // Verify the special chars are escaped in the JSON
        assert(body.find("\\\"test\\\"") != std::string::npos);
        assert(body.find("\\\\") != std::string::npos);
        std::cout << "  [PASS] JSON escaping in request" << std::endl;
    }

    std::cout << "=== LiteLLMBackend Tests: ALL PASSED ===" << std::endl;
}
