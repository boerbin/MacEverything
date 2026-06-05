/// Tests for input lag fixes (P0-P3) — verifies that the typing guard correctly
/// suppresses index refreshes during active typing and defers them to cooldown.
/// Compile & run:  swiftc -o test_input_lag MacEverything/App/IndexRefreshThrottle.swift tests/test_input_lag.swift && ./test_input_lag

import Foundation

// ---------------------------------------------------------------------------
// Minimal test harness
// ---------------------------------------------------------------------------

var testsPassed = 0
var testsFailed = 0

func check(_ condition: Bool, _ msg: String, file: String = #file, line: Int = #line) {
    if condition {
        testsPassed += 1
    } else {
        testsFailed += 1
        print("  FAIL [\(file):\(line)] \(msg)")
    }
}

func assertEqual<T: Equatable>(_ a: T, _ b: T, _ msg: String = "", file: String = #file, line: Int = #line) {
    if a == b {
        testsPassed += 1
    } else {
        testsFailed += 1
        print("  FAIL [\(file):\(line)] expected \(b), got \(a). \(msg)")
    }
}

// ---------------------------------------------------------------------------
// TypingGuardSimulator — replicates SearchViewModel.onIndexChanged() logic
// ---------------------------------------------------------------------------

struct TypingGuardSimulator {
    let throttle = IndexRefreshThrottle()
    var lastKeystrokeTime: Date = .distantPast
    let typingGuardInterval: TimeInterval = 0.5
    var cooldownScheduled = false

    mutating func keystroke() {
        lastKeystrokeTime = Date()
    }

    mutating func keystrokeAt(_ time: Date) {
        lastKeystrokeTime = time
    }

    mutating func indexChanged() {
        if Date().timeIntervalSince(lastKeystrokeTime) < typingGuardInterval {
            throttle.markPending()
            if !cooldownScheduled {
                cooldownScheduled = true
            }
            return
        }
        if throttle.indexChanged() {
            cooldownScheduled = true
        }
    }

    mutating func cooldownExpired() {
        cooldownScheduled = false
        if throttle.cooldownExpired() {
            cooldownScheduled = true
        }
    }
}

// ---------------------------------------------------------------------------
// Tests
// ---------------------------------------------------------------------------

func testTypingGuardSuppressesRefresh() {
    print("  test: typing guard suppresses index refresh during active typing")
    var sim = TypingGuardSimulator()
    sim.keystroke()
    sim.indexChanged()
    assertEqual(sim.throttle.refreshCount, 0, "no refresh during typing")
    check(sim.throttle.isPending, "change marked as pending")
    check(sim.cooldownScheduled, "cooldown scheduled for deferred delivery")
}

func testDeferredRefreshAfterTypingStops() {
    print("  test: deferred refresh fires on cooldown after typing stops")
    var sim = TypingGuardSimulator()
    sim.keystroke()
    sim.indexChanged()
    assertEqual(sim.throttle.refreshCount, 0, "suppressed during typing")

    sim.cooldownExpired()
    assertEqual(sim.throttle.refreshCount, 1, "deferred refresh fires on cooldown")
    check(!sim.throttle.isPending, "pending cleared after refresh")
}

func testNoTypingAllowsNormalRefresh() {
    print("  test: without recent typing, indexChanged triggers immediate refresh")
    var sim = TypingGuardSimulator()
    sim.indexChanged()
    assertEqual(sim.throttle.refreshCount, 1, "immediate refresh")
    check(sim.throttle.isCooldownActive, "cooldown started")
    check(!sim.throttle.isPending, "nothing pending")
}

func testRapidTypingWithMultipleFSEvents() {
    print("  test: rapid typing with multiple FSEvents — all suppressed, single catch-up")
    var sim = TypingGuardSimulator()

    for _ in 0..<5 {
        sim.keystroke()
        sim.indexChanged()
    }

    assertEqual(sim.throttle.refreshCount, 0, "all suppressed during typing")
    check(sim.throttle.isPending, "pending from suppressed events")

    sim.cooldownExpired()
    assertEqual(sim.throttle.refreshCount, 1, "single coalesced catch-up refresh")
}

func testTypingGuardBoundary() {
    print("  test: at exactly the guard interval boundary, refresh is allowed")
    var sim = TypingGuardSimulator()
    sim.keystrokeAt(Date().addingTimeInterval(-0.5))
    sim.indexChanged()
    assertEqual(sim.throttle.refreshCount, 1, "refresh allowed at boundary (>= 0.5s)")
}

func testFullContentionScenario() {
    print("  test: full contention — type rapidly, FSEvents fire, pause, catch-up, then normal")
    var sim = TypingGuardSimulator()

    // Phase 1: rapid typing with FSEvents
    for _ in 0..<5 {
        sim.keystroke()
        sim.indexChanged()
    }
    assertEqual(sim.throttle.refreshCount, 0, "zero refreshes during typing")

    // Phase 2: user stops typing, cooldown fires
    sim.cooldownExpired()
    assertEqual(sim.throttle.refreshCount, 1, "single catch-up refresh")

    // Phase 3: cooldown from catch-up refresh expires, then normal FSEvent
    sim.lastKeystrokeTime = .distantPast
    sim.cooldownExpired()          // expire the cooldown started by catch-up refresh
    assertEqual(sim.throttle.refreshCount, 1, "no extra refresh — nothing pending")
    sim.indexChanged()
    assertEqual(sim.throttle.refreshCount, 2, "normal refresh after typing stopped")
}

func testMarkPendingCoalescesMultiple() {
    print("  test: markPending called multiple times coalesces to single refresh")
    let throttle = IndexRefreshThrottle()
    for _ in 0..<10 {
        throttle.markPending()
    }
    check(throttle.isPending, "pending is set")
    assertEqual(throttle.refreshCount, 0, "no refresh from markPending alone")

    let fired = throttle.cooldownExpired()
    check(fired, "trailing refresh should fire")
    assertEqual(throttle.refreshCount, 1, "exactly one coalesced refresh")
}

func testCooldownWithoutPendingNoRefresh() {
    print("  test: cooldown expiry without pending does not trigger refresh")
    var sim = TypingGuardSimulator()
    sim.indexChanged()
    assertEqual(sim.throttle.refreshCount, 1, "initial refresh")

    sim.cooldownExpired()
    assertEqual(sim.throttle.refreshCount, 1, "no extra refresh without pending")
    check(!sim.cooldownScheduled, "no new cooldown scheduled")
}

// ---------------------------------------------------------------------------
// Entry point
// ---------------------------------------------------------------------------

@main
struct TestRunner {
    static func main() {
        print("Running input lag fix verification tests...")
        testTypingGuardSuppressesRefresh()
        testDeferredRefreshAfterTypingStops()
        testNoTypingAllowsNormalRefresh()
        testRapidTypingWithMultipleFSEvents()
        testTypingGuardBoundary()
        testFullContentionScenario()
        testMarkPendingCoalescesMultiple()
        testCooldownWithoutPendingNoRefresh()

        print("\nResults: \(testsPassed) passed, \(testsFailed) failed")
        if testsFailed > 0 {
            exit(1)
        } else {
            print("All tests passed!")
            exit(0)
        }
    }
}
