#pragma once
// ═══════════════════════════════════════════════════════
//  Volume tracking tests
// ═══════════════════════════════════════════════════════

#include "../MacEverything/Core/VolumeIndex.h"
#include <iostream>

static void runVolumeIndexTests() {
    std::cout << "========================================\n";
    std::cout << "  Volume Index Tests\n";
    std::cout << "========================================\n\n";

    VolumeIndex v;

    // ── addVolume is idempotent ──
    {
        uint32_t a = v.addVolume("/Volumes/A");
        uint32_t b = v.addVolume("/Volumes/A");
        check(a == b, "addVolume is idempotent");
        check(v.volumeCount() == 1, "single volume registered");
    }

    // ── markOnline / markOffline ──
    {
        v.addVolume("/Volumes/B");
        check(!v.isVolumeOffline("/Volumes/B"), "B starts online");
        v.markOffline("/Volumes/B");
        check(v.isVolumeOffline("/Volumes/B"), "B is offline after markOffline");
        v.markOnline("/Volumes/B");
        check(!v.isVolumeOffline("/Volumes/B"), "B is online after markOnline");
    }

    // ── offlineVolumePaths persistence shape ──
    {
        v.markOnlineAll();
        v.markOffline("/Volumes/A");
        v.markOffline("/Volumes/C");
        v.addVolume("/Volumes/C");
        auto offs = v.offlineVolumePaths();
        check(offs.size() == 2, "two offline volumes");
        // Order is implementation-defined (hash map); check membership.
        bool hasA = false, hasC = false;
        for (const auto& p : offs) {
            if (p == "/Volumes/A") hasA = true;
            if (p == "/Volumes/C") hasC = true;
        }
        check(hasA && hasC, "offline set contains A and C");
    }

    // ── loadOfflineVolumes: persists offline state across "restart" ──
    {
        VolumeIndex fresh;
        // Simulate restart with a fresh index: only persisted state survives.
        std::vector<std::string> persisted = {"/Volumes/A", "/Volumes/C", "/Volumes/Missing"};
        fresh.loadOfflineVolumes(persisted);

        // /Volumes/Missing was NOT registered — loadOfflineVolumes auto-registers it.
        check(fresh.volumeCount() == 3, "missing volume auto-registered");
        check(fresh.isVolumeOffline("/Volumes/A"), "A still offline after restart");
        check(fresh.isVolumeOffline("/Volumes/C"), "C still offline after restart");
        check(fresh.isVolumeOffline("/Volumes/Missing"), "missing volume kept offline");
    }

    // ── isRecordOffline: longest-prefix match ──
    {
        v.addVolume("/Volumes/System");
        v.markOffline("/Volumes/System");

        // Record under offline volume
        check(v.isRecordOffline("/Volumes/System/foo.txt"),
              "record under offline volume is offline");

        // Record at exact volume root (the volume itself)
        check(v.isRecordOffline("/Volumes/System"),
              "record at volume root is offline");

        // Record NOT under any volume → root volume, always online
        check(!v.isRecordOffline("/Users/me/foo.txt"),
              "record on root volume is online");

        // Boundary: /Volumes/System2 must NOT be considered under /Volumes/System
        v.markOnline("/Volumes/System");
        v.addVolume("/Volumes/System2");
        v.markOffline("/Volumes/System2");
        check(!v.isRecordOffline("/Volumes/System"),
              "/Volumes/System is online again");
        check(v.isRecordOffline("/Volumes/System2"),
              "/Volumes/System2 offline under prefix");

        // Longer prefix wins when both would match
        v.markOnlineAll();
        v.addVolume("/Volumes");
        v.addVolume("/Volumes/USB");
        v.markOffline("/Volumes/USB");
        // "/Volumes/USB/foo" should match /Volumes/USB (longer) not /Volumes.
        check(v.isRecordOffline("/Volumes/USB/foo"),
              "longest prefix wins for offline lookup");
        check(!v.isRecordOffline("/Volumes/Other/bar"),
              "other volume under /Volumes/USB's parent: NOT offline");
    }

    // ── removeVolume ──
    {
        VolumeIndex v2;
        v2.addVolume("/tmp/test-vol");
        v2.markOffline("/tmp/test-vol");
        check(v2.isVolumeOffline("/tmp/test-vol"), "offline before remove");
        v2.removeVolume("/tmp/test-vol");
        check(!v2.isVolumeOffline("/tmp/test-vol"), "offline cleared after remove");
        check(v2.volumeCount() == 0, "volume count decremented");
    }

    // ── hasOfflineVolumes ──
    {
        VolumeIndex v3;
        check(!v3.hasOfflineVolumes(), "no offline on empty index");
        v3.addVolume("/v1");
        check(!v3.hasOfflineVolumes(), "no offline on online volume");
        v3.markOffline("/v1");
        check(v3.hasOfflineVolumes(), "hasOffline when any volume offline");
        v3.markOnline("/v1");
        check(!v3.hasOfflineVolumes(), "no offline after markOnline");
    }

    std::cout << "  Volume Index: all passed\n\n";
}
