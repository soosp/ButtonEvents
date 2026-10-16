# ButtonEvents

Press, release, hold and press-count events for a push button, for ESP32,
ESP8266 and AVR.

The library reports facts: when a press began, how long it lasted, how long
the button has been held so far, and how many short presses made up a
sequence. What a duration or a count means is the application's decision.

## Features

- Time-based debouncing; edges are dated to the moment the level first
  changed, so durations do not depend on the debounce time or the poll rate
- Every press ends with a `RELEASED` event carrying its duration, short or
  long — the place for actions that must wait until the button is up
- `HOLDING` events while the button is held, for feedback such as a LED
- Press counting with an exact count: a long press or more presses than
  `maxCount` drop the whole sequence instead of reporting a wrong number
- A button already held at start-up can be ignored until it is released
- GPIO input, or any caller-supplied reader (I/O expander, shift register)
- Non-blocking `update()`, no interrupt, no task, no heap
- Thread-safe on the ESP32: a callback in the owner's context, and per-kind
  latches that any other task can take events from
- The logic (`ButtonEventsCore`) is plain C++ with no I/O and is tested on the
  host

## Quick start

```cpp
#include <ButtonEvents.h>

ButtonEvents button(0);   // GPIO 0, pressed = LOW, internal pull-up

void onButton(ButtonEvents::Event event, const ButtonEvents::EventData& data,
              ButtonEvents&) {
    if (event == ButtonEvents::Event::SEQUENCE && data.count == 3) {
        // three short presses
    }
    if (event == ButtonEvents::Event::RELEASED && data.heldMs >= 5000) {
        // held for 5 s and released
    }
}

void setup() {
    button.onEvent(onButton);
    button.begin();
}

void loop() {
    button.update();
}
```

See `examples/Basic` for every event, and `examples/Esp32Task` for a button
running in its own task with `loop()` taking the results.

## Events

|Event|When|Data|
|-----|----|----|
|`PRESSED`|a press began|—|
|`RELEASED`|a press ended (every press, short or long)|`heldMs`: its duration|
|`HOLDING`|held for `longMs`, then every `holdRepeatMs`|`heldMs`: held so far|
|`SEQUENCE`|`gapMs` of quiet after short presses|`count`: number of presses|

A short press is one released before `longMs`. A press that reaches `longMs`
drops the sequence: the presses before it, itself, and the presses after it
until `gapMs` of quiet. A press beyond `maxCount` does the same, so twelve
presses with `maxCount` 9 yield no `SEQUENCE` at all — neither a 9 nor a
shorter sequence made of their tail. A `count` is always exact.

Three short presses give `PRESSED RELEASED` three times, then `SEQUENCE(3)`.
A press held for 4.5 s gives `PRESSED HOLDING(2000) HOLDING(3000)
HOLDING(4000) RELEASED(4500)` and no `SEQUENCE`.

### Actions that restart the chip

Act on `RELEASED`, not on `HOLDING`. If the button sits on a strapping pin
(GPIO 0 on the ESP32 and ESP8266, GPIO 35 on the ESP32-P4), a restart while it
is still held boots the chip into its download mode.

### A button held at start-up

`begin(true)` (the default) ignores a press already held when `begin()` runs:
no event comes from it, and `isPressed()` stays false until it is released.
`begin(false)` takes it as a press beginning at `begin()`.

## Timing

Fixed at construction through a `Timing` struct. The defaults come from
macros, which a build can override (for example in `build_flags`).

|Field|Macro|Default|Meaning|
|-----|-----|-------|-------|
|`debounceMs`|`BUTTON_EVENTS_DEBOUNCE_MS`|50|stable time before a level counts; 0 = none|
|`gapMs`|`BUTTON_EVENTS_GAP_MS`|700|quiet time that ends a sequence|
|`longMs`|`BUTTON_EVENTS_LONG_MS`|2000|first `HOLDING`; longer presses drop the sequence|
|`holdRepeatMs`|`BUTTON_EVENTS_HOLD_REPEAT_MS`|1000|`HOLDING` period after the first; 0 = once|
|`maxCount`|`BUTTON_EVENTS_MAX_COUNT`|9|largest count reported|

```cpp
ButtonEvents::Timing timing;
timing.longMs = 3000;
ButtonEvents button(BUTTON_PIN, true, timing);
```

`begin()` returns false if `debounceMs` is not below both `gapMs` and
`longMs`, or `maxCount` is 0.

## Input

```cpp
ButtonEvents a(pin);                    // pressed = LOW, INPUT_PULLUP
ButtonEvents b(pin, false);             // pressed = HIGH
ButtonEvents c(readFn, ctx);            // bool readFn(void* ctx): true while held
```

With `activeLow` false the pin is set to `INPUT_PULLDOWN` on the ESP32 and to
`INPUT` elsewhere, where an external pull-down is needed. With a reader the
pin set-up is the caller's; the reader is called outside the library's lock,
so it may take the lock of a shared bus.

## Running it

`update()` is called regularly by one owner: `loop()` or a task. Every 5 to
20 ms suits human input; the timing does not depend on the period, only the
resolution does. A slow caller loses no events: the `HOLDING` ticks it missed
are reported once, with the current time.

`update(nowMs)` and `begin(ignoreHeld, nowMs)` take the time from the caller.

## Threads

On the ESP32 the state is guarded by a `std::timed_mutex`.

- `update()`, `begin()` and `onEvent()` belong to the owner.
- The callback runs in the owner's context, after the lock is released, once
  per event in order. It may call `take()` and the getters, not `update()`.
- `take(event, data)` hands the latest event of a kind to any task, once.
  Each kind has one slot; an event not taken before the next of its kind is
  replaced, and `overruns()` counts the replacements.
- `isPressed()`, `busy()` and `overruns()` may be called from any task.

On the ESP8266 and AVR there is no lock. These cores have no preemptive
tasks, and what does interrupt `loop()` (an ISR, or on the ESP8266 an SDK
callback such as `Ticker`'s) cannot wait for a lock anyway. Every call comes
from `loop()`'s context; an ESP8266 callback that needs the button hands over
with `schedule_function()`.

## Testing

`test/` holds host tests of the shipping headers. Run `test/run_tests.sh` or
`test/run_tests.bat`, or the VS Code build task; see `test/README.md`.

## License

MIT, see `LICENSE`.
