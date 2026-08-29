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

From the repository root, the reproducible one-time flow is:

```text
Install-CamBridge-Native.cmd
```

Run it as Administrator after building. It copies the Release artifacts to
`C:\Program Files\CamBridge\Native`, keeps the default Program Files ACL, registers
the machine-wide Media Source using that installed absolute path, verifies the HKLM
`InprocServer32` values, attempts virtual-camera Start, audits ACL/MOTW, and runs the
synthetic publisher plus capture probe even when an earlier step fails. The reverse
operation is `Uninstall-CamBridge-Native.cmd` (also elevated); it removes the
CurrentUser virtual camera, HKLM registration, and only the exact CamBridge install
directory.

The `--machine` manager path remains available for focused diagnostics, but the
build-directory DLL is not the production registration target. The installed Media
Source is machine-wide while the virtual camera continues to use
`MFVirtualCameraAccess_CurrentUser`.

Native MVP installation skips optional custom identity properties by default so
`AddProperty` permission behavior cannot mask the `IMFVirtualCamera::Start` result.
Use `--identity-properties` only for an explicit identity-property diagnostic.

The Media Source writes control-path diagnostics to
`C:\ProgramData\CamBridge\logs\media-source-<pid>.log` when possible. Logs are
PID-scoped so the CamBridge process and Windows Frame Server process can be separated.
They include DLL/class factory, QueryInterface IID/HRESULT, activation, initialization,
descriptor, allocator, Start/Stop/Shutdown events, IPC state, stream-announcement,
RequestSample, and the first three sample summaries. They do not log every video frame.

For an isolated synthetic check, start the Publisher for a finite interval and inspect
the shared-memory reader:

```powershell
.\build\native-mvp\Release\cambridge_synthetic_publisher.exe --duration-ms 5000
.\build\native-mvp\Release\cambridge_frame_ipc_probe.exe 3
```

The capture probe has a bounded child-process guard. Its default sample-delivery limit
is 10 seconds; use `--timeout-ms 2000` for a shorter diagnostic. On timeout it prints
the IPC mapping state and points to the Media Source control log instead of waiting
forever. A successful gate requires at least 120 samples.

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
