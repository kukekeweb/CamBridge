# Dynamic orientation validation

## Contract

CamBridge treats the Stage 2 1080p target as two exact layouts:

- landscape: `1920×1080 @ 60fps`
- portrait: `1080×1920 @ 60fps`

The Web Client derives `auto` from Safari's `screen.orientation.type` and
viewport dimensions. Explicit `portrait` or `landscape` selection takes
precedence. The active track dimensions are the transport contract; the preview
does not apply a second CSS rotation.

## Automated evidence

- Web capture constraints test: PASS for exact portrait dimensions.
- Web auto-orientation test: PASS for a portrait viewport.
- WebRTC sender test: PASS for portrait `1080×1920 @ 60fps` and landscape
  `1920×1080 @ 60fps`; unsupported dimensions remain rejected.
- Native shared-memory IPC test: PASS for portrait `1080×1920` NV12 frames.
- Native H.264 decoder configuration test: PASS; portrait dimensions are not
  rejected as invalid on the test host.
- Native Media Source contract test: PASS; portrait `1080×1920 @ 60fps` is
  exposed and a portrait-selected sample is delivered in the direct fixture.

## Live validation still required

The following are not claimed by these automated tests and require a real
Safari/Windows run:

1. Start Safari with `auto` in portrait and confirm `getSettings()` returns
   `1080×1920 @ 60fps`.
2. Connect WebRTC and verify the native receiver reports the same dimensions.
3. Confirm the decoder publishes portrait NV12 without rotation or resize.
4. Confirm the Windows camera client/Discord preview preserves portrait.
5. Rotate the device only between sessions unless a future renegotiation
   feature is explicitly added; a session keeps one stable media layout.

This document records orientation support and test evidence only. It does not
claim a live Stage 2 or Discord acceptance result.
