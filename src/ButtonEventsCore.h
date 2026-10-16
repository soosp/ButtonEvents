#pragma once
/**
 * @file ButtonEventsCore.h
 * @brief Debouncing and event recognition for one button. Pure logic: no
 *        I/O, no clock, no lock, no heap.
 *
 * Fed with the raw button level and the current time on every step. Each step
 * returns the events that became due, at most MAX_EVENTS_PER_STEP of them.
 *
 * ## Debouncing
 * A raw level counts once it has been stable for Timing::debounceMs (0: at
 * once). The edge is dated to the moment the raw level first changed, not to
 * the moment it was accepted, so held times and gaps do not depend on the
 * debounce time or on how often step() is called.
 *
 * ## Events
 * - PRESSED: a press began.
 * - RELEASED: a press ended; EventData::heldMs is how long it lasted. Every
 *   press ends with one, short or long. What a given duration means is the
 *   caller's decision; an action that restarts the chip belongs here rather
 *   than in HOLDING, so that the button is up when the chip samples its pins.
 * - HOLDING: the button is still held; heldMs is the time held so far. Fires
 *   first when heldMs reaches Timing::longMs, then every Timing::holdRepeatMs
 *   (only once if that is 0). Ticks missed between two steps are not
 *   reported one by one: the step reports one HOLDING with the current time.
 * - SEQUENCE: Timing::gapMs passed since the last release; EventData::count
 *   is the number of short presses (shorter than longMs) in the sequence.
 *   A press that reaches longMs drops the sequence: the presses before it,
 *   itself, and the presses after it until gapMs of quiet; its RELEASED
 *   carries its duration. A press beyond Timing::maxCount drops the
 *   sequence the same way: twelve presses yield no SEQUENCE, not a
 *   SEQUENCE(maxCount) and not a second, shorter sequence from their tail.
 *   count is therefore always the exact number of presses.
 *
 * Within a step the time-based event (HOLDING or SEQUENCE) comes before the
 * edge (PRESSED or RELEASED); hence the bound of two. Time-based events are
 * judged on the current level up to the moment a pending change began, so a
 * press that starts just before gapMs runs out continues the sequence once
 * accepted, and a release that starts just before longMs is a short press.
 *
 * ## A button held at start
 * start() takes the level at start-up. With ignoreHeld set, a button already
 * held then produces no event at all until it has been released; otherwise
 * the press is taken as beginning at start().
 */

#include <stdint.h>

#ifndef BUTTON_EVENTS_DEBOUNCE_MS
#define BUTTON_EVENTS_DEBOUNCE_MS 50
#endif
#ifndef BUTTON_EVENTS_GAP_MS
#define BUTTON_EVENTS_GAP_MS 700
#endif
#ifndef BUTTON_EVENTS_LONG_MS
#define BUTTON_EVENTS_LONG_MS 2000
#endif
#ifndef BUTTON_EVENTS_HOLD_REPEAT_MS
#define BUTTON_EVENTS_HOLD_REPEAT_MS 1000
#endif
#ifndef BUTTON_EVENTS_MAX_COUNT
#define BUTTON_EVENTS_MAX_COUNT 9
#endif

class ButtonEventsCore {
public:
    enum class Event : uint8_t {
        PRESSED = 0,
        RELEASED,
        HOLDING,
        SEQUENCE
    };
    static constexpr uint8_t EVENT_KINDS = 4;

    /// What an event carries. Each field belongs to the events named.
    struct EventData {
        uint32_t heldMs;   ///< RELEASED, HOLDING: time held (0 otherwise)
        uint8_t  count;    ///< SEQUENCE: short presses in the sequence (0 otherwise)
    };

    /// Timing parameters, fixed at construction. Defaults from the
    /// BUTTON_EVENTS_* macros, which a build may override.
    /// (A constructor rather than member initializers: GCC rejects a nested
    /// struct's member initializers in a default argument of the enclosing
    /// class.)
    struct Timing {
        uint32_t debounceMs;    ///< Stable time before a level counts; 0 = none
        uint32_t gapMs;         ///< Quiet time that ends a sequence
        uint32_t longMs;        ///< First HOLDING; longer presses drop the sequence
        uint32_t holdRepeatMs;  ///< HOLDING period after the first; 0 = once
        uint8_t  maxCount;      ///< Largest count reported; more presses drop the sequence

        Timing()
            : debounceMs(BUTTON_EVENTS_DEBOUNCE_MS), gapMs(BUTTON_EVENTS_GAP_MS),
              longMs(BUTTON_EVENTS_LONG_MS), holdRepeatMs(BUTTON_EVENTS_HOLD_REPEAT_MS),
              maxCount(BUTTON_EVENTS_MAX_COUNT) {}
    };

    static constexpr uint8_t MAX_EVENTS_PER_STEP = 2;

    /// The events of one step, in order.
    struct Step {
        uint8_t   size;
        Event     event[MAX_EVENTS_PER_STEP];
        EventData data[MAX_EVENTS_PER_STEP];
    };

    explicit ButtonEventsCore(const Timing& timing = Timing())
        : _timing(timing), _raw(false), _rawSinceMs(0), _level(false), _edgeMs(0),
          _ignoring(false), _long(false), _holdDone(false), _nextHoldMs(timing.longMs),
          _count(0), _dropped(false) {}

    /**
     * @brief Whether a Timing can be used: debounceMs < gapMs,
     *        debounceMs < longMs, maxCount >= 1.
     */
    static bool isValid(const Timing& t) {
        return t.debounceMs < t.gapMs && t.debounceMs < t.longMs && t.maxCount >= 1;
    }

    /**
     * @brief Sets the initial state. Call once before the first step().
     * @param pressed    Raw level at start-up, true while held.
     * @param nowMs      Current time.
     * @param ignoreHeld true: a press already held yields no event until released.
     */
    void start(bool pressed, uint32_t nowMs, bool ignoreHeld) {
        _raw        = pressed;
        _rawSinceMs = nowMs;
        _ignoring   = pressed && ignoreHeld;
        // A press that is not ignored starts from the released level, so that
        // it is accepted through the ordinary debounce, dated nowMs
        _level      = _ignoring;
        _edgeMs     = nowMs;
        _long       = false;
        _holdDone   = false;
        _nextHoldMs = _timing.longMs;
        _count      = 0;
        _dropped    = false;
    }

    /**
     * @brief One step.
     * @param pressed Raw level, true while held.
     * @param nowMs   Current time; may wrap.
     * @return The events that became due in this step.
     */
    Step step(bool pressed, uint32_t nowMs) {
        Step s;
        s.size = 0;
        if (pressed != _raw) {
            _raw        = pressed;
            _rawSinceMs = nowMs;
        }
        const bool     pending = _raw != _level;
        const uint32_t inLevel = (pending ? _rawSinceMs : nowMs) - _edgeMs;

        if (_level) {
            if (!_ignoring && !_holdDone && inLevel >= _nextHoldMs) {
                _emit(s, Event::HOLDING, inLevel, 0);
                if (!_long) {
                    _long    = true;
                    _count   = 0;
                    _dropped = true;
                }
                if (_timing.holdRepeatMs == 0) {
                    _holdDone = true;
                } else {
                    // Next tick on the longMs + k * holdRepeatMs grid, past inLevel
                    _nextHoldMs += ((inLevel - _nextHoldMs) / _timing.holdRepeatMs + 1)
                                   * _timing.holdRepeatMs;
                }
            }
        } else if ((_count > 0 || _dropped) && inLevel >= _timing.gapMs) {
            if (_count > 0) _emit(s, Event::SEQUENCE, 0, _count);   // never both
            _count   = 0;
            _dropped = false;
        }

        if (pending && nowMs - _rawSinceMs >= _timing.debounceMs) {
            const uint32_t pressMs = _edgeMs;
            _level  = _raw;
            _edgeMs = _rawSinceMs;
            if (_level) {
                _emit(s, Event::PRESSED, 0, 0);
                _long       = false;
                _holdDone   = false;
                _nextHoldMs = _timing.longMs;
            } else if (_ignoring) {
                _ignoring = false;
            } else {
                _emit(s, Event::RELEASED, _edgeMs - pressMs, 0);
                if (!_long && !_dropped) {
                    if (_count < _timing.maxCount) {
                        _count++;
                    } else {
                        _count   = 0;
                        _dropped = true;
                    }
                }
            }
        }
        return s;
    }

    /// Debounced level (false while a press held at start is being ignored).
    bool pressed() const { return _level && !_ignoring; }

    /// True while a press is held or a sequence is being collected or dropped.
    bool busy() const { return pressed() || _count > 0 || _dropped; }

    const Timing& timing() const { return _timing; }

private:
    static void _emit(Step& s, Event e, uint32_t heldMs, uint8_t count) {
        s.event[s.size]       = e;
        s.data[s.size].heldMs = heldMs;
        s.data[s.size].count  = count;
        s.size++;
    }

    Timing   _timing;
    bool     _raw;          ///< Last raw sample
    uint32_t _rawSinceMs;   ///< When the raw level last changed
    bool     _level;        ///< Debounced level
    uint32_t _edgeMs;       ///< When the debounced level's edge began (raw change time)
    bool     _ignoring;     ///< Press held at start, ignored until released
    bool     _long;         ///< Current press reached longMs
    bool     _holdDone;     ///< No further HOLDING for this press (holdRepeatMs 0)
    uint32_t _nextHoldMs;   ///< Held time at which the next HOLDING is due
    uint8_t  _count;        ///< Short presses in the current sequence
    bool     _dropped;      ///< Sequence dropped; presses ignored until gapMs of quiet
};
