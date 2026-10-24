@echo off
rem SPDX-License-Identifier: BSD-2-Clause
rem Copyright (c) 2026 Scott Fusilier
rem
rem Builds and tests every preset (debug, asan, release) and checks the
rem formatting.  A stage or change is done when this passes on Windows,
rem FreeBSD and Linux.
rem
rem   scripts\check.bat
rem
rem Uses the MSYS2 CLANG64 toolchain (clang, ninja, clang-format) from
rem C:\msys64\clang64\bin, or from MOAS_CLANG64 if set.  That folder goes
rem first on PATH even if another clang is installed (such as the LLVM
rem installer's, which cannot build on its own), because the sanitizer
rem builds' programs need its DLLs to start.  CMake comes from PATH.  On
rem FreeBSD, scripts/check.sh does the same.
rem
rem If gcc is installed too (MSYS2 UCRT64: pacman -S
rem mingw-w64-ucrt-x86_64-gcc), an optimised gcc build is also built and
rem tested, with warnings as errors and _FORTIFY_SOURCE, because gcc warns
rem about things clang does not.  Set MOAS_GCC to use another gcc, or to
rem "none" to skip this.
setlocal
cd /d "%~dp0.."

if "%MOAS_CLANG64%"=="" set "MOAS_CLANG64=C:\msys64\clang64\bin"
if not exist "%MOAS_CLANG64%\clang.exe" (
    echo MSYS2 CLANG64 clang not found in %MOAS_CLANG64%; install it with
    echo   pacman -S mingw-w64-clang-x86_64-toolchain mingw-w64-clang-x86_64-ninja
    echo or set MOAS_CLANG64 to the folder that holds clang.exe.
    exit /b 1
)
set "PATH=%MOAS_CLANG64%;%PATH%"
where cmake >nul 2>nul || (echo cmake not found. & exit /b 1)
set "CC=%MOAS_CLANG64%\clang.exe"

for %%P in (debug asan release) do (
    echo ==== %%P
    cmake --preset %%P || exit /b 1
    cmake --build --preset %%P || exit /b 1
    ctest --preset %%P || exit /b 1
)

echo ==== formatting
where clang-format >nul 2>nul || (
    echo clang-format not found; formatting not checked.
    echo The project's formatting is defined by clang-format 23.
    goto gcc
)
clang-format --version
cmake --build --preset debug --target format-check || exit /b 1
echo Formatting is clean.

:gcc
echo ==== gcc
if "%MOAS_GCC%"=="" set "MOAS_GCC=C:\msys64\ucrt64\bin\gcc.exe"
if /i "%MOAS_GCC%"=="none" (
    echo Skipped: MOAS_GCC is none.
    goto done
)
if not exist "%MOAS_GCC%" (
    echo gcc not found at %MOAS_GCC%; gcc build not checked.
    goto done
)
rem The gcc build's programs need gcc's own folder on PATH to run.
for %%G in ("%MOAS_GCC%") do set "PATH=%PATH%;%%~dpG"
"%MOAS_GCC%" --version | findstr /n "^" | findstr "^1:"
set "CC=%MOAS_GCC%"
if exist build\gcc-release rmdir /s /q build\gcc-release
cmake -S . -B build\gcc-release -G Ninja -DCMAKE_BUILD_TYPE=Release -DMOAS_WERROR=ON "-DCMAKE_C_FLAGS=-D_FORTIFY_SOURCE=3" || exit /b 1
cmake --build build\gcc-release || exit /b 1
ctest --test-dir build\gcc-release --output-on-failure || exit /b 1

:done
echo ==== all checks passed
exit /b 0
