# Dynamic Orientation Support Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Preserve the camera's actual portrait or landscape orientation from Safari through the CamBridge media contract without silently rotating or rejecting a valid 60fps track.

**Architecture:** The Web Client treats `1920x1080@60` and `1080x1920@60` as two exact layouts. The selected layout is derived from the live track settings and carried as width, height, stride, and orientation metadata. Windows components accept either layout, while each session keeps one stable output format and reports a format change instead of silently resizing.

**Tech Stack:** Safari Media Capture, WebRTC, JavaScript ES modules, C++20, Media Foundation, NV12 shared-memory IPC, Node.js tests, CMake/CTest.

**Spec:** `docs/superpowers/specs/2026-08-31-stage2-webrtc-native-mvp-design.md`

## Global Constraints

- Do not silently fall back from an exact requested layout or FPS.
- Preserve portrait as portrait and landscape as landscape; do not use CSS-only rotation as the transport contract.
- Keep WebRTC LAN-only with no TURN, external STUN, cloud relay, or OBS path.
- Do not change codec selection or signaling behavior as part of orientation support.
- Keep the existing Web Stage 1 diagnostics and 10-minute stability test intact.
- Every accepted layout must have unit/fixture coverage before live claims.

---

### Task 1: Define orientation-aware Web capture targets

**Files:**
- Modify: `web/client/src/settings.js`
- Modify: `web/client/src/capture-controller.js`
- Test: `web/client/tests/settings.test.js`
- Test: `web/client/tests/capture-controller.test.js`

**Interfaces:**
- Produce `captureDimensions(settings)` returning `{ width, height }`; portrait swaps the configured resolution dimensions, all other modes preserve them.
- Produce `matchesRequestedCapture(settings, actualSettings)` accepting only the selected exact layout and requested FPS.

- [x] **Step 1: Write failing tests** for portrait exact constraints and portrait actual settings.
- [x] **Step 2: Run the focused tests and confirm they fail.**
- [x] **Step 3: Implement the smallest orientation-aware constraint and comparison helpers.**
- [x] **Step 4: Run focused tests and the existing Web Stage 1 tests.**
- [ ] **Step 5: Commit** with `feat: make web capture targets orientation aware`.

### Task 2: Accept and expose both WebRTC layouts

**Files:**
- Modify: `web/client/src/webrtc-sender.js`
- Modify: `web/client/src/main.js`
- Test: `web/client/tests/webrtc-sender.test.js`

**Interfaces:**
- Produce `formatWebRtcLayout(settings)` returning `landscape` for `1920x1080` and `portrait` for `1080x1920`.
- `WebRtcSender.connect()` accepts either layout only when its FPS is 60 and reports the selected layout in status/diagnostics.

- [x] **Step 1: Write failing tests** for portrait acceptance, invalid dimensions, and layout text.
- [x] **Step 2: Run the focused tests and confirm they fail.**
- [x] **Step 3: Implement layout validation without changing codec or signaling.**
- [x] **Step 4: Run all web tests and syntax checks.**
- [ ] **Step 5: Commit** with `feat: accept portrait WebRTC tracks`.

### Task 3: Extend the native frame contract safely

**Files:**
- Modify: `windows/native-mvp/include/frame_ipc.h`
- Modify: `windows/native-mvp/src/frame_ipc.cpp`
- Modify: `windows/native-mvp/receiver/h264_decoder.cpp`
- Test: `windows/native-mvp/tests/frame_ipc_tests.cpp`
- Test: `windows/native-mvp/tests/h264_decoder_tests.cpp`

**Interfaces:**
- `Nv12Frame` accepts even dimensions up to 1920x1920 and stride at least width.
- Decoder output metadata preserves the actual decoded width and height.

- [x] **Step 1: Add failing portrait validation and synthetic NV12 tests.**
- [x] **Step 2: Run the focused CTest targets and confirm failure.**
- [x] **Step 3: Extend validation and decoder configuration without adding a resize.**
- [x] **Step 4: Run Debug and Release CTest.**
- [ ] **Step 5: Commit** with `feat: preserve portrait NV12 frame dimensions`.

### Task 4: Make Virtual Camera media types orientation-aware

**Files:**
- Modify: `windows/native-mvp/virtual-camera/cambridge_media_source.cpp`
- Modify: `windows/native-mvp/virtual-camera/cambridge_media_source.h`
- Test: `windows/native-mvp/tests/media_source_tests.cpp`

**Interfaces:**
- The source exposes `1920x1080@60 NV12` and `1080x1920@60 NV12` as selectable layouts.
- A session's selected media type remains stable; a live layout change reports a media-type transition and does not silently copy/rotate frames.

- [x] **Step 1: Add failing media-type and portrait sample tests.**
- [x] **Step 2: Run the focused CTest target and confirm failure.**
- [x] **Step 3: Implement the two media types using the existing latest-frame contract.**
- [x] **Step 4: Run synthetic capture tests and confirm both layouts.**
- [ ] **Step 5: Commit** with `feat: expose portrait virtual camera media type`.

### Task 5: Validate live format changes and document limitations

**Files:**
- Modify: `windows/native-mvp/receiver/native_receiver_main.cpp`
- Modify: `docs/superpowers/specs/2026-08-31-stage2-webrtc-native-mvp-design.md`
- Create: `docs/validation/dynamic-orientation-2026-08-31.md`

- [ ] **Step 1: Add diagnostics for received dimensions, orientation, and output media type.**
- [ ] **Step 2: Run synthetic portrait and landscape loopback tests.**
- [ ] **Step 3: Record that real Safari and Discord acceptance remain separate gates.**
- [ ] **Step 4: Run the complete web/native CI-equivalent checks.**
- [ ] **Step 5: Commit** with `docs: document dynamic camera orientation validation`.

## Verification Checklist

- [ ] Web tests pass with both exact layouts.
- [ ] CTest passes for landscape and portrait NV12 frame validation.
- [ ] No RGB conversion, frame queue, OBS path, or network protocol change was introduced.
- [ ] Live Safari verification records actual `getSettings()` dimensions before WebRTC connection.
- [ ] Windows capture-client/Discord verification confirms the selected orientation or records a concrete compatibility limitation.
