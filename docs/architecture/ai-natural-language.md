# AI 自然语言搜索架构

> 结论：当前 AI 搜索是“自然语言 → MacEverything 查询语法”的翻译器，不是向量语义搜索。

## 1. 范围

本文描述 MacEverything 当前 AI / NL / “Semantic” 相关代码的真实状态。

重点覆盖：

- 搜索入口如何从 SwiftUI 路由到 C++ 引擎。
- AI 模式到底做了什么。
- `infile:` 内容搜索和 AI 搜索的边界。
- 本地 GGUF / llama.cpp / `ModelManager` 的生命周期。
- `LiteLLMBackend` 与旧 Python AI 服务的遗留状态。
- Swift `AIServiceClient` 与当前内置 HTTP API 的不一致。
- MCP 暴露能力和限制。
- 风险与后续路线图。

## 2. 一句话架构

AI 模式只把自然语言翻译为普通查询字符串，之后仍走文件名 / 路径搜索索引；内容搜索由 `infile:` 显式触发，走 trigram 内容倒排索引；当前没有 embedding / vector semantic search 路径。

证据：

- AI 开关依赖 bridge 的 translator 可用性：`MacEverything/App/SearchViewModel.swift:126-132`
- AI 输入翻译后回到 `performSearch`：`MacEverything/App/SearchViewModel.swift:373-389`
- 普通搜索最终调用 `bridge.queryResults`：`MacEverything/App/SearchViewModel.swift:337-346`
- 内容搜索调用 `bridge.queryContent`：`MacEverything/App/SearchViewModel.swift:393-399`
- 向量搜索已被删除：`docs/changelog/164-builtin-llm-refactoring.md:10-12`

## 3. 名词澄清

“AI Search” 在当前实现中等价于“NL-to-query translation”。

“Semantic” 文件名或历史文档不代表当前仍有向量语义搜索。

`MacSearchBridge+Semantic.mm` 的实际职责是调用 `NLTranslator::translate`。

证据：`MacEverything/Bridge/MacSearchBridge+Semantic.mm:7-22`

“内容搜索”是 `infile:` 前缀触发的全文关键字搜索。

它不是 embedding 检索。

它使用 trigram 倒排索引，查询时提取 keyword trigrams 并交集 posting lists。

证据：

- `ContentIndex` 注释说明 trigram inverted index：`MacEverything/Core/ContentIndex.h:28-32`
- 查询阶段提取 trigrams 并交集：`MacEverything/Core/ContentIndex.cpp:568-622`

## 4. 当前主路径

用户输入普通文本时：

1. SwiftUI `ContentView` 绑定 `searchText`。
2. 文本变化触发 `SearchViewModel.onSearchTextChanged()`。
3. 如果前缀是 `infile:`，进入内容搜索。
4. 否则如果 AI 模式开启，延迟 2 秒后执行自然语言翻译。
5. 翻译结果作为普通查询调用 `performSearch`。
6. `performSearch` 构造搜索选项并调用 bridge 的 `queryResults`。
7. C++ 搜索引擎执行文件名 / 路径查询。

关键路由证据：

- 输入框 placeholder 暴露 `infile:`：`MacEverything/App/ContentView.swift:26-29`
- `infile:` 分支优先于 AI 分支：`MacEverything/App/SearchViewModel.swift:285-305`
- AI 分支说明“NL translate → file name search”：`MacEverything/App/SearchViewModel.swift:311-323`
- 翻译后调用 `performSearch`：`MacEverything/App/SearchViewModel.swift:373-389`

## 5. 架构图

<style scoped>.ai-arch-wrapper{font-family:-apple-system,BlinkMacSystemFont,"Segoe UI",sans-serif;border:1px solid #d0d7de;border-radius:12px;padding:14px;background:#f6f8fa}.ai-arch-title{font-weight:700;font-size:16px;margin-bottom:10px;color:#24292f}.ai-arch-layer{border-radius:10px;padding:10px;margin:8px 0;border:1px solid #d8dee4;background:white}.ai-arch-layer.user{border-left:5px solid #0969da}.ai-arch-layer.app{border-left:5px solid #8250df}.ai-arch-layer.bridge{border-left:5px solid #bf8700}.ai-arch-layer.core{border-left:5px solid #1a7f37}.ai-arch-layer.data{border-left:5px solid #cf222e}.ai-arch-layer.external{border-left:5px dashed #6e7781}.ai-arch-layer-title{font-weight:700;margin-bottom:8px;color:#24292f}.ai-arch-grid{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:8px}.ai-arch-box{border:1px solid #d8dee4;border-radius:8px;padding:8px;background:#ffffff;font-size:12px;line-height:1.35}.ai-arch-box strong{display:block;font-size:13px;margin-bottom:3px}.ai-arch-box.highlight{background:#fff8c5;border-color:#d4a72c}.ai-arch-note{font-size:12px;color:#57606a;margin-top:8px}.ai-arch-flow{font-size:12px;color:#57606a;margin-top:6px}</style>
<div class="ai-arch-wrapper">
<div class="ai-arch-title">MacEverything AI / NL 搜索当前架构</div>
<div class="ai-arch-layer user">
<div class="ai-arch-layer-title">用户界面层</div>
<div class="ai-arch-grid">
<div class="ai-arch-box highlight"><strong>ContentView</strong>搜索框、AI 图标、状态栏显示 AI·NL</div>
<div class="ai-arch-box"><strong>SearchViewModel</strong>统一处理文本变化、AI 开关、内容搜索分支</div>
<div class="ai-arch-box"><strong>AISettingsView</strong>当前仍检查旧 19861 服务</div>
</div>
</div>
<div class="ai-arch-layer app">
<div class="ai-arch-layer-title">应用路由层</div>
<div class="ai-arch-grid">
<div class="ai-arch-box highlight"><strong>AI 默认路径</strong>自然语言 → 查询语法 → 普通文件搜索</div>
<div class="ai-arch-box"><strong>infile: 路径</strong>绕过 AI，直接内容搜索</div>
<div class="ai-arch-box"><strong>普通路径</strong>输入文本 → SearchOptions → queryResults</div>
</div>
</div>
<div class="ai-arch-layer bridge">
<div class="ai-arch-layer-title">Bridge / HTTP / MCP 边界</div>
<div class="ai-arch-grid">
<div class="ai-arch-box"><strong>MacSearchBridge+Semantic</strong>实际是 NLTranslator 调用入口</div>
<div class="ai-arch-box"><strong>HTTP :19860</strong>/api/ai/translate、/api/ai/status、/api/search</div>
<div class="ai-arch-box"><strong>MCP</strong>stdio JSON-RPC 代理到 :19860 的搜索 API</div>
</div>
</div>
<div class="ai-arch-layer core">
<div class="ai-arch-layer-title">核心 AI / 搜索层</div>
<div class="ai-arch-grid">
<div class="ai-arch-box highlight"><strong>NLTranslator</strong>prompt + few-shot + backend.chat()</div>
<div class="ai-arch-box"><strong>ModelManager</strong>扫描 GGUF，优先 qwen，创建 LlamaBackend</div>
<div class="ai-arch-box"><strong>LlamaBackend</strong>llama.cpp 本地推理，Metal GPU 层卸载</div>
</div>
</div>
<div class="ai-arch-layer data">
<div class="ai-arch-layer-title">索引层</div>
<div class="ai-arch-grid">
<div class="ai-arch-box"><strong>SearchEngine</strong>文件名 / 路径 / 高级过滤语法</div>
<div class="ai-arch-box"><strong>ContentIndex</strong>trigram 内容倒排索引</div>
<div class="ai-arch-box"><strong>无 VectorSearch</strong>embedding + vector 路径已移除</div>
</div>
</div>
<div class="ai-arch-layer external">
<div class="ai-arch-layer-title">遗留 / 外部表面</div>
<div class="ai-arch-grid">
<div class="ai-arch-box"><strong>LiteLLMBackend</strong>仍存在，但 ModelManager 不选择</div>
<div class="ai-arch-box"><strong>Python ai_service</strong>默认 19861，支持 Ollama / Claude</div>
<div class="ai-arch-box"><strong>Swift AIServiceClient</strong>仍指向 19861，和主路径不一致</div>
</div>
</div>
<div class="ai-arch-flow">核心流：AI 搜索 ≠ 向量语义搜索；AI 搜索 = NLTranslator 生成查询语法，然后 SearchEngine 执行普通搜索。</div>
</div>

## 6. SearchViewModel 路由细节

AI 按钮并不直接启动外部服务。

它只在 bridge 报告 translator 可用时开启 AI 模式。

证据：`MacEverything/App/SearchViewModel.swift:126-132`

文本为空时会取消当前 GUI session 查询并恢复最近文件。

证据：`MacEverything/App/SearchViewModel.swift:248-281`

`infile:` 的优先级高于 AI 模式。

只要输入以 `infile:` 开头，就设置 `isContentSearch = true`。

然后提取 `infile:` 后面的 keyword。

再以 300ms debounce 调用 `performContentSearch(keyword)`。

证据：`MacEverything/App/SearchViewModel.swift:285-305`

非 `infile:` 时，如果 AI 模式开启，SearchViewModel 会：

- 清空展示结果。
- 标记 `isAITranslating = true`。
- 等待 2 秒 debounce。
- 调用 `performAITranslatedSearch(text)`。

证据：`MacEverything/App/SearchViewModel.swift:311-323`

AI 翻译完成后：

- 从 bridge 取 `translated_query`。
- 成功则显示 `translatedQuery`。
- 成功则用翻译结果搜索。
- 失败则回退到原始输入搜索。
- 最终仍调用 `performSearch(searchQuery)`。

证据：`MacEverything/App/SearchViewModel.swift:373-389`

普通搜索 `performSearch` 会调用 `searchOptions.buildQuery(keyword)`。

然后调用 `bridge.queryResults(query, maxResults:sessionId:)`。

证据：`MacEverything/App/SearchViewModel.swift:337-346`

内容搜索 `performContentSearch` 调用 `bridge.queryContent(keyword, maxResults: 200)`。

证据：`MacEverything/App/SearchViewModel.swift:393-399`

## 7. UI 呈现语义

UI 状态栏显示的是 `AI·NL`，这比“Semantic”更接近真实实现。

证据：`MacEverything/App/ContentView.swift:110-123`

内容搜索结果区仅在 `isContentSearch` 为真时展示。

证据：`MacEverything/App/ContentView.swift:158-224`

AI 翻译中会进入独立 loading 状态。

证据：`MacEverything/App/ContentView.swift:225-230`

因此用户感知上存在三种模式：

1. 普通文件名 / 路径搜索。
2. `infile:` 内容搜索。
3. AI·NL 翻译后文件名 / 路径搜索。

## 8. 内容搜索不是向量搜索

Bridge 内容搜索先拿 `ContentIndex`。

然后调用 `contentIndex->query(key, maxResults)`。

再根据 `fileIndex` 回查文件记录。

最后并行读取文件生成 snippet。

证据：`MacEverything/Bridge/MacSearchBridge+Content.mm:8-18`

snippet 生成阶段会重新读取文件内容。

证据：`MacEverything/Bridge/MacSearchBridge+Content.mm:50-53`

`ContentIndex` 文档明确写着：

- 提取 3 字节 trigram。
- 建立 trigram → fileIndex 集合的倒排索引。
- 查询时对 keyword trigrams 做 posting list 交集。
- 最后重新读文件验证匹配。

证据：`MacEverything/Core/ContentIndex.h:28-32`

这意味着当前内容搜索是精确 / 子串风格的全文检索。

它不会理解“语义相似”。

它不会把 query 和文档映射到 embedding 空间。

它不会做 最近邻向量检索。

历史变更也确认 embedding 和 VectorSearch 已删除。

证据：`docs/changelog/164-builtin-llm-refactoring.md:24-31`

## 9. NLTranslator 行为

`NLTranslator` 会先判断输入是否已经像查询语法。

如果包含已知 filter 前缀，就直接 passthrough。

已知 filter 包含 `content:`。

证据：`MacEverything/Core/NLTranslator.cpp:14-20`

passthrough 判断发生在调用模型之前。

证据：`MacEverything/Core/NLTranslator.cpp:333-340`

如果不是查询语法，Translator 会构造 messages：

- system prompt。
- few-shot examples。
- 用户输入。

证据：`MacEverything/Core/NLTranslator.cpp:291-307`

随后调用 `backend_->chat(messages, temperature)`。

证据：`MacEverything/Core/NLTranslator.cpp:360-362`

返回文本会被清理：

- 去掉 markdown fence。
- 去掉 `Query:` / `Result:` / `Translation:` 等前缀。
- 去掉外层引号。
- 只取第一行。

证据：`MacEverything/Core/NLTranslator.cpp:63-97`

## 10. Prompt 加载顺序

`ServiceEngine` 创建 translator 后，先尝试加载用户 prompt：

`~/Library/Application Support/MacEverything/prompt.txt`

如果失败，再加载 app bundle prompt。

如果仍失败，使用内置默认 prompt。

证据：`MacEverything/Core/ServiceEngine.cpp:31-37`

bundle prompt 路径由 Objective-C++ bridge 注入。

证据：`MacEverything/Bridge/MacSearchBridge.mm:106-114`

prompt 文件格式要求 `<SYSTEM_PROMPT>`。

可选 `<FEW_SHOT>`。

证据：`MacEverything/Core/NLTranslator.cpp:195-248`

当前 bundle prompt 明确把模型定位为查询翻译器。

证据：`MacEverything/prompt.txt:1-16`

## 11. `content:` 的关键风险

当前 prompt 示例包含：

`包含import的Python文件 -> content:import ext:py`

证据：`MacEverything/prompt.txt:50-51`

但是 SearchViewModel 只有 `infile:` 会路由到 `ContentIndex`。

证据：`MacEverything/App/SearchViewModel.swift:285-305`

普通搜索路径不识别 `content:` 为内容索引路由。

高级查询里未知 filter 会 pass through。

证据：`MacEverything/Core/SearchEngineAdvancedQuery.cpp:251-252`

因此 AI 生成 `content:import ext:py` 后，很可能不会执行用户期望的全文内容搜索。

这是当前 AI prompt、SearchViewModel 路由、SearchEngine filter 语义之间最大的产品风险。

短期应避免让 AI 输出 `content:`。

中期应统一 `content:` 与 `infile:` 的路由语义。

## 12. 本地模型后端状态

当前抽象层是 `IModelBackend`。

它只定义：

- `chat`
- `isAvailable`
- `modelName`

证据：`MacEverything/Core/IModelBackend.h:7-19`

`ModelManager` 当前只扫描 `.gguf` 文件。

证据：`MacEverything/Core/ModelManager.cpp:12-34`

模型目录硬编码在用户 Application Support 下：

`~/Library/Application Support/MacEverything/models`

证据：`MacEverything/Core/ServiceEngine.cpp:20-24`

`ModelManager::loadAsync` 会：

- 后台扫描可用模型。
- 如果没有模型，则回调失败。
- 默认选择第一个模型。
- 优先选择文件名包含 `qwen` 的模型。
- 调用 `switchModel`。

证据：`MacEverything/Core/ModelManager.cpp:55-73`

`switchModel` 只创建 `LlamaBackend`。

证据：`MacEverything/Core/ModelManager.cpp:37-50`

这说明 `LiteLLMBackend` 不在当前自动选择路径上。

## 13. LlamaBackend 状态

`LlamaBackend` 使用 llama.cpp 加载 GGUF。

它调用 `llama_backend_init()`。

它将 `n_gpu_layers` 设置为 99，意图尽可能使用 Metal GPU 层卸载。

证据：`MacEverything/Core/LlamaBackend.cpp:31-38`

上下文参数为：

- `n_ctx = 2048`
- `n_batch = 2048`
- `no_perf = true`

证据：`MacEverything/Core/LlamaBackend.cpp:43-49`

`chat` 使用 mutex 串行化。

证据：`MacEverything/Core/LlamaBackend.cpp:91-96`

如果 prompt token 数超过上下文长度，会返回空字符串。

证据：`MacEverything/Core/LlamaBackend.cpp:155-162`

因此当前 AI 翻译能力受本地 GGUF 模型、prompt 长度、2048 ctx、单实例串行推理共同约束。

## 14. ServiceEngine 生命周期

App 启动时 `ServiceEngine` 同时创建：

- `SearchEngine`
- `FileSystemWatcher`
- `ContentIndex`
- `ModelManager`

证据：`MacEverything/Core/ServiceEngine.cpp:17-24`

模型异步加载成功后，才会创建 `NLTranslator`。

证据：`MacEverything/Core/ServiceEngine.cpp:26-39`

析构时会等待模型加载完成后再 shutdown。

证据：`MacEverything/Core/ServiceEngine.cpp:50-53`

所以 AI 可用性不是进程启动即刻可用。

它取决于模型加载是否完成。

这也解释了 AI 按钮为什么需要通过 `bridge.isAIAvailable()` 判断。

## 15. LiteLLM 与 Python AI 服务状态

`LiteLLMBackend` 仍存在。

它默认连接 `127.0.0.1:19861`。

证据：`MacEverything/Core/LiteLLMBackend.h:7-19`

它仍保留 legacy embedding API 注释。

证据：`MacEverything/Core/LiteLLMBackend.h:18-19`

它的 chat 请求走 `/v1/chat/completions`。

证据：`MacEverything/Core/LiteLLMBackend.cpp:147-158`

它的 embedding 请求走 `/v1/embeddings`。

证据：`MacEverything/Core/LiteLLMBackend.cpp:161-171`

但 `ModelManager` 当前不会实例化 `LiteLLMBackend`。

证据：`MacEverything/Core/ModelManager.cpp:37-50`

Python `ai_service` 仍默认监听 19861。

证据：`ai_service/src/maceverything_ai/config.py:4-13`

该服务支持 Ollama 与 Claude 后端。

证据：`ai_service/src/maceverything_ai/llm_backend.py:16-116`

该 legacy 服务面与当前内置 19860 AI 端点并不一致，架构文档不应把它当作当前主路径。

证据：`ai_service/src/maceverything_ai/server.py:62-114`

这部分应视为遗留 / 可选路径，而不是当前 app 内搜索的主路径。

历史决策也写明“不再依赖 Ollama 和 LiteLLM”。

证据：`docs/changelog/164-builtin-llm-refactoring.md:59-63`

## 16. 内置 HTTP AI API

当前 app 内置 HTTP 端口为 19860。

证据：`MacEverything/Bridge/MacSearchBridge.mm:106-110`

HTTP 路由包含：

- `GET /api/ai/status`
- `GET /api/ai/prompt`
- `POST /api/ai/translate`
- `POST /api/ai/prompt`

证据：`MacEverything/Core/HttpServer.cpp:320-348`

`/api/ai/status` 返回字段为：

- `status`
- `model_ready`
- `model_name`
- `translator_available`

证据：`MacEverything/Core/HttpServer.cpp:718-745`

这组字段和旧 Python AI service 的 status schema 不同。

## 17. Swift AIServiceClient 不一致

`AIServiceClient` 默认端口是 19861。

证据：`MacEverything/App/AIServiceClient.swift:34-48`

`AIStatusResponse` 期望字段是：

- `status`
- `ai_available`
- `model`

证据：`MacEverything/App/AIServiceClient.swift:23-31`

但内置 app 的 `/api/ai/status` 返回的是：

- `model_ready`
- `model_name`
- `translator_available`

证据：`MacEverything/Core/HttpServer.cpp:739-744`

`AISettingsView` 仍通过 `AIServiceClient.shared` 检查 AI 状态。

证据：`MacEverything/App/AISettingsView.swift:60-77`

而真实 AI 搜索开关使用的是 bridge 的 `isAIAvailable()`。

证据：`MacEverything/App/SearchViewModel.swift:126-132`

因此当前存在“双 AI 服务面”问题：

- 搜索实际走本地 bridge / `ServiceEngine` / llama.cpp。
- 设置页仍看旧 19861 Python 服务。
- 两者状态可能互相矛盾。

## 18. MCP 能力边界

MCP server 是独立 CLI 可执行文件。

它通过 stdio 接收 JSON-RPC 2.0。

然后代理到正在运行的 MacEverything app HTTP API。

证据：`MacEverything/CLI/mcp_main.cpp:1-7`

默认代理目标是 `127.0.0.1:19860`。

证据：`MacEverything/CLI/mcp_main.cpp:24-30`

MCP 暴露的 tools 只有：

- `search_files`
- `search_content`
- `recent_files`
- `index_status`

证据：`MacEverything/CLI/mcp_main.cpp:314-320`

`search_files` 代理 `GET /api/search`。

`search_content` 代理 `GET /api/search/content`。

`recent_files` 代理 `GET /api/recent`。

`index_status` 代理 `GET /api/status`。

证据：`MacEverything/CLI/mcp_main.cpp:327-367`

MCP 当前没有暴露：

- `translate_query`
- `natural_language_search`
- `/api/ai/status`
- `/api/ai/prompt`
- `/api/ai/translate`

证据：`MacEverything/CLI/mcp_main.cpp:411-443`

MCP batch request 当前不支持。

代码注释甚至写到 spec 期望支持 batch，但当前实现选择忽略。

证据：`MacEverything/CLI/mcp_main.cpp:505-510`

MCP 也要求 MacEverything app 或 daemon 已经在本机运行。

连接失败会返回 “Cannot connect to MacEverything”。

证据：`MacEverything/CLI/mcp_main.cpp:229-233`

历史文档也说明前提是 app 必须运行。

证据：`docs/changelog/074-mcp-server.md:63-64`

## 19. MCP 配置管理

App 内的 `MCPConfigManager` 支持三个客户端：

- Claude Code
- Cursor
- Claude Desktop

证据：`MacEverything/App/MCPConfigManager.swift:3-27`

启用时写入 `mcpServers.maceverything.command`。

command 指向 app bundle 内的 `MacEverythingMCP`。

证据：`MacEverything/App/MCPConfigManager.swift:30-35`

写入 args 为空数组。

证据：`MacEverything/App/MCPConfigManager.swift:53-63`

因此 MCP 默认只能连接 19860。

如果用户 app HTTP 端口变化，当前菜单配置不会自动写入 `--port`。

## 20. 当前风险

### R1：命名误导

`MacSearchBridge+Semantic.mm` 文件名仍叫 Semantic。

但实现是 AI / NL 翻译 bridge。

证据：`MacEverything/Bridge/MacSearchBridge+Semantic.mm:5-22`

这容易让维护者误以为仍存在语义向量搜索。

建议改名或补充文件头注释。

### R2：旧文档可能过期

历史 changelog `156-ai-search-routing` 曾描述 AI + `infile:` 为向量搜索。

当前代码已不支持该路径。

以当前代码为准，AI 与 `infile:` 是互斥路由。

证据：`MacEverything/App/SearchViewModel.swift:285-323`

### R3：`content:` prompt 与实际路由不一致

Prompt 诱导 AI 输出 `content:`。

但实际内容索引用 `infile:` 触发。

证据：

- Prompt 示例：`MacEverything/prompt.txt:50-51`
- `infile:` 路由：`MacEverything/App/SearchViewModel.swift:285-305`
- 未知 filter pass-through：`MacEverything/Core/SearchEngineAdvancedQuery.cpp:251-252`

### R4：AI 设置页状态不可信

设置页看 19861。

实际搜索看 bridge translator。

这会导致“设置页显示不可用，但搜索 AI 可用”或反向情况。

证据：

- 设置页：`MacEverything/App/AISettingsView.swift:60-77`
- 旧客户端端口：`MacEverything/App/AIServiceClient.swift:41-48`
- 新 HTTP 状态字段：`MacEverything/Core/HttpServer.cpp:718-745`

### R5：模型缺失时 AI 完全不可用

`ModelManager` 如果找不到 `.gguf`，直接回调失败。

证据：`MacEverything/Core/ModelManager.cpp:55-61`

AI 可用性取决于 `NLTranslator` 是否创建。

证据：`MacEverything/Bridge/MacSearchBridge+Semantic.mm:46-50`

### R6：MCP 不能使用 AI 翻译

MCP tools 没有自然语言翻译工具。

证据：`MacEverything/CLI/mcp_main.cpp:314-320`

LLM 客户端通过 MCP 只能自己生成 MacEverything query，或调用基础搜索。

这和 App UI 的 AI·NL 能力不对齐。

## 21. 路线图

### P0：文档与命名先止血

把所有“Semantic Search”描述改为“AI·NL Query Translation”。

在 `MacSearchBridge+Semantic.mm` 增加文件头说明：

“历史文件名，当前仅负责自然语言查询翻译。”

同步清理过期 changelog / README 中的向量语义表述。

### P0：修复 Swift AI 状态面

让 `AIServiceClient` 改为默认连接 19860。

把 `AIStatusResponse` 改为匹配 `model_ready`、`model_name`、`translator_available`。

或删除设置页对旧 Python 服务的依赖，直接通过 bridge 查询状态。

目标：搜索页和设置页对 AI 可用性的判断必须一致。

### P0：修正 prompt 中的 `content:` 示例

短期删除 `content:import ext:py` 示例。

或者让 prompt 对内容搜索输出明确的 `infile:` 形式。

但注意：当前 `infile:` 是 SwiftViewModel 的 UI 前缀，不是 SearchEngine 统一查询语法。

如果让 AI 输出 `infile:`，需要保证翻译结果回到 `onSearchTextChanged` 路由，而不是直接进入 `performSearch`。

更稳妥的短期方案是避免 AI 生成内容搜索查询。

### P1：统一内容搜索语法

选择一种正式语义：

方案 A：`infile:` 是 UI 命令，AI 不输出内容搜索。

方案 B：`content:` 成为 SearchEngine 级 filter，并真正调用 `ContentIndex`。

方案 C：SearchViewModel 识别 AI 翻译结果中的 `content:`，转发到内容搜索。

推荐 B。

理由：`content:` 已在 prompt 和 `NLTranslator` known filters 中存在。

证据：`MacEverything/Core/NLTranslator.cpp:14-20`

### P1：补齐 MCP AI 能力

新增 MCP tool：

- `translate_query`
- 或 `search_natural_language`

`translate_query` 只返回翻译结果，便于 Agent 决定是否调用 search。

`search_natural_language` 则调用 `/api/ai/translate` 后再调用 `/api/search`。

但必须明确命名为 NL，不要叫 semantic。

还应增加 `ai_status` tool，对齐 `/api/ai/status`。

### P1：MCP 支持端口配置

`MCPConfigManager` 写入 args 时可包含 `--port 19860`。

如果未来 app HTTP 端口可配置，菜单应同步写入实际端口。

当前 MCP binary 已支持 `--port`。

证据：`MacEverything/CLI/mcp_main.cpp:454-481`

### P2：清理遗留 Python AI service

如果产品方向确认本地内置模型为唯一主路径：

- 删除或归档 `ai_service`。
- 删除 Swift 中对 19861 的依赖。
- 删除 `LiteLLMBackend` embedding API。
- 保留远程模型能力时，应该通过 `ModelManager` 显式配置，而不是隐式遗留代码。

### P2：如需真正语义搜索，作为新功能重建

真正 semantic search 应包含：

- embedding backend 抽象。
- 文档分块策略。
- embedding 持久化。
- ANN / vector index。
- 增量更新与删除。
- 与当前 `ContentIndex` 的关系定义。
- UI 明确区分“内容关键字搜索”和“语义相似搜索”。

不应复用当前 AI·NL 名称混淆用户。

## 22. 验收口径

判断 AI·NL 功能是否正常时，应验证：

1. `bridge.isAIAvailable()` 为真。
2. `/api/ai/status` 中 `translator_available` 为真。
3. 普通自然语言输入能翻译为 MacEverything 查询语法。
4. 翻译结果被传入普通 `performSearch`。
5. `infile:` 输入不会触发 AI 翻译。
6. `infile:` 内容搜索通过 `ContentIndex` 返回 snippet。
7. AI Settings 不再检查 19861，或明确标记为 legacy。
8. MCP 搜索工具仅验证基础搜索，不应宣称 AI 语义搜索。

## 23. 推荐对外表述

推荐：

“AI 搜索：把自然语言转换为 MacEverything 查询语法。”

推荐：

“内容搜索：使用 `infile:` 在已索引文本文件中做关键字检索。”

推荐：

“当前没有向量语义搜索；如果未来加入，会作为独立能力说明。”

避免：

“AI 语义搜索”

避免：

“Semantic search”

避免：

“infile 使用向量搜索”

避免：

“content: 一定会搜索文件内容”

## 24. 关键证据索引

SearchViewModel AI 开关：`MacEverything/App/SearchViewModel.swift:126-132`

SearchViewModel `infile:` 分支：`MacEverything/App/SearchViewModel.swift:285-305`

SearchViewModel AI 分支：`MacEverything/App/SearchViewModel.swift:311-323`

SearchViewModel 普通搜索：`MacEverything/App/SearchViewModel.swift:337-346`

SearchViewModel AI 翻译回普通搜索：`MacEverything/App/SearchViewModel.swift:373-389`

SearchViewModel 内容搜索：`MacEverything/App/SearchViewModel.swift:393-399`

ContentView AI·NL 状态：`MacEverything/App/ContentView.swift:110-123`

ContentIndex trigram 设计：`MacEverything/Core/ContentIndex.h:28-32`

ContentIndex 查询实现：`MacEverything/Core/ContentIndex.cpp:568-622`

Bridge AI 翻译入口：`MacEverything/Bridge/MacSearchBridge+Semantic.mm:7-22`

NLTranslator known filters：`MacEverything/Core/NLTranslator.cpp:14-20`

NLTranslator messages 构造：`MacEverything/Core/NLTranslator.cpp:291-307`

NLTranslator backend chat：`MacEverything/Core/NLTranslator.cpp:360-362`

ServiceEngine 模型目录与加载：`MacEverything/Core/ServiceEngine.cpp:20-39`

ModelManager GGUF 与 qwen 优先：`MacEverything/Core/ModelManager.cpp:12-34`

ModelManager 只创建 LlamaBackend：`MacEverything/Core/ModelManager.cpp:37-50`

LlamaBackend GGUF 加载：`MacEverything/Core/LlamaBackend.cpp:31-49`

LiteLLMBackend 遗留 embedding：`MacEverything/Core/LiteLLMBackend.h:18-19`

HTTP AI 路由：`MacEverything/Core/HttpServer.cpp:320-348`

HTTP AI status schema：`MacEverything/Core/HttpServer.cpp:718-745`

Swift AIServiceClient 旧端口：`MacEverything/App/AIServiceClient.swift:41-48`

Swift AISettingsView 旧状态检查：`MacEverything/App/AISettingsView.swift:60-77`

MCP 工具定义：`MacEverything/CLI/mcp_main.cpp:314-320`

MCP HTTP 代理 handlers：`MacEverything/CLI/mcp_main.cpp:327-367`

MCP batch 限制：`MacEverything/CLI/mcp_main.cpp:505-510`

向量搜索移除决策：`docs/changelog/164-builtin-llm-refactoring.md:10-12`