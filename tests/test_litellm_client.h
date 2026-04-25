#pragma once
#include "LiteLLMClient.h"
#include <cassert>
#include <iostream>
#include <cmath>

inline void runLiteLLMClientTests() {
    std::cout << "=== LiteLLMClient Tests ===" << std::endl;

    // Test 1: Default construction
    {
        LiteLLMClient client;
        assert(client.getHost() == "127.0.0.1");
        assert(client.getPort() == 19861);
        std::cout << "  [PASS] Default construction" << std::endl;
    }

    // Test 2: Custom host/port
    {
        LiteLLMClient client("localhost", 9999);
        assert(client.getHost() == "localhost");
        assert(client.getPort() == 9999);
        std::cout << "  [PASS] Custom host/port" << std::endl;
    }

    // Test 3: buildChatRequestBody builds correct JSON
    {
        LiteLLMClient client;
        std::vector<std::pair<std::string, std::string>> messages = {
            {"system", "You are a translator."},
            {"user", "hello world"}
        };
        auto body = client.buildChatRequestBody("translate", messages, 0.1f, 200);
        assert(body.find("\"model\"") != std::string::npos);
        assert(body.find("translate") != std::string::npos);
        assert(body.find("\"messages\"") != std::string::npos);
        assert(body.find("system") != std::string::npos);
        assert(body.find("hello world") != std::string::npos);
        assert(body.find("\"temperature\"") != std::string::npos);
        assert(body.find("\"max_tokens\"") != std::string::npos);
        std::cout << "  [PASS] buildChatRequestBody JSON format" << std::endl;
    }

    // Test 4: buildEmbedRequestBody builds correct JSON
    {
        LiteLLMClient client;
        auto body = client.buildEmbedRequestBody("embed", "some text to embed");
        assert(body.find("\"model\"") != std::string::npos);
        assert(body.find("embed") != std::string::npos);
        assert(body.find("\"input\"") != std::string::npos);
        assert(body.find("some text to embed") != std::string::npos);
        std::cout << "  [PASS] buildEmbedRequestBody JSON format" << std::endl;
    }

    // Test 5: parseChatResponse extracts content
    {
        LiteLLMClient client;
        std::string json = R"({"id":"x","choices":[{"index":0,"message":{"role":"assistant","content":"ext:pdf dm:last7days"},"finish_reason":"stop"}]})";
        auto content = client.parseChatResponse(json);
        assert(content == "ext:pdf dm:last7days");
        std::cout << "  [PASS] parseChatResponse" << std::endl;
    }

    // Test 6: parseChatResponse handles whitespace/newlines
    {
        LiteLLMClient client;
        std::string json = R"({"choices":[{"message":{"content":"  path:Downloads ext:pdf  \n"}}]})";
        auto content = client.parseChatResponse(json);
        assert(content == "path:Downloads ext:pdf");
        std::cout << "  [PASS] parseChatResponse trims whitespace" << std::endl;
    }

    // Test 7: parseEmbedResponse extracts vector
    {
        LiteLLMClient client;
        std::string json = R"({"data":[{"embedding":[0.1,0.2,0.3,0.456]}],"model":"embed"})";
        auto vec = client.parseEmbedResponse(json);
        assert(vec.size() == 4);
        assert(std::abs(vec[0] - 0.1f) < 0.001f);
        assert(std::abs(vec[1] - 0.2f) < 0.001f);
        assert(std::abs(vec[2] - 0.3f) < 0.001f);
        assert(std::abs(vec[3] - 0.456f) < 0.001f);
        std::cout << "  [PASS] parseEmbedResponse" << std::endl;
    }

    // Test 8: parseEmbedResponse handles negative values
    {
        LiteLLMClient client;
        std::string json = R"({"data":[{"embedding":[-0.5,0.0,1.0]}]})";
        auto vec = client.parseEmbedResponse(json);
        assert(vec.size() == 3);
        assert(std::abs(vec[0] - (-0.5f)) < 0.001f);
        assert(std::abs(vec[1] - 0.0f) < 0.001f);
        std::cout << "  [PASS] parseEmbedResponse negative values" << std::endl;
    }

    // Test 9: isAvailable returns false for unreachable service
    {
        LiteLLMClient client("127.0.0.1", 59999);
        assert(!client.isAvailable());
        std::cout << "  [PASS] isAvailable false for unreachable" << std::endl;
    }

    // Test 10: JSON escaping in request body
    {
        LiteLLMClient client;
        std::vector<std::pair<std::string, std::string>> messages = {
            {"user", "quote \"test\" and backslash \\ end"}
        };
        auto body = client.buildChatRequestBody("m", messages, 0.1f, 100);
        // Verify the special chars are escaped in the JSON
        assert(body.find("\\\"test\\\"") != std::string::npos);
        assert(body.find("\\\\") != std::string::npos);
        std::cout << "  [PASS] JSON escaping in request" << std::endl;
    }

    std::cout << "=== LiteLLMClient Tests: ALL PASSED ===" << std::endl;
}
