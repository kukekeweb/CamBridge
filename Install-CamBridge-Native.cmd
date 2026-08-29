@echo off
setlocal

set "ROOT=%~dp0"
set "BIN=%ROOT%build\native-mvp\Release"
set "MANAGER=%BIN%\cambridge_virtual_camera_manager.exe"
set "PROBE=%BIN%\cambridge_capture_probe.exe"
set "DLL=%BIN%\cambridge_media_source.dll"
set "LOGDIR=%ROOT%build\native-mvp\diagnostics\install"
set "CAMBRIDGE_NATIVE_MVP_LOG_DIR=%LOGDIR%"

if not exist "%MANAGER%" (
  echo CamBridge native build is missing: %MANAGER%
  echo Build first with the commands in windows\native-mvp\README.md.
  exit /b 2
)
if not exist "%DLL%" (
  echo Custom Media Source DLL is missing: %DLL%
  exit /b 2
)
if not exist "%LOGDIR%" mkdir "%LOGDIR%" >nul 2>&1

echo CamBridge Native one-time machine installation
echo This command requires an elevated Administrator PowerShell/CMD.
echo.
echo Media Source: %DLL%
echo Diagnostics:  C:\ProgramData\CamBridge\logs\media-source-*.log
echo Local logs:   %LOGDIR%
echo.

"%MANAGER%" --install --source "%DLL%" --machine
set "INSTALL_EXIT=%ERRORLEVEL%"

echo.
echo Registry verification (HKLM):
reg query "HKLM\Software\Classes\CLSID\{F6DC0D8C-8D0E-4DD2-9F5C-A9B83A2A3A61}\InprocServer32" 2>&1

echo.
echo Capture probe (runs even when installation reported a failure):
"%PROBE%"
set "PROBE_EXIT=%ERRORLEVEL%"

echo.
echo Install result: %INSTALL_EXIT%
echo Capture probe result: %PROBE_EXIT%
echo Frame Server logs: C:\ProgramData\CamBridge\logs\media-source-*.log
echo.
echo Keep this window output and the newest media-source log when reporting a failure.
exit /b %INSTALL_EXIT%
