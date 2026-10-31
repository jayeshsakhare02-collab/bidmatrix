@echo off
REM ============================================================================
REM  build.bat — Build BidMatrix C++ Backend (Windows)
REM ============================================================================
REM  Usage:
REM    build.bat          — compile and link
REM    build.bat run      — compile, link, and run
REM ============================================================================

echo.
echo ============================================
echo   Building BidMatrix Auction System...
echo ============================================
echo.

g++ -std=c++14 -O2 -Wall -Wextra -I include -o bidmatrix.exe src\main.cpp -lws2_32

if %ERRORLEVEL% NEQ 0 (
    echo.
    echo [ERROR] Build failed!
    pause
    exit /b 1
)

echo.
echo [SUCCESS] Build complete: bidmatrix.exe
echo.

if "%1"=="run" (
    echo Starting BidMatrix server...
    echo.
    bidmatrix.exe
)
