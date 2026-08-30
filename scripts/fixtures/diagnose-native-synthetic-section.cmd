@echo off
setlocal EnableExtensions EnableDelayedExpansion

set "ROOT=%~dp0..\..\"
set "INSTALL_ROOT=%ProgramFiles%\CamBridge\Native"
set "PROBE=%INSTALL_ROOT%\cambridge_capture_probe.exe"
set "SYNTHETIC=%INSTALL_ROOT%\cambridge_synthetic_publisher.exe"
set "IPC_PROBE=%INSTALL_ROOT%\cambridge_frame_ipc_probe.exe"
set "LOGDIR=%ROOT%build\native-mvp\diagnostics\fixture"
set "RUN_TAG=%RANDOM%-%RANDOM%"
if not exist "%LOGDIR%" mkdir "%LOGDIR%" >nul 2>&1

echo Synthetic publisher and capture probe:
set "SYNTHETIC_PID="
set "SYNTHETIC_PID_FILE=%LOGDIR%\synthetic-publisher.pid"
set "SYNTHETIC_START_EXIT=2"
set "IPC_READY_EXIT=2"
del /q "%SYNTHETIC_PID_FILE%" >nul 2>&1
if exist "%SYNTHETIC%" (
  echo Starting Synthetic Publisher in background...
  powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%ROOT%scripts\Start-CamBridge-Synthetic.ps1" -Executable "%SYNTHETIC%" -WorkingDirectory "%INSTALL_ROOT%" -LogDirectory "%LOGDIR%" -PidFile "%SYNTHETIC_PID_FILE%"
  set "SYNTHETIC_START_EXIT=!ERRORLEVEL!"
  if exist "%SYNTHETIC_PID_FILE%" set /p SYNTHETIC_PID=<"%SYNTHETIC_PID_FILE%"
  if defined SYNTHETIC_PID if exist "%IPC_PROBE%" (
    echo Checking shared-memory IPC readiness - bounded to about 10 seconds...
    "%IPC_PROBE%" 1 >"%LOGDIR%\ipc-readiness-%RUN_TAG%.log" 2>&1
    set "IPC_READY_EXIT=!ERRORLEVEL!"
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
  echo Running bounded capture probe - maximum 10 seconds...
  "%PROBE%"
  set "PROBE_EXIT=!ERRORLEVEL!"
) else (
  echo Installed capture probe is missing; probe skipped.
  set "PROBE_EXIT=2"
)
if defined SYNTHETIC_PID taskkill /pid %SYNTHETIC_PID% /t /f >nul 2>&1
del /q "%SYNTHETIC_PID_FILE%" >nul 2>&1

echo Fixture completed.
echo Synthetic publisher start: %SYNTHETIC_START_EXIT%
echo Shared-memory IPC probe: %IPC_PROBE_EXIT%
echo Capture probe: %PROBE_EXIT%
exit /b 0
