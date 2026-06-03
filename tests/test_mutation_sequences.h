#pragma once
#include <iostream>
#include <string>
#include <vector>
#include <cstdint>

// ── Part 91: Cross-Operation Mutation Sequence Tests ──
// Verifies correctness of add/remove/update sequences through the public API.

// ── Test 1: Add → Remove → Add again with different size ──
inline void testAddRemoveAddQueryCycle() {
    std::cout << "  --- mutation: add-remove-add cycle ---\n";
    {
        SearchEngine engine;
        std::vector<FileRecord> init;
        init.push_back({"base.txt", "/tmp", 1, 50, 500});
        engine.loadRecords(std::move(init));

        // Add record A
        FileRecord a1;
        a1.name = "target.txt";
        a1.path = "/tmp";
        a1.type = 1;
        a1.size = 100;
        a1.modTime = 1000;
        engine.addRecord(std::move(a1));
        check(engine.indexForPath("/tmp/target.txt") != UINT32_MAX, "cycle: target.txt added");

        // Remove A
        bool removed = engine.removeByPath("/tmp/target.txt");
        check(removed, "cycle: removeByPath succeeded");
        check(engine.indexForPath("/tmp/target.txt") == UINT32_MAX, "cycle: target.txt removed");

        // Add A again with different size
        FileRecord a2;
        a2.name = "target.txt";
        a2.path = "/tmp";
        a2.type = 1;
        a2.size = 999;
        a2.modTime = 2000;
        engine.addRecord(std::move(a2));

        uint32_t idx = engine.indexForPath("/tmp/target.txt");
        check(idx != UINT32_MAX, "cycle: target.txt re-added");
        FileRecord rec = engine.getRecord(idx);
        check(rec.size == 999, "cycle: re-added record has new size 999");
    }
    std::cout << "  add-remove-add cycle tests passed.\n";
}

// ── Test 2: Interleaved add/remove on different paths ──
inline void testInterleavedAddRemoveDifferentPaths() {
    std::cout << "  --- mutation: interleaved add/remove ---\n";
    {
        SearchEngine engine;
        engine.loadRecords({});

        FileRecord rA; rA.name = "a.txt"; rA.path = "/d1"; rA.type = 1; rA.size = 10; rA.modTime = 100;
        FileRecord rB; rB.name = "b.txt"; rB.path = "/d2"; rB.type = 1; rB.size = 20; rB.modTime = 200;
        FileRecord rC; rC.name = "c.txt"; rC.path = "/d3"; rC.type = 1; rC.size = 30; rC.modTime = 300;
        FileRecord rD; rD.name = "d.txt"; rD.path = "/d4"; rD.type = 1; rD.size = 40; rD.modTime = 400;

        engine.addRecord(std::move(rA));
        engine.addRecord(std::move(rB));
        engine.removeByPath("/d1/a.txt");
        engine.addRecord(std::move(rC));
        engine.removeByPath("/d2/b.txt");
        engine.addRecord(std::move(rD));

        check(engine.indexForPath("/d1/a.txt") == UINT32_MAX, "interleaved: a.txt removed");
        check(engine.indexForPath("/d2/b.txt") == UINT32_MAX, "interleaved: b.txt removed");

        uint32_t cIdx = engine.indexForPath("/d3/c.txt");
        check(cIdx != UINT32_MAX, "interleaved: c.txt exists");
        check(engine.getRecord(cIdx).size == 30, "interleaved: c.txt size correct");

        uint32_t dIdx = engine.indexForPath("/d4/d.txt");
        check(dIdx != UINT32_MAX, "interleaved: d.txt exists");
        check(engine.getRecord(dIdx).size == 40, "interleaved: d.txt size correct");
    }
    std::cout << "  interleaved add/remove tests passed.\n";
}

// ── Test 3: Add then update 3 times, verify final state ──
inline void testAddUpdateQuerySequence() {
    std::cout << "  --- mutation: add-update sequence ---\n";
    {
        SearchEngine engine;
        engine.loadRecords({});

        FileRecord r1;
        r1.name = "evolve.txt";
        r1.path = "/data";
        r1.type = 1;
        r1.size = 100;
        r1.modTime = 1000;
        engine.addRecord(std::move(r1));

        // Update 3 times with different sizes
        for (uint64_t sz : {200ULL, 300ULL, 400ULL}) {
            FileRecord upd;
            upd.name = "evolve.txt";
            upd.path = "/data";
            upd.type = 1;
            upd.size = sz;
            upd.modTime = static_cast<time_t>(sz * 10);
            engine.updateByPath("/data/evolve.txt", std::move(upd));
        }

        uint32_t idx = engine.indexForPath("/data/evolve.txt");
        check(idx != UINT32_MAX, "update-seq: evolve.txt found");
        FileRecord rec = engine.getRecord(idx);
        check(rec.size == 400, "update-seq: final size is 400");
    }
    std::cout << "  add-update sequence tests passed.\n";
}

// ── Test 4: Remove non-existent path returns false, no crash ──
inline void testRemoveNonexistentPath() {
    std::cout << "  --- mutation: remove non-existent path ---\n";
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        records.push_back({"keep.txt", "/safe", 1, 100, 1000});
        engine.loadRecords(std::move(records));

        bool removed = engine.removeByPath("/nonexistent/ghost.txt");
        check(!removed, "remove-nonexist: returns false for missing path");

        // Existing record unaffected
        check(engine.indexForPath("/safe/keep.txt") != UINT32_MAX,
              "remove-nonexist: existing record survives");
    }
    std::cout << "  remove non-existent path tests passed.\n";
}

// ── Test 5: Mass add 1000, remove odd, query correctness ──
inline void testMassAddRemoveCorrectness() {
    std::cout << "  --- mutation: mass add/remove 1000 records ---\n";
    {
        SearchEngine engine;
        engine.loadRecords({});

        // Add 1000 records
        for (int i = 0; i < 1000; i++) {
            FileRecord r;
            r.name = "f" + std::to_string(i) + ".dat";
            r.path = "/mass/d" + std::to_string(i % 50);
            r.type = 1;
            r.size = static_cast<uint64_t>(i * 10);
            r.modTime = static_cast<time_t>(i);
            engine.addRecord(std::move(r));
        }

        // Remove odd-indexed ones
        for (int i = 1; i < 1000; i += 2) {
            std::string path = "/mass/d" + std::to_string(i % 50) + "/f" + std::to_string(i) + ".dat";
            engine.removeByPath(path);
        }

        // Even-indexed should exist with correct size
        int foundEven = 0;
        int correctSize = 0;
        for (int i = 0; i < 1000; i += 2) {
            std::string path = "/mass/d" + std::to_string(i % 50) + "/f" + std::to_string(i) + ".dat";
            uint32_t idx = engine.indexForPath(path);
            if (idx != UINT32_MAX) {
                foundEven++;
                FileRecord rec = engine.getRecord(idx);
                if (rec.size == static_cast<uint64_t>(i * 10)) correctSize++;
            }
        }
        check(foundEven == 500, "mass: all 500 even records found");
        check(correctSize == 500, "mass: all 500 even records have correct size");

        // Odd-indexed should be gone
        int foundOdd = 0;
        for (int i = 1; i < 1000; i += 2) {
            std::string path = "/mass/d" + std::to_string(i % 50) + "/f" + std::to_string(i) + ".dat";
            if (engine.indexForPath(path) != UINT32_MAX) foundOdd++;
        }
        check(foundOdd == 0, "mass: all 500 odd records removed");
    }
    std::cout << "  mass add/remove tests passed.\n";
}

// ── Test 6: Add same path 5 times — last wins ──
inline void testAddDuplicatePathPreservesLatest() {
    std::cout << "  --- mutation: duplicate path last-wins ---\n";
    {
        SearchEngine engine;
        engine.loadRecords({});

        for (int i = 1; i <= 5; i++) {
            FileRecord r;
            r.name = "dup.txt";
            r.path = "/repeat";
            r.type = 1;
            r.size = static_cast<uint64_t>(i * 100);
            r.modTime = static_cast<time_t>(i * 1000);
            engine.addRecord(std::move(r));
        }

        uint32_t idx = engine.indexForPath("/repeat/dup.txt");
        check(idx != UINT32_MAX, "dup-last-wins: dup.txt found");
        FileRecord rec = engine.getRecord(idx);
        check(rec.size == 500, "dup-last-wins: last add (size=500) wins");
    }
    std::cout << "  duplicate path last-wins tests passed.\n";
}

// ── Runner ──
inline void runMutationSequenceTests() {
    std::cout << "========================================\n";
    std::cout << "  Part 91: Cross-Operation Mutation Tests\n";
    std::cout << "========================================\n\n";

    testAddRemoveAddQueryCycle();
    testInterleavedAddRemoveDifferentPaths();
    testAddUpdateQuerySequence();
    testRemoveNonexistentPath();
    testMassAddRemoveCorrectness();
    testAddDuplicatePathPreservesLatest();

    std::cout << "\n  All mutation sequence tests passed.\n\n";
}
