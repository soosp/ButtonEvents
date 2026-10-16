# Changelog

All notable changes to this project will be documented in this file.

The format is based on
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- `Pull` (`CHIP`, `BOARD`): where a GPIO button's pull resistor is, chosen
  apart from the polarity through a new constructor
  `ButtonEvents(pin, activeLow, pull, timing)`. `BOARD` sets the pin to
  `INPUT`; `CHIP` is what the existing constructor uses
- `Error` and `error()`: why `begin()` failed (`NO_READER`, `TIMING`, `PULL`,
  `LOCK`)

### Changed

- With `Pull::CHIP`, `begin()` refuses (`Error::PULL`) where the chip has no
  resistor in the direction the polarity needs, instead of setting the pin to
  `INPUT` and leaving it floating: an active-high button on AVR and on the
  ESP8266 apart from GPIO16, an active-low one on the ESP8266's GPIO16 (now
  `INPUT_PULLDOWN_16` when active high), either on the ESP32's input-only
  GPIOs 34–39. Such a button needs `Pull::BOARD`

## [0.1.0] - 2026-10-16

### Added

- `ButtonEventsCore`: time-based debouncing with edges dated to the raw
  change; `PRESSED`, `RELEASED` (with duration), `HOLDING` (at `longMs`, then
  every `holdRepeatMs`) and `SEQUENCE` (exact count of short presses; a long
  press or a press beyond `maxCount` drops the sequence until `gapMs` of
  quiet); optional ignoring of a press held at start-up
- `ButtonEvents`: GPIO or caller-supplied reader, callback in the owner's
  context, per-kind `take()` latches with an overrun count, `std::timed_mutex`
  on ESP32
- Timing defaults overridable through `BUTTON_EVENTS_*` macros
- Host tests (`test/`) and the examples `Basic` and `Esp32Task`

[Unreleased]: https://github.com/soosp/ButtonEvents/compare/0.1.0...HEAD
[0.1.0]: https://github.com/soosp/ButtonEvents/releases/tag/0.1.0
