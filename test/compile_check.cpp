// compile_check.cpp — the headers compile at the AVR toolchain's language
// level (C++11). Built three times by run_tests: on the host (the lock), and
// with ARDUINO plus ARDUINO_ARCH_ESP8266 or ARDUINO_ARCH_AVR against the
// Arduino.h stub (the GPIO variant, no lock). Nothing to run beyond main().

#include "ButtonEvents.h"

#if BUTTON_EVENTS_LOCKED != !defined(ARDUINO)
#error "lock expected on the host only (ESP32 is not built here)"
#endif

static bool readNothing(void*) { return false; }

int main() {
    ButtonEvents::Timing t;
    t.longMs = 3000;
    ButtonEvents b(readNothing, nullptr, t);
#if defined(ARDUINO)
    ButtonEvents g(2, true, t);
    if (!g.begin()) return 1;
    g.update();
#endif
    return b.begin(true, 0) ? 0 : 1;
}
