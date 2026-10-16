// Arduino.h — stub for platform_check.cpp's Arduino builds: the few calls the
// GPIO variant of ButtonEvents makes, with pinMode() recorded. Not used by
// the other tests.
#pragma once
#include <stdint.h>

#define LOW          0
#define HIGH         1
#define INPUT        0x01
#define INPUT_PULLUP 0x05
#if defined(ARDUINO_ARCH_ESP32)
#define INPUT_PULLDOWN 0x09
#elif defined(ARDUINO_ARCH_ESP8266)
#define INPUT_PULLDOWN_16 0x04
#endif

// The AVR core's analogReference() names, macros there: the library must not
// collide with them
#define INTERNAL 3
#define EXTERNAL 0

extern int g_pinModePin;
extern int g_pinMode;

inline void     pinMode(uint8_t pin, uint8_t mode) { g_pinModePin = pin; g_pinMode = mode; }
inline int      digitalRead(uint8_t) { return HIGH; }
inline uint32_t millis() { return 0; }
