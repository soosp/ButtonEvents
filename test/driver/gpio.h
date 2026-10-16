// driver/gpio.h — stub for platform_check.cpp's ESP32 build: the classic
// ESP32's input-only GPIOs are 34-39.
#pragma once
#define GPIO_IS_VALID_OUTPUT_GPIO(n) ((n) < 34)
