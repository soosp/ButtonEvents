# Changelog

All notable changes to this project will be documented in this file.

The format is based on
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/),
and this project adheres to
[Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

- `ButtonEventsCore`: time-based debouncing with edges dated to the raw
  change; `PRESSED`, `RELEASED` (with duration), `HOLDING` (at `longMs`, then
  every `holdRepeatMs`) and `SEQUENCE` (exact count of short presses; a long
  press or a press beyond `maxCount` drops the sequence until `gapMs` of
  quiet); optional ignoring of a press held at start-up
- `ButtonEvents`: GPIO or caller-supplied reader, callback in the owner's
  context, per-kind `take()` latches with an overrun count, `std::timed_mutex`
  on ESP32 and ESP8266
- Timing defaults overridable through `BUTTON_EVENTS_*` macros
- Host tests (`test/`) and the examples `Basic` and `Esp32Task`

[Unreleased]: https://github.com/soosp/ButtonEvents/commits/main
