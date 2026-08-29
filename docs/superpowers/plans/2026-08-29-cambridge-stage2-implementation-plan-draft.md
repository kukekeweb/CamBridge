# CamBridge Native MVP Implementation Plan

**Goal:** OBSを使わず、Safari WebRTC映像をWindowsのCamBridge Virtual Cameraとして公開する。

**Spec:** `docs/superpowers/specs/2026-08-29-cambridge-stage2-design-draft.md`

## Guardrails

- Web Stage 1と`ios/` legacy implementationは変更しない。
- WebRTC、decoder、Virtual Cameraを一つの未検証経路として実装しない。
- Synthetic Virtual Cameraが合格するまでSafari media transportを統合しない。
- TURN、外部STUN、cloud relay、OBS、kernel driver、DirectShow filterを追加しない。
- UAC、COM registration、Virtual Camera registrationはdry-runと実測結果を分離する。

## Phase 0: environment and dependency decision

**Files:** `docs/...stage2-design-draft.md`, `windows/native-mvp/README.md`, CI workflow

- Record actual Windows build number and SDK/toolchain.
- Pin libdatachannel revision and license/third-party notices.
- Add a CMake configure/build smoke target.
- Keep libwebrtc as a documented contingency only.

## Phase 1: synthetic Virtual Camera first

**Files:**

- `windows/native-mvp/virtual-camera/`
- `windows/native-mvp/frame-ipc/`
- `windows/native-mvp/tests/`
- `scripts/`

Tasks:

1. Define `Nv12Frame` and a bounded latest-frame provider.
2. Implement shared-memory header/slots with atomic sequence and event notification.
3. Implement a deterministic 1920x1080@60 NV12 pattern generator.
4. Implement Custom Media Source with black output when no frame is available.
5. Implement `MFCreateVirtualCamera` registration using CurrentUser first.
6. Add install/uninstall command separation and report UAC/registration status.
7. Implement a Media Foundation Source Reader capture client.
8. Enumerate by device identity/SourceId, not Friendly Name equality.

Tests:

- shared-memory sequence/overwrite and stale-reader tests
- NV12 stride/timestamp/frame-rate tests
- Media Source state/event/sample tests
- Virtual Camera registration dry-run and live test
- capture client enumeration/open/sample tests
- synthetic 10-minute 60fps test

Commit: `feat: add synthetic CamBridge virtual camera`

## Phase 2: Frame Server process-boundary validation

- Run the Custom Media Source under the actual Frame Server activation path.
- Verify the source can open the shared memory created by Native Publisher.
- Verify cross-process synchronization and cleanup after producer exit.
- Test producer crash, consumer restart, and stale shared-memory version.

Commit: `test: verify virtual camera frame server ipc`

## Phase 3: signaling and receiver skeleton

**Files:**

- `windows/stage1-server/server.mjs`
- `windows/stage1-server/signaling/`
- `windows/native-mvp/receiver/`
- `windows/native-mvp/tests/`

Tasks:

- Add same-origin WSS with schema and size validation.
- Add one-to-one session lifecycle and reconnect cleanup.
- Build libdatachannel with no configured ICE server.
- Add recvonly video track and H.264 payload negotiation.
- Record candidate pair and reject relay evidence in direct-LAN mode.

Tests:

- signaling schema, pairing, malformed message, disconnect, reconnect
- SDP H.264 payload mapping and recvonly direction
- candidate classification and private IPv4 validation
- receiver state lifecycle without media

Commit: `feat: add native receiver and local signaling`

## Phase 4: Safari-H.264 interoperability Probe

- Add Stage 2-only sender code that reuses the exact active Stage 1 track.
- Prefer runtime-advertised H.264 using `setCodecPreferences` when available.
- Exchange Offer/Answer/ICE over same-origin WSS.
- Verify ICE/DTLS/SRTP connected, LAN-direct pair, H.264 selected.
- Verify `onFrame` access units, RTP timestamps, RTCP, and reconnect.

If the Probe has a major libdatachannel/Safari incompatibility, stop media integration and switch the receiver dependency to the pinned libwebrtc plan.

Commit: `test: prove safari h264 native interoperability`

## Phase 5: H.264 decode and publish

- Normalize H.264 access-unit boundaries without re-encoding.
- Enumerate Media Foundation H.264 hardware MFTs first.
- Decode to NV12, preserving timestamps and sequence.
- Publish decoded frames through the existing IPC only.
- Log selected transform and explicit software fallback.

Tests:

- H.264 depacketizer Annex-B/length-prefix fixtures
- SPS/PPS and keyframe handling
- decoder timestamp/order/error tests
- hardware/software path evidence tests
- decoded NV12 IPC test

Commit: `feat: decode h264 into published nv12 frames`

## Phase 6: Native MVP integration

- Connect Safari receiver to decoder and publisher.
- Keep Virtual Camera Media Source independent of WebRTC.
- Start HTTPS/WSS, receiver, and Virtual Camera from one command.
- Keep no-signal camera enumerated with black frames.
- Add connection, codec, decoder, IPC, and camera status output.

Commit: `feat: integrate safari receiver with cambridge camera`

## Phase 7: acceptance and release evidence

- Capture 10 minutes in Windows capture client.
- Capture 10 minutes in Discord without OBS.
- Verify no cloud/TURN/external STUN path.
- Record registration/UAC/COM load method, codec, decoder path, FPS, drops, reconnects, memory/CPU/GPU.
- Add D3D11 preview only after Native MVP acceptance.

## CI boundary

GitHub Actions may build and unit-test C++ components and run synthetic Media Foundation tests where the runner permits. CI must not claim live Discord, Frame Server registration, camera privacy, or Safari-LAN acceptance. Those remain explicit Windows-host and iPhone acceptance gates.
