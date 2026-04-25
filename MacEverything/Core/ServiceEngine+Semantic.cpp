// ServiceEngine+Semantic.cpp — Semantic indexing (embeddings + vector search)
#include "ServiceEngine.h"
#include "Logger.h"
#include <fstream>
#include <filesystem>
#include <dispatch/dispatch.h>

namespace fs = std::filesystem;

// ═══════════════════════════════════════════════════════
//  Thread-safe accessors
// ═══════════════════════════════════════════════════════

std::shared_ptr<EmbeddingIndex> ServiceEngine::safeEmbeddingIndex() {
    std::shared_lock lock(semanticMutex_);
    return embeddingIndex_;
}

std::shared_ptr<VectorSearch> ServiceEngine::safeVectorSearch() {
    std::shared_lock lock(semanticMutex_);
    return vectorSearch_;
}

std::shared_ptr<LiteLLMClient> ServiceEngine::safeLiteLLMClient() {
    std::shared_lock lock(semanticMutex_);
    return litellmClient_;
}

std::shared_ptr<NLTranslator> ServiceEngine::safeNLTranslator() {
    std::shared_lock lock(semanticMutex_);
    return nlTranslator_;
}

// ═══════════════════════════════════════════════════════
//  Full semantic indexing (background, sequential API calls)
// ═══════════════════════════════════════════════════════

void ServiceEngine::startSemanticIndexing() {
    auto engine = safeEngine();
    auto embIdx = safeEmbeddingIndex();
    auto vecSearch = safeVectorSearch();
    auto llmClient = safeLiteLLMClient();
    if (!engine || !embIdx || !llmClient) return;

    // Check if LiteLLM is available
    if (!llmClient->isAvailable()) {
        LOG_INFO("ServiceEngine", "Semantic indexing skipped: LiteLLM not available");
        return;
    }

    cancelSemanticIndexing_.store(false, std::memory_order_relaxed);
    uint64_t myGeneration = semanticIndexGeneration_.load(std::memory_order_acquire);

    LOG_INFO("ServiceEngine", "Semantic indexing started");

    dispatch_group_async(backgroundGroup_, dispatch_get_global_queue(QOS_CLASS_UTILITY, 0), ^{
        // Use ContentIndex config for extensions and file size
        auto contentIdx = this->safeContentIndex();
        if (!contentIdx) {
            LOG_INFO("ServiceEngine", "Semantic indexing: ContentIndex not available");
            return;
        }
        auto extensions = contentIdx->getExtensions();
        if (extensions.empty()) {
            LOG_INFO("ServiceEngine", "Semantic indexing: no extensions configured");
            return;
        }

        uint64_t maxSize = contentIdx->getMaxFileSize();

        // Collect eligible files
        struct FileEntry {
            uint32_t idx;
            std::string fullPath;
            time_t modTime;
        };
        std::vector<FileEntry> entries;

        uint32_t totalRecords = engine->recordCount();
        std::vector<uint32_t> allIndices;
        allIndices.reserve(totalRecords);
        for (uint32_t i = 0; i < totalRecords; i++) allIndices.push_back(i);

        engine->forEachRecordWithPath(allIndices, [&](uint32_t idx, const FileRecord& r, const std::string& path) {
            if (this->cancelSemanticIndexing_.load(std::memory_order_relaxed)) return;
            if (this->semanticIndexGeneration_.load(std::memory_order_acquire) != myGeneration) return;

            if (r.type != 1) return;  // files only
            if (r.size == 0 || r.size > maxSize) return;

            // Check extension
            auto dotPos = r.name.rfind('.');
            if (dotPos == std::string::npos) return;
            std::string ext = r.name.substr(dotPos + 1);
            for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));

            bool extMatch = false;
            for (const auto& e : extensions) {
                if (e == ext) { extMatch = true; break; }
            }
            if (!extMatch) return;

            std::string fullPath = SearchEngine::makeFullPath(path, r.name);
            entries.push_back({idx, fullPath, r.modTime});
        });

        uint64_t total = entries.size();
        LOG_INFO("ServiceEngine", "Semantic indexing: " << total << " eligible files");

        std::atomic<uint64_t> indexed{0};

        // Index sequentially (embedding API calls are the bottleneck, not CPU)
        for (size_t i = 0; i < entries.size(); i++) {
            if (this->shuttingDown_.load(std::memory_order_relaxed)) return;
            if (this->cancelSemanticIndexing_.load(std::memory_order_relaxed)) return;
            if (this->semanticIndexGeneration_.load(std::memory_order_acquire) != myGeneration) return;

            auto& entry = entries[i];

            // Read file content
            std::ifstream file(entry.fullPath);
            if (!file.is_open()) continue;
            std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
            if (content.empty()) continue;

            // Truncate to max 4KB for embedding (bge-m3 has 8192 token limit, ~4KB is safe)
            if (content.size() > 4096) content.resize(4096);

            // Hash content for change detection (FNV-1a)
            uint64_t hash = 0xcbf29ce484222325ULL;
            for (char c : content) {
                hash ^= static_cast<uint8_t>(c);
                hash *= 0x100000001b3ULL;
            }

            // Skip if content hasn't changed
            if (!embIdx->needsUpdate(entry.fullPath, hash)) continue;

            // Get embedding from LiteLLM
            try {
                auto vec = llmClient->embed("embed", content);
                if (vec.empty()) continue;

                embIdx->storeEmbedding(entry.fullPath, vec, hash, entry.modTime);

                // Also add to in-memory vector search
                if (vecSearch) {
                    vecSearch->addVector(entry.idx, vec);
                }
            } catch (...) {
                // LiteLLM unavailable or error — skip this file
                continue;
            }

            uint64_t cur = indexed.fetch_add(1, std::memory_order_relaxed) + 1;
            if (cur % 100 == 0 && this->onSemanticIndexProgress) {
                this->onSemanticIndexProgress(cur, total);
            }
        }

        uint64_t finalIndexed = indexed.load(std::memory_order_relaxed);
        LOG_INFO("ServiceEngine", "Semantic indexing completed: " << finalIndexed << " files embedded");

        if (this->onSemanticIndexComplete) {
            this->onSemanticIndexComplete(static_cast<uint32_t>(finalIndexed));
        }
    });
}

// ═══════════════════════════════════════════════════════
//  Per-file semantic update (called from FSEvents path)
// ═══════════════════════════════════════════════════════

void ServiceEngine::updateSemanticForPath(const std::string& path, bool isRemove, std::shared_ptr<SearchEngine> engine) {
    auto embIdx = safeEmbeddingIndex();
    auto vecSearch = safeVectorSearch();
    auto llmClient = safeLiteLLMClient();
    if (!embIdx || !llmClient) return;
    if (!llmClient->isAvailable()) return;

    if (isRemove) {
        embIdx->removeEmbedding(path);
        // VectorSearch removeVector needs fileIndex; handled on next rebuild
        return;
    }

    // Check extension eligibility (follows ContentIndex config)
    auto contentIdx = safeContentIndex();
    if (!contentIdx) return;
    auto extensions = contentIdx->getExtensions();
    if (extensions.empty()) return;

    auto dotPos = path.rfind('.');
    if (dotPos == std::string::npos) return;
    std::string ext = path.substr(dotPos + 1);
    for (auto& c : ext) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    bool extMatch = false;
    for (const auto& e : extensions) {
        if (e == ext) { extMatch = true; break; }
    }
    if (!extMatch) return;

    // Read and embed
    std::ifstream file(path);
    if (!file.is_open()) return;
    std::string content((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (content.empty() || content.size() > contentIdx->getMaxFileSize()) return;
    if (content.size() > 4096) content.resize(4096);

    // FNV-1a hash
    uint64_t hash = 0xcbf29ce484222325ULL;
    for (char c : content) {
        hash ^= static_cast<uint8_t>(c);
        hash *= 0x100000001b3ULL;
    }

    if (!embIdx->needsUpdate(path, hash)) return;

    try {
        auto vec = llmClient->embed("embed", content);
        if (!vec.empty()) {
            embIdx->storeEmbedding(path, vec, hash, std::time(nullptr));
            if (vecSearch && engine) {
                uint32_t fileIndex = engine->indexForPath(path);
                if (fileIndex != UINT32_MAX) {
                    vecSearch->addVector(fileIndex, vec);
                }
            }
        }
    } catch (...) {
        // LiteLLM unavailable — graceful degradation
    }
}

// ═══════════════════════════════════════════════════════
//  Rebuild semantic index (cancel in-flight, clear, re-index)
// ═══════════════════════════════════════════════════════

void ServiceEngine::rebuildSemanticIndex() {
    cancelSemanticIndexing_.store(true, std::memory_order_relaxed);
    semanticIndexGeneration_.fetch_add(1, std::memory_order_acq_rel);

    {
        std::unique_lock lock(semanticMutex_);
        embeddingIndex_ = std::make_shared<EmbeddingIndex>();
        vectorSearch_ = std::make_shared<VectorSearch>(1024);  // bge-m3 dimension
    }

    // Re-setup and restart
    auto embIdx = safeEmbeddingIndex();
    std::string dbPath = config_.cachePath + "/semantic_index.db";
    embIdx->open(dbPath);

    startSemanticIndexing();
}
