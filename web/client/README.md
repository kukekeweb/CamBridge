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
