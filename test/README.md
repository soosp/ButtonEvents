# ButtonEvents host tests

Host-side tests of the shipping headers, built with the host compiler; no
board needed.

|File|What it covers|
|----|--------------|
|`core_harness.cpp`|`ButtonEventsCore.h`: every case below except C1–C6|
|`wrapper_test.cpp`|`ButtonEvents.h` through the `ReadFn` constructor and the `nowMs` overloads: V5, C1–C6|
|`compile_check.cpp`|both headers at C++11 (the AVR toolchain's level), with and without the lock|

Run `run_tests.sh` (Linux, macOS, WSL, MSYS2) or `run_tests.bat` (Windows,
MinGW-w64 / MSYS2 `g++` on PATH), or the VS Code build task (Ctrl+Shift+B).
Needs a C++17 compiler.

ThreadSanitizer is not part of the run: libstdc++ implements
`std::timed_mutex::try_lock_for` with `pthread_mutex_clocklock`, which GCC's
TSan does not intercept, so it reports every access under that lock as a race.
With the lock taken by `lock()` instead, C6 runs clean under TSan.

## Cases

## Timing validation

|#|Case|Expected|
|-|----|--------|
|V1|Defaults|`isValid` true|
|V2|`debounceMs >= gapMs`|false|
|V3|`debounceMs >= longMs`|false|
|V4|`maxCount == 0`|false|
|V5|`begin()` with invalid timing or a null reader|returns false; `update()` yields nothing|

## Debouncing

|#|Case|Expected|
|-|----|--------|
|D1|Raw press shorter than `debounceMs`|no event|
|D2|Glitch inside a held press (release shorter than `debounceMs`)|no RELEASED, press continues|
|D3|Clean press, step every 1 / 10 / 20 ms|PRESSED once; `heldMs` of RELEASED equal across step periods (edge dated to raw change)|
|D4|Bouncing edge (several flips within `debounceMs`)|one PRESSED, dated to the last flip|

## Presses and holding

|#|Case|Expected|
|-|----|--------|
|H1|Short press 200 ms|PRESSED, RELEASED(200)|
|H2|Press held to `longMs`|HOLDING(longMs) while held|
|H3|Held 4.5 s, `holdRepeatMs` 1000|HOLDING at 2000, 3000, 4000; RELEASED(4500)|
|H4|`holdRepeatMs` 0, held 5 s|one HOLDING|
|H5|Step gap 3 s while held (slow caller)|one HOLDING with the current held time, no backlog|
|H6|Release begins just before `longMs`, accepted after it|no HOLDING; counted as short|
|H7|Same step: HOLDING due and release accepted (`debounceMs` 0: with debouncing they fall in different steps)|HOLDING before RELEASED (`size` 2)|

## Sequences

|#|Case|Expected|
|-|----|--------|
|S1|1 / 3 / 5 short presses|SEQUENCE(1/3/5) `gapMs` after the last release, once|
|S2|SEQUENCE not before `gapMs`|nothing at `gapMs - 1`|
|S3|Press begins just before `gapMs` expires, accepted after it|sequence continues|
|S4|2 short, then one long|no SEQUENCE; RELEASED of the long carries its duration|
|S5|Long press, `gapMs` of quiet, then 3 short|SEQUENCE(3)|
|S6|`maxCount` presses|SEQUENCE(maxCount)|
|S6a|`maxCount` + 1 presses|no SEQUENCE; PRESSED/RELEASED for each|
|S6b|`maxCount` + 6 presses in one burst|no SEQUENCE, not even from the tail|
|S6c|S6a, `gapMs` of quiet, then 1 press|SEQUENCE(1)|
|S6d|S6a, then a press before `gapMs` of quiet has passed|still dropped; the quiet time restarts from its release|
|S7|Same step: SEQUENCE due and a new press accepted (`debounceMs` 0)|SEQUENCE before PRESSED; new sequence starts at 1|
|S8|`busy()`|true from first press until SEQUENCE, or until `gapMs` of quiet after a dropped sequence; false after|
|S9|2 short, 1 long, then 1 short before `gapMs` of quiet|still dropped (the long press drops the burst like S6d); a press after `gapMs` of quiet gives SEQUENCE(1)|

## Start-up

|#|Case|Expected|
|-|----|--------|
|B1|Held at start, `ignoreHeldAtStart` true, held 5 s, released|no event at all; `isPressed()` false meanwhile|
|B2|As B1, then a short press|PRESSED, RELEASED, SEQUENCE(1)|
|B3|Held at start, `ignoreHeldAtStart` false|PRESSED dated to `begin()`; HOLDING from there|
|B4|Not held at start|no event|

## Time wrap

|#|Case|Expected|
|-|----|--------|
|W1|Press across the `uint32_t` wrap of `nowMs`|correct `heldMs`|
|W2|Sequence gap across the wrap|SEQUENCE on time|

## Wrapper

|#|Case|Expected|
|-|----|--------|
|C1|Callback order|same order and data as the core's steps|
|C2|Callback calls `take()` / `isPressed()`|no deadlock|
|C3|`take()` per kind|latest data, then false|
|C4|One press, then two, nothing taken|SEQUENCE(2) kept; `overruns()` 5 (PRESSED 2, RELEASED 2, SEQUENCE 1)|
|C5|`onEvent(nullptr)`|events still latched for `take()`|
|C6|`take()` from a second thread while the first runs `update()` (std::thread)|nothing torn; taken + `overruns()` = produced|
