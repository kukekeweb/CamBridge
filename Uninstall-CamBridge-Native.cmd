@echo off
setlocal

set "ROOT=%~dp0"
set "INSTALL_ROOT=%ProgramFiles%\CamBridge\Native"
set "MANAGER=%INSTALL_ROOT%\cambridge_virtual_camera_manager.exe"
set "DLL=%INSTALL_ROOT%\cambridge_media_source.dll"
set "DLL_MANIFEST=%INSTALL_ROOT%\cambridge_media_source.active.txt"

if exist "%DLL_MANIFEST%" set /p DLL=<"%DLL_MANIFEST%"

fltmc >nul 2>&1
if errorlevel 1 (
  echo This launcher must be run as Administrator.
  exit /b 740
)

echo CamBridge Native uninstall
echo Virtual Camera Remove, HKLM COM registration removal, and Program Files cleanup.
echo Install root: %INSTALL_ROOT%
echo.

if exist "%MANAGER%" (
  "%MANAGER%" --uninstall --source "%DLL%" --machine
  set "REMOVE_EXIT=%ERRORLEVEL%"
) else (
  echo Installed manager is missing; Virtual Camera Remove skipped.
  set "REMOVE_EXIT=2"
)

echo.
echo Registry verification after Remove:
reg.exe query "HKLM\Software\Classes\CLSID\{F6DC0D8C-8D0E-4DD2-9F5C-A9B83A2A3A61}\InprocServer32" 2>&1

if exist "%INSTALL_ROOT%" (
  rmdir /s /q "%INSTALL_ROOT%"
  set "FILES_EXIT=%ERRORLEVEL%"
) else (
  echo Install root already absent.
  set "FILES_EXIT=0"
)

echo.
echo Virtual Camera/COM result: %REMOVE_EXIT%
echo Program Files cleanup result: %FILES_EXIT%
set "UNINSTALL_EXIT=%REMOVE_EXIT%"
echo Frame Server logs: C:\ProgramData\CamBridge\logs\media-source-*.log
exit /b %UNINSTALL_EXIT%
