#include "EmbeddingIndex.h"
#include <sqlite3.h>
#include <stdexcept>
#include <cstring>

EmbeddingIndex::EmbeddingIndex() = default;

EmbeddingIndex::~EmbeddingIndex() {
    close();
}

void EmbeddingIndex::open(const std::string& dbPath) {
    close();
    int rc = sqlite3_open(dbPath.c_str(), &db_);
    if (rc != SQLITE_OK) {
        std::string err = sqlite3_errmsg(db_);
        sqlite3_close(db_);
        db_ = nullptr;
        throw std::runtime_error("EmbeddingIndex: failed to open db: " + err);
    }
    // WAL mode for better concurrent read performance
    sqlite3_exec(db_, "PRAGMA journal_mode=WAL;", nullptr, nullptr, nullptr);
    ensureTable();
}

void EmbeddingIndex::close() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

void EmbeddingIndex::ensureTable() {
    const char* sql =
        "CREATE TABLE IF NOT EXISTS embeddings ("
        "  file_path TEXT PRIMARY KEY,"
        "  embedding BLOB NOT NULL,"
        "  content_hash INTEGER NOT NULL,"
        "  mod_time INTEGER NOT NULL,"
        "  dim INTEGER NOT NULL"
        ");";
    char* errMsg = nullptr;
    int rc = sqlite3_exec(db_, sql, nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::string err = errMsg ? errMsg : "unknown error";
        sqlite3_free(errMsg);
        throw std::runtime_error("EmbeddingIndex: failed to create table: " + err);
    }
}

void EmbeddingIndex::storeEmbedding(const std::string& filePath,
                                     const std::vector<float>& embedding,
                                     uint64_t contentHash, time_t modTime) {
    const char* sql =
        "INSERT OR REPLACE INTO embeddings (file_path, embedding, content_hash, mod_time, dim) "
        "VALUES (?, ?, ?, ?, ?);";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        throw std::runtime_error("EmbeddingIndex: prepare failed: " +
                                 std::string(sqlite3_errmsg(db_)));
    }

    sqlite3_bind_text(stmt, 1, filePath.c_str(), -1, SQLITE_TRANSIENT);
    sqlite3_bind_blob(stmt, 2, embedding.data(),
                      static_cast<int>(embedding.size() * sizeof(float)), SQLITE_TRANSIENT);
    sqlite3_bind_int64(stmt, 3, static_cast<sqlite3_int64>(contentHash));
    sqlite3_bind_int64(stmt, 4, static_cast<sqlite3_int64>(modTime));
    sqlite3_bind_int(stmt, 5, static_cast<int>(embedding.size()));

    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        throw std::runtime_error("EmbeddingIndex: insert failed: " +
                                 std::string(sqlite3_errmsg(db_)));
    }
}

bool EmbeddingIndex::getEmbedding(const std::string& filePath, std::vector<float>& embedding) {
    const char* sql = "SELECT embedding, dim FROM embeddings WHERE file_path = ?;";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        throw std::runtime_error("EmbeddingIndex: prepare failed: " +
                                 std::string(sqlite3_errmsg(db_)));
    }

    sqlite3_bind_text(stmt, 1, filePath.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        int dim = sqlite3_column_int(stmt, 1);
        const void* blob = sqlite3_column_blob(stmt, 0);
        int blobBytes = sqlite3_column_bytes(stmt, 0);
        // Validate blob size matches dimension
        if (blob && blobBytes == dim * static_cast<int>(sizeof(float))) {
            const float* data = static_cast<const float*>(blob);
            embedding.assign(data, data + dim);
        } else {
            embedding.clear();
        }
        sqlite3_finalize(stmt);
        return true;
    }

    sqlite3_finalize(stmt);
    return false;
}

void EmbeddingIndex::removeEmbedding(const std::string& filePath) {
    const char* sql = "DELETE FROM embeddings WHERE file_path = ?;";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        throw std::runtime_error("EmbeddingIndex: prepare failed: " +
                                 std::string(sqlite3_errmsg(db_)));
    }

    sqlite3_bind_text(stmt, 1, filePath.c_str(), -1, SQLITE_TRANSIENT);
    rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    if (rc != SQLITE_DONE) {
        throw std::runtime_error("EmbeddingIndex: delete failed: " +
                                 std::string(sqlite3_errmsg(db_)));
    }
}

bool EmbeddingIndex::needsUpdate(const std::string& filePath, uint64_t contentHash) {
    const char* sql = "SELECT content_hash FROM embeddings WHERE file_path = ?;";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        throw std::runtime_error("EmbeddingIndex: prepare failed: " +
                                 std::string(sqlite3_errmsg(db_)));
    }

    sqlite3_bind_text(stmt, 1, filePath.c_str(), -1, SQLITE_TRANSIENT);

    rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        uint64_t storedHash = static_cast<uint64_t>(sqlite3_column_int64(stmt, 0));
        sqlite3_finalize(stmt);
        return storedHash != contentHash;
    }

    sqlite3_finalize(stmt);
    return true;  // not indexed => needs update
}

std::vector<std::pair<std::string, std::vector<float>>> EmbeddingIndex::getAllEmbeddings() {
    std::vector<std::pair<std::string, std::vector<float>>> result;
    const char* sql = "SELECT file_path, embedding, dim FROM embeddings;";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        throw std::runtime_error("EmbeddingIndex: prepare failed: " +
                                 std::string(sqlite3_errmsg(db_)));
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const char* path = reinterpret_cast<const char*>(sqlite3_column_text(stmt, 0));
        int dim = sqlite3_column_int(stmt, 2);
        const void* blob = sqlite3_column_blob(stmt, 1);
        int blobBytes = sqlite3_column_bytes(stmt, 1);

        std::vector<float> vec;
        if (blob && blobBytes == dim * static_cast<int>(sizeof(float))) {
            const float* data = static_cast<const float*>(blob);
            vec.assign(data, data + dim);
        }
        result.emplace_back(std::string(path ? path : ""), std::move(vec));
    }

    sqlite3_finalize(stmt);
    return result;
}

uint32_t EmbeddingIndex::indexedCount() {
    const char* sql = "SELECT COUNT(*) FROM embeddings;";
    sqlite3_stmt* stmt = nullptr;
    int rc = sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr);
    if (rc != SQLITE_OK) {
        throw std::runtime_error("EmbeddingIndex: prepare failed: " +
                                 std::string(sqlite3_errmsg(db_)));
    }

    uint32_t count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = static_cast<uint32_t>(sqlite3_column_int(stmt, 0));
    }
    sqlite3_finalize(stmt);
    return count;
}

