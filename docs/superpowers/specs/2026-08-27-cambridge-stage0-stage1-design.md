# CamBridge Stage 0 / Stage 1 Design

Date: 2026-08-27
Status: approved for implementation

## Scope

This project is a new native iPhone camera application and a future Windows
desktop receiver. This delivery covers Stage 0 design and Stage 1 iPhone
capture only.

Stage 2 networking, WebRTC, H.264/HEVC transport encoding, Windows decoding,
and Stage 3 virtual-camera registration are designed here but are not
implemented in this delivery.

The working directory initially contained no source files and is not currently
a Git repository. No existing application, driver, service, firewall rule, or
system setting is modified.

## Goals and non-goals

Stage 1 must provide an iOS-native camera preview that can:

- select the rear or front camera;
- enumerate the lenses exposed by the running device;
- enumerate and display capture formats, frame-rate ranges, pixel formats, and
  HDR capability;
- explicitly select 1920x1080 at 60 fps when the selected lens exposes it;
- display the actual active format and measured FPS;
- count dropped capture frames and show the drop reason when available;
- expose settings for orientation, output resolution, frame rate, quality, and
  codec without pretending that Stage 1 already encodes or transmits video.

Stage 1 must not silently change a requested resolution or FPS. If a selected
lens cannot provide the requested capture capability, the UI reports
`Unsupported` and does not select 30 fps as a hidden fallback.

## Product settings model

The settings model is independent of AVFoundation and future transport code.
It distinguishes the requested final output from the native capture format:

```text
CaptureRequest
  cameraPosition: front | back
  lensID: stable runtime device identifier
  outputResolution: 1920x1080 | 2560x1440 | 3840x2160
  targetFPS: 24 | 30 | 60 | other supported value
  orientation: portrait | portraitUpsideDown | landscapeLeft | landscapeRight
  quality: low | medium | high
  codec: h264 | hevc

CapturePlan
  nativeFormat: AVCaptureDevice.Format descriptor, or unsupported
  nativeDimensions: width x height
  nativeFPS: requested range or unsupported
  processingPath: directCapture | gpuScale | unsupported
  outputDimensions: requested dimensions
  outputTransform: orientation transform for the encoded/output frame
  pixelFormat: NV12 video-range preferred, actual verified format recorded
  capabilityWarnings: explicit, user-visible warnings
```

`quality` is a semantic preset only in Stage 1. Stage 2 maps it to a bitrate
and rate-control policy using output resolution, target FPS, and codec. No
bitrate constants are hard-coded in the Stage 1 capture service.

The output-resolution rule is explicit:

- If 2560x1440 is exposed as a native format and satisfies the requested FPS,
  capture it directly.
- Otherwise, if a suitable higher native format such as 3840x2160 exists,
  capture that native format and use a GPU pixel-transfer/downscale stage to
  produce 2560x1440.
- If neither direct capture nor a supported conversion path exists, report
  `Unsupported`.

The same rule applies to every requested resolution: the application records
the native capture dimensions separately from the final output dimensions.

## iPhone architecture

The iOS target uses Swift, SwiftUI for controls/status, AVFoundation for
capture, Core Media/Core Video for timestamps and pixel buffers, and a
`UIViewRepresentable` wrapper around `AVCaptureVideoPreviewLayer` for preview.

The capture queue is a dedicated serial `DispatchQueue`. UI state is published
on the main actor. The capture service owns session configuration, device
selection, active format assignment, frame delivery, and drop callbacks. It
does not own codec or network code.

Planned boundaries:

- `CaptureSettings.swift`: platform-neutral settings enums and request value
  types.
- `CameraFormatDescriptor.swift`: immutable AVFoundation format metadata,
  including dimensions, FPS ranges, pixel format, HDR, and binned status.
- `CameraFormatSelector.swift`: pure selection and direct-vs-GPU-scale plan
  logic. It never changes a setting to an unrequested FPS.
- `CaptureStatistics.swift`: timestamp-window FPS and dropped-frame counters.
- `CameraCaptureService.swift`: AVFoundation session and output delegate.
- `CameraPreviewView.swift`: preview layer only.
- `ContentView.swift`: settings, capability list, active-format status, and
  statistics display.

The Stage 1 output contract is a verified `CVPixelBuffer` plus a presentation
timestamp and orientation metadata. The later encoder consumes this contract;
the capture service does not return encoded bytes.

### Pixel format

NV12 (`kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange`) is the preferred
pipeline format because it is suitable for VideoToolbox H.264/HEVC encoding,
GPU scaling, Windows NV12 decode/output, and the planned Media Foundation
virtual camera path. The service verifies the actual format of delivered
buffers. Full-range NV12 is recorded as a distinct actual format; an RGB
fallback is reported as a warning and is not treated as an optimized path.

### Orientation

Orientation is represented in the settings and capture plan, not only in the
preview layer. The plan carries output dimensions and a transform. Stage 1
uses the transform for preview/output metadata and displays it. Stage 2 must
apply the same transform when producing encoded frames, so the transmitted
frame dimensions and orientation agree. No orientation is silently inferred
from a preview-only rotation.

### Stage 1 camera selection

At startup and whenever the camera position changes, the app creates discovery
sessions for the available built-in camera device types and enumerates the
actual devices on the connected iPhone. The UI identifies camera position,
localized lens name, device type, and available format count. Every listed
format includes width, height, min/max FPS, pixel format, HDR support, and
whether it is binned.

For the initial acceptance path the selector searches the selected lens for an
exact 1920x1080 format whose supported FPS range contains 60.0. It assigns
both active min and max frame duration to 1/60 only after selecting that exact
format, then re-reads `activeFormat` and active durations for the status panel.

No iPhone 17 lens is assumed to expose 2560x1440. Apple documents 4K60,
1080p60, H.264, and HEVC for iPhone 17, but the app treats those as product
capabilities to verify against the runtime AVFoundation device/format list.

## Windows architecture for later stages

The Windows application will be a C++20 x64 Win32 desktop application built
with CMake and Visual Studio 2022. It will separate:

```text
NetworkTransport -> H264/HEVC Decoder -> Frame Buffer -> Preview -> Virtual Camera Output
```

Each component publishes counters and timestamps for input/output FPS,
dropped frames, processing duration, and errors. WebRTC is the first transport
candidate. The application pins one libwebrtc revision for iOS and Windows;
the build is owned by the project rather than depending on an opaque paid
service or an unpinned binary.

WebRTC has a meaningful build/distribution constraint: current native iOS
usage is generally a source build or a separately maintained prebuilt SDK.
The project therefore defers adding the dependency until Stage 2 and records
the exact revision and build arguments when it is introduced. A transport
interface keeps a future RTP/UDP/LAN implementation replaceable.

VideoToolbox is the intended iPhone encoder backend. A later encoder must
request hardware acceleration and fail or report an explicit software path if
the requested hardware encoder is unavailable. The Windows decoder must log
the selected codec implementation and hardware/software path; “hardware
available” is not evidence that hardware was used.

## Virtual Camera design for Stage 3

The target is a device named `CamBridge Virtual Camera`, created through
`MFCreateVirtualCamera` on Windows 11. The source ID points to the CLSID of a
custom Media Foundation media source. The eventual implementation therefore
contains a media-source DLL, registration/install handling, a frame bridge, and
tests; it is not implemented as a simple preview-window trick.

The Microsoft Windows-Camera VirtualCamera sample is the reference structure:
media source, installer, manager, systray/configuration pieces, and tests.
The initial virtual-camera format will be NV12 1920x1080 at 60 fps where the
consumer supports it. No kernel driver is planned.

## Validation strategy

### Automated/static checks in this Windows workspace

- PowerShell validates that the Xcode project references every expected Swift
  source and test file.
- Source checks verify camera privacy usage text, no microphone permission,
  explicit 60 fps selection, no `30` fallback branch, drop callback presence,
  actual format readback, and the required UI labels.
- Swift unit tests cover the pure selector, output-plan logic, orientation
  dimensions, unsupported behavior, and rolling statistics. They require
  Xcode/Swift and are not claimed as run when those tools are absent.

### Physical-device acceptance

On a Mac with Xcode, or a hosted macOS runner, build the device target,
install it on the registered iPhone 17, and run the Stage 1 checklist:

1. Grant camera permission and select rear/front cameras.
2. Inspect all enumerated lenses and formats.
3. Select 1920x1080/60 and confirm the active format says exactly 1920x1080
   and a 60 FPS-supported range.
4. Confirm Actual FPS remains approximately 59-60 for at least 10 minutes.
5. Record dropped frames and confirm no hidden FPS fallback.
6. Repeat for each lens that claims 1080p60.
7. Exercise orientation settings and confirm output dimensions/transform in the
   status panel.

### iPhone 17 build, signing, and installation paths

There is no Apple-supported Xcode/device-signing workflow that runs entirely
on Windows. The reproducible paths are:

1. **Selected path: GitHub Actions unsigned device build + Windows Sideloadly.**
   Windows remains the source-authoring environment. The workflow runs on the
   GitHub Actions `xcode-27` macOS image, selects an installed Xcode 27 toolchain,
   asserts the `iphoneos27.0` SDK, runs tests, archives for
   `generic/platform=iOS` with `CODE_SIGNING_ALLOWED=NO`, and packages the
   archive's app into an unsigned IPA Artifact. No Apple ID, certificate,
   provisioning profile, or signing secret is used in Actions. Windows
   Sideloadly then signs the downloaded IPA with the user's Apple ID and
   installs it on the iPhone 17 running iOS 27. AltStore PAL remains untouched.
2. **Alternative: direct registered-device development build.** A Mac or hosted
   macOS interactive runner uses Xcode automatic signing, with the iPhone UDID
   registered to the Apple Developer team. This is not the selected workflow.
3. **Windows-only source/build preparation.** Windows can edit, run static
   checks, and prepare the archive inputs. It cannot replace Xcode/macOS for
   Apple code signing. Unofficial Windows sideload tools may install a
   user-signed IPA in some configurations, but they are not the acceptance
   path because signing validity, provisioning, device compatibility, and
   reproducibility vary.

The project will document the exact selected macOS runner, Xcode version,
bundle identifier, signing mode, build number, device registration, and
installation result when a real iPhone test is performed. Until then, a
Windows static result is explicitly not a Stage 1 physical-device pass.

## Risks and boundaries

- iPhone 17's published camera recording capabilities do not guarantee that
  every lens exposes every AVFoundation format.
- 2560x1440 may require GPU scaling and its performance must be measured on
  the real device.
- HDR capture may conflict with an SDR H.264/HEVC pipeline; Stage 1 reports
  HDR support but does not silently enable a conversion.
- Xcode and a signed device are unavailable in this Windows workspace, so
  device build/install and 10-minute stability remain unverified.
- WebRTC and virtual-camera dependencies remain intentionally out of Stage 1.

## Source references

- Apple iPhone 17 technical specifications: https://www.apple.com/iphone-17/specs/
- Apple AVCaptureDevice.Format: https://developer.apple.com/documentation/avfoundation/avcapturedevice/format
- Apple camera format configuration: https://developer.apple.com/documentation/avfoundation/capture-device-formats
- Apple VideoToolbox: https://developer.apple.com/documentation/videotoolbox
- WebRTC native iOS guidance: https://webrtc.googlesource.com/src/+/refs/heads/main/docs/native-code/ios/README.md
- Microsoft MFCreateVirtualCamera: https://learn.microsoft.com/en-us/windows/win32/api/mfvirtualcamera/nf-mfvirtualcamera-mfcreatevirtualcamera
- Microsoft Virtual Camera sample: https://github.com/microsoft/Windows-Camera/tree/master/Samples/VirtualCamera
- Apple development provisioning: https://developer.apple.com/help/account/provisioning-profiles/create-a-development-provisioning-profile/
- Apple distribution and TestFlight: https://developer.apple.com/documentation/xcode/distributing-your-app-for-beta-testing-and-releases/
- GitHub Actions Xcode 27 runner image: https://github.com/actions/runner-images/blob/main/images/macos/xcode-27-Readme.md
