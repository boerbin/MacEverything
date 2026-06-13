#pragma once
#include <iostream>
#include <string>
#include <vector>
#include <cstdio>

static void runShortQueryCacheTests() {
    std::cout << "========================================\n";
    std::cout << "  Short Query Cache Tests\n";
    std::cout << "========================================\n\n";

    // ── Test 1: Build and lookup ──
    std::cout << "  --- Build and lookup ---\n";
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        records.push_back({"alpha.txt", "/tmp", 1, 100, 1000});
        records.push_back({"beta.txt", "/tmp", 1, 200, 2000});
        records.push_back({"abc.txt", "/usr", 1, 50, 3000});
        records.push_back({"delta", "/home", 2, 0, 4000});
        records.push_back({"ab_file.log", "/var/log", 1, 300, 5000});
        engine.loadRecords(std::move(records));
        engine.buildShortQueryCache();

        auto& cache = engine.getShortQueryCache();
        check(cache.isBuilt(), "ShortQueryCache: cache is built");

        auto* entryA = cache.lookup("a");
        check(entryA != nullptr, "ShortQueryCache: lookup 'a' returns non-null");
        check(entryA->totalMatches > 0, "ShortQueryCache: 'a' has matches");
        check(entryA->totalMatches >= 3, "ShortQueryCache: 'a' has >= 3 matches");

        auto* entryAb = cache.lookup("ab");
        check(entryAb != nullptr, "ShortQueryCache: lookup 'ab' returns non-null");
        check(entryAb->totalMatches >= 2, "ShortQueryCache: 'ab' has >= 2 matches");

        auto* entry3 = cache.lookup("abc");
        check(entry3 == nullptr, "ShortQueryCache: 3-char key returns null");

        auto* entryUpper = cache.lookup("A");
        check(entryUpper == nullptr, "ShortQueryCache: uppercase key returns null");
    }

    // ── Test 2: Results are sorted by score ──
    std::cout << "\n  --- Score ordering ---\n";
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        records.push_back({"x", "/deep/nested/path/here", 1, 100, 1000});
        records.push_back({"x.txt", "/tmp", 1, 200, 2000});
        records.push_back({"fox", "/bin", 1, 50, 3000});
        engine.loadRecords(std::move(records));
        engine.buildShortQueryCache();

        auto* entry = engine.getShortQueryCache().lookup("x");
        check(entry != nullptr, "ShortQueryCache: lookup 'x' non-null");
        check(entry->results.size() == 3, "ShortQueryCache: 'x' has 3 results");

        check(engine.getRecord(entry->results[0].idx).name == "x",
              "ShortQueryCache: exact 'x' is first");
        check(engine.getRecord(entry->results[1].idx).name == "x.txt",
              "ShortQueryCache: prefix 'x.txt' is second");
        check(engine.getRecord(entry->results[2].idx).name == "fox",
              "ShortQueryCache: substring 'fox' is third");
    }

    // ── Test 3: eraseRecord removes from results ──
    std::cout << "\n  --- eraseRecord ---\n";
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        for (int i = 0; i < 10; i++) {
            records.push_back({"q" + std::to_string(i) + ".dat", "/tmp", 1, 100, 1000});
        }
        engine.loadRecords(std::move(records));
        engine.buildShortQueryCache();

        auto& cache = engine.getShortQueryCache();
        auto* entry = cache.lookup("q");
        check(entry != nullptr, "ShortQueryCache: 'q' entry exists");
        check(entry->results.size() == 10, "ShortQueryCache: 'q' has 10 results");

        for (int i = 0; i < 3; i++) {
            std::string name = "q" + std::to_string(i) + ".dat";
            cache.eraseRecord(i, name.c_str(), static_cast<uint16_t>(name.size()));
        }
        check(entry->results.size() == 7, "ShortQueryCache: 'q' has 7 results after erase");
    }

    // ── Test 4: Persistence round-trip ──
    std::cout << "\n  --- Persistence ---\n";
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        records.push_back({"hello.c", "/src", 1, 100, 1000});
        records.push_back({"help.txt", "/docs", 1, 50, 2000});
        records.push_back({"world.py", "/lib", 1, 200, 3000});
        engine.loadRecords(std::move(records));
        engine.buildShortQueryCache();

        std::string tmpPath = "/tmp/test_sqcache_v2.bin";
        engine.getShortQueryCache().saveTo(tmpPath);

        ShortQueryCache loaded;
        check(loaded.loadFrom(tmpPath), "ShortQueryCache: loadFrom succeeds");
        check(loaded.isBuilt(), "ShortQueryCache: loaded cache isBuilt");

        auto* origEntry = engine.getShortQueryCache().lookup("he");
        auto* loadedEntry = loaded.lookup("he");
        check(origEntry != nullptr && loadedEntry != nullptr,
              "ShortQueryCache: both have 'he' entry");
        check(origEntry->results.size() == loadedEntry->results.size(),
              "ShortQueryCache: same result count after load");
        check(origEntry->totalMatches == loadedEntry->totalMatches,
              "ShortQueryCache: same totalMatches after load");

        std::remove(tmpPath.c_str());
    }

    // ── Test 5: Query integration ──
    std::cout << "\n  --- Query integration ---\n";
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        records.push_back({"a.txt", "/tmp", 1, 100, 1000});
        records.push_back({"banana", "/usr", 1, 200, 2000});
        records.push_back({"cat", "/bin", 1, 50, 3000});
        engine.loadRecords(std::move(records));
        engine.buildShortQueryCache();

        QueryTimingInfo timing{};
        auto results = engine.query("a", 100, true, timing);
        check(!results.empty(), "ShortQueryCache: query 'a' returns results");
        check(timing.searchPath == "short-query-cache",
              "ShortQueryCache: searchPath is 'short-query-cache'");
        check(timing.totalMs < 5.0, "ShortQueryCache: query time < 5ms");
    }

    // ── Test 6: tryInsert via addRecord ──
    std::cout << "\n  --- tryInsert via addRecord ---\n";
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        records.push_back({"zz.txt", "/tmp", 1, 100, 1000});
        engine.loadRecords(std::move(records));
        engine.buildShortQueryCache();

        auto* entry = engine.getShortQueryCache().lookup("z");
        check(entry != nullptr, "ShortQueryCache: 'z' entry exists");
        size_t beforeCount = entry->results.size();

        engine.addRecord({"zoo.dat", "/usr", 1, 200, 2000});

        check(entry->results.size() == beforeCount + 1,
              "ShortQueryCache: addRecord inserts into cache");
    }

    // ── Test 7: tombstone removes from cache ──
    std::cout << "\n  --- tombstone removes from cache ---\n";
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        records.push_back({"m.txt", "/tmp", 1, 100, 1000});
        records.push_back({"mm.dat", "/usr", 1, 200, 2000});
        engine.loadRecords(std::move(records));
        engine.buildShortQueryCache();

        auto* entry = engine.getShortQueryCache().lookup("m");
        check(entry != nullptr, "ShortQueryCache: 'm' entry exists");
        check(entry->results.size() == 2, "ShortQueryCache: 'm' has 2 results");

        engine.removeByPath("/tmp/m.txt");
        check(entry->results.size() == 1, "ShortQueryCache: 'm' has 1 after delete");
    }

    // ── Test 8: compactRecords rebuilds cache with correct indices ──
    std::cout << "\n  --- compactRecords rebuilds cache ---\n";
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        records.push_back({"apple.txt", "/tmp", 1, 100, 1000});   // idx 0, has 'a'
        records.push_back({"berry.txt", "/tmp", 1, 200, 2000});   // idx 1, no 'a'
        records.push_back({"avocado", "/usr", 1, 50, 3000});      // idx 2, has 'a'
        records.push_back({"cherry", "/bin", 1, 300, 4000});      // idx 3, no 'a'
        records.push_back({"date.log", "/var", 1, 150, 5000});    // idx 4, has 'a'
        engine.loadRecords(std::move(records));
        engine.buildShortQueryCache();

        // Verify initial state: 'a' cache has entries for apple, avocado, date
        auto* entryA = engine.getShortQueryCache().lookup("a");
        check(entryA != nullptr, "ShortQueryCache-compact: 'a' entry exists");
        check(entryA->totalMatches == 3, "ShortQueryCache-compact: 'a' has 3 matches initially");

        // Remove some records to create tombstones, then compact
        engine.removeByPath("/tmp/berry.txt");
        engine.removeByPath("/bin/cherry");

        auto remap = engine.compactRecords();
        check(!remap.empty(), "ShortQueryCache-compact: compaction produced remap");

        // After compaction, query 'a' via the engine
        QueryTimingInfo timing{};
        auto results = engine.query("a", 100, true, timing);
        check(timing.searchPath == "short-query-cache",
              "ShortQueryCache-compact: uses cache after compaction");

        // All results must have 'a' in their filename
        bool allHaveA = true;
        for (uint32_t idx : results) {
            auto rec = engine.getRecord(idx);
            std::string lower = rec.name;
            for (auto& c : lower) c = std::tolower(c);
            if (lower.find('a') == std::string::npos) {
                allHaveA = false;
                std::cout << "    FAIL: result '" << rec.name << "' has no 'a'\n";
            }
        }
        check(allHaveA, "ShortQueryCache-compact: all results contain 'a' in name");
        check(results.size() == 3, "ShortQueryCache-compact: returns 3 results after compaction");
    }

    // ── Test 9: updateByPath inserts replacement into cache ──
    std::cout << "\n  --- updateByPath cache insert ---\n";
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        records.push_back({"placeholder.txt", "/tmp", 1, 100, 1000});
        engine.loadRecords(std::move(records));
        engine.buildShortQueryCache();

        QueryTimingInfo beforeTiming{};
        auto before = engine.query("fo", 100, true, beforeTiming);
        check(beforeTiming.searchPath == "short-query-cache",
              "ShortQueryCache-update: initial query uses cache");
        check(before.empty(), "ShortQueryCache-update: no initial 'fo' matches");

        auto tmpDir = fs::temp_directory_path() / ("me_sqcache_update_" + std::to_string(getpid()));
        fs::remove_all(tmpDir);
        fs::create_directories(tmpDir / "WebPomodoro.app" / "Contents");
        std::ofstream plist(tmpDir / "WebPomodoro.app" / "Contents" / "Info.plist");
        plist << R"(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict><key>CFBundleDisplayName</key><string>Focus To-Do</string></dict></plist>
)";
        plist.close();

        engine.updateByPath("/tmp/placeholder.txt", {"WebPomodoro.app", tmpDir.string(), 5, 0, 2000});

        QueryTimingInfo afterTiming{};
        auto after = engine.query("fo", 100, true, afterTiming);
        check(afterTiming.searchPath == "short-query-cache",
              "ShortQueryCache-update: replacement query uses cache");
        check(after.size() == 1, "ShortQueryCache-update: display alias inserted into cache");
        if (!after.empty()) {
            check(engine.getRecord(after[0]).name == "WebPomodoro.app",
                  "ShortQueryCache-update: cache returns updated app record");
        }
        fs::remove_all(tmpDir);
    }

    // ── Test 10: display alias prefix ranks before canonical substring ──
    std::cout << "\n  --- display alias prefix ranking ---\n";
    {
        auto tmpDir = fs::temp_directory_path() / ("me_sqcache_rank_" + std::to_string(getpid()));
        fs::remove_all(tmpDir);
        fs::create_directories(tmpDir / "WebPomodoro.app" / "Contents");
        std::ofstream plist(tmpDir / "WebPomodoro.app" / "Contents" / "Info.plist");
        plist << R"(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict><key>CFBundleDisplayName</key><string>Focus To-Do</string></dict></plist>
)";
        plist.close();

        SearchEngine engine;
        std::vector<FileRecord> records;
        records.push_back({"prefixfo.txt", "/tmp/deep/path/that/is/longer", 1, 100, 1000});
        records.push_back({"WebPomodoro.app", tmpDir.string(), 5, 0, 2000});
        engine.loadRecords(std::move(records));
        engine.buildShortQueryCache();

        QueryTimingInfo timing{};
        auto results = engine.query("fo", 1, true, timing);
        check(timing.searchPath == "short-query-cache",
              "ShortQueryCache-rank: query uses cache");
        check(results.size() == 1, "ShortQueryCache-rank: limited query returns one result");
        if (!results.empty()) {
            check(engine.getRecord(results[0]).name == "WebPomodoro.app",
                  "ShortQueryCache-rank: display alias prefix outranks canonical substring");
        }
        fs::remove_all(tmpDir);
    }

    // ── Test 11: BoundedSortedVec eviction ──
    std::cout << "\n  --- BoundedSortedVec eviction ---\n";
    {
        BoundedSortedVec<int> vec(3);
        vec.insert(5);
        vec.insert(3);
        vec.insert(1);
        check(vec.size() == 3, "BoundedSortedVec: size=3 after 3 inserts");
        check(vec[0] == 1, "BoundedSortedVec: sorted [0]=1");

        bool inserted = vec.insert(2);
        check(inserted, "BoundedSortedVec: insert(2) succeeds");
        check(vec.size() == 3, "BoundedSortedVec: still size=3");
        check(vec.back() == 3, "BoundedSortedVec: worst is now 3 (5 evicted)");

        inserted = vec.insert(10);
        check(!inserted, "BoundedSortedVec: insert(10) rejected when full");

        bool erased = vec.erase(2);
        check(erased, "BoundedSortedVec: erase(2) succeeds");
        check(vec.size() == 2, "BoundedSortedVec: size=2 after erase");
    }

    std::cout << "\n";
}
