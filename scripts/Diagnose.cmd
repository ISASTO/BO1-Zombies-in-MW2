@echo off
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%~dp0DiagnoseStartup.ps1" %*
pause
