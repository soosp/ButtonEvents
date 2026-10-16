// Basic — one button polled from loop(), events printed by the callback.
//
// Wire a push button between BUTTON_PIN and GND (the internal pull-up is
// used). Try: one, three and five short presses; a press held for a few
// seconds; a press held while resetting the board (ignored until released).

#include <ButtonEvents.h>

#ifndef BUTTON_PIN
#if defined(ARDUINO_ARCH_AVR)
#define BUTTON_PIN 2
#else
#define BUTTON_PIN 0      // the BOOT / FLASH key on most ESP32 and ESP8266 boards
#endif
#endif

static ButtonEvents button(BUTTON_PIN);

static void onButton(ButtonEvents::Event event, const ButtonEvents::EventData& data,
                     ButtonEvents&) {
    switch (event) {
        case ButtonEvents::Event::PRESSED:
            Serial.println(F("pressed"));
            break;
        case ButtonEvents::Event::HOLDING:
            Serial.print(F("holding "));
            Serial.print(data.heldMs);
            Serial.println(F(" ms"));
            break;
        case ButtonEvents::Event::RELEASED:
            Serial.print(F("released after "));
            Serial.print(data.heldMs);
            Serial.println(F(" ms"));
            // What a duration means is the application's decision; a restart
            // belongs here, when the button is already up
            if (data.heldMs >= 5000) Serial.println(F("  -> long enough for a restart"));
            break;
        case ButtonEvents::Event::SEQUENCE:
            Serial.print(F("sequence of "));
            Serial.println(data.count);
            break;
    }
}

void setup() {
    Serial.begin(115200);
    button.onEvent(onButton);
    if (!button.begin()) Serial.println(F("ButtonEvents: invalid timing"));
}

void loop() {
    button.update();
    delay(10);
}
