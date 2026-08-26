# CamBridge Stage 1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build the native iOS Stage 1 camera app that enumerates real AVFoundation capabilities, explicitly selects verified 1080p60 when available, measures actual FPS/drops, and preserves future output/codec/orientation settings without implementing transport.

**Architecture:** Platform-neutral settings, format descriptors, selection plans, and statistics are kept separate from the AVFoundation session and SwiftUI view. The capture service delivers timestamped pixel buffers and verified metadata; future encoder and transport components will consume that boundary without changing camera-selection code.

**Tech Stack:** Swift, SwiftUI, AVFoundation, Core Media, Core Video, XCTest, Xcode project, PowerShell static validation.

**Spec:** `docs/superpowers/specs/2026-08-27-cambridge-stage0-stage1-design.md`

## Global Constraints

- Stage 1 implements capture and preview only; WebRTC, encoding, decoding, networking, and virtual camera output remain out of scope.
- Requested FPS and resolution never silently fallback; unsupported capabilities are reported explicitly.
- Native capture dimensions and final output dimensions are different fields.
- NV12 video-range is the preferred pixel format and actual delivered buffers are verified.
- Orientation includes output dimensions and transform metadata, not preview rotation alone.
- H.264/HEVC and quality are settings-model values only; bitrate mapping belongs to Stage 2.
- No microphone permission, external service, driver, firewall, or OS setting is added.
- Xcode is unavailable in the current Windows workspace; do not claim Swift compilation or iPhone acceptance without fresh evidence.

### Task 1: Define testable settings, descriptors, and selection plans

**Files:**
- Create: `ios/CamBridge/CamBridgeApp/CaptureSettings.swift`
- Create: `ios/CamBridge/CamBridgeApp/CameraFormatDescriptor.swift`
- Create: `ios/CamBridge/CamBridgeApp/CameraFormatSelector.swift`
- Test: `ios/CamBridge/CamBridgeTests/CameraFormatSelectorTests.swift`

**Interfaces:**
- `CaptureSettings` contains `CameraPosition`, `OutputResolution`, `TargetFrameRate`, `CaptureOrientation`, `QualityPreset`, and `VideoCodec`.
- `CameraFormatDescriptor` contains `width`, `height`, `minFPS`, `maxFPS`, `pixelFormat`, `isHDRSupported`, and `isVideoBinned`.
- `CameraFormatSelector.select(request:formats:) -> CapturePlan` returns either a supported direct/GPU-scale plan or an explicit unsupported result.

- [ ] **Step 1: Write failing tests**

```swift
func testSelectsExact1080p60WithoutFallback() {
    let request = CaptureRequest(outputResolution: .hd1080, targetFPS: 60)
    let result = CameraFormatSelector.select(request: request, formats: [
        .init(width: 1920, height: 1080, minFPS: 30, maxFPS: 60,
              pixelFormat: .nv12VideoRange, isHDRSupported: false, isVideoBinned: false),
        .init(width: 1920, height: 1080, minFPS: 24, maxFPS: 30,
              pixelFormat: .nv12VideoRange, isHDRSupported: false, isVideoBinned: false)
    ])
    XCTAssertEqual(result.nativeDimensions, .init(width: 1920, height: 1080))
    XCTAssertEqual(result.processingPath, .directCapture)
    XCTAssertEqual(result.targetFPS, 60)
}

func testReportsUnsupportedInsteadOfChanging60To30() {
    let request = CaptureRequest(outputResolution: .uhd4K, targetFPS: 60)
    let result = CameraFormatSelector.select(request: request, formats: [
        .init(width: 3840, height: 2160, minFPS: 24, maxFPS: 30,
              pixelFormat: .nv12VideoRange, isHDRSupported: false, isVideoBinned: false)
    ])
    XCTAssertEqual(result.status, .unsupported)
    XCTAssertEqual(result.targetFPS, 60)
}

func testPlansGpuDownscaleWhen1440pNativeFormatIsAbsent() {
    let request = CaptureRequest(outputResolution: .qhd1440, targetFPS: 60)
    let result = CameraFormatSelector.select(request: request, formats: [
        .init(width: 3840, height: 2160, minFPS: 24, maxFPS: 60,
              pixelFormat: .nv12VideoRange, isHDRSupported: false, isVideoBinned: false)
    ])
    XCTAssertEqual(result.status, .supported)
    XCTAssertEqual(result.processingPath, .gpuScale)
    XCTAssertEqual(result.outputDimensions, .init(width: 2560, height: 1440))
}
```

- [ ] **Step 2: Run the focused test before implementation**

Run from a Mac with Xcode:

```text
xcodebuild test -project ios/CamBridge/CamBridge.xcodeproj -scheme CamBridge -destination 'platform=iOS Simulator,name=iPhone 16'
```

Expected: FAIL because the model and selector are not defined yet. In the current Windows workspace, record `xcodebuild` as unavailable rather than treating a static check as a pass.

- [ ] **Step 3: Implement the minimal model and selector**

Use typed enums for the requested output values. Select a direct format only when dimensions match and `minFPS...maxFPS` contains the requested FPS. For 2560x1440, select a matching native format first; otherwise select the smallest supported higher format that can be GPU-scaled and retains the requested FPS. Return `.unsupported` for missing FPS or dimensions and preserve the original request.

- [ ] **Step 4: Run the focused tests again**

Run the same `xcodebuild test` command. Expected: all selector tests PASS with no warnings.

### Task 2: Add capture statistics and AVFoundation metadata conversion

**Files:**
- Create: `ios/CamBridge/CamBridgeApp/CaptureStatistics.swift`
- Modify: `ios/CamBridge/CamBridgeApp/CameraFormatDescriptor.swift`
- Test: `ios/CamBridge/CamBridgeTests/CaptureStatisticsTests.swift`

**Interfaces:**
- `CaptureStatistics.recordFrame(presentationTimestamp:)`
- `CaptureStatistics.recordDrop(reason:)`
- `CaptureStatistics.snapshot(now:) -> CaptureStatisticsSnapshot`
- `CameraFormatDescriptor.from(format:) -> CameraFormatDescriptor`

- [ ] **Step 1: Write failing statistics tests**

```swift
func testComputesDeliveredFPSFromPresentationTimestamps() {
    var stats = CaptureStatistics()
    for index in 0..<60 {
        stats.recordFrame(presentationTimestamp: CMTime(value: CMTimeValue(index), timescale: 60))
    }
    let snapshot = stats.snapshot(now: 1.0)
    XCTAssertEqual(snapshot.deliveredFrames, 60)
    XCTAssertEqual(snapshot.fps, 60, accuracy: 0.01)
}

func testCountsDroppedFramesAndPreservesReason() {
    var stats = CaptureStatistics()
    stats.recordDrop(reason: "frameWasLate")
    stats.recordDrop(reason: "frameWasLate")
    let snapshot = stats.snapshot(now: 1.0)
    XCTAssertEqual(snapshot.droppedFrames, 2)
    XCTAssertEqual(snapshot.dropReasons["frameWasLate"], 2)
}
```

- [ ] **Step 2: Run the focused tests and confirm the expected failure**

Run the same Xcode test command with `-only-testing:CamBridgeTests/CaptureStatisticsTests`. Expected: FAIL because the statistics types are absent.

- [ ] **Step 3: Implement timestamp-window statistics**

Keep cumulative delivered/drop counters and compute a rolling FPS from recent timestamps. Do not use a target FPS as the measured result. Store drop reasons as strings so platform-specific AVFoundation attachment values can be displayed without coupling the model to the UI.

- [ ] **Step 4: Implement AVFoundation format conversion**

Read dimensions from `CMVideoFormatDescriptionGetDimensions`, frame ranges from `videoSupportedFrameRateRanges`, the media subtype from `CMFormatDescriptionGetMediaSubType`, and HDR/binned flags from `AVCaptureDevice.Format`. Preserve all ranges in the descriptor used by the capability list.

- [ ] **Step 5: Run all model tests**

Run:

```text
xcodebuild test -project ios/CamBridge/CamBridge.xcodeproj -scheme CamBridge -destination 'platform=iOS Simulator,name=iPhone 16'
```

Expected: all model tests PASS.

### Task 3: Implement the AVFoundation capture service

**Files:**
- Create: `ios/CamBridge/CamBridgeApp/CameraCaptureService.swift`
- Create: `ios/CamBridge/CamBridgeApp/CameraPreviewView.swift`
- Create: `ios/CamBridge/CamBridgeApp/Info.plist`

**Interfaces:**
- `CameraCaptureService.requestCameraAccess() async -> Bool`
- `CameraCaptureService.start(request:) throws`
- `CameraCaptureService.stop()`
- Published state: `availableCameras`, `availableFormats`, `activeFormatDescription`, `statisticsSnapshot`, `status`, `previewLayer`

- [ ] **Step 1: Add the service contract test fixture**

Add this hardware-independent test before implementing the service:

```swift
func testCapturePlanPreservesRequestedFPSAtServiceBoundary() {
    let request = CaptureRequest(outputResolution: .hd1080, targetFPS: 60)
    let plan = CameraFormatSelector.select(request: request, formats: [
        .init(width: 1920, height: 1080, minFPS: 30, maxFPS: 60,
              pixelFormat: .nv12VideoRange, isHDRSupported: false, isVideoBinned: false)
    ])
    XCTAssertEqual(plan.status, .supported)
    XCTAssertEqual(plan.targetFPS, 60)
    XCTAssertEqual(plan.outputDimensions, .init(width: 1920, height: 1080))
}
```

The test must not instantiate AVFoundation hardware; it proves the service
boundary receives the exact plan selected from the request.

- [ ] **Step 2: Run the fixture test and confirm it fails for the absent service contract**

Run the focused XCTest. Expected: FAIL because `CameraCaptureService` and its published state are absent.

- [ ] **Step 3: Implement session setup on a serial queue**

Create an `AVCaptureSession`, discover actual front/back built-in devices, add the selected device input, and add `AVCaptureVideoDataOutput`. Prefer `kCVPixelFormatType_420YpCbCr8BiPlanarVideoRange`, set `alwaysDiscardsLateVideoFrames` to true for low-latency capture, and record the actual delivered pixel format.

- [ ] **Step 4: Implement exact active-format selection**

Lock the device, assign only the `CapturePlan.nativeFormat`, set min/max frame duration to the requested FPS, unlock, and re-read `activeFormat`, `activeVideoMinFrameDuration`, and `activeVideoMaxFrameDuration`. If no exact plan exists, leave the session unstarted and expose `Unsupported`.

- [ ] **Step 5: Implement sample and drop callbacks**

For each delivered sample buffer, read the PTS and pixel-buffer subtype, update statistics, and publish the latest frame metadata. In `captureOutput(_:didDrop:from:)`, read `kCMSampleBufferAttachmentKey_DroppedFrameReason` when present, increment the counter, and publish the reason.

- [ ] **Step 6: Implement preview transform metadata**

Expose the requested orientation, output dimensions, and transform in the published capture state. Apply only preview display adjustments to `AVCaptureVideoPreviewLayer`; keep the output transform in the capture plan so Stage 2 can apply it to encoded frames.

- [ ] **Step 7: Run compilation and tests on macOS**

Run the full `xcodebuild test` command and then a device build with a registered iPhone 17. In Windows, run only the static validator and report compilation as unexecuted.

### Task 4: Add the Stage 1 UI and Xcode target metadata

**Files:**
- Create: `ios/CamBridge/CamBridgeApp/CamBridgeApp.swift`
- Create: `ios/CamBridge/CamBridgeApp/ContentView.swift`
- Create: `ios/CamBridge/CamBridge.xcodeproj/project.pbxproj`

**Interfaces:**
- UI labels: `Camera`, `Lens`, `Resolution`, `Target FPS`, `Actual FPS`, `Quality`, `Encoder`, `Orientation`, `Status`, and `Dropped Frames`.
- UI actions call `CameraCaptureService` for camera switch, lens selection, and start/stop.

- [ ] **Step 1: Write a static UI contract fixture**

The validator fixture is the required-label array in
`scripts/validate-stage1.ps1`; it must include exactly these strings:

```powershell
$requiredLabels = @(
  'Camera', 'Lens', 'Resolution', 'Target FPS', 'Actual FPS',
  'Quality', 'Encoder', 'Orientation', 'Status', 'Dropped Frames'
)
```

It must fail until the SwiftUI view and plist are present.

- [ ] **Step 2: Run the validator and confirm the expected failure**

Run `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/validate-stage1.ps1`. Expected: FAIL listing missing files/labels.

- [ ] **Step 3: Implement the SwiftUI view**

Show the actual selected camera/lens, native capture dimensions, final output dimensions, target FPS, measured FPS, drop count, current pixel format, HDR flag, and explicit unsupported/error status. Keep quality and encoder controls visible but disabled or marked `Stage 2` until transport exists.

- [ ] **Step 4: Implement camera preview bridging**

Use `CameraPreviewView` to host the service’s preview layer. Do not use the preview layer’s orientation as the only output orientation state.

- [ ] **Step 5: Configure privacy and target metadata**

Add `NSCameraUsageDescription` only. Set the iOS deployment target and device target consistently in the project file; do not add microphone, background audio, network, or unrelated capabilities.

- [ ] **Step 6: Run the static validator**

Expected: PASS for required file references, labels, privacy key, exact-60 selection symbols, drop callback, active-format readback, and no automatic 30 FPS fallback.

### Task 5: Document and execute the Windows-to-iPhone verification handoff

**Files:**
- Create: `scripts/validate-stage1.ps1`
- Create: `ios/README.md`
- Modify: `docs/superpowers/specs/2026-08-27-cambridge-stage0-stage1-design.md`

- [ ] **Step 1: Write validator assertions before implementation**

The PowerShell script must resolve the repository root from its own location, verify exact expected files, scan for required symbols/labels, and fail with a nonzero exit code on missing content. It must not invoke a compiler or modify files.

- [ ] **Step 2: Run the validator before the app files exist**

Expected: FAIL with actionable missing-file output.

- [ ] **Step 3: Implement the validator and handoff README**

The README must give exact macOS/Xcode/TestFlight and registered-device commands, distinguish development/ad hoc/TestFlight signing, state that Windows-only source preparation is not a physical-device test, and include the 10-minute acceptance checklist.

- [ ] **Step 4: Run the validator after implementation**

Expected: PASS with a summary of checked files and symbols.

- [ ] **Step 5: Inspect the final diff and environment evidence**

Run `git status --short` only if a repository exists; otherwise list created files and record that no commit is possible. Run `Get-Command xcodebuild,swift` and preserve the result in the final report. Do not claim iPhone build, signing, installation, or real FPS results unless executed.
