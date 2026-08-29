@echo off
setlocal

set "ROOT=%~dp0"
set "BIN=%ROOT%build\native-mvp\Release"
set "MANAGER=%BIN%\cambridge_virtual_camera_manager.exe"
set "DLL=%BIN%\cambridge_media_source.dll"

if not exist "%MANAGER%" (
  echo CamBridge native build is missing: %MANAGER%
  exit /b 2
)

echo CamBridge Native uninstall
echo This command removes the CamBridge virtual camera and HKLM Media Source registration.
echo Administrator elevation may be required.
echo.

"%MANAGER%" --uninstall --source "%DLL%" --machine
set "UNINSTALL_EXIT=%ERRORLEVEL%"

echo.
echo Uninstall result: %UNINSTALL_EXIT%
echo Frame Server logs: C:\ProgramData\CamBridge\logs\media-source-*.log
exit /b %UNINSTALL_EXIT%
