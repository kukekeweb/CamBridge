# CamBridge Web Client (Stage 1)

This is the iPhone Safari camera capability probe. It is served by the local
Windows Stage 1 HTTPS server; it has no external hosting dependency.

Stage 1 requests the selected resolution and frame rate with `exact`
constraints. A rejected request or a `getSettings()` mismatch is reported as
unsupported and is not retried with a lower resolution or frame rate.

The page measures displayed frames with
`HTMLVideoElement.requestVideoFrameCallback()` when Safari provides it. The
codec list and low-latency API list are runtime probes only. No
`RTCPeerConnection`, WebRTC sender, WSS signaling, or video transport is
created in this stage.

Open the URL printed by the Windows server, preferably the private-LAN IPv4
URL. The page requires HTTPS and camera permission.

## Diagnostic Matrix Probe

Use **Run Selected Camera Matrix** or **Run All Cameras** after camera
permission is granted. Each camera is tested with a fresh `getUserMedia()`
request for these exact combinations, in order:

```text
1280×720 @ 30/60
1920×1080 @ 30/60
2560×1440 @ 30/60
3840×2160 @ 30/60
```

Every previous track is stopped before the next request. A successful request
whose settings do not match is not stopped immediately: it remains active for
10 seconds so Track FPS and empirical 1-second/10-second FPS can be captured.
The result is marked red as `mismatch-observed` and classified as A, B, or C
when the returned values support one of those causes. Failed requests retain
their exception name and message.

Use **Copy JSON** or **Copy CSV** to copy the complete matrix, including
capabilities, settings, constraints, measurements, and diagnosis.

## 60fps Constraint Probe

After permission is granted, the page keeps the priming video track live while
it enumerates all `videoinput` devices. It then records a second enumeration
after stopping that track; the active-capture snapshot is the only source used
for Matrix selection. The diagnostic buttons always use
`deviceId: { exact: selectedDeviceId }`. If Safari exposes no device IDs during
active capture, Matrix execution is stopped and the exposure result is kept in
the diagnostics output.
The separate **60fps Constraint Probe** compares:

- resolution-free `frameRate: { exact: 60 }`
- a base track followed by `track.applyConstraints({ frameRate: { exact: 60 } })`
- `frameRate: { min: 60, ideal: 60 }`

If a probe actually reaches 60fps, width-only, height-only, and combined
width/height exact constraints are added using the returned resolution. The
probe also records the returned device ID, facing mode, settings, measured
FPS, and whether 60fps was only reported by the Track or measured in practice.
It never changes the normal capture fallback policy.

## 1080p60 Stability Test

Select the rear camera in the camera list and use **安定性テストを開始**.
The test performs one fresh exact request for 1920×1080 @ 60fps, keeps that
Track for 600 seconds, and does not silently reacquire it. The live panel shows
elapsed time, current FPS, minimum/average one-second FPS, and Track state.

The exported JSON contains schema version 1 and records requested/actual
settings, start/end time, elapsed time, total frames, one-second samples,
ten-second moving-average samples, minimum/maximum/average values, requested-FPS
deficiency, Track ended/mute/unmute events, intermediate settings changes,
resolution/FPS mismatches, `requestVideoFrameCallback()` state, JavaScript
errors, unhandled rejections, visibility/pagehide/pageshow events, background
intervals, and camera reacquisition count.

The test does not fail because of one transient one-second FPS dip. PASS requires
600 seconds completed, exact 1920×1080 @ 60 settings maintained, no Track ended
event, no unintended reacquisition, no JavaScript error/unhandled rejection,
and observed video frames. Manual stop, Track state loss, settings mismatch, or
an unavailable frame callback produces FAIL. The final JSON is the evidence for
the real iPhone acceptance; fake-clock unit tests do not replace it.
