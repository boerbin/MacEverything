#pragma once
#include <iostream>
#include <string>
#include <vector>
#include <set>
#include <cassert>
#include <cstdint>

// ── Test 1: pathHash is deterministic ──
inline void testPathHashDeterministic() {
    std::cout << "  --- pathHash deterministic ---\n";
    {
        std::string p = "/Users/test/Documents/hello.txt";
        uint64_t h1 = SearchEngine::pathHash(p);
        uint64_t h2 = SearchEngine::pathHash(p);
        check(h1 == h2, "pathHash: same string => same hash");

        uint64_t h3 = SearchEngine::pathHash(p);
        check(h1 == h3, "pathHash: deterministic across calls");
    }
    std::cout << "  deterministic tests passed.\n";
}

// ── Test 2: pathHash produces distinct values for different strings ──
inline void testPathHashDistinct() {
    std::cout << "  --- pathHash distinct ---\n";
    {
        uint64_t h1 = SearchEngine::pathHash("/usr/bin/ls");
        uint64_t h2 = SearchEngine::pathHash("/usr/bin/cat");
        check(h1 != h2, "pathHash: different paths => different hashes");

        uint64_t h3 = SearchEngine::pathHash("/a");
        uint64_t h4 = SearchEngine::pathHash("/b");
        check(h3 != h4, "pathHash: single-char diff => different hashes");

        // Near-miss: paths differing by one character
        uint64_t h5 = SearchEngine::pathHash("/tmp/file_a.txt");
        uint64_t h6 = SearchEngine::pathHash("/tmp/file_b.txt");
        check(h5 != h6, "pathHash: near-miss paths => different hashes");
    }
    std::cout << "  distinct tests passed.\n";
}

// ── Test 3: loadRecords + indexForPath finds records ──
inline void testPathIndexAddAndFind() {
    std::cout << "  --- pathIndex add and find ---\n";
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        records.push_back({"hello.txt", "/tmp", 1, 100, 1000});
        records.push_back({"world.txt", "/var/log", 1, 200, 2000});
        records.push_back({"readme.md", "/home/user", 1, 50, 3000});
        engine.loadRecords(std::move(records));

        uint32_t idx0 = engine.indexForPath("/tmp/hello.txt");
        check(idx0 != UINT32_MAX, "indexForPath: /tmp/hello.txt found");
        check(idx0 == 0, "indexForPath: /tmp/hello.txt is index 0");

        uint32_t idx1 = engine.indexForPath("/var/log/world.txt");
        check(idx1 != UINT32_MAX, "indexForPath: /var/log/world.txt found");
        check(idx1 == 1, "indexForPath: /var/log/world.txt is index 1");

        uint32_t idx2 = engine.indexForPath("/home/user/readme.md");
        check(idx2 != UINT32_MAX, "indexForPath: /home/user/readme.md found");
        check(idx2 == 2, "indexForPath: /home/user/readme.md is index 2");

        uint32_t missing = engine.indexForPath("/nonexistent/path.txt");
        check(missing == UINT32_MAX, "indexForPath: missing path returns UINT32_MAX");
    }
    std::cout << "  add-and-find tests passed.\n";
}

// ── Test 4: removeByPath then indexForPath returns UINT32_MAX ──
inline void testPathIndexRemoveByPath() {
    std::cout << "  --- pathIndex removeByPath ---\n";
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        records.push_back({"a.txt", "/tmp", 1, 10, 100});
        records.push_back({"b.txt", "/tmp", 1, 20, 200});
        records.push_back({"c.txt", "/var", 1, 30, 300});
        engine.loadRecords(std::move(records));

        // Verify present before removal
        check(engine.indexForPath("/tmp/a.txt") != UINT32_MAX,
              "removeByPath: /tmp/a.txt exists before remove");

        bool removed = engine.removeByPath("/tmp/a.txt");
        check(removed, "removeByPath: returns true on success");

        uint32_t afterRemove = engine.indexForPath("/tmp/a.txt");
        check(afterRemove == UINT32_MAX,
              "removeByPath: /tmp/a.txt gone after remove");

        // Others remain
        check(engine.indexForPath("/tmp/b.txt") != UINT32_MAX,
              "removeByPath: /tmp/b.txt still exists");
        check(engine.indexForPath("/var/c.txt") != UINT32_MAX,
              "removeByPath: /var/c.txt still exists");
    }
    std::cout << "  removeByPath tests passed.\n";
}

// ── Test 5: removeByPathPrefix removes all under prefix ──
inline void testPathIndexRemoveByPrefix() {
    std::cout << "  --- pathIndex removeByPathPrefix ---\n";
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        records.push_back({"f1.txt", "/data/project", 1, 10, 100});
        records.push_back({"f2.txt", "/data/project/sub", 1, 20, 200});
        records.push_back({"f3.txt", "/data/other", 1, 30, 300});
        records.push_back({"f4.txt", "/backup", 1, 40, 400});
        engine.loadRecords(std::move(records));

        uint32_t removedCount = engine.removeByPathPrefix("/data/project");
        check(removedCount == 2,
              "removeByPathPrefix: removed 2 records under /data/project");

        check(engine.indexForPath("/data/project/f1.txt") == UINT32_MAX,
              "removeByPathPrefix: /data/project/f1.txt gone");
        check(engine.indexForPath("/data/project/sub/f2.txt") == UINT32_MAX,
              "removeByPathPrefix: /data/project/sub/f2.txt gone");

        // Others remain
        check(engine.indexForPath("/data/other/f3.txt") != UINT32_MAX,
              "removeByPathPrefix: /data/other/f3.txt still exists");
        check(engine.indexForPath("/backup/f4.txt") != UINT32_MAX,
              "removeByPathPrefix: /backup/f4.txt still exists");
    }
    std::cout << "  removeByPathPrefix tests passed.\n";
}

// ── Test 6: duplicate paths — last wins ──
inline void testPathIndexLastWinsDedup() {
    std::cout << "  --- pathIndex last-wins dedup ---\n";
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        // Two records with same full path (name+path), different sizes
        records.push_back({"dup.txt", "/tmp", 1, 100, 1000});
        records.push_back({"other.txt", "/var", 1, 50, 2000});
        records.push_back({"dup.txt", "/tmp", 1, 999, 3000});
        engine.loadRecords(std::move(records));

        uint32_t idx = engine.indexForPath("/tmp/dup.txt");
        check(idx != UINT32_MAX, "lastWins: /tmp/dup.txt found");

        // The last record (index 2) should win
        FileRecord rec = engine.getRecord(idx);
        check(rec.size == 999, "lastWins: last record size=999 wins");
    }
    std::cout << "  last-wins dedup tests passed.\n";
}

// ── Test 7: collision safety — 10k paths, no false positives ──
inline void testPathIndexCollisionSafety() {
    std::cout << "  --- pathIndex collision safety (10k paths) ---\n";
    {
        constexpr int N = 10000;
        SearchEngine engine;
        std::vector<FileRecord> records;
        records.reserve(N);
        for (int i = 0; i < N; ++i) {
            std::string name = "file_" + std::to_string(i) + ".dat";
            std::string path = "/gen/dir_" + std::to_string(i % 100);
            records.push_back({name, path, 1, static_cast<uint64_t>(i), static_cast<time_t>(i)});
        }
        engine.loadRecords(std::move(records));

        // Verify all 10k paths can be found — no false negatives
        int foundCount = 0;
        for (int i = 0; i < N; ++i) {
            std::string fullPath = "/gen/dir_" + std::to_string(i % 100)
                                 + "/file_" + std::to_string(i) + ".dat";
            uint32_t idx = engine.indexForPath(fullPath);
            if (idx != UINT32_MAX) ++foundCount;
        }
        check(foundCount == N,
              "collisionSafety: all 10k paths found (no false negatives)");

        // Verify non-existent paths are not found — no false positives
        int falsePositives = 0;
        for (int i = 0; i < 1000; ++i) {
            std::string fakePath = "/gen/dir_" + std::to_string(i % 100)
                                 + "/fake_" + std::to_string(i) + ".dat";
            uint32_t idx = engine.indexForPath(fakePath);
            if (idx != UINT32_MAX) ++falsePositives;
        }
        check(falsePositives == 0,
              "collisionSafety: no false positives for 1k non-existent paths");

        // Verify hash uniqueness across all generated paths
        std::set<uint64_t> hashes;
        for (int i = 0; i < N; ++i) {
            std::string fullPath = "/gen/dir_" + std::to_string(i % 100)
                                 + "/file_" + std::to_string(i) + ".dat";
            hashes.insert(SearchEngine::pathHash(fullPath));
        }
        check(static_cast<int>(hashes.size()) == N,
              "collisionSafety: all 10k hashes are unique");
    }
    std::cout << "  collision safety tests passed.\n";
}

// ── Runner ──
inline void runPathIndexHashTests() {
    std::cout << "========================================\n";
    std::cout << "  Part 88: Path Index Hash Tests\n";
    std::cout << "========================================\n\n";

    testPathHashDeterministic();
    testPathHashDistinct();
    testPathIndexAddAndFind();
    testPathIndexRemoveByPath();
    testPathIndexRemoveByPrefix();
    testPathIndexLastWinsDedup();
    testPathIndexCollisionSafety();

    std::cout << "\n  All pathIndex hash tests passed.\n\n";
}
