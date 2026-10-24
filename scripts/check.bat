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
rem Uses the MSYS2 CLANG64 toolchain (clang, ninja, clang-format).  If clang
rem is not on PATH, C:\msys64\clang64\bin is tried.  CMake comes from PATH.
rem On FreeBSD, scripts/check.sh does the same.
setlocal
cd /d "%~dp0.."

where clang >nul 2>nul || set "PATH=C:\msys64\clang64\bin;%PATH%"
where clang >nul 2>nul || (echo clang not found; install the MSYS2 CLANG64 toolchain. & exit /b 1)
where cmake >nul 2>nul || (echo cmake not found. & exit /b 1)
set CC=clang

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
    goto done
)
clang-format --version
cmake --build --preset debug --target format-check || exit /b 1
echo Formatting is clean.

:done
echo ==== all checks passed
exit /b 0
