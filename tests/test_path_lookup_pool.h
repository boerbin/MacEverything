#pragma once
#include <iostream>
#include <string>
#include <vector>
#include <cassert>
#include <cstdint>

// ── Part 89: pathLookup_ / lowerPathLookup_ hash-key tests ──
// Verifies that pathLookup_ (now keyed by pathHash) correctly deduplicates
// directory paths through internPath, survives removeByPath, and stays
// consistent after compactRecords.

// ── Test 1: pathLookup deduplication — shared dir path yields single pool entry ──
inline void testPathLookupDedup() {
    std::cout << "  --- pathLookup dedup ---\n";
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        // Three files in the same directory
        records.push_back({"a.txt", "/tmp/shared", 1, 100, 1000});
        records.push_back({"b.txt", "/tmp/shared", 1, 200, 2000});
        records.push_back({"c.txt", "/tmp/shared", 1, 300, 3000});
        // One file in a different directory
        records.push_back({"d.txt", "/var/log", 1, 400, 4000});
        engine.loadRecords(std::move(records));

        // pathPool should have exactly 2 unique directory entries
        auto pool = engine.pathPoolSnapshot();
        uint32_t liveCount = 0;
        for (uint32_t i = 0; i < pool.entryCount(); i++) {
            if (pool.isLive(i)) liveCount++;
        }
        check(liveCount == 2, "pathLookup dedup: 2 unique dirs in pathPool");

        // All 4 records should be findable
        check(engine.indexForPath("/tmp/shared/a.txt") != UINT32_MAX,
              "pathLookup dedup: a.txt found");
        check(engine.indexForPath("/tmp/shared/b.txt") != UINT32_MAX,
              "pathLookup dedup: b.txt found");
        check(engine.indexForPath("/tmp/shared/c.txt") != UINT32_MAX,
              "pathLookup dedup: c.txt found");
        check(engine.indexForPath("/var/log/d.txt") != UINT32_MAX,
              "pathLookup dedup: d.txt found");
    }
    std::cout << "  pathLookup dedup tests passed.\n";
}

// ── Test 2: internPath reuses existing pool entries ──
inline void testInternPathReuse() {
    std::cout << "  --- internPath reuse ---\n";
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        records.push_back({"first.txt", "/home/user", 1, 10, 100});
        engine.loadRecords(std::move(records));

        auto poolBefore = engine.pathPoolSnapshot();
        uint32_t countBefore = 0;
        for (uint32_t i = 0; i < poolBefore.entryCount(); i++) {
            if (poolBefore.isLive(i)) countBefore++;
        }
        check(countBefore == 1, "internPath reuse: 1 dir after initial load");

        // Add another record in the same directory via addRecord
        FileRecord r2;
        r2.name = "second.txt";
        r2.path = "/home/user";
        r2.type = 1;
        r2.size = 20;
        r2.modTime = 200;
        engine.addRecord(std::move(r2));

        auto poolAfter = engine.pathPoolSnapshot();
        uint32_t countAfter = 0;
        for (uint32_t i = 0; i < poolAfter.entryCount(); i++) {
            if (poolAfter.isLive(i)) countAfter++;
        }
        check(countAfter == 1, "internPath reuse: still 1 dir after adding same-dir record");

        // Add a record in a new directory
        FileRecord r3;
        r3.name = "third.txt";
        r3.path = "/etc";
        r3.type = 1;
        r3.size = 30;
        r3.modTime = 300;
        engine.addRecord(std::move(r3));

        auto poolAfter2 = engine.pathPoolSnapshot();
        uint32_t countAfter2 = 0;
        for (uint32_t i = 0; i < poolAfter2.entryCount(); i++) {
            if (poolAfter2.isLive(i)) countAfter2++;
        }
        check(countAfter2 == 2, "internPath reuse: 2 dirs after adding new-dir record");
    }
    std::cout << "  internPath reuse tests passed.\n";
}

// ── Test 3: removeByPath removes records but pathLookup entries persist ──
inline void testPathLookupAfterRemove() {
    std::cout << "  --- pathLookup after remove ---\n";
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        records.push_back({"keep.txt", "/tmp/dir", 1, 100, 1000});
        records.push_back({"gone.txt", "/tmp/dir", 1, 200, 2000});
        engine.loadRecords(std::move(records));

        // Remove one record
        bool removed = engine.removeByPath("/tmp/dir/gone.txt");
        check(removed, "pathLookup remove: removeByPath succeeded");

        // The remaining record should still be found
        check(engine.indexForPath("/tmp/dir/keep.txt") != UINT32_MAX,
              "pathLookup remove: keep.txt still found");

        // The removed record should not be found
        check(engine.indexForPath("/tmp/dir/gone.txt") == UINT32_MAX,
              "pathLookup remove: gone.txt not found");

        // pathPool entry should persist (it maps to pool index, not record)
        auto pool = engine.pathPoolSnapshot();
        uint32_t liveCount = 0;
        for (uint32_t i = 0; i < pool.entryCount(); i++) {
            if (pool.isLive(i)) liveCount++;
        }
        check(liveCount == 1, "pathLookup remove: pathPool entry persists after record removal");
    }
    std::cout << "  pathLookup after remove tests passed.\n";
}

// ── Test 4: compactRecords produces correct pathLookup state ──
inline void testPathLookupAfterCompact() {
    std::cout << "  --- pathLookup after compact ---\n";
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        // Two dirs, multiple files
        records.push_back({"a.txt", "/dir1", 1, 100, 1000});
        records.push_back({"b.txt", "/dir1", 1, 200, 2000});
        records.push_back({"c.txt", "/dir2", 1, 300, 3000});
        records.push_back({"d.txt", "/dir2", 1, 400, 4000});
        engine.loadRecords(std::move(records));

        // Remove some records to create tombstones
        engine.removeByPath("/dir1/b.txt");
        engine.removeByPath("/dir2/d.txt");

        check(engine.recordCount() == 4, "pathLookup compact: 4 total records before compact");

        // Compact
        engine.compactRecords();

        // After compaction, only 2 live records remain
        check(engine.recordCount() == 2, "pathLookup compact: 2 records after compact");

        // Surviving records should still be findable
        check(engine.indexForPath("/dir1/a.txt") != UINT32_MAX,
              "pathLookup compact: a.txt found after compact");
        check(engine.indexForPath("/dir2/c.txt") != UINT32_MAX,
              "pathLookup compact: c.txt found after compact");

        // Removed records should not be found
        check(engine.indexForPath("/dir1/b.txt") == UINT32_MAX,
              "pathLookup compact: b.txt gone after compact");
        check(engine.indexForPath("/dir2/d.txt") == UINT32_MAX,
              "pathLookup compact: d.txt gone after compact");

        // Both dirs still have live records, so pathPool should still have 2 entries
        auto pool = engine.pathPoolSnapshot();
        uint32_t liveCount = 0;
        for (uint32_t i = 0; i < pool.entryCount(); i++) {
            if (pool.isLive(i)) liveCount++;
        }
        check(liveCount == 2, "pathLookup compact: pathPool has 2 dirs after compact");
    }
    std::cout << "  pathLookup after compact tests passed.\n";
}

// ── Runner ──
inline void runPathLookupPoolTests() {
    std::cout << "\n=== Part 89: pathLookup/lowerPathLookup hash-key tests ===\n";
    testPathLookupDedup();
    testInternPathReuse();
    testPathLookupAfterRemove();
    testPathLookupAfterCompact();
    std::cout << "=== Part 89 complete ===\n";
}
