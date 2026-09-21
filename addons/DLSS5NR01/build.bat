@echo off
setlocal
cd /d "%~dp0"
if not exist build mkdir build
cmake -S . -B build -G "Visual Studio 17 2022" -A x64
if errorlevel 1 exit /b 1
cmake --build build --config Release -- /nologo /v:m
if errorlevel 1 exit /b 1
echo.
echo === build\Release ===
dir /b build\Release\*.dll build\Release\*.exe 2>nul
