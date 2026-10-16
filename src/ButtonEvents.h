#pragma once
/**
 * @file ButtonEvents.h
 * @brief Press, release, hold and press-count events for one button.
 *
 * Wraps ButtonEventsCore (the logic, see there for what each event means)
 * with an input, a callback, a lock and per-kind latches for other tasks.
 *
 * ## Input
 * Either a GPIO (constructed with a pin and its polarity; begin() sets the
 * pin mode) or a ReadFn the caller supplies, for an expander or anything
 * else that can tell whether the button is held. With a ReadFn the caller
 * owns the pin set-up. The reader is called outside the lock, so a reader on
 * a shared bus may take that bus's own lock.
 *
 * ## Running it
 * update() is non-blocking and is called regularly from one context, the
 * owner: loop() or a task of the application's. A period of 5-20 ms suits
 * human input; the timing does not depend on it, only the resolution does.
 * There is no interrupt and no task inside the library.
 *
 * ## Threads (ESP32)
 * - The state is guarded by a std::timed_mutex; update(), take() and the
 *   getters may be called from any task without corrupting it. An update()
 *   that cannot take the lock in BUTTON_EVENTS_LOCK_TIMEOUT_MS skips its
 *   step; the next one catches up, the logic being time-based.
 * - update() itself is meant for one owner: two tasks calling it would
 *   interleave steps, which the lock keeps consistent but not meaningful.
 *   begin() and onEvent() belong to the owner as well.
 * - The callback runs in the owner's context, after the lock is released,
 *   once per event in order. It may call take() and the getters; it must not
 *   call update().
 * - take() hands the latest event of a kind to any other task, once.
 *   Each kind has one slot: an event not taken before the next of its kind
 *   is replaced, and overruns() counts such replacements.
 * ## ESP8266 and AVR
 * There is no lock: these cores have no preemptive tasks, and a lock could
 * not protect against the contexts that do interrupt loop() (an ISR, or on
 * the ESP8266 an SDK callback such as Ticker's), which cannot wait for it.
 * Every call comes from loop()'s context; an ESP8266 callback that needs the
 * button hands over with schedule_function().
 *
 * Nothing is allocated; an instance holds its whole state.
 */

#include "ButtonEventsCore.h"

#if defined(ARDUINO)
#include <Arduino.h>
#endif

// The lock exists on the ESP32, and on the host for the tests
#if defined(ARDUINO_ARCH_ESP32) || !defined(ARDUINO)
#include <chrono>
#include <mutex>
#define BUTTON_EVENTS_LOCKED 1
#else
#define BUTTON_EVENTS_LOCKED 0
#endif

#ifndef BUTTON_EVENTS_LOCK_TIMEOUT_MS
#define BUTTON_EVENTS_LOCK_TIMEOUT_MS 50
#endif

class ButtonEvents {
public:
    using Event     = ButtonEventsCore::Event;
    using EventData = ButtonEventsCore::EventData;
    using Timing    = ButtonEventsCore::Timing;

    /// Tells whether the button is held (true) or not. Polarity is the
    /// reader's business.
    using ReadFn = bool (*)(void* ctx);

    /**
     * @brief Invoked once per event, in the context of update().
     * @param event  The event.
     * @param data   Its data (see EventData for which field applies).
     * @param button The instance that produced it.
     */
    using EventCb = void (*)(Event event, const EventData& data, ButtonEvents& button);

#if defined(ARDUINO)
    /**
     * @brief A button on a GPIO. Does not touch the hardware; begin() does.
     * @param pin       GPIO number.
     * @param activeLow true: pressed reads LOW (pin set to INPUT_PULLUP);
     *                  false: pressed reads HIGH (INPUT_PULLDOWN on ESP32,
     *                  INPUT elsewhere, which needs an external pull-down).
     * @param timing    Timing parameters.
     */
    explicit ButtonEvents(uint8_t pin, bool activeLow = true, const Timing& timing = Timing())
        : _pin(pin), _activeLow(activeLow), _read(_readPin), _readCtx(this),
          _cb(nullptr), _ready(false), _core(timing), _slots(), _overruns(0) {}
#endif

    /**
     * @brief A button read through a caller-supplied function.
     * @param read   Reader, called from update() and begin().
     * @param ctx    Passed to the reader as is.
     * @param timing Timing parameters.
     */
    ButtonEvents(ReadFn read, void* ctx, const Timing& timing = Timing())
        :
#if defined(ARDUINO)
          _pin(0), _activeLow(true),
#endif
          _read(read), _readCtx(ctx), _cb(nullptr), _ready(false), _core(timing),
          _slots(), _overruns(0) {}

    ButtonEvents(const ButtonEvents&) = delete;
    ButtonEvents& operator=(const ButtonEvents&) = delete;

#if defined(ARDUINO)
    /**
     * @brief Sets the pin mode (GPIO variant), reads the initial level and
     *        validates the timing.
     * @param ignoreHeldAtStart true: a press already held now yields no event
     *        until released (default). false: it counts as a press starting now.
     * @return false if the timing is invalid (ButtonEventsCore::isValid()) or
     *         there is no reader; update() then does nothing.
     */
    bool begin(bool ignoreHeldAtStart = true) { return begin(ignoreHeldAtStart, millis()); }
#endif

    /// begin() at the given time (host tests, or a caller with its own clock).
    bool begin(bool ignoreHeldAtStart, uint32_t nowMs) {
#if defined(ARDUINO)
        if (_read == _readPin) {
#if defined(ARDUINO_ARCH_ESP32)
            pinMode(_pin, _activeLow ? INPUT_PULLUP : INPUT_PULLDOWN);
#else
            pinMode(_pin, _activeLow ? INPUT_PULLUP : INPUT);
#endif
        }
#endif
        if (_read == nullptr || !ButtonEventsCore::isValid(_core.timing())) return false;
        const bool pressed = _read(_readCtx);
        if (!_lock()) return false;
        _core.start(pressed, nowMs, ignoreHeldAtStart);
        _unlock();
        _ready = true;
        return true;
    }

    /**
     * @brief Registers the event callback. One callback; nullptr unregisters.
     *        Set it from the owner's context, before begin() or between
     *        two update() calls.
     */
    void onEvent(EventCb cb) { _cb = cb; }

#if defined(ARDUINO)
    /// One step at millis().
    void update() { update(millis()); }
#endif

    /// One step at the given time (host tests, or a caller with its own clock).
    void update(uint32_t nowMs) {
        if (!_ready) return;
        const bool pressed = _read(_readCtx);
        if (!_lock()) return;
        const ButtonEventsCore::Step s = _core.step(pressed, nowMs);
        for (uint8_t i = 0; i < s.size; i++) {
            Slot& slot = _slots[(uint8_t)s.event[i]];
            if (slot.full) _overruns++;
            slot.full = true;
            slot.data = s.data[i];
        }
        const EventCb cb = _cb;
        _unlock();
        if (cb == nullptr) return;
        for (uint8_t i = 0; i < s.size; i++) cb(s.event[i], s.data[i], *this);
    }

    /**
     * @brief Takes the latest unconsumed event of a kind. Any task.
     * @param event The kind to take.
     * @param data  Filled with its data on success.
     * @return true if there was one; the slot is then empty.
     */
    bool take(Event event, EventData& data) {
        const uint8_t i = (uint8_t)event;
        if (i >= ButtonEventsCore::EVENT_KINDS || !_lock()) return false;
        const bool full = _slots[i].full;
        if (full) {
            data            = _slots[i].data;
            _slots[i].full  = false;
        }
        _unlock();
        return full;
    }

    /// Debounced level. Any task (false if the lock could not be taken).
    bool isPressed() const {
        if (!_lock()) return false;
        const bool v = _core.pressed();
        _unlock();
        return v;
    }

    /// True while a press is held or a sequence is being collected or
    /// dropped. Any task (false if the lock could not be taken).
    bool busy() const {
        if (!_lock()) return false;
        const bool v = _core.busy();
        _unlock();
        return v;
    }

    /// Events replaced in their slot before take() got them. Any task
    /// (0 if the lock could not be taken).
    uint32_t overruns() const {
        if (!_lock()) return 0;
        const uint32_t v = _overruns;
        _unlock();
        return v;
    }

    const Timing& timing() const { return _core.timing(); }

private:
    struct Slot {
        bool      full;
        EventData data;
    };

    bool _lock() const {
#if BUTTON_EVENTS_LOCKED
        return _mutex.try_lock_for(std::chrono::milliseconds(BUTTON_EVENTS_LOCK_TIMEOUT_MS));
#else
        return true;
#endif
    }

    void _unlock() const {
#if BUTTON_EVENTS_LOCKED
        _mutex.unlock();
#endif
    }

#if defined(ARDUINO)
    static bool _readPin(void* self) {
        const ButtonEvents* b = static_cast<const ButtonEvents*>(self);
        return (digitalRead(b->_pin) == LOW) == b->_activeLow;
    }
    uint8_t _pin;
    bool    _activeLow;
#endif
    ReadFn  _read;
    void*   _readCtx;
    EventCb _cb;
    bool    _ready;     ///< begin() succeeded

    ButtonEventsCore _core;
    Slot             _slots[ButtonEventsCore::EVENT_KINDS];
    uint32_t         _overruns;

#if BUTTON_EVENTS_LOCKED
    mutable std::timed_mutex _mutex;
#endif
};
