#!/usr/bin/env sh
# run_tests.sh — build and run the ButtonEvents host tests.
#
# Works on Linux, macOS, WSL, Git-Bash and MSYS2. Needs a C++17 compiler
# (g++ or clang++) on PATH. No make required.
#
#   ./run_tests.sh

# Run from this script's own directory, so calling it from elsewhere still
# finds the sources and the library headers.
cd "$(dirname "$0")" || exit 1

if   command -v g++      >/dev/null 2>&1; then CXX=g++
elif command -v clang++  >/dev/null 2>&1; then CXX=clang++
elif command -v c++      >/dev/null 2>&1; then CXX=c++
else
  echo "ERROR: no C++ compiler found (need g++, clang++ or c++ on PATH)."
  exit 1
fi

FLAGS="-std=c++17 -O0 -Wall -Wextra -pthread -I. -I../src"
FLAGS11="-std=c++11 -O0 -Wall -Wextra -pthread -I. -I../src"

build_and_run() {
  src="$1"; out="$2"; label="$3"; flags="$4"
  echo "------------------------------------------------------------"
  echo "Building $label ..."
  if ! $CXX $flags "$src" -o "$out"; then
    echo "BUILD FAILED: $src"
    return 1
  fi
  echo "Running $label:"
  "./$out"
  return $?
}

rc=0
build_and_run core_harness.cpp  core_harness      "ButtonEventsCore suite"          "$FLAGS"   || rc=1
build_and_run wrapper_test.cpp  wrapper_test      "ButtonEvents wrapper test"       "$FLAGS"   || rc=1
build_and_run platform_check.cpp platform_check_host  "C++11 platform check (host)"    "$FLAGS11" || rc=1
build_and_run platform_check.cpp platform_check_esp32 "C++11 platform check (ESP32)"   "$FLAGS11 -DARDUINO -DARDUINO_ARCH_ESP32" || rc=1
build_and_run platform_check.cpp platform_check_8266  "C++11 platform check (ESP8266)" "$FLAGS11 -DARDUINO -DARDUINO_ARCH_ESP8266" || rc=1
build_and_run platform_check.cpp platform_check_avr   "C++11 platform check (AVR)"     "$FLAGS11 -DARDUINO -DARDUINO_ARCH_AVR" || rc=1

echo "------------------------------------------------------------"
if [ "$rc" -eq 0 ]; then
  echo "ALL TESTS PASSED."
else
  echo "SOME TESTS FAILED (see output above)."
fi
exit "$rc"
