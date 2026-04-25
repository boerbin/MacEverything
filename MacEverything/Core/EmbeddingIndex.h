#pragma once
#include <string>
#include <vector>
#include <utility>
#include <cstdint>
#include <ctime>

struct sqlite3;  // forward declare

class EmbeddingIndex {
public:
    EmbeddingIndex();
    ~EmbeddingIndex();

    void open(const std::string& dbPath);
    void close();

    // CRUD
    void storeEmbedding(const std::string& filePath, const std::vector<float>& embedding,
                        uint64_t contentHash, time_t modTime);
    bool getEmbedding(const std::string& filePath, std::vector<float>& embedding);
    void removeEmbedding(const std::string& filePath);

    // Change detection
    bool needsUpdate(const std::string& filePath, uint64_t contentHash);

    // Bulk access
    std::vector<std::pair<std::string, std::vector<float>>> getAllEmbeddings();
    uint32_t indexedCount();

private:
    sqlite3* db_ = nullptr;
    void ensureTable();
};
