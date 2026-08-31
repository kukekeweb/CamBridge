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
Source DLL, registration manager, and a Media Foundation capture probe. The
dependency-free receiver/session core, the opt-in libdatachannel adapter, H.264
RTP depacketizer fixtures, and the Media Foundation H.264-to-NV12 decoder
boundary are also present. Receiver-to-decoder-to-IPC wiring is covered by a
fixture and a native RTP loopback; the receiver probe reports peer/ICE state,
H.264 offer/answer presence, and access-unit/pipeline metrics. Live Safari
WebRTC reception, hardware-path confirmation, sustained decoding, and
integrated network-to-Virtual-Camera output remain open and must not be
inferred from the unit or fixture tests.

The dependency-free `receiver/receiver_session.*` core now provides the first
native receiver boundary: one-session lifecycle, Safari Offer validation for a
single sending H.264 video m-line, and private-IPv4 host-candidate policy. It
does not open a PeerConnection or receive media. The opt-in
`receiver/libdatachannel_receiver.*` adapter now covers PeerConnection creation,
recv-only H.264 track setup, validated Offer application, Answer callback, and
H.264 RTP depacketizer wiring. It deliberately does not claim Safari ICE/DTLS/
SRTP interoperability, received H.264 access units, decoding, or IPC output.
The opt-in `NativeSignalingSession` and WSS wrapper add the versioned local
`hello`/`offer`/`answer`/`ice` boundary. The server and native receiver have
also been checked through a local WSS startup/waiting run, but this is not a
live Safari interoperability result.

After an Answer is applied, the Web Client samples
`RTCPeerConnection.getStats()` and displays the runtime H.264 codec, sender
FPS, encoded/dropped frames, bitrate, packet loss, RTT, and jitter when Safari
exposes those fields. Missing fields remain unavailable rather than being
inferred. The native receiver probe reports the H.264 codec strings found in
the remote Offer and local Answer, plus decoded/published FPS derived from
media timestamps, output dimensions/stride, and the selected decoder's
hardware flag and transform name.

The opt-in `cambridge_native_receiver.exe` is a bounded native receiver probe
around that wrapper. It accepts the WSS URL and the CamBridge Local CA; by
default it registers as an `auto` native listener and adopts the first browser
session ID received by the local broker. This removes the need to copy a
session ID from the Web Client for the normal one-iPhone/one-PC flow. An
explicit `--session-id <id>` remains available for deterministic diagnostics.
The probe prints receiver state, ICE state, H.264 offer/answer presence,
access-unit counts, and RTP timestamps. Start this probe before pressing Connect in Safari
so the native peer is ready for the browser Offer:

```powershell
.\cambridge_native_receiver.exe `
  --url wss://192.168.11.2:8443/signaling `
  --ca C:\path\to\CamBridge-Local-CA.pem `
  --bind-address 192.168.11.2
```

The reserved `auto` value is accepted only for the native role. The broker
binds it to the first browser session, and resets the binding when that browser
disconnects. This is a bounded pairing convenience, not a claim that the
native WebSocket transport itself automatically reconnects after its TCP/TLS
socket is lost. When the signaling socket remains open and Safari sends a
close/re-offer sequence, the native session now recycles only its WebRTC
receiver and accepts the next Offer; this behavior is covered by the native
signaling-session test. Full live Safari reconnect and WSS socket reconnect
remain separate runtime acceptance checks.

`--duration-ms` is available for a bounded local run and `--allow-insecure-tls`
is restricted to an explicit local probe. The independent
`receiver/latest_frame_publisher.*` boundary accepts a decoded `Nv12Frame` and
publishes it through the existing shared-memory contract; it is unit-tested.
The `receiver/h264_decoder.*` boundary independently converts valid H.264 access
units to NV12 and records the selected Media Foundation transform. The
`receiver/receiver_media_pipeline.*` boundary connects it to the existing
latest-frame IPC, and the default native receiver probe wires received access
units into that boundary; `--no-publish` keeps the access-unit-only diagnostic
mode. The probe updates the shared-memory frame provider only after real H.264
access units arrive. A successful build or signaling loopback
test must not be reported as Safari ICE/DTLS/SRTP or media-receive success.

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

If the existing Media Source DLL is still loaded by Frame Server, Windows may reject
an in-place replacement with a sharing violation. The installer then copies the new
DLL side-by-side under a versioned filename and records the active path in
`cambridge_media_source.active.txt`; the manager and inspection step use that path.
Before copying, it also stops only an existing CamBridge Synthetic Publisher whose
executable path is inside the CamBridge install root, because that test process can
otherwise lock its own Program Files executable.
The installer stops before registration and probing if artifact copy fails, so an old
DLL cannot be reported as a successful new installation.

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
per-user result and the installed synthetic capture gate can enumerate and open
the camera for 120 NV12 samples. This does not claim live WebRTC or Discord
acceptance.

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

The libdatachannel adapter is an opt-in target so the baseline build remains
dependency-free. With a vcpkg checkout matching the repository `vcpkg.json`:

```powershell
vcpkg install --triplet x64-windows --clean-after-build
cmake -S windows/native-mvp -B build/native-mvp-libdatachannel `
  -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE=<vcpkg>\scripts\buildsystems\vcpkg.cmake `
  -DCAMBRIDGE_ENABLE_LIBDATACHANNEL=ON
cmake --build build/native-mvp-libdatachannel --config Release
ctest --test-dir build/native-mvp-libdatachannel -C Release --output-on-failure
```

The adapter build must not be used as evidence of Safari interoperation until
the separate LAN probe passes. It also does not change the existing Virtual
Camera registration or Media Source process boundary.

The opt-in build also includes `cambridge_latest_frame_publisher_tests`, which
verifies decoded-NV12 handoff, invalid-frame rejection, and publisher metrics
without loading the Frame Server or registering a camera.

The build itself does not change the registry, firewall, or camera registration.
Installation commands above are explicit runtime operations and should be tested
separately from the build.
