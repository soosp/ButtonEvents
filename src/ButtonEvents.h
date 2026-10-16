#pragma once
/**
 * @file ButtonEvents.h
 * @brief Press, release, hold and press-count events for one button.
 *
 * Wraps ButtonEventsCore (the logic, see there for what each event means)
 * with an input, a callback, a lock and per-kind latches for other tasks.
 *
 * ## Input
 * Either a GPIO (constructed with a pin, its polarity and where its pull
 * resistor is; begin() sets the pin mode) or a ReadFn the caller supplies,
 * for an expander or anything else that can tell whether the button is held.
 * With a ReadFn the caller owns the pin set-up. The reader is called outside
 * the lock, so a reader on a shared bus may take that bus's own lock.
 *
 * Pull::CHIP asks for the chip's own resistor in the direction the polarity
 * needs: a pull-up for an active-low button, a pull-down for an active-high
 * one. begin() refuses (Error::PULL) where the chip has none: a pull-down on
 * AVR and on the ESP8266 apart from GPIO16, a pull-up on the ESP8266's
 * GPIO16, either on the ESP32's input-only GPIOs (34-39). Pull::BOARD sets
 * the pin to INPUT and relies on a resistor on the board.
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
 *   once per event in order. It may call take(), the getters and
 *   eventContext(); it must not call update().
 * - take() hands the latest event of a kind to any other task, once.
 *   Each kind has one slot: an event not taken before the next of its kind
 *   is replaced, and overruns() counts such replacements.
 *
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
#if defined(ARDUINO_ARCH_ESP32)
#include <driver/gpio.h>     // GPIO_IS_VALID_OUTPUT_GPIO
#endif

#if !defined(ARDUINO_ARCH_AVR) && !defined(ARDUINO_ARCH_ESP8266)
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

    /// Where the GPIO's pull resistor is. (Not INTERNAL / EXTERNAL: the AVR
    /// core defines both as macros for analogReference().)
    enum class Pull : uint8_t {
        CHIP = 0,   ///< The chip's own, in the direction the polarity needs
        BOARD       ///< A resistor on the board; the pin is set to INPUT
    };

    /// Why begin() failed.
    enum class Error : uint8_t {
        NONE = 0,   ///< begin() succeeded, or has not run
        NO_READER,  ///< Constructed with a null ReadFn
        TIMING,     ///< The Timing fails ButtonEventsCore::isValid()
        PULL,       ///< Pull::CHIP, but the chip has no such resistor on that pin
        LOCK        ///< The lock could not be taken
    };

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
     * @brief A button on a GPIO, pulled by the chip's own resistor. Does not
     *        touch the hardware; begin() does.
     * @param pin       GPIO number.
     * @param activeLow true: pressed reads LOW; false: pressed reads HIGH.
     * @param timing    Timing parameters.
     */
    explicit ButtonEvents(uint8_t pin, bool activeLow = true, const Timing& timing = Timing())
        : ButtonEvents(pin, activeLow, Pull::CHIP, timing) {}

    /**
     * @brief A button on a GPIO. Does not touch the hardware; begin() does.
     * @param pin       GPIO number.
     * @param activeLow true: pressed reads LOW; false: pressed reads HIGH.
     * @param pull      Where the pull resistor is.
     * @param timing    Timing parameters.
     */
    ButtonEvents(uint8_t pin, bool activeLow, Pull pull, const Timing& timing = Timing())
        : _pin(pin), _activeLow(activeLow), _pull(pull), _read(_readPin), _readCtx(this),
          _cb(nullptr), _cbCtx(nullptr), _ready(false), _error(Error::NONE), _core(timing), _slots(),
          _overruns(0) {}
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
          _pin(0), _activeLow(true), _pull(Pull::BOARD),
#endif
          _read(read), _readCtx(ctx), _cb(nullptr), _cbCtx(nullptr), _ready(false),
          _error(Error::NONE),
          _core(timing), _slots(), _overruns(0) {}

    ButtonEvents(const ButtonEvents&) = delete;
    ButtonEvents& operator=(const ButtonEvents&) = delete;

#if defined(ARDUINO)
    /**
     * @brief Sets the pin mode (GPIO variant), reads the initial level and
     *        validates the timing.
     * @param ignoreHeldAtStart true: a press already held now yields no event
     *        until released (default). false: it counts as a press starting now.
     * @return false on failure, error() tells why; update() then does nothing.
     */
    bool begin(bool ignoreHeldAtStart = true) { return begin(ignoreHeldAtStart, millis()); }
#endif

    /// begin() at the given time (host tests, or a caller with its own clock).
    bool begin(bool ignoreHeldAtStart, uint32_t nowMs) {
        _ready = false;
        if (_read == nullptr) return _fail(Error::NO_READER);
        if (!ButtonEventsCore::isValid(_core.timing())) return _fail(Error::TIMING);
#if defined(ARDUINO)
        if (_read == _readPin) {
            uint8_t mode;
            if (!_pinMode(mode)) return _fail(Error::PULL);
            pinMode(_pin, mode);
        }
#endif
        const bool pressed = _read(_readCtx);
        if (!_lock()) return _fail(Error::LOCK);
        _core.start(pressed, nowMs, ignoreHeldAtStart);
        _unlock();
        _error = Error::NONE;
        _ready = true;
        return true;
    }

    /// Why the last begin() failed (Error::NONE after a successful one).
    /// Owner's context.
    Error error() const { return _error; }

    /**
     * @brief Registers the event callback. One callback; nullptr unregisters.
     *        Set it from the owner's context, before begin() or between
     *        two update() calls.
     * @param cb  The callback.
     * @param ctx Handed back by eventContext(), for the callback to find its
     *            owner (an object, a channel) without a static.
     */
    void onEvent(EventCb cb, void* ctx = nullptr) {
        _cb    = cb;
        _cbCtx = ctx;
    }

    /// The ctx given to onEvent(); for the callback. Owner's context.
    void* eventContext() const { return _cbCtx; }

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

    bool _fail(Error e) {
        _error = e;
        return false;
    }

#if defined(ARDUINO)
    /// The pin mode for _activeLow and _pull; false where the chip has no
    /// resistor in the direction needed.
    bool _pinMode(uint8_t& mode) const {
        if (_pull == Pull::BOARD) {
            mode = INPUT;
            return true;
        }
#if defined(ARDUINO_ARCH_ESP32)
        if (!GPIO_IS_VALID_OUTPUT_GPIO(_pin)) return false;    // 34-39: no pull resistors
        mode = _activeLow ? INPUT_PULLUP : INPUT_PULLDOWN;
        return true;
#elif defined(ARDUINO_ARCH_ESP8266)
        if (_pin == 16) {                                     // pull-down only
            if (_activeLow) return false;
            mode = INPUT_PULLDOWN_16;
            return true;
        }
        if (!_activeLow) return false;
        mode = INPUT_PULLUP;
        return true;
#else
        if (!_activeLow) return false;                        // AVR: pull-up only
        mode = INPUT_PULLUP;
        return true;
#endif
    }

    static bool _readPin(void* self) {
        const ButtonEvents* b = static_cast<const ButtonEvents*>(self);
        return (digitalRead(b->_pin) == LOW) == b->_activeLow;
    }
    uint8_t _pin;
    bool    _activeLow;
    Pull    _pull;
#endif
    ReadFn  _read;
    void*   _readCtx;
    EventCb _cb;
    void*   _cbCtx;     ///< onEvent()'s ctx, for eventContext()
    bool    _ready;     ///< begin() succeeded
    Error   _error;     ///< Why the last begin() failed

    ButtonEventsCore _core;
    Slot             _slots[ButtonEventsCore::EVENT_KINDS];
    uint32_t         _overruns;

#if BUTTON_EVENTS_LOCKED
    mutable std::timed_mutex _mutex;
#endif
};
