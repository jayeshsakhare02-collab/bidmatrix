@echo off
REM BidMatrix launcher: starts the backend and opens the website.
cd /d "%~dp0"
if not exist bidmatrix.exe (
  echo bidmatrix.exe not found. Build it first with backend\build.bat
  pause
  exit /b 1
)
if not exist frontend\index.html (
  echo frontend folder not found next to bidmatrix.exe
  pause
  exit /b 1
)
start "" cmd /c "timeout /t 2 /nobreak >nul & start http://localhost:8080"
bidmatrix.exe
pause
