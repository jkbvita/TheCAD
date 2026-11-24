@echo off
setlocal
if exist "%~dp0build\package\TheCAD-current\TheCAD.exe" (
  start "" "%~dp0build\package\TheCAD-current\TheCAD.exe" %*
  exit /b 0
)
if not "%~1"=="" (
  echo TheCAD-current is not packaged. Run scripts\build.ps1 and scripts\package-windows.ps1 first.
  exit /b 1
)
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0scripts\run.ps1"
if errorlevel 1 (
  echo.
  echo TheCAD could not start. See the error above and docs\build-windows.md.
  pause
  exit /b 1
)
