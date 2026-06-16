#pragma once
// ═══════════════════════════════════════════════════════
//  Part 3b: Path-based Search Tests
// ═══════════════════════════════════════════════════════

static void runPathSearchTests() {
    std::cout << "========================================\n";
    std::cout << "  Part 3b: Path-based Search Tests\n";
    std::cout << "========================================\n\n";

    SearchEngine engine;

    std::vector<FileRecord> records;
    records.push_back({"main.cpp", "/usr/local/src", 1, 100, 1000});
    records.push_back({"config.json", "/etc/myapp", 1, 200, 2000});
    records.push_back({"readme.md", "/home/user/projects", 1, 300, 3000});
    records.push_back({"libfoo.dylib", "/usr/local/lib", 1, 400, 4000});
    records.push_back({"include", "/usr/local", 2, 0, 5000});
    engine.loadRecords(std::move(records));

    // Plain keyword search by path component (no slash → PLAIN mode → path trigram)
    // "local" matches all 3 files under /usr/local/ via path trigram
    auto res = engine.query("local");
    check(res.size() == 3, "Path search 'local': 3 matches (path contains 'local')");

    // Slash query with node-centric semantics:
    // "/usr/local" → SEGMENTS: name contains "local", path contains "usr"
    // "include" (name) doesn't contain "local", "main.cpp" doesn't, "libfoo.dylib" doesn't
    // So 0 results — this is expected node-centric behavior.
    // To find files UNDER /usr/local, users search "local" (plain) not "/usr/local"
    res = engine.query("/usr/local");
    // Node-centric: name must contain "local" — no file names do
    check(res.size() == 0, "Path search '/usr/local': 0 (node-centric, no name contains 'local')");

    // "/etc/config" → name contains "config", path contains "etc"
    res = engine.query("/etc/config");
    check(res.size() == 1, "Path search '/etc/config': 1 match");
    check(engine.getRecord(res[0]).name == "config.json", "Path search '/etc/config': correct file");

    // Search by directory name only (plain, no slash)
    res = engine.query("projects");
    check(res.size() == 1, "Dir name search 'projects': 1 match");
    check(engine.getRecord(res[0]).name == "readme.md", "Dir name search 'projects': correct file");

    // Glob on path (still works for PLAIN mode glob patterns)
    res = engine.query("*/lib/*");
    check(res.size() == 1, "Glob '*/lib/*': 1 match");
    check(engine.getRecord(res[0]).name == "libfoo.dylib", "Glob '*/lib/*': correct file");

    // Path search after addRecord (plain search)
    engine.addRecord({"newlib.a", "/usr/local/lib", 1, 500, 6000});
    // "newlib" (plain) finds the new file
    res = engine.query("newlib");
    check(res.size() == 1, "After addRecord: 'newlib' returns 1 match");

    // Path search after removeByPath
    engine.removeByPath("/usr/local/lib/libfoo.dylib");
    res = engine.query("libfoo");
    check(res.size() == 0, "After removeByPath: 'libfoo' returns 0 match");

    // Path search after updateByPath
    engine.updateByPath("/usr/local/lib/newlib.a", {"newlib_v2.a", "/opt/lib", 1, 600, 7000});
    res = engine.query("newlib_v2");
    check(res.size() == 1, "After updateByPath: 'newlib_v2' returns 1 match");

    std::cout << "\n";
}

// ═══════════════════════════════════════════════════════
//  Part 3b-2: lowerPathPool_ / Dedup Path Matching Tests
// ═══════════════════════════════════════════════════════

static void runLowerPathPoolTests() {
    std::cout << "========================================\n";
    std::cout << "  Part 3b-2: lowerPathPool & Dedup Path Match\n";
    std::cout << "========================================\n\n";

    // --- 1. Case-insensitive path search via lowerPathPool_ ---
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        records.push_back({"App.swift", "/Users/Dev/MyProject/Sources", 1, 100, 1000});
        records.push_back({"main.cpp", "/Volumes/DATA/CppProject/src", 1, 200, 2000});
        records.push_back({"readme.txt", "/home/User/Documents", 1, 300, 3000});
        engine.loadRecords(std::move(records));

        // Plain keyword path search (no slash) should be case-insensitive
        auto res = engine.query("myproject");
        check(res.size() == 1, "lowerPathPool: 'myproject' matches '/Users/Dev/MyProject/Sources'");
        check(engine.getRecord(res[0]).name == "App.swift", "lowerPathPool: correct file for 'myproject'");

        res = engine.query("CPPPROJECT");
        check(res.size() == 1, "lowerPathPool: 'CPPPROJECT' matches path with mixed case");

        res = engine.query("documents");
        check(res.size() == 1, "lowerPathPool: 'documents' matches '/home/User/Documents'");

        // Short keyword (2 chars) triggers linear scan path
        res = engine.query("de");
        check(res.size() >= 1, "lowerPathPool: 2-char search 'de' finds path match");
    }

    // --- 2. addRecord preserves lowerPathPool_ invariant ---
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        records.push_back({"base.txt", "/root", 1, 100, 1000});
        engine.loadRecords(std::move(records));

        // Add record with mixed-case path
        engine.addRecord({"NewFile.py", "/Users/Admin/PyProject", 1, 200, 2000});

        auto res = engine.query("pyproject");
        check(res.size() == 1, "addRecord lowerPathPool: 'pyproject' matches after addRecord");
        check(engine.getRecord(res[0]).name == "NewFile.py", "addRecord lowerPathPool: correct file");

        res = engine.query("admin");
        check(res.size() == 1, "addRecord lowerPathPool: 'admin' matches path '/Users/Admin/...'");
    }

    // --- 3. updateByPath preserves lowerPathPool_ invariant ---
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        records.push_back({"old.txt", "/Data/OldDir", 1, 100, 1000});
        engine.loadRecords(std::move(records));

        engine.updateByPath("/Data/OldDir/old.txt", {"new.txt", "/Data/NewDir/SubFolder", 1, 200, 2000});

        auto res = engine.query("olddir");
        check(res.size() == 0, "updateByPath lowerPathPool: old path no longer matches");

        res = engine.query("subfolder");
        check(res.size() == 1, "updateByPath lowerPathPool: new path matches");
        check(engine.getRecord(res[0]).name == "new.txt", "updateByPath lowerPathPool: correct updated file");
    }

    // --- 4. compaction preserves lowerPathPool_ and path search ---
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        records.push_back({"a.txt", "/Path/Alpha", 1, 100, 1000});
        records.push_back({"b.txt", "/Path/Beta", 1, 200, 2000});
        records.push_back({"c.txt", "/Path/Gamma", 1, 300, 3000});
        engine.loadRecords(std::move(records));

        // Remove middle record to create tombstone
        engine.removeByPath("/Path/Beta/b.txt");
        check(engine.liveRecordCount() == 2, "compaction setup: 2 live records");

        // Compact
        auto mapping = engine.compactRecords();
        check(!mapping.empty(), "compaction occurred");
        check(engine.liveRecordCount() == 2, "compaction: still 2 live records");

        // Path search still works after compaction with case-insensitive match
        auto res = engine.query("alpha");
        check(res.size() == 1, "post-compaction lowerPathPool: 'alpha' matches");
        check(engine.getRecord(res[0]).name == "a.txt", "post-compaction: correct file for 'alpha'");

        res = engine.query("gamma");
        check(res.size() == 1, "post-compaction lowerPathPool: 'gamma' matches");

        res = engine.query("beta");
        check(res.size() == 0, "post-compaction: removed path 'beta' no longer matches");
    }

    // --- 5. Slash-boundary query with node-centric semantics ---
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        // Path ends with "foo", name starts with "bar"
        records.push_back({"bar_file.txt", "/data/foo", 1, 100, 1000});
        records.push_back({"other.txt", "/data/baz", 1, 200, 2000});
        engine.loadRecords(std::move(records));

        // "foo/bar" → node-centric: name contains "bar", path contains "foo"
        auto res = engine.query("foo/bar");
        check(res.size() == 1, "slash-boundary: 'foo/bar' matches bar_file.txt (name has 'bar', path has 'foo')");
        check(engine.getRecord(res[0]).name == "bar_file.txt", "slash-boundary: correct file");

        // Another test: "settings/config"
        SearchEngine engine2;
        std::vector<FileRecord> records2;
        records2.push_back({"config.yml", "/app/Settings", 1, 100, 1000});
        records2.push_back({"data.json", "/app/Cache", 1, 200, 2000});
        engine2.loadRecords(std::move(records2));

        // "settings/config" → name contains "config", path contains "settings"
        res = engine2.query("settings/config");
        check(res.size() == 1, "slash-boundary: 'settings/config' matches config.yml");
    }

    // --- 6. resolveRecordPath returns original casing (not lowered) ---
    {
        SearchEngine engine;
        std::vector<FileRecord> records;
        records.push_back({"Test.cpp", "/Users/Dev/MyProject", 1, 100, 1000});
        engine.loadRecords(std::move(records));

        std::string path = engine.resolveRecordPath(0);
        check(path == "/Users/Dev/MyProject", "resolveRecordPath returns original casing, not lowered");
    }

    // --- 7. App bundle display name is searchable without changing filesystem name ---
    {
        auto tmpDir = fs::temp_directory_path() / ("me_app_display_name_" + std::to_string(getpid()));
        fs::remove_all(tmpDir);
        fs::create_directories(tmpDir / "WebPomodoro.app" / "Contents");
        std::ofstream plist(tmpDir / "WebPomodoro.app" / "Contents" / "Info.plist");
        plist << R"(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDisplayName</key>
    <string>Focus To-Do</string>
    <key>CFBundleName</key>
    <string>Focus To-Do</string>
</dict>
</plist>
)";
        plist.close();

        SearchEngine engine;
        std::vector<FileRecord> records;
        records.push_back({"WebPomodoro.app", tmpDir.string(), 5, 0, 1000});
        engine.loadRecords(std::move(records));

        auto fsNameResults = engine.query("WebPomodoro");
        check(fsNameResults.size() == 1, "app display-name: filesystem bundle name still matches");
        check(engine.getRecord(fsNameResults[0]).name == "WebPomodoro.app",
              "app display-name: filesystem name preserved in result");

        auto displayNameResults = engine.query("Focus To-Do");
        check(displayNameResults.size() == 1, "app display-name: Finder display name matches");
        if (!displayNameResults.empty()) {
            check(engine.getRecord(displayNameResults[0]).name == "WebPomodoro.app",
                  "app display-name: display-name match returns original bundle name");
        }

        auto shortAliasResults = engine.query("fo");
        check(shortAliasResults.size() == 1, "app display-name: short-query cache matches display alias");

        auto globAliasResults = engine.query("*focus*");
        check(globAliasResults.size() == 1, "app display-name: glob matches display alias");

        auto prefixGlobAliasResults = engine.query("focus*");
        check(prefixGlobAliasResults.size() == 1, "app display-name: prefix glob matches display alias");

        auto crossAliasPhraseResults = engine.query("\"app focus\"");
        check(crossAliasPhraseResults.empty(), "app display-name: quoted phrase does not cross alias boundary");

        auto regexAliasResults = engine.query("regex:focus");
        check(regexAliasResults.size() == 1, "app display-name: regex matches display alias");

        auto caseAliasResults = engine.query("case:Focus");
        check(caseAliasResults.size() == 1, "app display-name: case-sensitive query matches original display alias");

        auto wholeWordAliasResults = engine.query("ww:focus");
        check(wholeWordAliasResults.size() == 1, "app display-name: whole-word matches display alias");

        auto partialWholeFilenameAliasResults = engine.query("wfn:focus");
        check(partialWholeFilenameAliasResults.empty(), "app display-name: whole-filename does not partial-match display alias");

        auto structuredAliasResults = engine.query(tmpDir.filename().string() + "/focus");
        check(structuredAliasResults.size() == 1, "app display-name: structured path query matches display alias");

        fs::create_directories(tmpDir / "BadMetadata.app" / "Contents");
        std::ofstream badPlist(tmpDir / "BadMetadata.app" / "Contents" / "Info.plist");
        badPlist << R"(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDisplayName</key>
    <integer>42</integer>
</dict>
</plist>
)";
        badPlist.close();

        SearchEngine malformedEngine;
        std::vector<FileRecord> malformedRecords;
        malformedRecords.push_back({"BadMetadata.app", tmpDir.string(), 5, 0, 1000});
        malformedEngine.loadRecords(std::move(malformedRecords));
        auto malformedResults = malformedEngine.query("BadMetadata");
        check(malformedResults.size() == 1, "app display-name: malformed plist keeps filesystem search working");
        auto ignoredBadAliasResults = malformedEngine.query("424242424242");
        check(ignoredBadAliasResults.empty(), "app display-name: non-string display metadata is ignored");

        fs::create_directories(tmpDir / "NameOnly.app" / "Contents");
        std::ofstream nameOnlyPlist(tmpDir / "NameOnly.app" / "Contents" / "Info.plist");
        nameOnlyPlist << R"(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleName</key>
    <string>Name Only Display</string>
</dict>
</plist>
)";
        nameOnlyPlist.close();

        SearchEngine nameOnlyEngine;
        std::vector<FileRecord> nameOnlyRecords;
        nameOnlyRecords.push_back({"NameOnly.app", tmpDir.string(), 5, 0, 1000});
        nameOnlyEngine.loadRecords(std::move(nameOnlyRecords));
        auto nameOnlyResults = nameOnlyEngine.query("Name Only Display");
        check(nameOnlyResults.size() == 1, "app display-name: CFBundleName fallback matches when display name is absent");

        fs::create_directories(tmpDir / "Localized.app" / "Contents" / "Resources" / "fr.lproj");
        std::ofstream localizedPlist(tmpDir / "Localized.app" / "Contents" / "Info.plist");
        localizedPlist << R"(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleDisplayName</key>
    <string>Base Localized Name</string>
</dict>
</plist>
)";
        localizedPlist.close();
        std::ofstream localizedStrings(tmpDir / "Localized.app" / "Contents" / "Resources" / "fr.lproj" / "InfoPlist.strings");
        localizedStrings << R"("CFBundleDisplayName" = "Nom Localisé";
)";
        localizedStrings.close();

        SearchEngine localizedEngine;
        std::vector<FileRecord> localizedRecords;
        localizedRecords.push_back({"Localized.app", tmpDir.string(), 5, 0, 1000});
        localizedEngine.loadRecords(std::move(localizedRecords));
        auto localizedResults = localizedEngine.query("Nom Localisé");
        check(localizedResults.size() == 1, "app display-name: enumerated localized InfoPlist.strings display name matches");

        fs::create_directories(tmpDir / "PhaseTwoDir");
        SearchEngine persistedSource;
        std::vector<FileRecord> persistedRecords;
        persistedRecords.push_back({"WebPomodoro.app", tmpDir.string(), 5, 0, 1000});
        persistedRecords.push_back({"DeletedSnapshot.app", (tmpDir / "PhaseTwoDir").string(), 5, 0, 100000});
        for (int i = 0; i < 201; i++) {
            persistedRecords.push_back({"RecentBackfill" + std::to_string(i) + ".dat", tmpDir.string(), 1, 1, 90000 - i});
        }
        for (int i = 0; i < 20000; i++) {
            persistedRecords.push_back({"PhaseTwoLoad" + std::to_string(i) + ".dat", tmpDir.string(), 1, 1, 1000 + i});
        }
        persistedSource.loadRecords(std::move(persistedRecords));
        auto snap = persistedSource.snapshotForV6();

        SearchEngine persistedEngine;
        persistedEngine.loadRecordsV6(std::move(snap.origNamePool), std::move(snap.namePool), std::move(snap.pathIndices),
                                      std::move(snap.pathPool), std::move(snap.lowerPathPool), std::move(snap.types),
                                      std::move(snap.sizes), std::move(snap.modTimes), std::move(snap.inodes), std::move(snap.devIds));
        auto deferredAliasBeforePhase2 = persistedEngine.query("Focus To-Do");
        check(deferredAliasBeforePhase2.empty(), "app display-name: v6 startup defers display alias filesystem reads before Phase 2");
        static std::mutex phase2HookMutex;
        static std::condition_variable phase2SnapshotReady;
        static std::condition_variable phase2MayContinue;
        static bool phase2SnapshotReached = false;
        static bool phase2Continue = false;
        phase2SnapshotReached = false;
        phase2Continue = false;
        SearchEngine::setPhase2PostSnapshotHook([] {
            std::unique_lock<std::mutex> lock(phase2HookMutex);
            phase2SnapshotReached = true;
            phase2SnapshotReady.notify_one();
            phase2MayContinue.wait(lock, [] { return phase2Continue; });
        });
        std::thread phase2Thread([&] {
            persistedEngine.completePhase2();
        });
        {
            std::unique_lock<std::mutex> lock(phase2HookMutex);
            phase2SnapshotReady.wait(lock, [] { return phase2SnapshotReached; });
        }
        persistedEngine.removeByPath((tmpDir / "PhaseTwoDir" / "DeletedSnapshot.app").string());
        persistedEngine.addRecord({"DeletedDuringPhase2.app", tmpDir.string(), 5, 0, 2000});
        persistedEngine.removeByPath((tmpDir / "DeletedDuringPhase2.app").string());
        fs::create_directories(tmpDir / "NewPhaseTwoPath");
        persistedEngine.addRecord({"LiveDuringPhase2.app", (tmpDir / "NewPhaseTwoPath").string(), 5, 0, 3000});
        {
            std::lock_guard<std::mutex> lock(phase2HookMutex);
            phase2Continue = true;
        }
        phase2MayContinue.notify_one();
        phase2Thread.join();
        SearchEngine::setPhase2PostSnapshotHook(nullptr);
        auto deferredAliasAfterPhase2 = persistedEngine.query("Focus To-Do");
        check(deferredAliasAfterPhase2.size() == 1, "app display-name: v6 Phase 2 rebuilds display alias search data");
        auto replayGapResults = persistedEngine.query("LiveDuringPhase2");
        check(replayGapResults.size() == 1, "app display-name: v6 Phase 2 replay keeps searchable-name pool aligned across deleted additions");
        QueryTimingInfo newPathTiming;
        auto replayNewPathResults = persistedEngine.queryAdvanced("newphasetwopath/LiveDuringPhase2", 100, true, newPathTiming);
        check(replayNewPathResults.size() == 1, "app display-name: v6 Phase 2 replay restores path trigram index for added paths");
        auto recentAfterPhase2 = persistedEngine.recentIndices(200);
        bool recentIncludesDeletedSnapshot = false;
        bool recentIncludesBackfilledRecord = false;
        for (uint32_t idx : recentAfterPhase2) {
            auto rec = persistedEngine.getRecord(idx);
            if (rec.name == "DeletedSnapshot.app") recentIncludesDeletedSnapshot = true;
            if (rec.name == "RecentBackfill1.dat") recentIncludesBackfilledRecord = true;
        }
        check(recentAfterPhase2.size() == 200 && !recentIncludesDeletedSnapshot,
              "app display-name: v6 Phase 2 tombstone replay removes stale recent-cache entries");
        check(recentIncludesBackfilledRecord,
              "app display-name: v6 Phase 2 tombstone replay backfills bounded recent cache");
        QueryTimingInfo extTiming;
        auto extAfterPhase2 = persistedEngine.queryAdvanced("ext:app", 100, true, extTiming);
        check(extAfterPhase2.size() == 2, "app display-name: v6 Phase 2 tombstone replay removes stale extension-index results");
        check(extTiming.candidates == 2, "app display-name: v6 Phase 2 tombstone replay removes stale extension-index candidates");
        QueryTimingInfo pathTiming;
        auto pathAfterPhase2 = persistedEngine.queryAdvanced("phasetwodir", 100, true, pathTiming);
        check(pathAfterPhase2.empty(), "app display-name: v6 Phase 2 tombstone replay removes stale path-index results");
        check(pathTiming.candidates == 0, "app display-name: v6 Phase 2 tombstone replay removes stale path-index candidates");

        fs::remove_all(tmpDir);
    }

    // --- 8. Structured query ranking scores display aliases independently ---
    {
        auto tmpDir = fs::temp_directory_path() / ("me_app_display_rank_" + std::to_string(getpid()));
        fs::remove_all(tmpDir);
        fs::create_directories(tmpDir / "WebPomodoro.app" / "Contents");
        std::ofstream plist(tmpDir / "WebPomodoro.app" / "Contents" / "Info.plist");
        plist << R"(<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict><key>CFBundleDisplayName</key><string>focus</string></dict></plist>
)";
        plist.close();

        SearchEngine engine;
        std::vector<FileRecord> records;
        records.push_back({"focus-file.txt", tmpDir.string(), 1, 1, 1000});
        records.push_back({"WebPomodoro.app", tmpDir.string(), 5, 0, 2000});
        engine.loadRecords(std::move(records));

        auto rankedResults = engine.query(tmpDir.filename().string() + "/focus", 1);
        check(rankedResults.size() == 1, "app display-name: structured ranking limited query returns one result");
        if (!rankedResults.empty()) {
            check(engine.getRecord(rankedResults[0]).name == "WebPomodoro.app",
                  "app display-name: structured exact display alias outranks canonical prefix");
        }

        fs::remove_all(tmpDir);
    }

    std::cout << "\n";
}
