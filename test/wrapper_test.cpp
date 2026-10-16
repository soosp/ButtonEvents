// wrapper_test.cpp — host tests for the shipping ButtonEvents.h wrapper:
// reader, callback, take() latches, overrun count, and a two-thread run.
// Built without ARDUINO, so the ReadFn constructor and the *(…, nowMs)
// overloads are used. Case ids refer to TESTS.md. Build:
//   g++ -std=c++17 -O0 -Wall -pthread -I../src wrapper_test.cpp -o wrapper_test

#include "ButtonEvents.h"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <string>
#include <thread>

using Event     = ButtonEvents::Event;
using EventData = ButtonEvents::EventData;
using Timing    = ButtonEvents::Timing;

static int g_fail = 0;
static int g_pass = 0;

static void check(bool ok, const char* id, const std::string& what) {
    if (ok) {
        g_pass++;
    } else {
        g_fail++;
        std::printf("  FAIL %s: %s\n", id, what.c_str());
    }
}

static std::string tag(Event e, const EventData& d) {
    switch (e) {
        case Event::PRESSED:  return "P";
        case Event::RELEASED: return "R" + std::to_string(d.heldMs);
        case Event::HOLDING:  return "H" + std::to_string(d.heldMs);
        case Event::SEQUENCE: return "S" + std::to_string(d.count);
    }
    return "?";
}

// ---- a button driven by a simulated level and clock -------------------------
struct Rig {
    std::atomic<bool> level{false};
    uint32_t          now = 1000;
    ButtonEvents      button;

    static bool read(void* ctx) { return static_cast<Rig*>(ctx)->level.load(); }

    explicit Rig(const Timing& t = Timing()) : button(read, this, t) {}

    void run(uint32_t ms) {
        for (uint32_t i = 0; i < ms; i += 10) { now += 10; button.update(now); }
    }
    void hold(uint32_t ms) { level = true; run(ms); level = false; }
};

static std::string g_log;
static bool        g_takeOk = true;

static void logCb(Event e, const EventData& d, ButtonEvents&) {
    if (!g_log.empty()) g_log += ' ';
    g_log += tag(e, d);
}

// Takes the event just delivered (its slot is filled before the callback runs)
// and reads the getters, from inside the callback.
static void takingCb(Event e, const EventData& d, ButtonEvents& b) {
    EventData got{};
    const bool ok = b.take(e, got);
    if (!ok || got.heldMs != d.heldMs || got.count != d.count) g_takeOk = false;
    (void)b.isPressed();
    (void)b.busy();
    (void)b.overruns();
    logCb(e, d, b);
}

static void testBegin() {
    std::printf("begin()\n");
    {   Timing t; t.maxCount = 0;
        Rig r(t);
        check(!r.button.begin(true, r.now), "V5", "begin() refuses invalid timing");
        r.button.onEvent(logCb); g_log.clear();
        r.hold(200); r.run(1000);
        check(g_log.empty(), "V5", "no events after a refused begin(): \"" + g_log + "\""); }
    {   ButtonEvents b(nullptr, nullptr);
        check(!b.begin(true, 0), "V5", "begin() refuses a null reader"); }
}

static void testCallback() {
    std::printf("Callback\n");
    {   Rig r; r.button.onEvent(logCb); g_log.clear();
        check(r.button.begin(true, r.now), "C1", "begin()");
        r.hold(200); r.run(1000); r.hold(2500); r.run(1000);
        check(g_log == "P R200 S1 P H2000 R2500", "C1", "log \"" + g_log + "\""); }
    {   Rig r; r.button.onEvent(takingCb); g_log.clear(); g_takeOk = true;
        r.button.begin(true, r.now);
        r.hold(200); r.run(1000);
        check(g_takeOk, "C2", "take() inside the callback gets the event just delivered");
        check(g_log == "P R200 S1", "C2", "log \"" + g_log + "\"");
        check(r.button.overruns() == 0, "C2", "no overruns"); }
}

static void testTake() {
    std::printf("take()\n");
    {   Rig r; r.button.begin(true, r.now);
        r.hold(200); r.run(1000);
        EventData d{};
        check(r.button.take(Event::SEQUENCE, d) && d.count == 1, "C3", "SEQUENCE(1)");
        check(!r.button.take(Event::SEQUENCE, d), "C3", "SEQUENCE slot empty after take");
        check(r.button.take(Event::RELEASED, d) && d.heldMs == 200, "C3", "RELEASED(200)");
        check(r.button.take(Event::PRESSED, d), "C3", "PRESSED");
        check(!r.button.take(Event::HOLDING, d), "C3", "no HOLDING"); }
    {   Rig r; r.button.begin(true, r.now);
        r.hold(200); r.run(1000);
        r.hold(200); r.run(200); r.hold(200); r.run(1000);   // one press, then two
        EventData d{};
        check(r.button.take(Event::SEQUENCE, d) && d.count == 2, "C4", "latest SEQUENCE kept");
        // 3 PRESSED, 3 RELEASED, 2 SEQUENCE, none taken in between: 2 + 2 + 1
        check(r.button.overruns() == 5, "C4",
              "overruns " + std::to_string(r.button.overruns()) + ", want 5"); }
    {   Rig r; r.button.onEvent(logCb); r.button.begin(true, r.now);
        r.button.onEvent(nullptr); g_log.clear();
        r.hold(200); r.run(1000);
        EventData d{};
        check(g_log.empty(), "C5", "no callback after onEvent(nullptr)");
        check(r.button.take(Event::SEQUENCE, d) && d.count == 1, "C5", "still latched"); }
}

// ---- two threads ---------------------------------------------------------------
// The owner thread runs N single-press sequences; a second thread takes every
// kind as fast as it can. Whatever it misses must be counted as an overrun,
// and nothing it takes may be torn.
static void testThreads() {
    std::printf("Two threads\n");
    const int N = 2000;
    Rig r; r.button.begin(true, r.now);
    std::atomic<bool> done{false};
    std::atomic<uint32_t> taken{0};
    std::atomic<bool> torn{false};

    std::thread consumer([&] {
        EventData d{};
        for (;;) {
            const bool last = done.load();
            for (uint8_t k = 0; k < ButtonEventsCore::EVENT_KINDS; k++) {
                if (r.button.take((Event)k, d)) {
                    taken++;
                    if ((Event)k == Event::RELEASED && d.heldMs != 200) torn = true;
                    if ((Event)k == Event::SEQUENCE && d.count != 1)    torn = true;
                    if ((Event)k == Event::PRESSED  && (d.heldMs || d.count)) torn = true;
                }
            }
            if (last) break;
        }
    });
    for (int i = 0; i < N; i++) { r.hold(200); r.run(800); }
    done = true;
    consumer.join();

    const uint32_t produced = 3u * N;
    check(!torn, "C6", "no torn data");
    check(taken + r.button.overruns() == produced, "C6",
          "taken " + std::to_string(taken.load()) + " + overruns " +
          std::to_string(r.button.overruns()) + " != " + std::to_string(produced));
}

int main() {
    testBegin();
    testCallback();
    testTake();
    testThreads();
    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
