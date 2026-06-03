#pragma once
#include <iostream>
#include <string>
#include <vector>
#include <cstdint>

// ── Part 92: compactRecords Correctness Under Hash Keys ──

// ── Test 1: Compact preserves all live records with correct fields ──
inline void testCompactPreservesAllLiveRecords() {
    std::cout << "  --- compact: preserves all live records ---\n";
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        for (int i = 0; i < 100; i++) {
            records.push_back({
                "file" + std::to_string(i) + ".txt",
                "/dir" + std::to_string(i % 10),
                1,
                static_cast<uint64_t>(i * 100 + 1),
                static_cast<time_t>(i * 1000 + 1)
            });
        }
        engine.loadRecords(std::move(records));

        // Remove 50 records (even indices)
        for (int i = 0; i < 100; i += 2) {
            std::string path = "/dir" + std::to_string(i % 10) +
                               "/file" + std::to_string(i) + ".txt";
            engine.removeByPath(path);
        }

        engine.compactRecords();

        check(engine.recordCount() == 50, "compact-live: 50 records after compaction");

        // Verify all 50 surviving (odd-indexed) records exist with correct data
        int found = 0;
        int correctData = 0;
        for (int i = 1; i < 100; i += 2) {
            std::string path = "/dir" + std::to_string(i % 10) +
                               "/file" + std::to_string(i) + ".txt";
            uint32_t idx = engine.indexForPath(path);
            if (idx != UINT32_MAX) {
                found++;
                FileRecord rec = engine.getRecord(idx);
                uint64_t expectedSize = static_cast<uint64_t>(i * 100 + 1);
                if (rec.size == expectedSize &&
                    rec.name == ("file" + std::to_string(i) + ".txt")) {
                    correctData++;
                }
            }
        }
        check(found == 50, "compact-live: all 50 surviving records found");
        check(correctData == 50, "compact-live: all 50 have correct name+size");
    }
    std::cout << "  compact preserves live records tests passed.\n";
}

// ── Test 2: Compact deduplicates correctly (last wins) ──
inline void testCompactDeduplicatesCorrectly() {
    std::cout << "  --- compact: dedup with last-wins ---\n";
    {
        SearchEngine engine;
        engine.loadRecords({});

        // Add same path 3 times with different sizes
        for (int i = 1; i <= 3; i++) {
            FileRecord r;
            r.name = "dup.txt";
            r.path = "/shared";
            r.type = 1;
            r.size = static_cast<uint64_t>(i * 100);
            r.modTime = static_cast<time_t>(i * 1000);
            engine.addRecord(std::move(r));
        }

        // Also add a unique record
        FileRecord unique;
        unique.name = "unique.txt";
        unique.path = "/shared";
        unique.type = 1;
        unique.size = 42;
        unique.modTime = 9999;
        engine.addRecord(std::move(unique));

        engine.compactRecords();

        // After compaction, dup.txt should have last-wins size
        uint32_t dupIdx = engine.indexForPath("/shared/dup.txt");
        check(dupIdx != UINT32_MAX, "compact-dedup: dup.txt found");
        FileRecord dupRec = engine.getRecord(dupIdx);
        check(dupRec.size == 300, "compact-dedup: last-wins size=300");

        // unique.txt should survive
        uint32_t uniqIdx = engine.indexForPath("/shared/unique.txt");
        check(uniqIdx != UINT32_MAX, "compact-dedup: unique.txt found");
        check(engine.getRecord(uniqIdx).size == 42, "compact-dedup: unique.txt size=42");
    }
    std::cout << "  compact dedup tests passed.\n";
}

// ── Test 3: Compact rebuilds pathLookup — new records use correct pool index ──
inline void testCompactRebuildPathLookup() {
    std::cout << "  --- compact: pathLookup rebuilt correctly ---\n";
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        for (int d = 0; d < 5; d++) {
            records.push_back({
                "f.txt",
                "/compdir" + std::to_string(d),
                1,
                static_cast<uint64_t>(d * 10),
                static_cast<time_t>(d * 100)
            });
        }
        engine.loadRecords(std::move(records));

        // Remove some to trigger compaction
        engine.removeByPath("/compdir1/f.txt");
        engine.removeByPath("/compdir3/f.txt");
        engine.compactRecords();

        check(engine.recordCount() == 3, "compact-lookup: 3 records after compact");

        // Add new record under one of the surviving directories
        FileRecord newRec;
        newRec.name = "new.txt";
        newRec.path = "/compdir0";
        newRec.type = 1;
        newRec.size = 777;
        newRec.modTime = 7777;
        engine.addRecord(std::move(newRec));

        uint32_t newIdx = engine.indexForPath("/compdir0/new.txt");
        check(newIdx != UINT32_MAX, "compact-lookup: new.txt found after compact+add");
        std::string resolvedPath = engine.resolveRecordPath(newIdx);
        check(resolvedPath == "/compdir0",
              "compact-lookup: new.txt path resolves correctly to /compdir0");
    }
    std::cout << "  compact pathLookup rebuild tests passed.\n";
}

// ── Test 4: Compact then add then query — both old and new records correct ──
inline void testCompactThenAddThenQuery() {
    std::cout << "  --- compact: compact-add-query sequence ---\n";
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        records.push_back({"old1.txt", "/stable", 1, 100, 1000});
        records.push_back({"old2.txt", "/stable", 1, 200, 2000});
        records.push_back({"trash.txt", "/stable", 1, 0, 0});
        engine.loadRecords(std::move(records));

        engine.removeByPath("/stable/trash.txt");
        engine.compactRecords();

        check(engine.recordCount() == 2, "compact-add-query: 2 after compact");

        // Add new records
        FileRecord n1; n1.name = "new1.txt"; n1.path = "/stable"; n1.type = 1; n1.size = 500; n1.modTime = 5000;
        FileRecord n2; n2.name = "new2.txt"; n2.path = "/fresh"; n2.type = 1; n2.size = 600; n2.modTime = 6000;
        engine.addRecord(std::move(n1));
        engine.addRecord(std::move(n2));

        // Old records
        uint32_t o1 = engine.indexForPath("/stable/old1.txt");
        check(o1 != UINT32_MAX, "compact-add-query: old1.txt found");
        check(engine.getRecord(o1).size == 100, "compact-add-query: old1.txt size=100");

        uint32_t o2 = engine.indexForPath("/stable/old2.txt");
        check(o2 != UINT32_MAX, "compact-add-query: old2.txt found");
        check(engine.getRecord(o2).size == 200, "compact-add-query: old2.txt size=200");

        // New records
        uint32_t ni1 = engine.indexForPath("/stable/new1.txt");
        check(ni1 != UINT32_MAX, "compact-add-query: new1.txt found");
        check(engine.getRecord(ni1).size == 500, "compact-add-query: new1.txt size=500");

        uint32_t ni2 = engine.indexForPath("/fresh/new2.txt");
        check(ni2 != UINT32_MAX, "compact-add-query: new2.txt found");
        check(engine.getRecord(ni2).size == 600, "compact-add-query: new2.txt size=600");

        // Removed record stays gone
        check(engine.indexForPath("/stable/trash.txt") == UINT32_MAX,
              "compact-add-query: trash.txt still gone");
    }
    std::cout << "  compact-add-query tests passed.\n";
}

// ── Test 5: Compact preserves field correctness (name, size, modTime) ──
inline void testCompactPreservesFieldCorrectness() {
    std::cout << "  --- compact: field correctness ---\n";
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        // Records with distinct, verifiable field values
        records.push_back({"alpha.cpp", "/src", 1, 1111, 10001});
        records.push_back({"beta.h",    "/inc", 1, 2222, 20002});
        records.push_back({"gamma.py",  "/scripts", 1, 3333, 30003});
        records.push_back({"delta.rs",  "/src", 1, 4444, 40004});
        records.push_back({"remove_me", "/tmp", 1, 9999, 99999});
        engine.loadRecords(std::move(records));

        engine.removeByPath("/tmp/remove_me");
        engine.compactRecords();

        check(engine.recordCount() == 4, "compact-fields: 4 records after compact");

        struct Expected { std::string path; std::string name; uint64_t size; };
        Expected expectations[] = {
            {"/src/alpha.cpp",     "alpha.cpp", 1111},
            {"/inc/beta.h",        "beta.h",    2222},
            {"/scripts/gamma.py",  "gamma.py",  3333},
            {"/src/delta.rs",      "delta.rs",  4444},
        };

        int verified = 0;
        for (auto& exp : expectations) {
            uint32_t idx = engine.indexForPath(exp.path);
            check(idx != UINT32_MAX, ("compact-fields: " + exp.name + " found").c_str());
            FileRecord rec = engine.getRecord(idx);
            check(rec.name == exp.name, ("compact-fields: " + exp.name + " name correct").c_str());
            check(rec.size == exp.size, ("compact-fields: " + exp.name + " size correct").c_str());
            verified++;
        }
        check(verified == 4, "compact-fields: all 4 records verified");
    }
    std::cout << "  compact field correctness tests passed.\n";
}

// ── Runner ──
inline void runCompactHashTests() {
    std::cout << "========================================\n";
    std::cout << "  Part 92: Compaction Hash-Key Tests\n";
    std::cout << "========================================\n\n";

    testCompactPreservesAllLiveRecords();
    testCompactDeduplicatesCorrectly();
    testCompactRebuildPathLookup();
    testCompactThenAddThenQuery();
    testCompactPreservesFieldCorrectness();

    std::cout << "\n  All compaction hash-key tests passed.\n\n";
}
