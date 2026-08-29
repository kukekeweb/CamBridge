# CamBridge Native MVP

This directory is the Windows-native path for the direct camera MVP. It is intentionally separate from the Web Stage 1 client and the legacy `ios/` implementation.

The first milestone is synthetic-only:

```text
Synthetic NV12 1920x1080@60
  -> latest-frame shared memory IPC
  -> Custom Media Source in the Windows Frame Server
  -> MFCreateVirtualCamera
  -> Media Foundation capture client
```

The shared-memory mapping is a bounded two-slot latest-frame buffer. A consumer that falls behind observes the newest sequence and does not drain an old frame queue. Named events are notifications only; video is not sent through a Named Pipe.

The current source tree contains the IPC contract, synthetic producer, Custom Media
Source DLL, registration manager, and a Media Foundation capture probe. WebRTC,
H.264 RTP receive, and H.264 decode are later milestones and are not implemented yet.

## Virtual Camera installation boundary

`cambridge_virtual_camera_manager` deliberately separates source registration from
normal runtime. The first test is per-user (`HKCU` / `MFVirtualCameraAccess_CurrentUser`):

```powershell
.\cambridge_virtual_camera_manager.exe --install --source .\cambridge_media_source.dll
```

If the Windows Frame Server cannot load a per-user COM source, the supported fallback
is a one-time elevated install. Run the same command with `--machine` from an elevated
PowerShell, then run CamBridge normally without elevation:

```powershell
.\cambridge_virtual_camera_manager.exe --install --source .\cambridge_media_source.dll --machine
```

The manager checks the actual Windows build number (not the ProductName string) and
requires build 22000 or newer. `--machine` writes HKLM and therefore requires UAC;
do not use it as the normal startup path. The current implementation records the
per-user result and does not claim live Frame Server registration until a capture
client can enumerate and open the camera.

The Media Source is designed for the separate Frame Server process boundary. It reads
the latest NV12 frame from the fixed shared-memory mapping and does not access the
receiver's in-process objects. Named events are notification-only.

## Build

Use a Visual Studio Developer Command Prompt or invoke `VsDevCmd.bat` before CMake. The project targets C++20 and Windows SDK 10.0.26100.0 or newer.

```powershell
cmake -S windows/native-mvp -B build/native-mvp -G "Visual Studio 17 2022" -A x64
cmake --build build/native-mvp --config Release
ctest --test-dir build/native-mvp -C Release --output-on-failure
```

The build itself does not change the registry, firewall, or camera registration.
Installation commands above are explicit runtime operations and should be tested
separately from the build.
