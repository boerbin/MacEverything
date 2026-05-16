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

        // "alpha.txt" and "abc.txt" and "ab_file.log" and "delta" all contain 'a'
        check(entryA->totalMatches >= 3, "ShortQueryCache: 'a' has >= 3 matches");

        auto* entryAb = cache.lookup("ab");
        check(entryAb != nullptr, "ShortQueryCache: lookup 'ab' returns non-null");
        check(entryAb->totalMatches >= 2, "ShortQueryCache: 'ab' has >= 2 matches (abc.txt, ab_file.log)");

        // Non-cache key (3+ chars) returns null
        auto* entry3 = cache.lookup("abc");
        check(entry3 == nullptr, "ShortQueryCache: 3-char key returns null");

        // Non-lowercase returns null
        auto* entryUpper = cache.lookup("A");
        check(entryUpper == nullptr, "ShortQueryCache: uppercase key returns null");
    }

    // ── Test 2: Results are sorted by score ──
    std::cout << "\n  --- Score ordering ---\n";
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        records.push_back({"x", "/deep/nested/path/here", 1, 100, 1000});  // exact match, long path
        records.push_back({"x.txt", "/tmp", 1, 200, 2000});               // prefix match, short path
        records.push_back({"fox", "/bin", 1, 50, 3000});                   // substring match
        engine.loadRecords(std::move(records));
        engine.buildShortQueryCache();

        auto* entry = engine.getShortQueryCache().lookup("x");
        check(entry != nullptr, "ShortQueryCache: lookup 'x' non-null");
        check(entry->results.size() == 3, "ShortQueryCache: 'x' has 3 results");

        // "x" should be first (exact match), "x.txt" second (prefix), "fox" third (substring)
        check(engine.getRecord(entry->results[0]).name == "x",
              "ShortQueryCache: exact 'x' is first");
        check(engine.getRecord(entry->results[1]).name == "x.txt",
              "ShortQueryCache: prefix 'x.txt' is second");
        check(engine.getRecord(entry->results[2]).name == "fox",
              "ShortQueryCache: substring 'fox' is third");
    }

    // ── Test 3: markDeleted and needsRebuild ──
    std::cout << "\n  --- Invalidation ---\n";
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
        check(entry->deletedCount == 0, "ShortQueryCache: initial deletedCount=0");
        check(!cache.needsRebuild(), "ShortQueryCache: no rebuild needed initially");

        // Delete 6 records (>50% of 10)
        for (int i = 0; i < 6; i++) {
            std::string name = "q" + std::to_string(i) + ".dat";
            std::string lowerName = name; // already lowercase
            cache.markDeleted(i, lowerName.c_str(), static_cast<uint16_t>(lowerName.size()));
        }

        check(entry->deletedCount == 6, "ShortQueryCache: deletedCount=6 after marking");
        check(cache.needsRebuild(), "ShortQueryCache: rebuild needed after >50% deleted");
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

        std::string tmpPath = "/tmp/test_sqcache.bin";
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

    // ── Test 5: Query integration — cache is used for short queries ──
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
        check(timing.totalMs < 5.0,
              "ShortQueryCache: query time < 5ms");
    }

    std::cout << "\n";
}
