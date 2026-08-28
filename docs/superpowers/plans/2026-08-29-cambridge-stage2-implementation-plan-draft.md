# CamBridge Web Stage 2 Implementation Plan Draft

> **For agentic workers:** This is a draft only. Do not execute any task until Web Stage 1's 600-second real-device acceptance is PASS and the Stage 2 design is approved.

**Goal:** Transfer the proven iPhone Safari 1920×1080@60 camera Track over a same-LAN WebRTC connection to a Windows preview.

**Architecture:** Keep Safari capture, WebRTC encode/transport, Windows receive/decode, and D3D11 preview as separate components. Use the existing Windows HTTPS origin for WSS signaling and use host-candidate direct media without TURN or external STUN.

**Tech Stack:** iOS Safari Web APIs, WebRTC runtime APIs, Windows C++20, libwebrtc, D3D11, existing Node.js local HTTPS server for signaling.

**Spec:** `docs/superpowers/specs/2026-08-29-cambridge-stage2-design-draft.md`

## Global Constraints

- Stage 2 starts only after the iPhone 17 / iOS 27 Safari 600-second 1080p60 Stage 1 acceptance is PASS.
- Stage 2 includes Preview only; Media Foundation Virtual Camera is Stage 3.
- Video media stays on the private LAN and never uses cloud signaling, TURN, external STUN, or relay upload.
- No silent resolution/FPS/codec fallback; actual Track settings, SDP codec, decoder path, and stats are displayed.
- No automatic Windows Firewall changes; document and surface the required Private network permission.
- Runtime-detect Safari stats and latency APIs; unsupported properties remain explicitly unavailable.

---

### Task 1: Freeze the Stage 2 test contract

**Files:**
- Create: `docs/validation/web-stage2-test-matrix.md`
- Test: `web/client/tests/webrtc-contract.test.js`

**Interfaces:**
- Produces the exact signaling message schema, metrics names, candidate classification, codec evidence fields, and acceptance checklist used by later tasks.

- [ ] Define JSON message types `hello`, `offer`, `answer`, `ice-candidate`, `state`, and `error`, including session id and correlation fields.
- [ ] Define the direct-LAN evidence fields: selected candidate pair, candidate types, private IPv4 status, and relay/stun absence.
- [ ] Define the codec evidence fields: SDP mime type, codec stats id, frames encoded/decoded, and decoder path.
- [ ] Add contract tests that reject unknown message types and missing session/correlation fields.
- [ ] Run `npm test --prefix web/client -- tests/webrtc-contract.test.js` and confirm it fails before the future contract module exists.

### Task 2: Add same-origin WSS signaling to the Windows server

**Files:**
- Modify: `windows/stage1-server/server.mjs`
- Create: `windows/stage1-server/signaling/session-store.mjs`
- Test: `windows/stage1-server/tests/signaling-session.test.mjs`

**Interfaces:**
- Consumes WebSocket messages from Safari and the future native receiver.
- Produces validated Offer/Answer/ICE messages scoped to one short-lived session.

- [ ] Add a WSS endpoint on the same HTTPS listener and reject non-WebSocket paths, invalid origin, invalid schema, and oversized messages.
- [ ] Store only active in-memory sessions; expire disconnected sessions and never persist SDP, ICE, or camera media.
- [ ] Add one-to-one pairing and explicit state transitions for `waiting`, `offer-received`, `answer-received`, `connected`, `disconnected`, `failed`, and `closed`.
- [ ] Test two clients, a third-client rejection, malformed JSON, oversized payload, disconnect cleanup, and reconnect with a new session.
- [ ] Run `node --check windows/stage1-server/server.mjs` and the signaling tests.

### Task 3: Implement the Safari WebRTC sender

**Files:**
- Create: `web/client/src/webrtc-sender.js`
- Modify: `web/client/src/main.js`
- Modify: `web/client/index.html`
- Test: `web/client/tests/webrtc-sender.test.js`

**Interfaces:**
- Consumes the existing exact 1920×1080@60 Track and WSS signaling client.
- Produces sender stats, selected codec evidence, ICE state, and connection lifecycle events.

- [ ] Add a Stage 2-only sender that reuses the active exact Track and does not create one unless Stage 1 settings match.
- [ ] Detect `setCodecPreferences`, `getStats`, sender stats, receiver stats, jitter-buffer properties, and target-latency properties at runtime.
- [ ] Prefer runtime-advertised H.264 when available, then preserve the actual negotiated codec as evidence rather than labeling the requested codec as selected.
- [ ] Add tests for H.264 present/absent, `setCodecPreferences` unavailable, selected codec mapping, stats unavailable, and ICE failure.
- [ ] Keep this file out of Stage 1 startup so the Stage 1 validator can still prove no Stage 2 transport is created.

### Task 4: Build the Windows native WebRTC receiver

**Files:**
- Create: `windows/stage2-receiver/CMakeLists.txt`
- Create: `windows/stage2-receiver/src/ReceiverSession.cpp`
- Create: `windows/stage2-receiver/src/ReceiverSession.h`
- Create: `windows/stage2-receiver/src/ReceiverStats.cpp`
- Test: `windows/stage2-receiver/tests/ReceiverSessionTests.cpp`

**Interfaces:**
- Consumes WSS signaling and WebRTC media.
- Produces decoded frames and receiver statistics to the Preview boundary.

- [ ] Pin and document the libwebrtc build input without downloading binaries at runtime.
- [ ] Configure a one-to-one PeerConnection, host-candidate-only validation, and explicit ICE state/reconnect transitions.
- [ ] Surface actual codec, frames decoded, frames dropped, packet loss, RTT, jitter, jitter buffer delay, and decoder path.
- [ ] Make the receiver reject a selected relay candidate when the Stage 2 direct-LAN gate is enabled.
- [ ] Unit-test lifecycle, malformed signaling, candidate classification, stale-session rejection, and reconnect cleanup.

### Task 5: Add D3D11 Preview without Virtual Camera

**Files:**
- Create: `windows/stage2-receiver/src/D3D11Preview.cpp`
- Create: `windows/stage2-receiver/src/D3D11Preview.h`
- Test: `windows/stage2-receiver/tests/D3D11PreviewTests.cpp`

**Interfaces:**
- Consumes the decoded frame adapter from `ReceiverSession`.
- Produces a visible preview and render statistics only; it does not register a Windows camera device.

- [ ] Prefer a zero-copy or one-copy D3D11-compatible frame path and record when a CPU color conversion is required.
- [ ] Use a bounded latest-frame buffer with queue depth metrics; discard stale frames under pressure.
- [ ] Report render FPS, queue depth, frame size, dropped render frames, and device removal errors.
- [ ] Test frame arrival, stale-frame replacement, queue bounds, resize, and D3D11 device loss handling.

### Task 6: End-to-end acceptance and latency evidence

**Files:**
- Create: `docs/validation/web-stage2-acceptance.md`
- Modify: `.github/workflows/web-stage1.yml` only for future static/unit checks; no real iPhone or LAN test is claimed by CI.

**Interfaces:**
- Consumes Safari and Windows metrics from Tasks 3–5.
- Produces reproducible LAN-direct, 10-minute, and visual-latency evidence.

- [ ] Verify Safari exact Track settings before creating the PeerConnection.
- [ ] Verify the selected ICE pair is private-LAN host-to-host with no TURN/STUN/relay.
- [ ] Verify negotiated H.264 and the actual sender/receiver stats codec mapping.
- [ ] Run 10 minutes while recording capture/outbound/inbound/decode/render FPS, dropped frames, packet loss, RTT, jitter, queue depth, reconnects, decoder errors, CPU/GPU, and memory.
- [ ] Perform the 240fps visual-latency test with a common LED/clock event and report median/min/max/p95 separately from RTT.
- [ ] Do not mark Stage 2 PASS if Preview is stable only because frames are buffered, codec differs without evidence, or a cloud relay is involved.
