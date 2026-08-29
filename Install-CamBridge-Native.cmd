@echo off
setlocal EnableExtensions

set "ROOT=%~dp0"
set "BIN=%ROOT%build\native-mvp\Release"
set "INSTALL_ROOT=%ProgramFiles%\CamBridge\Native"
set "MANAGER=%INSTALL_ROOT%\cambridge_virtual_camera_manager.exe"
set "PROBE=%INSTALL_ROOT%\cambridge_capture_probe.exe"
set "SYNTHETIC=%INSTALL_ROOT%\cambridge_synthetic_publisher.exe"
set "DLL=%INSTALL_ROOT%\cambridge_media_source.dll"
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
echo Registered DLL:   %DLL%
echo Frame Server logs: C:\ProgramData\CamBridge\logs\media-source-*.log
echo.

powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%ROOT%scripts\Install-CamBridge-Native.ps1" -SourceRoot "%BIN%" -InstallRoot "%INSTALL_ROOT%"
set "COPY_EXIT=%ERRORLEVEL%"

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
powershell.exe -NoProfile -ExecutionPolicy Bypass -File "%ROOT%scripts\Inspect-CamBridge-NativeInstall.ps1" -InstallRoot "%INSTALL_ROOT%"
set "AUDIT_EXIT=%ERRORLEVEL%"

echo.
echo Synthetic publisher and capture probe (probe runs even after earlier failures):
set "SYNTHETIC_PID="
if exist "%SYNTHETIC%" (
  for /f "usebackq delims=" %%P in (`powershell.exe -NoProfile -Command "$p=Start-Process -FilePath '%SYNTHETIC%' -WorkingDirectory '%INSTALL_ROOT%' -RedirectStandardOutput '%LOGDIR%\synthetic-publisher.log' -RedirectStandardError '%LOGDIR%\synthetic-publisher-error.log' -PassThru; $p.Id"`) do set "SYNTHETIC_PID=%%P"
  timeout /t 1 /nobreak >nul
)
if exist "%PROBE%" (
  "%PROBE%"
  set "PROBE_EXIT=%ERRORLEVEL%"
) else (
  echo Installed capture probe is missing; probe skipped.
  set "PROBE_EXIT=2"
)
if defined SYNTHETIC_PID taskkill /pid %SYNTHETIC_PID% /t /f >nul 2>&1

echo.
echo CamBridge Native Install: %INSTALL_EXIT%
echo Artifact copy: %COPY_EXIT%
echo Installed ACL/MOTW audit: %AUDIT_EXIT%
echo Capture probe: %PROBE_EXIT%
echo Registered DLL: %DLL%
echo Frame Server logs: C:\ProgramData\CamBridge\logs\media-source-*.log
echo.
echo Review the HRESULT, capture count, ACL, and newest Frame Server log before declaring PASS.
if not "%COPY_EXIT%"=="0" exit /b %COPY_EXIT%
exit /b %INSTALL_EXIT%
