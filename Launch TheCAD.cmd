@echo off
setlocal
if exist "%~dp0build\package\TheCAD\TheCAD.exe" (
  start "" "%~dp0build\package\TheCAD\TheCAD.exe"
  exit /b 0
)
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\run.ps1"
if errorlevel 1 (
  echo.
  echo TheCAD could not start. See the error above and docs\build-windows.md.
  pause
  exit /b 1
)
