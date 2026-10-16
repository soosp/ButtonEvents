// compile_check.cpp — the headers compile at the AVR toolchain's language
// level (C++11), with and without the lock (ARDUINO_ARCH_AVR selects the
// lock-free branch). Built twice by run_tests; nothing to run beyond main().

#include "ButtonEvents.h"

static bool readNothing(void*) { return false; }

int main() {
    ButtonEvents::Timing t;
    t.longMs = 3000;
    ButtonEvents b(readNothing, nullptr, t);
    return b.begin(true, 0) ? 0 : 1;
}
