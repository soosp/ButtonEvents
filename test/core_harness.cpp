// core_harness.cpp — host regression suite for ButtonEventsCore.
//
// Drives the shipping ButtonEventsCore.h with a simulated clock and a raw
// button level, and asserts the exact event log of each scenario. Case ids
// refer to TESTS.md. Build:
//   g++ -std=c++17 -O0 -Wall -I../src core_harness.cpp -o core_harness

#include "ButtonEventsCore.h"

#include <cstdint>
#include <cstdio>
#include <string>

using Core   = ButtonEventsCore;
using Event  = Core::Event;
using Timing = Core::Timing;

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

static void expectLog(const char* id, const std::string& got, const std::string& want) {
    check(got == want, id, "got \"" + got + "\", want \"" + want + "\"");
}

// ---- simulation ------------------------------------------------------------
// The raw level is set with press()/release() and sampled at every step. Steps
// run every `period` ms. The log is a compact string:
//   P = PRESSED, R<ms> = RELEASED, H<ms> = HOLDING, S<n> = SEQUENCE.
struct Sim {
    Core        core;
    uint32_t    now;
    uint32_t    period;
    bool        raw;
    std::string log;

    explicit Sim(const Timing& t = Timing(), uint32_t startMs = 1000, uint32_t periodMs = 10,
                 bool heldAtStart = false, bool ignoreHeld = true)
        : core(t), now(startMs), period(periodMs), raw(heldAtStart) {
        core.start(heldAtStart, now, ignoreHeld);
    }

    void stepOnce() {
        const Core::Step s = core.step(raw, now);
        for (uint8_t i = 0; i < s.size; i++) {
            if (!log.empty()) log += ' ';
            switch (s.event[i]) {
                case Event::PRESSED:  log += "P"; break;
                case Event::RELEASED: log += "R" + std::to_string(s.data[i].heldMs); break;
                case Event::HOLDING:  log += "H" + std::to_string(s.data[i].heldMs); break;
                case Event::SEQUENCE: log += "S" + std::to_string(s.data[i].count); break;
            }
        }
    }

    /// Advance ms, stepping every period (the last step lands on now + ms).
    void run(uint32_t ms) {
        while (ms > 0) {
            const uint32_t d = ms < period ? ms : period;
            now += d;
            ms  -= d;
            stepOnce();
        }
    }

    void press()   { raw = true; }
    void release() { raw = false; }

    /// Press for ms (the press is sampled from the next step on; its held
    /// time comes out as ms when ms is a multiple of the period).
    void hold(uint32_t ms) { press(); run(ms); release(); }

    /// n presses of pressMs, each followed by gapMs.
    void presses(int n, uint32_t pressMs = 200, uint32_t gapMs = 200) {
        for (int i = 0; i < n; i++) { hold(pressMs); run(gapMs); }
    }
};

static std::string repeatLog(const std::string& unit, int n) {
    std::string s;
    for (int i = 0; i < n; i++) { if (i) s += ' '; s += unit; }
    return s;
}

// ---- timing validation ------------------------------------------------------
static void testValidation() {
    std::printf("Timing validation\n");
    Timing t;
    check(Core::isValid(t), "V1", "defaults valid");
    t = Timing(); t.debounceMs = t.gapMs;  check(!Core::isValid(t), "V2", "debounce >= gap");
    t = Timing(); t.debounceMs = t.longMs; t.gapMs = t.longMs + 1;
    check(!Core::isValid(t), "V3", "debounce >= long");
    t = Timing(); t.maxCount = 0;           check(!Core::isValid(t), "V4", "maxCount 0");
    t = Timing(); t.debounceMs = 0;         check(Core::isValid(t), "V-", "debounce 0 valid");
}

// ---- debouncing ---------------------------------------------------------------
static void testDebounce() {
    std::printf("Debouncing\n");
    {   Sim s; s.hold(40); s.run(1000);
        expectLog("D1", s.log, ""); }
    {   Sim s; s.press(); s.run(500); s.release(); s.run(30); s.press(); s.run(500);
        s.release(); s.run(1000);
        expectLog("D2", s.log, "P R1030 S1"); }
    for (uint32_t p : {1u, 10u, 20u}) {
        Sim s(Timing(), 1000, p); s.hold(200); s.run(1000);
        expectLog(("D3/" + std::to_string(p)).c_str(), s.log, "P R200 S1");
    }
    {   Sim s; s.press(); s.run(10); s.release(); s.run(10); s.press(); s.run(300);
        s.release(); s.run(1000);
        expectLog("D4", s.log, "P R300 S1"); }
}

// ---- presses and holding --------------------------------------------------------
static void testHolding() {
    std::printf("Presses and holding\n");
    {   Sim s; s.hold(200); s.run(1000);
        expectLog("H1", s.log, "P R200 S1"); }
    {   Sim s; s.press(); s.run(2010);           // sampled from +10
        expectLog("H2", s.log, "P H2000");
        check(s.core.pressed(), "H2", "still pressed"); }
    {   Sim s; s.hold(4500); s.run(1000);
        expectLog("H3", s.log, "P H2000 H3000 H4000 R4500"); }
    {   Timing t; t.holdRepeatMs = 0;
        Sim s(t); s.hold(5000); s.run(1000);
        expectLog("H4", s.log, "P H2000 R5000"); }
    {   Sim s; s.press(); s.run(1000);          // held 990
        s.period = 3000; s.run(3000);            // one step, held 3990
        s.period = 10;   s.run(20);              // held 4000, 4010
        expectLog("H5", s.log, "P H3990 H4000"); }
    {   Sim s; s.hold(1990); s.run(1000);
        expectLog("H6", s.log, "P R1990 S1"); }
    {   // HOLDING due and the release accepted in one step: only without debounce
        Timing t; t.debounceMs = 0;
        Sim s(t); s.press(); s.run(1990); s.release();
        s.period = 100; s.run(100);
        expectLog("H7", s.log, "P H2080 R2080"); }
}

// ---- sequences ----------------------------------------------------------------
static void testSequences() {
    std::printf("Sequences\n");
    for (int n : {1, 3, 5}) {
        Sim s; s.presses(n); s.run(1000); s.run(2000);
        expectLog(("S1/" + std::to_string(n)).c_str(), s.log,
                  repeatLog("P R200", n) + " S" + std::to_string(n));
    }
    {   Sim s; s.hold(200); s.run(709);         // release sampled at +10: 699 ms of gap
        expectLog("S2", s.log, "P R200");
        s.run(1);
        expectLog("S2", s.log, "P R200 S1"); }
    {   Sim s; s.hold(200); s.run(680); s.hold(200); s.run(1000);
        expectLog("S3", s.log, "P R200 P R200 S2"); }
    {   Sim s; s.presses(2); s.hold(2500); s.run(1000);
        expectLog("S4", s.log, "P R200 P R200 P H2000 R2500"); }
    {   Sim s; s.hold(2500); s.run(800); s.presses(3); s.run(1000);
        expectLog("S5", s.log, "P H2000 R2500 " + repeatLog("P R200", 3) + " S3"); }
    {   Sim s; s.presses(9); s.run(1000);
        expectLog("S6", s.log, repeatLog("P R200", 9) + " S9"); }
    {   Sim s; s.presses(10); s.run(1000);
        expectLog("S6a", s.log, repeatLog("P R200", 10)); }
    {   Sim s; s.presses(15); s.run(1000);
        expectLog("S6b", s.log, repeatLog("P R200", 15)); }
    {   Sim s; s.presses(10); s.run(1000); s.log.clear();
        s.presses(1); s.run(1000);
        expectLog("S6c", s.log, "P R200 S1"); }
    {   Sim s; s.presses(10, 200, 200); s.hold(200); s.run(600); s.hold(200); s.run(1000);
        expectLog("S6d", s.log, repeatLog("P R200", 12));
        s.log.clear(); s.presses(1); s.run(1000);
        expectLog("S6d", s.log, "P R200 S1"); }
    {   // SEQUENCE due and a new press accepted in one step: only without debounce
        Timing t; t.debounceMs = 0;
        Sim s(t); s.hold(200); s.run(690); s.press();
        s.period = 100; s.run(100);              // gap ran out at +700, press at +790
        s.period = 10;  s.run(190); s.release(); s.run(1000);
        expectLog("S7", s.log, "P R200 S1 P R200 S1"); }
    {   Sim s;
        check(!s.core.busy(), "S8", "idle before");
        s.hold(200);
        check(s.core.busy(), "S8", "busy while collecting");
        s.run(1000);
        check(!s.core.busy(), "S8", "idle after SEQUENCE");
        s.presses(10); s.run(400);       // 590 ms of quiet
        check(s.core.busy(), "S8", "busy within the quiet time of a dropped sequence");
        s.run(200);                      // 790 ms
        check(!s.core.busy(), "S8", "idle after the quiet time"); }
    {   Sim s; s.presses(2); s.hold(2500); s.run(200); s.hold(200); s.run(1000);
        expectLog("S9", s.log, "P R200 P R200 P H2000 R2500 P R200");
        s.log.clear(); s.presses(1); s.run(1000);
        expectLog("S9", s.log, "P R200 S1"); }
}

// ---- start-up -------------------------------------------------------------------
static void testStartup() {
    std::printf("Start-up\n");
    {   Sim s(Timing(), 1000, 10, true, true);
        s.run(5000);
        check(!s.core.pressed(), "B1", "ignored press not reported as pressed");
        s.release(); s.run(1000);
        expectLog("B1", s.log, "");
        s.presses(1); s.run(1000);
        expectLog("B2", s.log, "P R200 S1"); }
    {   Sim s(Timing(), 1000, 10, true, false);
        s.run(2490); s.release(); s.run(1000);
        expectLog("B3", s.log, "P H2000 R2500"); }
    {   Sim s; s.run(5000);
        expectLog("B4", s.log, ""); }
}

// ---- time wrap --------------------------------------------------------------------
static void testWrap() {
    std::printf("Time wrap\n");
    {   Sim s(Timing(), 0xFFFFFF00u); s.hold(500); s.run(1000);
        expectLog("W1", s.log, "P R500 S1"); }
    {   Sim s(Timing(), 0xFFFFFC00u); s.hold(200); s.run(699);
        expectLog("W2", s.log, "P R200");
        s.run(11);
        expectLog("W2", s.log, "P R200 S1"); }
}

int main() {
    testValidation();
    testDebounce();
    testHolding();
    testSequences();
    testStartup();
    testWrap();
    std::printf("%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
