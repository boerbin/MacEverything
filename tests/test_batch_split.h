#pragma once
// ═══════════════════════════════════════════════════════
//  Part 77: batchMutate Lock Split Tests
// ═══════════════════════════════════════════════════════

static void runBatchSplitTests() {
    std::cout << "========================================\n";
    std::cout << "  Part 77: batchMutate Lock Split Tests\n";
    std::cout << "========================================\n\n";

    // Test 1: Empty ops — no deadlock, no crash
    {
        std::cout << "  --- Test 1: empty ops ---\n";
        SearchEngine engine;
        std::vector<SearchEngine::MutationOp> ops;
        engine.batchMutate(std::move(ops));
        check(engine.liveRecordCount() == 0, "empty ops: liveRecordCount == 0");
    }

    // Test 2: Small batch (<300) — all ops applied correctly
    {
        std::cout << "  --- Test 2: small batch ---\n";
        SearchEngine engine;
        std::vector<FileRecord> initial;
        initial.push_back({"a.txt", "/tmp", 1, 100, 1000});
        initial.push_back({"b.txt", "/tmp", 1, 200, 2000});
        engine.loadRecords(std::move(initial));
        check(engine.liveRecordCount() == 2, "small batch: initial live == 2");

        std::vector<SearchEngine::MutationOp> ops;
        // Add 50 new records
        for (int i = 0; i < 50; ++i) {
            SearchEngine::MutationOp op;
            op.type = SearchEngine::MutationOp::UPDATE;
            op.path = "/tmp/new_" + std::to_string(i) + ".txt";
            op.record = {"new_" + std::to_string(i) + ".txt", "/tmp", 1, (uint64_t)(i * 10), (int64_t)(5000 + i)};
            ops.push_back(std::move(op));
        }
        engine.batchMutate(std::move(ops));
        check(engine.liveRecordCount() == 52, "small batch: live == 52 after 50 adds");

        auto res = engine.query("new_25");
        check(res.size() == 1, "small batch: can query new_25");
    }

    // Test 3: Large batch (>300) — all ops applied, verifies chunking works
    {
        std::cout << "  --- Test 3: large batch ---\n";
        SearchEngine engine;

        std::vector<SearchEngine::MutationOp> ops;
        for (int i = 0; i < 1000; ++i) {
            SearchEngine::MutationOp op;
            op.type = SearchEngine::MutationOp::UPDATE;
            op.path = "/data/file_" + std::to_string(i) + ".dat";
            op.record = {"file_" + std::to_string(i) + ".dat", "/data", 1, (uint64_t)i, (int64_t)(1000 + i)};
            ops.push_back(std::move(op));
        }
        engine.batchMutate(std::move(ops));
        check(engine.liveRecordCount() == 1000, "large batch: live == 1000");

        auto res = engine.query("file_999");
        check(res.size() == 1, "large batch: can query file_999");
        res = engine.query("file_0");
        check(res.size() == 1, "large batch: can query file_0");
        res = engine.query("file_500");
        check(res.size() == 1, "large batch: can query file_500");
    }

    // Test 4: Concurrent — batchMutate(1000 ops) must not block shared_lock query >100ms
    {
        std::cout << "  --- Test 4: concurrent lock yielding ---\n";
        SearchEngine engine;

        // Pre-populate so queries have something to scan
        std::vector<FileRecord> initial;
        for (int i = 0; i < 100; ++i) {
            initial.push_back({"exist_" + std::to_string(i) + ".txt", "/pre", 1, (uint64_t)i, (int64_t)i});
        }
        engine.loadRecords(std::move(initial));

        std::atomic<bool> mutationStarted{false};
        std::atomic<bool> mutationDone{false};
        std::atomic<int64_t> queryTimeMs{-1};

        // Writer thread: batchMutate with 2000 ops (will span multiple chunks)
        std::thread writer([&]() {
            std::vector<SearchEngine::MutationOp> ops;
            for (int i = 0; i < 2000; ++i) {
                SearchEngine::MutationOp op;
                op.type = SearchEngine::MutationOp::UPDATE;
                op.path = "/concurrent/f_" + std::to_string(i) + ".txt";
                op.record = {"f_" + std::to_string(i) + ".txt", "/concurrent", 1, (uint64_t)i, (int64_t)(9000 + i)};
                ops.push_back(std::move(op));
            }
            mutationStarted.store(true);
            engine.batchMutate(std::move(ops));
            mutationDone.store(true);
        });

        // Wait for mutation to start
        while (!mutationStarted.load()) {
            std::this_thread::sleep_for(std::chrono::microseconds(100));
        }
        // Small delay to let writer acquire first chunk lock
        std::this_thread::sleep_for(std::chrono::milliseconds(1));

        // Reader thread: query should complete within 100ms if lock is yielded between chunks
        auto t0 = std::chrono::steady_clock::now();
        auto res = engine.query("exist_50");
        auto t1 = std::chrono::steady_clock::now();
        queryTimeMs.store(std::chrono::duration_cast<std::chrono::milliseconds>(t1 - t0).count());

        writer.join();

        int64_t elapsed = queryTimeMs.load();
        // With chunking (300 ops, ~0.01-0.05ms/op), max single chunk hold ≈ 3-15ms.
        // Without chunking, 2000 ops could hold lock 20-100ms.
        // Use 100ms as generous upper bound.
        check(elapsed < 100, ("concurrent: query completed in " + std::to_string(elapsed) + "ms (<100ms)").c_str());
        check(mutationDone.load(), "concurrent: mutation completed");
        check(engine.liveRecordCount() == 2100, "concurrent: all 2100 records live");
    }

    // Test 5: Mixed REMOVE + UPDATE in large batch
    {
        std::cout << "  --- Test 5: mixed REMOVE + UPDATE ---\n";
        SearchEngine engine;

        // Pre-populate 500 records
        std::vector<FileRecord> initial;
        for (int i = 0; i < 500; ++i) {
            initial.push_back({"item_" + std::to_string(i) + ".txt", "/mix", 1, (uint64_t)(i * 10), (int64_t)(2000 + i)});
        }
        engine.loadRecords(std::move(initial));
        check(engine.liveRecordCount() == 500, "mixed: initial live == 500");

        // Build batch: remove first 200, update next 100 (rename), add 300 new
        std::vector<SearchEngine::MutationOp> ops;

        // Remove 200
        for (int i = 0; i < 200; ++i) {
            SearchEngine::MutationOp op;
            op.type = SearchEngine::MutationOp::REMOVE;
            op.path = "/mix/item_" + std::to_string(i) + ".txt";
            ops.push_back(std::move(op));
        }

        // Update 100 (items 200-299 get new size)
        for (int i = 200; i < 300; ++i) {
            SearchEngine::MutationOp op;
            op.type = SearchEngine::MutationOp::UPDATE;
            op.path = "/mix/item_" + std::to_string(i) + ".txt";
            op.record = {"item_" + std::to_string(i) + ".txt", "/mix", 1, (uint64_t)(i * 100), (int64_t)(2000 + i)};
            ops.push_back(std::move(op));
        }

        // Add 300 new
        for (int i = 0; i < 300; ++i) {
            SearchEngine::MutationOp op;
            op.type = SearchEngine::MutationOp::UPDATE;
            op.path = "/mix/brand_new_" + std::to_string(i) + ".txt";
            op.record = {"brand_new_" + std::to_string(i) + ".txt", "/mix", 1, (uint64_t)(i * 5), (int64_t)(8000 + i)};
            ops.push_back(std::move(op));
        }

        check(ops.size() == 600, "mixed: batch has 600 ops");
        engine.batchMutate(std::move(ops));

        // Expected: 500 - 200 + 300 = 600 live
        check(engine.liveRecordCount() == 600, "mixed: live == 600 (500 - 200 removed + 300 new)");

        // Verify removed records not queryable
        auto res = engine.query("item_0");
        check(res.empty(), "mixed: item_0 removed");
        res = engine.query("item_199");
        check(res.empty(), "mixed: item_199 removed");

        // Verify surviving records
        res = engine.query("item_300");
        check(res.size() == 1, "mixed: item_300 survives");

        // Verify new records
        res = engine.query("brand_new_0");
        check(res.size() == 1, "mixed: brand_new_0 added");
        res = engine.query("brand_new_299");
        check(res.size() == 1, "mixed: brand_new_299 added");
    }

    std::cout << "\n";
}
