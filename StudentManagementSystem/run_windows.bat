@echo off
setlocal
cd /d "%~dp0"
echo.
echo  CampusDesk - Student Management System
echo  ---------------------------------------
where g++ >nul 2>nul
if errorlevel 1 (
  echo ERROR: g++ was not found.
  echo Install MinGW-w64 or another GCC distribution and add g++ to PATH.
  echo See README.md for setup instructions.
  pause
  exit /b 1
)
echo Compiling C++ backend...
g++ -std=c++17 -O2 backend\main.cpp -o student_server.exe -lws2_32
if errorlevel 1 (
  echo.
  echo Build failed. Review the compiler messages above.
  pause
  exit /b 1
)
echo.
echo Starting server. Keep this window open.
echo Open http://127.0.0.1:8080 in your browser.
echo.
student_server.exe
pause
