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
