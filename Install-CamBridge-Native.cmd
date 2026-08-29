@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "ROOT=%~dp0"
set "BIN=%ROOT%build\native-mvp\Release"
set "INSTALL_ROOT=%ProgramFiles%\CamBridge\Native"
set "MANAGER=%INSTALL_ROOT%\cambridge_virtual_camera_manager.exe"
set "PROBE=%INSTALL_ROOT%\cambridge_capture_probe.exe"
set "SYNTHETIC=%INSTALL_ROOT%\cambridge_synthetic_publisher.exe"
set "IPC_PROBE=%INSTALL_ROOT%\cambridge_frame_ipc_probe.exe"
set "DLL=%INSTALL_ROOT%\cambridge_media_source.dll"
set "DLL_MANIFEST=%INSTALL_ROOT%\cambridge_media_source.active.txt"
set "LOGDIR=%ROOT%build\native-mvp\diagnostics\install"
set "CAMBRIDGE_NATIVE_MVP_LOG_DIR=%LOGDIR%"

if not exist "%BIN%\cambridge_virtual_camera_manager.exe" (
  echo CamBridge native build is missing: %BIN%
  echo Build first with the commands in windows\native-mvp\README.md.
  exit /b 2
)
if not exist "%BIN%\cambridge_media_source.dll" (
  echo Custom Media Source DLL is missing: %BIN%\cambridge_media_source.dll
  exit /b 2
)
if not exist "%LOGDIR%" mkdir "%LOGDIR%" >nul 2>&1

fltmc >nul 2>&1
if errorlevel 1 (
  echo This launcher must be run as Administrator.
  exit /b 740
)

echo CamBridge Native one-time machine installation
echo This command requires an elevated Administrator PowerShell/CMD.
echo.
echo Source artifacts: %BIN%
echo Install root:     %INSTALL_ROOT%
echo Registered DLL:   determined during install
echo Frame Server logs: C:\ProgramData\CamBridge\logs\media-source-*.log
echo.

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%ROOT%scripts\Install-CamBridge-Native.ps1" -SourceRoot "%BIN%" -InstallRoot "%INSTALL_ROOT%"
set "COPY_EXIT=%ERRORLEVEL%"

if not "%COPY_EXIT%"=="0" (
  echo.
  echo Artifact copy failed; machine registration and capture probe were skipped.
  echo A loaded Media Source DLL may require a new Windows camera session before replacement.
  exit /b %COPY_EXIT%
)
if not exist "%DLL_MANIFEST%" (
  echo Active Media Source manifest is missing; machine registration and capture probe were skipped.
  exit /b 1
)
set /p DLL=<"%DLL_MANIFEST%"
if "%DLL%"=="" (
  echo Active Media Source manifest is empty; machine registration and capture probe were skipped.
  exit /b 1
)
echo Active registered DLL: %DLL%

echo.
echo Machine COM registration and Virtual Camera probe:
if exist "%MANAGER%" (
  "%MANAGER%" --install --source "%DLL%" --machine
  set "INSTALL_EXIT=%ERRORLEVEL%"
) else (
  echo Installed manager is missing; registration skipped.
  set "INSTALL_EXIT=2"
)

echo.
echo Registry verification (HKLM):
reg query "HKLM\Software\Classes\CLSID\{F6DC0D8C-8D0E-4DD2-9F5C-A9B83A2A3A61}\InprocServer32" 2>&1

echo.
echo Installed file/ACL/MOTW verification:
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%ROOT%scripts\Inspect-CamBridge-NativeInstall.ps1" -InstallRoot "%INSTALL_ROOT%" -MediaSourcePath "%DLL%"
set "AUDIT_EXIT=%ERRORLEVEL%"

echo.
echo Synthetic publisher and capture probe:
set "SYNTHETIC_PID="
set "SYNTHETIC_START_EXIT=2"
set "IPC_READY_EXIT=2"
if exist "%SYNTHETIC%" (
  for /f "usebackq delims=" %%P in (`powershell.exe -NoProfile -Command "$p=Start-Process -FilePath '%SYNTHETIC%' -WorkingDirectory '%INSTALL_ROOT%' -RedirectStandardOutput '%LOGDIR%\synthetic-publisher.log' -RedirectStandardError '%LOGDIR%\synthetic-publisher-error.log' -WindowStyle Hidden -PassThru; $p.Id"`) do set "SYNTHETIC_PID=%%P"
  if defined SYNTHETIC_PID set "SYNTHETIC_START_EXIT=0"
  if defined SYNTHETIC_PID if exist "%IPC_PROBE%" (
    for /l %%N in (1,1,20) do (
      if not "!IPC_READY_EXIT!"=="0" (
        "%IPC_PROBE%" 1 >"%LOGDIR%\ipc-readiness.log" 2>&1
        set "IPC_READY_EXIT=!ERRORLEVEL!"
        if not "!IPC_READY_EXIT!"=="0" timeout /t 1 /nobreak >nul
      )
    )
  )
  if not defined SYNTHETIC_PID echo Synthetic publisher did not start.
) else (
  echo Installed synthetic publisher is missing; publisher start skipped.
)
if not defined SYNTHETIC_PID set "SYNTHETIC_START_EXIT=1"
if not exist "%IPC_PROBE%" echo Installed IPC probe is missing; readiness check skipped.
if defined SYNTHETIC_PID if not "!IPC_READY_EXIT!"=="0" echo IPC readiness failed; capture probe will still run for diagnostics.
if exist "%IPC_PROBE%" (
  echo Shared-memory IPC readiness: !IPC_READY_EXIT!
  set "IPC_PROBE_EXIT=!IPC_READY_EXIT!"
) else (
  echo Installed IPC probe is missing; IPC probe skipped.
  set "IPC_PROBE_EXIT=2"
)
if exist "%PROBE%" (
  "%PROBE%"
  set "PROBE_EXIT=!ERRORLEVEL!"
) else (
  echo Installed capture probe is missing; probe skipped.
  set "PROBE_EXIT=2"
)
if defined SYNTHETIC_PID taskkill /pid %SYNTHETIC_PID% /t /f >nul 2>&1

echo.
echo CamBridge Native Install: %INSTALL_EXIT%
echo Artifact copy: %COPY_EXIT%
echo Installed ACL/MOTW audit: %AUDIT_EXIT%
echo Synthetic publisher start: %SYNTHETIC_START_EXIT%
echo Shared-memory IPC probe: %IPC_PROBE_EXIT%
echo Capture probe: %PROBE_EXIT%
echo Registered DLL: %DLL%
echo Frame Server logs: C:\ProgramData\CamBridge\logs\media-source-*.log
echo.
echo Review the HRESULT, capture count, ACL, and newest Frame Server log before declaring PASS.
if not "%COPY_EXIT%"=="0" exit /b %COPY_EXIT%
exit /b %INSTALL_EXIT%
