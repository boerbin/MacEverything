# 164 — 内置 LLM 替代外部依赖

## 背景

MacEverything 的 AI 搜索功能此前依赖外部进程链（Ollama + LiteLLM proxy）来运行语言模型。用户需要安装 Ollama、拉取模型、启动 LiteLLM 才能使用 AI 搜索功能。实际使用中发现：
1. 用户只使用简单的自然语言搜索（NL→搜索语法转换），不使用语义向量搜索
2. 外部模型可用性（进程未启动等）严重影响功能可用率

## 决策

- 砍掉语义向量搜索（embedding + VectorSearch）
- 内置 llama.cpp 推理引擎 + Qwen2.5-0.5B-Instruct 模型
- 引入 IModelBackend 抽象接口，支持未来模型替换

## 架构变更

### 新增
- `IModelBackend` — 模型提供者抽象接口（chat, isAvailable, modelName）
- `LlamaBackend` — llama.cpp 封装，加载 GGUF 模型进行本地推理，支持 Metal GPU 加速
- `ModelManager` — 模型发现、异步加载、切换管理
- `LiteLLMBackend` — 原 LiteLLMClient 重命名，作为可选远程后端保留
- `vendor/llama.cpp` — git submodule，编译为静态库链接

### 删除
- `EmbeddingIndex` — SQLite embedding 存储
- `VectorSearch` — 内存向量暴力搜索
- `ServiceEngine+Semantic.cpp` — 语义索引逻辑
- `SemanticSettingsView.swift` — 语义设置 UI
- `AISetupHelper.swift` / `AISetupView.swift` — Ollama 安装引导
- `litellm/config.yaml` — LiteLLM 配置
- HTTP 端点：`/api/search/semantic`、`/api/search/similar`、`/api/semantic/rebuild`

### 重构
- `NLTranslator` — 依赖从 LiteLLMClient 改为 IModelBackend
- `ServiceEngine` — 移除语义相关成员，增加 ModelManager 生命周期管理
- `HttpServer` — 简化 AI 端点，新增 `/api/ai/status` 报告模型状态
- Swift UI — 移除向量搜索 UI，简化 AI 设置

## 模型

- 默认模型：Qwen2.5-0.5B-Instruct Q4_K_M（~469MB）
- 存放路径：`~/Library/Application Support/MacEverything/models/`
- 用户可自行放入其他 GGUF 模型文件进行替换

## 启动时序

App 启动后：
- 普通搜索立即可用
- 模型在后台异步加载（3-5 秒）
- AI 按钮在模型加载完成后变为可用

## 测试

- 新增 LlamaBackend 真实模型测试（中英文 NL 翻译）
- 新增 ModelManager 单元测试
- 全量测试通过：12025 tests, 0 failures
- 端到端验证：HTTP API 翻译功能正常

## Breaking Changes

- 移除了语义搜索相关 HTTP API 端点
- 不再依赖 Ollama 和 LiteLLM
- App 体积增加约 469MB（GGUF 模型文件）
