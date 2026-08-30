@echo off
setlocal EnableExtensions DisableDelayedExpansion

set "ROOT=%~dp0"
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%ROOT%scripts\Start-CamBridge-Native.ps1" %*
exit /b %ERRORLEVEL%
