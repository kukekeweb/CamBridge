# CamBridge Web Stage 1 Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Build a LAN-only HTTPS-served Safari camera capability probe that measures exact camera capture behavior without implementing Stage 2 transport.

**Architecture:** A vanilla ES-module client owns settings, exact constraints, capability snapshots, track validation, codec probing, and video-frame measurement. A replaceable Node.js standard-library Stage 1 server serves the client over HTTPS, validates the bind-IP SAN, and reports mDNS service/hostname status without claiming a fixed alias.

**Tech Stack:** Browser Web APIs, vanilla JavaScript ES modules, Node.js built-in `node:test`, Node.js `https`/`fs`/`crypto`/`child_process`, PowerShell `New-SelfSignedCertificate`, Windows DNS-SD when available.

**Spec:** `docs/superpowers/specs/2026-08-29-cambridge-web-stage0-stage1-design.md`

## Global Constraints

- Keep all existing `ios/` Swift/SwiftUI/AVFoundation files unchanged and retained as legacy/reference implementation.
- Stage 1 accepts `https://<private-LAN-IPv4>:<port>`; mDNS and `cambridge.local` are informational and must not block the gate.
- Request resolution and FPS with exact constraints; never retry with a weaker value.
- Display actual `getCapabilities()`, `getSettings()`, `getConstraints()`, codec capabilities, and empirical frame timing.
- Do not implement WebRTC media sending, WSS signaling, Windows receiver, decoder, D3D11, Virtual Camera, STUN, TURN, or external relay.
- Do not commit PFX files, private keys, passwords, DerivedData, build output, or generated IPA files.

---

### Task 1: Add Web Client package and pure settings model

**Files:**
- Create: `web/client/package.json`
- Create: `web/client/src/settings.js`
- Create: `web/client/tests/settings.test.js`
- Modify: `.gitignore`

**Interfaces:**
- `settings.js` exports `RESOLUTIONS`, `FRAME_RATES`, `ORIENTATIONS`, `QUALITY_PRESETS`, `createSettings(overrides)`, `buildExactVideoConstraints(settings, deviceId)`, and `createOutputPlan(settings)`.
- `buildExactVideoConstraints` returns `{ audio: false, video: { width: { exact }, height: { exact }, frameRate: { exact }, deviceId?: { exact } } }`.
- `createOutputPlan` returns requested dimensions, orientation-adjusted output dimensions, rotation degrees, and `transportTransform: "future-stage-2"`.

- [ ] **Step 1: Write failing tests** for exact constraints, orientation dimension swap, and no-FPS-fallback plan behavior.
- [ ] **Step 2: Run `npm test`** and confirm the new tests fail because the module is absent.
- [ ] **Step 3: Implement the minimal pure model** with the exact exported functions.
- [ ] **Step 4: Run `npm test`** and confirm all pure model tests pass.
- [ ] **Step 5: Commit** with `feat: add web capture settings model`.

### Task 2: Add capability and codec probe utilities

**Files:**
- Create: `web/client/src/capability-probe.js`
- Create: `web/client/tests/capability-probe.test.js`

**Interfaces:**
- `enumerateVideoInputs(mediaDevices)` returns normalized video input objects with only returned label/deviceId/groupId values.
- `snapshotTrackCapabilities(track)` returns only capability keys actually present on `track.getCapabilities()`.
- `snapshotTrackSettings(track)` and `snapshotTrackConstraints(track)` return normalized JSON-safe snapshots of actual browser responses.
- `probeCodecCapabilities(RTCRtpSenderClass)` returns `{ available, codecs, error }` and never invents codec names.
- `probeLowLatencyAPIs(scope)` returns runtime presence for `RTCRtpReceiver`, `targetLatency`, `jitterBufferTarget`, `RTCPeerConnection`, and receiver `getStats`.

- [ ] **Step 1: Write failing tests** using small fake objects for returned/missing capability keys and codec absence.
- [ ] **Step 2: Run the focused tests** and confirm failure before implementation.
- [ ] **Step 3: Implement property-preserving, runtime-only probes** with no WebRTC connection creation.
- [ ] **Step 4: Run all client tests** and confirm pass.
- [ ] **Step 5: Commit** with `feat: add browser capability probes`.

### Task 3: Add empirical video-frame meter

**Files:**
- Create: `web/client/src/frame-rate-meter.js`
- Create: `web/client/tests/frame-rate-meter.test.js`

**Interfaces:**
- `rateForSamples(samples, windowSeconds)` calculates a measured rate from timestamp samples.
- `estimateMissingFrames(frameCount, elapsedSeconds, targetFPS)` returns a non-negative estimate.
- `FrameRateMeter` exposes `start(video, targetFPS, onUpdate)`, `stop()`, and `supported`.
- The meter uses `requestVideoFrameCallback` when present and reports one-second rate, ten-second rate, frame count, missing estimate, and presented-frame metadata when available.

- [ ] **Step 1: Write failing tests** for one-second/ten-second rate and missing-frame estimate.
- [ ] **Step 2: Run focused tests** and confirm failure.
- [ ] **Step 3: Implement pure calculations and browser callback loop**.
- [ ] **Step 4: Run all client tests** and confirm pass.
- [ ] **Step 5: Commit** with `feat: measure browser video frame rate`.

### Task 4: Add exact capture controller and Web Client UI

**Files:**
- Create: `web/client/index.html`
- Create: `web/client/styles.css`
- Create: `web/client/src/capture-controller.js`
- Create: `web/client/src/main.js`
- Create: `web/client/README.md`

**Interfaces:**
- `CaptureController.start(settings, deviceId)` calls `getUserMedia` once with exact constraints, stops any prior track, snapshots actual values, and returns a success/mismatch result.
- `CaptureController.stop()` stops the current track and meter.
- A mismatch result includes requested settings, actual settings, and a user-visible unsupported/mismatch message; it never retries.
- The UI includes Camera, Orientation, Resolution, Frame Rate, Quality, Start Camera, Requested, Actual, Measured, codec capabilities, Capture status, and a Developer/Diagnostics section.

- [ ] **Step 1: Write controller contract tests** for rejection and settings mismatch using injected mediaDevices/video/meter doubles.
- [ ] **Step 2: Run the focused tests** and confirm the contract tests fail.
- [ ] **Step 3: Implement controller exact-request and no-fallback behavior.**
- [ ] **Step 4: Implement HTML/CSS and wire live probes, preview, settings comparison, plan, errors, and frame meter.**
- [ ] **Step 5: Run Node tests, `node --check` on browser modules, and inspect the HTML labels.
- [ ] **Step 6: Commit** with `feat: add Safari camera capability probe`.

### Task 5: Add local CA generation and HTTPS server

**Files:**
- Create: `scripts/generate-local-ca.ps1`
- Create: `windows/stage1-server/server.mjs`
- Create: `windows/stage1-server/README.md`
- Create: `windows/stage1-server/certificates/.gitkeep`
- Modify: `.gitignore`

**Interfaces:**
- `generate-local-ca.ps1 -IPAddress <one-or-more> -OutputDirectory <dir>` generates the public root `.cer`, public server `.cer`, and password-protected server `.pfx`; it never writes a password to disk or stdout.
- `server.mjs --web-root <dir> --pfx <file> --certificate <file> [--bind <private-ip>] [--port <port>]` serves HTTPS and prints bind address, port, IP URL, actual SAN, mDNS status, actual `.local` hostname status, service name, and friendly URL status.
- The server fails clearly if the bind address is not private or missing from the certificate SAN.
- mDNS only advertises `_cambridge._tcp.local.` where DNS-SD is available; it never claims that this creates `cambridge.local`.

- [ ] **Step 1: Add certificate-path ignore rules and certificate directory marker.**
- [ ] **Step 2: Implement the PowerShell certificate script with DNS and IP SAN entries and secure interactive PFX export.**
- [ ] **Step 3: Implement static-file HTTPS serving, path traversal rejection, SAN parsing, private-address selection, and startup diagnostics.**
- [ ] **Step 4: Implement DNS-SD service status and `.local` resolution status without using either as the base URL.**
- [ ] **Step 5: Run a local certificate generation dry run using a temporary output directory, validate SAN with PowerShell certificate APIs, and remove only the temporary generated files.**
- [ ] **Step 6: Start the server with a test certificate, request `/`, verify HTTPS 200 and an invalid path rejection, then stop the server.**
- [ ] **Step 7: Commit** with `feat: add LAN HTTPS stage1 server`.

### Task 6: Add static validation and CI

**Files:**
- Create: `scripts/validate-web-stage1.ps1`
- Create: `.github/workflows/web-stage1.yml`
- Modify: `README.md` or create it if absent

**Interfaces:**
- The validator checks required Web Client/server/docs files, required UI labels, exact constraint literals, runtime capability calls, no fallback branch, no external URLs, no `turn:`/`stun:`/cloud relay, and no Stage 2 receiver symbols.
- CI runs Node tests, JavaScript syntax checks, and the PowerShell validator on `windows-latest` without signing or network service dependencies.
- README documents the base IP URL, local CA generation, iOS Certificate Trust Settings, mDNS limitations, server startup output, and physical-device acceptance checklist.

- [ ] **Step 1: Write validator checks and workflow contract checks.**
- [ ] **Step 2: Run the validator before fixing any failures and record the expected red result for missing files.**
- [ ] **Step 3: Implement the validator and workflow.**
- [ ] **Step 4: Run Node tests, syntax checks, and validator locally.**
- [ ] **Step 5: Inspect the complete diff for Stage 2 exclusions and certificate-secret leaks.**
- [ ] **Step 6: Commit** with `ci: validate web stage1 locally`.

### Task 7: Final verification and handoff

**Files:**
- Modify: documentation only if verification reveals a contradiction.

- [ ] **Step 1: Run the complete local test command set from a clean working tree state.**
- [ ] **Step 2: Verify `git status`, ignored generated paths, and the full changed-file list.**
- [ ] **Step 3: Verify the Windows server startup output contains the IP URL, SAN, mDNS status, `.local` status, and friendly URL status.**
- [ ] **Step 4: Report the physical iPhone 17 / iOS 27 Safari checks that remain unverified, especially permission, exact 1080p60, measured 59–60 FPS, and ten-minute stability.**
- [ ] **Step 5: Do not begin Stage 2.**
