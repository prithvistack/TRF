@echo off
REM Put the MinGW-w64 (posix-threads, UCRT) toolchain first on PATH for this
REM build only. The machine also has an old 32-bit MinGW.org g++ 6.3.0 on
REM PATH (no std::thread/std::mutex/<filesystem> support), which httplib.h
REM needs, so it cannot compile this project. This does not change PATH for
REM anything outside this script.
set "PATH=%LOCALAPPDATA%\Microsoft\WinGet\Packages\BrechtSanders.WinLibs.POSIX.UCRT_Microsoft.Winget.Source_8wekyb3d8bbwe\mingw64\bin;%PATH%"
REM -static-libgcc -static-libstdc++ -static: link the MinGW runtime DLLs
REM (libstdc++, libgcc, libwinpthread) directly into trf_system.exe, so
REM run.bat (and just double-clicking the exe) works with no PATH setup -
REM the same reason the PATH line above is only needed here, not in run.bat.
g++ -std=c++17 -Wall -Wextra -Iinclude -Ilib src\*.cpp -o trf_system.exe -D_WIN32_WINNT=0x0A00 -lws2_32 -static-libgcc -static-libstdc++ -static
