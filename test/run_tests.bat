@echo off
REM run_tests.bat - build and run the ButtonEvents host tests on Windows.
REM
REM Needs a C++17 g++ on PATH (e.g. MinGW-w64 / MSYS2). No make required.
REM Double-click it, or run it from a terminal. The window stays open at the
REM end so you can read the results.

REM Run from this script's own folder, so double-clicking works.
cd /d "%~dp0"

where g++ >nul 2>nul
if errorlevel 1 (
  echo ERROR: g++ not found on PATH.
  echo Install MinGW-w64 / MSYS2 and add its bin folder to PATH, then retry.
  echo.
  if "%~1"=="" pause
  exit /b 1
)

set FLAGS=-std=c++17 -O0 -Wall -Wextra -pthread -I. -I..\src
set FLAGS11=-std=c++11 -O0 -Wall -Wextra -pthread -I. -I..\src

set RC=0

echo ------------------------------------------------------------
echo Building ButtonEventsCore suite ...
g++ %FLAGS% core_harness.cpp -o core_harness.exe
if errorlevel 1 ( echo BUILD FAILED: core_harness.cpp & set RC=1 & goto :wrapper )
echo Running ButtonEventsCore suite:
core_harness.exe
if errorlevel 1 set RC=1

:wrapper
echo ------------------------------------------------------------
echo Building ButtonEvents wrapper test ...
g++ %FLAGS% wrapper_test.cpp -o wrapper_test.exe
if errorlevel 1 ( echo BUILD FAILED: wrapper_test.cpp & set RC=1 & goto :c11 )
echo Running ButtonEvents wrapper test:
wrapper_test.exe
if errorlevel 1 set RC=1

:c11
echo ------------------------------------------------------------
echo Building C++11 compile check (host, lock) ...
g++ %FLAGS11% compile_check.cpp -o compile_check.exe
if errorlevel 1 ( echo BUILD FAILED: compile_check.cpp & set RC=1 & goto :esp8266 )
compile_check.exe
if errorlevel 1 set RC=1

:esp8266
echo ------------------------------------------------------------
echo Building C++11 compile check (ESP8266, no lock) ...
g++ %FLAGS11% -DARDUINO -DARDUINO_ARCH_ESP8266 compile_check.cpp -o compile_check_8266.exe
if errorlevel 1 ( echo BUILD FAILED: compile_check.cpp [ESP8266] & set RC=1 & goto :avr )
compile_check_8266.exe
if errorlevel 1 set RC=1

:avr
echo ------------------------------------------------------------
echo Building C++11 compile check (AVR, no lock) ...
g++ %FLAGS11% -DARDUINO -DARDUINO_ARCH_AVR compile_check.cpp -o compile_check_avr.exe
if errorlevel 1 ( echo BUILD FAILED: compile_check.cpp [AVR] & set RC=1 & goto :done )
compile_check_avr.exe
if errorlevel 1 set RC=1

:done
echo ------------------------------------------------------------
if "%RC%"=="0" (
  echo ALL TESTS PASSED.
) else (
  echo SOME TESTS FAILED ^(see output above^).
)
echo.
REM Keep the window open for double-click users; the VSCode task passes an
REM argument to skip the pause so the integrated terminal does not block.
if "%~1"=="" pause
exit /b %RC%
