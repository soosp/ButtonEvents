// Arduino.h — stub for compile_check.cpp's ESP8266 and AVR builds: the few
// calls the GPIO variant of ButtonEvents makes. Not used by the other tests.
#pragma once
#include <stdint.h>

#define LOW          0
#define HIGH         1
#define INPUT        0
#define INPUT_PULLUP 2

inline void     pinMode(uint8_t, uint8_t) {}
inline int      digitalRead(uint8_t) { return HIGH; }
inline uint32_t millis() { return 0; }
