@echo off
setlocal
cd /d "%~dp0"

REM nanosemver with MSVC. Run from a Developer Command Prompt, or after vcvars32.bat /
REM vcvars64.bat; the target architecture is whatever that environment sets up.
REM
REM   build.bat         build and run the unit tests, as configured and as for a small
REM                     MCU: -DNSV_INT_MAX=32767 (the checks a 16-bit int needs),
REM                     division by shifts, and nsv_cmp's 32-bit compare
REM   build.bat fuzz    then run the standalone fuzz driver (FUZZ_ARGS, default -t 30)
REM   build.bat clean   delete build\ except the envy package cache
REM
REM Set DOCTEST_H to a doctest.h to skip envy, as with the Makefile.

if /i "%~1"=="clean" goto :clean

if defined DOCTEST_H (
  for %%i in ("%DOCTEST_H%") do set "DOCTEST_DIR=%%~dpi."
) else (
  for /f "usebackq delims=" %%i in (`call bin\envy.bat -q product doctest_cpp_dir`) do set "DOCTEST_DIR=%%i"
)
if not defined DOCTEST_DIR (
  echo build.bat: could not locate doctest.h 1>&2
  exit /b 1
)

set "OUT=build\msvc"
set "CFLAGS=/nologo /W4 /WX /std:c11 /O1 /Z7 /I."
set "CXXFLAGS=/nologo /W4 /WX /EHsc /std:c++20 /permissive- /O1 /Z7 /I. /external:W0 /external:I "%DOCTEST_DIR%""

call :unit unit "" "" || exit /b 1
call :unit int16 "/DNSV_INT_MAX=32767 /DNSV_SOFT_DIV=1 /DNSV_WIDE_CMP=0" "tests\int16\test_int16.cc" || exit /b 1

set "DIR=%OUT%\fuzz"
if not exist %DIR% mkdir %DIR%
cl %CXXFLAGS% /MP /c tests\fuzz\driver.cc tests\fuzz\check.cc tests\fuzz\gen.cc tests\ref_semver.cc /Fo%DIR%\ || exit /b 1
link /nologo /out:%DIR%\nsv_fuzz.exe %OUT%\unit\nanosemver.obj %DIR%\*.obj || exit /b 1

if /i "%~1"=="fuzz" (
  if not defined FUZZ_ARGS set "FUZZ_ARGS=-t 30"
  call %DIR%\nsv_fuzz.exe %%FUZZ_ARGS%% || exit /b 1
)
exit /b 0

REM %1 build subdirectory, %2 extra compiler flags, %3 extra C++ sources.
:unit
set "DIR=%OUT%\%~1"
if not exist %DIR% mkdir %DIR%
cl %CFLAGS% %~2 /c nanosemver.c tests\*.c /Fo%DIR%\ || exit /b 1
cl %CXXFLAGS% %~2 /MP /c tests\*.cc tests\fuzz\check.cc tests\fuzz\gen.cc %~3 /Fo%DIR%\ || exit /b 1
link /nologo /out:%DIR%\nsv_unittests.exe %DIR%\*.obj || exit /b 1
echo   RUN   %DIR%\nsv_unittests.exe
%DIR%\nsv_unittests.exe -m || exit /b 1
exit /b 0

:clean
if exist build (
  for /d %%d in (build\*) do if /i not "%%~nxd"=="envy-cache" rd /s /q "%%d"
  del /q build\* 2>nul
)
exit /b 0
