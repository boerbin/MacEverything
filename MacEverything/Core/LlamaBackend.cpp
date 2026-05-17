#include "LlamaBackend.h"
#include "llama.h"
#include "Logger.h"
#include <filesystem>
#include <iostream>
#include <cstring>

// ── Lifecycle ──

LlamaBackend::~LlamaBackend() {
    unloadModel();
}

bool LlamaBackend::loadModel(const std::string& ggufPath) {
    std::lock_guard<std::mutex> lock(mutex_);

    // Already loaded?
    if (model_) {
        unloadModel();
    }

    if (!std::filesystem::exists(ggufPath)) {
        return false;
    }

    llama_backend_init();

    // Model params — offload all layers to Metal GPU
    auto mparams = llama_model_default_params();
    mparams.n_gpu_layers = 99;

    model_ = llama_model_load_from_file(ggufPath.c_str(), mparams);
    if (!model_) {
        return false;
    }

    // Context params
    auto cparams = llama_context_default_params();
    cparams.n_ctx = 2048;
    cparams.n_batch = 2048;
    cparams.no_perf = true;

    ctx_ = llama_init_from_model(model_, cparams);
    if (!ctx_) {
        llama_model_free(model_);
        model_ = nullptr;
        return false;
    }

    modelPath_ = ggufPath;

    // Extract model name from filename
    auto filename = std::filesystem::path(ggufPath).stem().string();
    modelName_ = "local:" + filename;

    return true;
}

void LlamaBackend::unloadModel() {
    // Note: caller must hold mutex_ OR be in destructor
    if (ctx_) {
        llama_free(ctx_);
        ctx_ = nullptr;
    }
    if (model_) {
        llama_model_free(model_);
        model_ = nullptr;
    }
    modelPath_.clear();
    modelName_.clear();
}

// ── IModelBackend ──

bool LlamaBackend::isAvailable() {
    std::lock_guard<std::mutex> lock(mutex_);
    return model_ != nullptr && ctx_ != nullptr;
}

std::string LlamaBackend::modelName() {
    std::lock_guard<std::mutex> lock(mutex_);
    return modelName_.empty() ? "local:none" : modelName_;
}

std::string LlamaBackend::chat(
    const std::vector<std::pair<std::string, std::string>>& messages,
    float temperature, int maxTokens)
{
    std::lock_guard<std::mutex> lock(mutex_);

    if (!model_ || !ctx_) {
        return "";
    }

    const llama_vocab* vocab = llama_model_get_vocab(model_);

    // 1. Build llama_chat_message array
    std::vector<llama_chat_message> chatMessages;
    chatMessages.reserve(messages.size());
    for (const auto& [role, content] : messages) {
        chatMessages.push_back({role.c_str(), content.c_str()});
    }

    // 2. Apply chat template
    const char* tmpl = llama_model_chat_template(model_, nullptr);

    // First call to get required buffer size
    int32_t needed = llama_chat_apply_template(
        tmpl, chatMessages.data(), chatMessages.size(),
        true, nullptr, 0);

    if (needed <= 0) {
        return "";
    }

    std::vector<char> buf(static_cast<size_t>(needed) + 1, 0);
    int32_t written = llama_chat_apply_template(
        tmpl, chatMessages.data(), chatMessages.size(),
        true, buf.data(), static_cast<int32_t>(buf.size()));

    if (written <= 0) {
        return "";
    }

    std::string prompt(buf.data(), static_cast<size_t>(written));

    // 3. Tokenize the prompt
    int32_t nPromptMax = static_cast<int32_t>(prompt.size()) + 256;
    std::vector<llama_token> tokens(nPromptMax);

    int32_t nTokens = llama_tokenize(
        vocab, prompt.c_str(), static_cast<int32_t>(prompt.size()),
        tokens.data(), nPromptMax,
        /*add_special=*/false, /*parse_special=*/true);

    if (nTokens < 0) {
        // Buffer too small, resize and retry
        tokens.resize(static_cast<size_t>(-nTokens));
        nTokens = llama_tokenize(
            vocab, prompt.c_str(), static_cast<int32_t>(prompt.size()),
            tokens.data(), static_cast<int32_t>(tokens.size()),
            false, true);
        if (nTokens < 0) {
            return "";
        }
    }
    tokens.resize(static_cast<size_t>(nTokens));

    LOG_INFO("LlamaBackend", "chat: nTokens=" << nTokens
             << " n_ctx=" << llama_n_ctx(ctx_)
             << " n_batch=" << llama_n_batch(ctx_));

    uint32_t n_ctx = llama_n_ctx(ctx_);
    if (static_cast<uint32_t>(nTokens) > n_ctx) {
        LOG_ERROR("LlamaBackend", "chat: prompt too long! nTokens=" << nTokens << " > n_ctx=" << n_ctx);
        return "";
    }

    // 4. Clear KV cache
    llama_memory_t mem = llama_get_memory(ctx_);
    llama_memory_clear(mem, true);

    // 5. Decode prompt tokens
    llama_batch batch = llama_batch_get_one(tokens.data(), static_cast<int32_t>(tokens.size()));
    if (llama_decode(ctx_, batch) != 0) {
        return "";
    }

    // 6. Set up sampler
    auto sparams = llama_sampler_chain_default_params();
    sparams.no_perf = true;
    llama_sampler* smpl = llama_sampler_chain_init(sparams);

    if (temperature <= 0.0f) {
        llama_sampler_chain_add(smpl, llama_sampler_init_greedy());
    } else {
        llama_sampler_chain_add(smpl, llama_sampler_init_temp(temperature));
        llama_sampler_chain_add(smpl, llama_sampler_init_dist(LLAMA_DEFAULT_SEED));
    }

    // 7. Generate tokens
    std::string result;
    result.reserve(512);

    for (int i = 0; i < maxTokens; i++) {
        llama_token newToken = llama_sampler_sample(smpl, ctx_, -1);

        // Check end of generation
        if (llama_vocab_is_eog(vocab, newToken)) {
            break;
        }

        // Convert token to text
        char piece[128];
        int32_t nPiece = llama_token_to_piece(
            vocab, newToken, piece, sizeof(piece), 0, true);

        if (nPiece > 0) {
            result.append(piece, static_cast<size_t>(nPiece));
        }

        // Prepare next decode
        llama_batch nextBatch = llama_batch_get_one(&newToken, 1);
        if (llama_decode(ctx_, nextBatch) != 0) {
            break;
        }
    }

    llama_sampler_free(smpl);
    return result;
}
