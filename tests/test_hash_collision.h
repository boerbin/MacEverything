#pragma once
#include <iostream>
#include <string>
#include <vector>
#include <cstdint>

// Helper class with friend access to SearchEngine private members.
// Enables collision injection for testing hash-only lookup vulnerabilities.
class HashCollisionTestHelper {
public:
    static void injectPathIndex(SearchEngine& e, uint64_t hash, uint32_t idx) {
        e.pathIndex_[hash] = idx;
    }
    static void erasePathIndex(SearchEngine& e, uint64_t hash) {
        e.pathIndex_.erase(hash);
    }
    static const std::unordered_map<uint64_t, uint32_t>& getPathIndex(const SearchEngine& e) {
        return e.pathIndex_;
    }
    static void injectPathLookup(SearchEngine& e, uint64_t hash, uint32_t idx) {
        e.pathLookup_[hash] = idx;
    }
    static const std::unordered_map<uint64_t, uint32_t>& getPathLookup(const SearchEngine& e) {
        return e.pathLookup_;
    }
    static const std::unordered_map<uint64_t, uint32_t>& getLowerPathLookup(const SearchEngine& e) {
        return e.lowerPathLookup_;
    }
    static uint32_t callInternPath(SearchEngine& e, const std::string& path) {
        return e.internPath(path);
    }
};

// ── Category 1: pathIndex_ collision tests ──

// Test 1: Collision in addRecord tombstones wrong record
inline void testCollisionAddTombstonesWrongRecord() {
    std::cout << "  --- collision: addRecord tombstones wrong record ---\n";
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        records.push_back({"a.txt", "/dir1", 1, 100, 1000});
        records.push_back({"b.txt", "/dir2", 1, 200, 2000});
        records.push_back({"c.txt", "/dir3", 1, 300, 3000});
        engine.loadRecords(std::move(records));

        // Verify all 3 records exist
        check(engine.indexForPath("/dir1/a.txt") != UINT32_MAX, "collision-add: a.txt exists");
        check(engine.indexForPath("/dir2/b.txt") != UINT32_MAX, "collision-add: b.txt exists");
        check(engine.indexForPath("/dir3/c.txt") != UINT32_MAX, "collision-add: c.txt exists");

        // Inject collision: map a NEW path's hash to record A's index (0)
        // This simulates what happens when two different paths produce the same FNV-1a hash.
        std::string colliderPath = "/dir4/new.txt";
        std::string colliderLower = me::toLower(colliderPath);
        uint64_t colliderHash = SearchEngine::pathHash(colliderLower);

        // Overwrite pathIndex_ so colliderHash points to record 0 (a.txt)
        HashCollisionTestHelper::injectPathIndex(engine, colliderHash, 0);

        // Now add a record with the collider path — addRecord will find the
        // injected entry and tombstone record 0 (a.txt) thinking it's a duplicate.
        FileRecord newRec;
        newRec.name = "new.txt";
        newRec.path = "/dir4";
        newRec.type = 1;
        newRec.size = 999;
        newRec.modTime = 9999;
        engine.addRecord(std::move(newRec));

        // The collision made addRecord tombstone record 0 (a.txt's data).
        // But a.txt's OWN pathIndex_ entry (keyed by its real hash) still exists,
        // creating a dangling reference to a tombstoned record.
        uint32_t aIdx = engine.indexForPath("/dir1/a.txt");
        check(aIdx != UINT32_MAX, "collision-add: a.txt index still in pathIndex_ (dangling)");
        FileRecord aRec = engine.getRecord(aIdx);
        check(aRec.type == 0, "collision-add: a.txt record tombstoned (corruption via collision)");

        // Record B should be unaffected
        check(engine.indexForPath("/dir2/b.txt") != UINT32_MAX, "collision-add: b.txt survives");

        // The new record should exist
        uint32_t newIdx = engine.indexForPath("/dir4/new.txt");
        check(newIdx != UINT32_MAX, "collision-add: new.txt was added");
        FileRecord newRec2 = engine.getRecord(newIdx);
        check(newRec2.size == 999, "collision-add: new.txt has correct size");
    }
    std::cout << "  collision addRecord tests passed.\n";
}

// Test 2: Collision in removeByPath deletes wrong record
inline void testCollisionRemoveDeletesWrongRecord() {
    std::cout << "  --- collision: removeByPath deletes wrong record ---\n";
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        records.push_back({"a.txt", "/dir1", 1, 100, 1000});
        records.push_back({"b.txt", "/dir2", 1, 200, 2000});
        records.push_back({"c.txt", "/dir3", 1, 300, 3000});
        engine.loadRecords(std::move(records));

        // Inject collision: map a fake path's hash to record B's index (1)
        std::string fakePath = "/fake/nonexistent.txt";
        std::string fakeLower = me::toLower(fakePath);
        uint64_t fakeHash = SearchEngine::pathHash(fakeLower);
        HashCollisionTestHelper::injectPathIndex(engine, fakeHash, 1);

        // removeByPath on the fake path will find record B via the injected hash
        bool removed = engine.removeByPath(fakePath);
        check(removed, "collision-remove: removeByPath returned true");

        // Record B's data was tombstoned, but its real pathIndex_ entry
        // (keyed by b.txt's real hash) still exists — dangling reference.
        uint32_t bIdx = engine.indexForPath("/dir2/b.txt");
        check(bIdx != UINT32_MAX, "collision-remove: b.txt index still in pathIndex_ (dangling)");
        FileRecord bRec = engine.getRecord(bIdx);
        check(bRec.type == 0, "collision-remove: b.txt record tombstoned (corruption via collision)");

        // Records A and C should survive
        check(engine.indexForPath("/dir1/a.txt") != UINT32_MAX, "collision-remove: a.txt survives");
        check(engine.indexForPath("/dir3/c.txt") != UINT32_MAX, "collision-remove: c.txt survives");
    }
    std::cout << "  collision removeByPath tests passed.\n";
}

// Test 3: Collision in indexForPath returns wrong record
inline void testCollisionIndexForPathReturnsWrongRecord() {
    std::cout << "  --- collision: indexForPath returns wrong record ---\n";
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        records.push_back({"alpha.txt", "/usr", 1, 111, 1000});
        records.push_back({"beta.txt", "/opt", 1, 222, 2000});
        engine.loadRecords(std::move(records));

        // Inject collision: map a non-existent path's hash to record 0 (alpha.txt)
        std::string queryPath = "/nonexistent/query.txt";
        std::string queryLower = me::toLower(queryPath);
        uint64_t queryHash = SearchEngine::pathHash(queryLower);
        HashCollisionTestHelper::injectPathIndex(engine, queryHash, 0);

        // indexForPath returns index 0 — but that's alpha.txt, not query.txt
        uint32_t idx = engine.indexForPath(queryPath);
        check(idx != UINT32_MAX, "collision-indexFor: returns a result (false positive)");
        FileRecord rec = engine.getRecord(idx);
        check(rec.name != "query.txt", "collision-indexFor: name mismatch proves wrong record returned");
        check(rec.name == "alpha.txt", "collision-indexFor: got alpha.txt instead of query.txt");
    }
    std::cout << "  collision indexForPath tests passed.\n";
}

// ── Category 3: pathLookup_ collision cascading corruption ──

// Test 4: pathLookup collision merges directories
inline void testPathLookupCollisionMergesDirectories() {
    std::cout << "  --- collision: pathLookup merges directories ---\n";
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        records.push_back({"file1.txt", "/projects/alpha", 1, 100, 1000});
        engine.loadRecords(std::move(records));

        // Get the pool index for /projects/alpha
        auto& lookup = HashCollisionTestHelper::getPathLookup(engine);
        uint64_t alphaHash = SearchEngine::pathHash("/projects/alpha");
        auto alphaIt = lookup.find(alphaHash);
        check(alphaIt != lookup.end(), "pathLookup-collision: alpha dir exists in lookup");
        uint32_t alphaPoolIdx = alphaIt->second;

        // Inject collision: map /projects/beta's hash to alpha's pool index
        uint64_t betaHash = SearchEngine::pathHash("/projects/beta");
        HashCollisionTestHelper::injectPathLookup(engine, betaHash, alphaPoolIdx);

        // Now add a record under /projects/beta — internPath will find the
        // injected entry and reuse alpha's pool index
        FileRecord r2;
        r2.name = "file2.txt";
        r2.path = "/projects/beta";
        r2.type = 1;
        r2.size = 200;
        r2.modTime = 2000;
        engine.addRecord(std::move(r2));

        // The new record's path is resolved from pathPool, which stores
        // "/projects/alpha" at that index — so file2.txt appears under alpha
        uint32_t idx2 = engine.indexForPath("/projects/beta/file2.txt");
        if (idx2 != UINT32_MAX) {
            std::string resolvedPath = engine.resolveRecordPath(idx2);
            check(resolvedPath == "/projects/alpha",
                  "pathLookup-collision: file2.txt path resolves to alpha (corruption)");
        } else {
            // indexForPath uses pathHash of the full path, so it might still find it
            // via the correct hash. The corruption is in the stored path.
            // Check the latest record directly.
            uint32_t lastIdx = engine.recordCount() - 1;
            std::string resolvedPath = engine.resolveRecordPath(lastIdx);
            check(resolvedPath == "/projects/alpha",
                  "pathLookup-collision: latest record path resolves to alpha (corruption)");
        }
    }
    std::cout << "  pathLookup collision merge tests passed.\n";
}

// Test 5: pathLookup collision affects multiple subsequent records
inline void testPathLookupCollisionAffectsMultipleRecords() {
    std::cout << "  --- collision: pathLookup affects multiple records ---\n";
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        records.push_back({"seed.txt", "/real/path", 1, 10, 100});
        engine.loadRecords(std::move(records));

        // Get pool index for /real/path
        auto& lookup = HashCollisionTestHelper::getPathLookup(engine);
        uint64_t realHash = SearchEngine::pathHash("/real/path");
        auto realIt = lookup.find(realHash);
        check(realIt != lookup.end(), "pathLookup-multi: real dir in lookup");
        uint32_t realPoolIdx = realIt->second;

        // Inject collision for a different directory
        uint64_t fakeHash = SearchEngine::pathHash("/fake/dir");
        HashCollisionTestHelper::injectPathLookup(engine, fakeHash, realPoolIdx);

        // Add multiple records under the fake directory
        int wrongPathCount = 0;
        for (int i = 0; i < 5; i++) {
            FileRecord r;
            r.name = "f" + std::to_string(i) + ".txt";
            r.path = "/fake/dir";
            r.type = 1;
            r.size = static_cast<uint64_t>(i * 100);
            r.modTime = static_cast<time_t>(i * 1000);
            engine.addRecord(std::move(r));
        }

        // All 5 records should have their path resolved to /real/path (wrong)
        uint32_t total = engine.recordCount();
        for (uint32_t i = 1; i < total; i++) {
            FileRecord rec = engine.getRecord(i);
            if (rec.size == 0 && rec.modTime == 0 && rec.name.empty()) continue; // tombstone
            std::string resolvedPath = engine.resolveRecordPath(i);
            if (resolvedPath == "/real/path") wrongPathCount++;
        }
        check(wrongPathCount == 5,
              "pathLookup-multi: all 5 fake-dir records got wrong path (cascading corruption)");
    }
    std::cout << "  pathLookup multi-record collision tests passed.\n";
}

// ── Runner ──
inline void runHashCollisionTests() {
    std::cout << "========================================\n";
    std::cout << "  Part 90: Hash Collision Tests\n";
    std::cout << "========================================\n\n";

    testCollisionAddTombstonesWrongRecord();
    testCollisionRemoveDeletesWrongRecord();
    testCollisionIndexForPathReturnsWrongRecord();
    testPathLookupCollisionMergesDirectories();
    testPathLookupCollisionAffectsMultipleRecords();

    std::cout << "\n  All hash collision tests passed.\n\n";
}
