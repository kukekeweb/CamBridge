# CamBridge Web Stage 0 / Stage 1 Design

Date: 2026-08-29  
Status: approved for implementation

## Scope and policy change

CamBridge's iOS-native Swift/SwiftUI/AVFoundation implementation remains in
`ios/` as a legacy/reference implementation. It is not deleted, rewritten, or
used by the Web Client.

The active iPhone client is a Web Client running in Safari on iPhone 17 / iOS
27. This change removes the iOS app signing, sideloading, and seven-day
development-install dependency from the active path.

This delivery covers Web Stage 0 design and Web Stage 1 camera capability
probe. It does not implement WebRTC media sending, a Windows receiver, a
decoder, D3D11 rendering, or Media Foundation Virtual Camera.

## Goals

- Serve the Web Client from the Windows CamBridge machine over local HTTPS.
- Make the first acceptance URL the detected private-LAN IPv4 URL, for example
  `https://192.168.11.20:8443`.
- Obtain camera permission in Safari and display the actual camera devices,
  capabilities, settings, constraints, codecs, and measured frame rate.
- Request 1920x1080 / 60 FPS with exact constraints and never silently fall
  back to 30 FPS or another resolution.
- Keep all video and control traffic local to the LAN. Stage 1 has no signaling
  connection and no video transport.
- Make measured runtime capability the source of truth. Published iPhone or
  Safari specifications are not treated as proof of a particular camera
  format or codec.

## Repository structure

```text
CamBridge/
├ ios/                         # legacy/reference native implementation
├ web/
│  └ client/                   # browser client, no external hosting
├ windows/
│  └ stage1-server/            # local HTTPS static server and mDNS status
├ docs/
│  └ superpowers/
│     ├ specs/
│     └ plans/
└ scripts/
```

The Web Client is vanilla ES modules with no runtime npm dependency. The
Windows Stage 1 server uses Node.js standard-library HTTPS and filesystem APIs.
This is a replaceable Stage 1 server boundary; the future Windows receiver and
Virtual Camera remain native Windows components.

## Web Client architecture

```text
Settings Model
      │
      ▼
Exact Constraint Builder ──► getUserMedia()
      │                              │
      │                              ▼
      ├──────────────► Capability / Settings / Constraints Snapshot
      │                              │
      ▼                              ▼
Orientation Output Plan       HTMLVideoElement
                                     │
                                     ▼
                         requestVideoFrameCallback()
                                     │
                                     ▼
                           Measured FPS / drops
```

Responsibilities are separated as follows:

- `settings.js`: output resolution, FPS, orientation, quality, and future
  codec intent. It has no browser or capture side effects.
- `capability-probe.js`: device enumeration, track capability/settings/
  constraints snapshots, WebRTC codec capability detection, and low-latency
  API presence detection.
- `capture-controller.js`: exact `getUserMedia()` request, track lifecycle,
  actual-setting validation, and stream attachment.
- `frame-rate-meter.js`: `requestVideoFrameCallback()` measurement with one-
  second and ten-second windows, frame counter, and estimated missing frames.
- `main.js`: UI event wiring and diagnostics presentation.

The `CapturePlan` equivalent in the Web Client carries requested dimensions,
actual track dimensions, output dimensions after orientation, and a transform
description. Stage 1 does not resample or encode frames; CSS rotation is only a
preview representation and is never treated as a transmitted transform.

### Exact constraint and mismatch policy

For a 1920x1080 / 60 request, the client sends the equivalent of:

```js
{
  width: { exact: 1920 },
  height: { exact: 1080 },
  frameRate: { exact: 60 }
}
```

If Safari rejects the request, the UI displays `1080p60 unavailable`. If the
track starts but `getSettings()` does not match the requested dimensions or FPS,
the track is stopped and the UI displays a mismatch/unsupported result. The
client never retries with a weaker constraint.

The same rule applies to 2560x1440 and 3840x2160. Stage 1 only probes whether
Safari accepts the exact request; a future 4K-capture/GPU-scale path is not
implemented here.

### Device and track diagnostics

The UI displays the fields Safari actually returns:

- `enumerateDevices()`: video input label, deviceId, and groupId when present.
- `getCapabilities()`: only fields present on the returned object, including
  width, height, frameRate, facingMode, resizeMode, zoom, torch, focus, and
  exposure-related fields.
- `getSettings()`: actual width, height, frameRate, facingMode, deviceId,
  resizeMode, and aspectRatio when present.
- `getConstraints()`: the browser's actual constraint object.
- runtime errors, request, actual settings, and the selected output plan.

Labels may be empty before permission; the client refreshes device enumeration
after permission is granted.

### Measured FPS

When `HTMLVideoElement.requestVideoFrameCallback` exists, the meter counts
callbacks and uses callback metadata such as `presentedFrames` when available.
It reports one-second FPS, ten-second FPS, total frame counter, and an estimate
of missing frames from elapsed time and the requested FPS. If the API is absent,
the UI says measurement is unavailable rather than presenting `getSettings()` as
an empirical measurement.

### Codec and low-latency capability probe

The client checks `RTCRtpSender.getCapabilities("video")` at runtime. It lists
only codecs returned by Safari, with MIME type, clock rate, channels, and SDPs
when exposed. H.264 and HEVC are not hard-coded into the selectable capability
result.

The client feature-detects `RTCRtpReceiver.prototype.targetLatency`,
`RTCRtpReceiver.prototype.jitterBufferTarget`, and related WebRTC objects. The
presence result is displayed, but Stage 1 does not create a peer connection or
apply receiver controls. In Stage 2, actual `RTCStatsReport` values will be
collected only after a receiver exists. Unsupported properties are represented
as unavailable.

WebKit's Safari 27 beta notes mention `targetLatency`, `jitterBufferTarget`,
`RTCRtpCodec`, and video dimensions in RTC stats. These notes guide the probe,
but the iPhone runtime result remains authoritative:

- https://webkit.org/blog/17967/news-from-wwdc26-webkit-beta/
- https://www.w3.org/TR/webrtc/
- https://www.w3.org/TR/webrtc-stats/

## Windows local HTTPS server

The Stage 1 server is a small Node.js standard-library program. It:

- binds to one detected private LAN IPv4 address by default, or an explicit
  `--bind` address;
- serves `web/client/` as static files;
- uses a PFX only on the Windows host and reads its password from an environment
  variable or an interactive prompt without logging it;
- parses the public server certificate to display its actual SAN;
- refuses to present an IP access URL if the certificate SAN does not contain
  the bind IP;
- does not open a Windows Firewall rule automatically;
- does not connect to the Internet, an external HTTPS host, STUN, TURN, or a
  cloud relay.

At startup it prints:

```text
CamBridge Stage 1 Server
Bind address: 192.168.11.20
HTTPS port: 8443
iPhone access URL: https://192.168.11.20:8443
Certificate SAN: DNS:cambridge.local, IP Address:192.168.11.20
mDNS / Bonjour: available
Bonjour service: CamBridge._cambridge._tcp.local.
.local hostname: unavailable
Friendly URL: unavailable
```

The IP URL is the Stage 1 acceptance route. A user may allow the process on a
Private network in Windows Defender Firewall when Windows prompts; the project
does not change firewall policy programmatically.

## TLS and Local CA

`scripts/generate-local-ca.ps1` uses the Windows certificate provider to create
a local root CA and a server certificate. The server certificate SAN includes
`cambridge.local` plus every IP address supplied to the script. The output is:

- `cambridge-root-ca.cer`: public root certificate for iPhone installation;
- `cambridge-server.cer`: public certificate used for SAN display;
- `cambridge-server.pfx`: server private key and certificate, Windows-only;
- certificate-store copies of the private keys.

The certificate directory, PFX, passwords, and private keys are Git-ignored.
Only the public root CA is transferred to the iPhone. The private key/PFX never
leaves Windows.

For manual iPhone trust:

1. Transfer only `cambridge-root-ca.cer` to the iPhone using a local file
   transfer path.
2. Open it and complete the iOS profile/certificate installation prompt.
3. Open **Settings → General → About → Certificate Trust Settings**.
4. Enable full trust for `CamBridge Local CA` and confirm the prompt.
5. Open the printed `https://<LAN IPv4>:<port>` URL in Safari.

Apple's HTTPS test-server guidance requires the explicit Certificate Trust
Settings step even after manual certificate installation:

- https://developer.apple.com/library/archive/qa/qa1948/_index.html

When the CA or server certificate is regenerated, the old root should be
removed from the iPhone, the new public root installed, and full trust enabled
again. The PFX password is entered only when starting the local server and is
not stored in the repository.

## mDNS / Bonjour and hostname policy

Service discovery and hostname resolution are separate concerns.

### Base route

Stage 1 does not depend on mDNS. The detected private IPv4 URL is always
printed, included in the certificate SAN, and used for the acceptance test.

### Windows host `.local`

The server may report the actual Windows host name with `.local` only after a
runtime resolution check succeeds and the certificate SAN contains that exact
name. It does not rename the Windows host or claim that a service registration
created an alias.

### DNS-SD service

If Windows Bonjour/DNS-SD registration is available, the server advertises the
service `_cambridge._tcp.local.` and reports the registration status. The
service name is informational/discovery data; it is not used as a browser URL.

Microsoft's `DnsServiceRegister` API explicitly supports mDNS service
advertisement, but service registration alone does not create an arbitrary
standalone A/AAAA alias:

- https://learn.microsoft.com/en-us/windows/win32/api/windns/ns-windns-dns_service_register_request
- https://learn.microsoft.com/en-us/windows/win32/api/windns/nf-windns-dnsserviceregister

### Fixed `cambridge.local` alias

Stage 1 does not implement or claim a fixed alias. A correct future solution
must publish an mDNS host A/AAAA record (or use a trusted home-router/local DNS
zone) and verify that Safari resolves it on the target LAN. `dns-sd.exe -R`
service registration by itself is not sufficient evidence, so it is not used as
the alias mechanism.

## Security model

- LAN-only bind and no external relay.
- No camera stream leaves the local network.
- HTTPS is required for `getUserMedia()`; HTTP is not used for the Web Client.
- Local CA public certificate is the only certificate material transferred to
  the iPhone.
- Server PFX/private keys and passwords are ignored and never logged.
- No automatic firewall, router, hosts-file, or certificate-trust mutation.
- Stage 1 has no WebSocket, signaling, peer connection, ICE, STUN, or TURN
  implementation.

## Future pipeline and boundaries

```text
Camera Capture
  → Browser/WebRTC Encoder
  → LAN-direct WebRTC Transport
  → Windows WebRTC Receiver
  → Hardware Decode
  → D3D11 Preview
  → Media Foundation Virtual Camera
```

Stage 2 will introduce a WSS signaling endpoint inside the Windows host and
will use host ICE candidates without external STUN/TURN by default. Stage 2
must add counters for capture, encode, send, receive, decode, render, packet
loss, RTT, jitter, decode time, queue depth, and latency. Stage 3 will use
`MFCreateVirtualCamera` and an NV12 frame path; no kernel driver is planned.

## Stage 1 acceptance gate

The gate is a real iPhone 17 / iOS 27 Safari session using the printed IP URL:

1. HTTPS opens without a certificate warning after the local CA is trusted.
2. Camera permission is granted.
3. Devices and post-permission labels are enumerated.
4. Exact 1920x1080 / 60 FPS request succeeds or clearly reports unsupported.
5. `getSettings()` confirms actual dimensions and FPS.
6. `requestVideoFrameCallback()` reports measured FPS; target is 59–60 FPS.
7. Capabilities, constraints, errors, codec capabilities, and low-latency API
   detection are visible.
8. Capture remains active for ten minutes without unexplained interruption.

The result is not a pass based on published specifications or on a simulator.
The recorded Safari capabilities, settings, measured FPS, and codec list are
the evidence.

## Out of scope for this delivery

- WebRTC media sender or WSS signaling.
- Windows receiver, H.264/HEVC decoder, hardware-path verification.
- D3D11 preview or Media Foundation Virtual Camera.
- TURN/STUN/cloud relay and custom UDP transport.
- Automatic fixed `cambridge.local` alias.
