// Esp32Task — the button runs in a task of its own; loop() takes the results.
//
// The task owns the button: it alone calls update(). Any other task reads the
// results through take(), which hands each event over once. Five short
// presses print a message; a press held for at least 5 s restarts the board
// when it is released, so the button is up while the chip samples its
// strapping pins.

#include <ButtonEvents.h>

#if !defined(ARDUINO_ARCH_ESP32)
#error "This example needs an ESP32"
#endif

#ifndef BUTTON_PIN
#define BUTTON_PIN 0      // the BOOT key on most ESP32 boards
#endif

static const uint32_t RESTART_HOLD_MS = 5000;

static ButtonEvents button(BUTTON_PIN);

static void buttonTask(void*) {
    TickType_t last = xTaskGetTickCount();
    for (;;) {
        button.update();
        vTaskDelayUntil(&last, pdMS_TO_TICKS(10));
    }
}

void setup() {
    Serial.begin(115200);
    if (!button.begin()) {
        Serial.println("ButtonEvents: invalid timing");
        return;
    }
    xTaskCreatePinnedToCore(buttonTask, "button", 2048, nullptr, 2, nullptr, 1);
}

void loop() {
    ButtonEvents::EventData data;
    if (button.take(ButtonEvents::Event::SEQUENCE, data)) {
        Serial.printf("sequence of %u%s\n", data.count, data.count == 5 ? ": five, as asked" : "");
    }
    if (button.take(ButtonEvents::Event::HOLDING, data) && data.heldMs >= RESTART_HOLD_MS) {
        Serial.println("release to restart");
    }
    if (button.take(ButtonEvents::Event::RELEASED, data) && data.heldMs >= RESTART_HOLD_MS) {
        Serial.println("restarting");
        Serial.flush();
        ESP.restart();
    }
    delay(20);
}
