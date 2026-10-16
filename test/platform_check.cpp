// platform_check.cpp — the headers at the AVR toolchain's language level
// (C++11), built four times by run_tests: on the host (lock, ReadFn only),
// and with ARDUINO plus ARDUINO_ARCH_ESP32, _ESP8266 or _AVR against the
// Arduino.h and driver/gpio.h stubs, where it checks which pin mode begin()
// sets, or which Error it refuses with, for every polarity and Pull.

#include "ButtonEvents.h"

#include <cstdio>

#if BUTTON_EVENTS_LOCKED != (!defined(ARDUINO_ARCH_AVR) && !defined(ARDUINO_ARCH_ESP8266))
#error "lock expected everywhere but on AVR and ESP8266"
#endif

using Pull  = ButtonEvents::Pull;
using Error = ButtonEvents::Error;

static int g_fail = 0;

static bool readNothing(void*) { return false; }

static void expect(bool ok, const char* what) {
    if (!ok) {
        g_fail++;
        std::printf("  FAIL %s\n", what);
    }
}

#if defined(ARDUINO)
int g_pinModePin = -1;
int g_pinMode    = -1;

/// begin() on pin with the polarity and pull: mode >= 0 expects that pin
/// mode, mode < 0 expects a refusal with Error::PULL and no pinMode() call.
static void gpio(uint8_t pin, bool activeLow, Pull pull, int mode, const char* what) {
    g_pinModePin = -1;
    g_pinMode    = -1;
    ButtonEvents b(pin, activeLow, pull);
    const bool ok = b.begin();
    if (mode >= 0) {
        expect(ok && b.error() == Error::NONE && g_pinModePin == pin && g_pinMode == mode, what);
    } else {
        expect(!ok && b.error() == Error::PULL && g_pinModePin == -1, what);
    }
}
#endif

int main() {
    {   ButtonEvents::Timing t;
        t.longMs = 3000;
        ButtonEvents b(readNothing, nullptr, t);
        expect(b.begin(true, 0) && b.error() == Error::NONE, "ReadFn begin");
        t.maxCount = 0;
        ButtonEvents bad(readNothing, nullptr, t);
        expect(!bad.begin(true, 0) && bad.error() == Error::TIMING, "Error::TIMING");
        ButtonEvents none(nullptr, nullptr);
        expect(!none.begin(true, 0) && none.error() == Error::NO_READER, "Error::NO_READER");
    }
#if defined(ARDUINO)
    {   ButtonEvents b(2);                       // the 0.1.0 constructor: Pull::CHIP
        expect(b.begin() && g_pinMode == INPUT_PULLUP, "default constructor: CHIP pull-up");
    }
    gpio(2, true,  Pull::BOARD, INPUT, "BOARD, active low");
    gpio(2, false, Pull::BOARD, INPUT, "BOARD, active high");
#if defined(ARDUINO_ARCH_ESP32)
    gpio(4,  true,  Pull::CHIP, INPUT_PULLUP,   "ESP32 CHIP, active low");
    gpio(4,  false, Pull::CHIP, INPUT_PULLDOWN, "ESP32 CHIP, active high");
    gpio(34, true,  Pull::CHIP, -1,             "ESP32 GPIO34 CHIP, active low refused");
    gpio(39, false, Pull::CHIP, -1,             "ESP32 GPIO39 CHIP, active high refused");
    gpio(34, true,  Pull::BOARD, INPUT,         "ESP32 GPIO34 BOARD");
#elif defined(ARDUINO_ARCH_ESP8266)
    gpio(4,  true,  Pull::CHIP, INPUT_PULLUP,      "ESP8266 CHIP, active low");
    gpio(4,  false, Pull::CHIP, -1,                "ESP8266 CHIP, active high refused");
    gpio(16, false, Pull::CHIP, INPUT_PULLDOWN_16, "ESP8266 GPIO16 CHIP, active high");
    gpio(16, true,  Pull::CHIP, -1,                "ESP8266 GPIO16 CHIP, active low refused");
#elif defined(ARDUINO_ARCH_AVR)
    gpio(2, true,  Pull::CHIP, INPUT_PULLUP, "AVR CHIP, active low");
    gpio(2, false, Pull::CHIP, -1,           "AVR CHIP, active high refused");
#endif
#endif
    std::printf("%s\n", g_fail ? "FAILED" : "ok");
    return g_fail ? 1 : 0;
}
