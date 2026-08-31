# Stage 2 Native WebRTC MVP design draft

## Scope and gate

This document starts only after the synthetic native gate passed: the current
Media Source loaded by the restarted Frame Server delivered 120 NV12 samples to
a Media Foundation capture client. That gate is now a regression baseline. The
first native decoder boundary and receiver-to-IPC fixture wiring are also
present, but live Safari media reception remains open.

The first network milestone is:

```text
iPhone Safari 1080p60
  -> WebRTC H.264
  -> LAN-direct Windows native receiver
  -> H.264 decode
  -> NV12 LatestFrameProvider / existing IPC
  -> CamBridge Virtual Camera
```

There is no OBS path, browser receiver, cloud relay, TURN server, external STUN,
or Stage 3 Virtual Camera redesign in this milestone.

## Stack decision

### Candidate A: libwebrtc

libwebrtc is the broadest interoperability reference and exposes mature SDP,
ICE, DTLS/SRTP, RTP, H.264 handling, and detailed statistics. Its disadvantages
for this MVP are the large depot/toolchain build, long setup time, platform
integration surface, and a heavier update/maintenance burden on Windows.

### Candidate B: libdatachannel

libdatachannel is a smaller native WebRTC stack with C++ APIs for PeerConnection,
SDP, ICE, DTLS/SRTP, RTP tracks, and data channels. It is a better fit for the
one-iPhone/one-PC LAN receiver if its Safari interoperability probe confirms:

- Safari Offer can be accepted as a recvonly native answer;
- host-candidate IPv4 ICE connects without STUN/TURN;
- DTLS/SRTP completes;
- negotiated codec is H.264;
- H.264 RTP packets reach the track callback;
- RTCP and reconnect behavior are observable.

The stack decision is **libdatachannel first**, with a hard probe gate. If the
probe fails on Safari H.264, ICE/DTLS/SRTP, RTP packet delivery, or reconnect in
a way that cannot be repaired at the signaling/SDP boundary, stop and evaluate
libwebrtc. No custom WebRTC protocol or replacement transport is allowed.

## Component boundaries

```text
Stage1 HTTPS server + WSS signaling
             |
Safari Web Client -- WebRTC LAN direct -- Native Receiver
                                             |
                                      H264RtpDepacketizer
                                             |
                                      MF H.264 Decoder
                                             |
                                      NV12 Frame Publisher
                                             |
                                      existing shared-memory IPC
                                             |
                                      Custom Media Source
```

The receiver owns PeerConnection, SDP/ICE state, RTP/RTCP input, and decoder
input. The decoder owns H.264 access-unit to NV12 conversion. The frame publisher
owns only the latest-frame contract (`width`, `height`, `stride`, timestamp,
sequence, producer state). The Media Source remains unaware of WebRTC and keeps
its existing Frame Server process boundary.

The first implementation may reuse the existing shared-memory publisher contract
to bridge the receiver process to the already verified Media Source. It must not
add a second unbounded queue. If a consumer falls behind, sequence gaps are
allowed and the newest complete frame wins.

## Signaling

The existing local HTTPS server will expose a WSS endpoint on the same bind
address and port. The browser loads the Web Client over HTTPS and connects to the
same origin for signaling, avoiding a second certificate or port.

The protocol is one session with one iPhone and one Windows receiver:

1. The native receiver sends `hello` with either an explicit session identifier
   or the native-only reserved `auto` value. The browser sends `hello` with a
   non-empty session identifier.
2. When `auto` is used, the local broker binds it to the first browser session
   and relays subsequent messages using that effective session identifier.
3. Browser creates an Offer with one video transceiver in `sendonly` mode.
4. Browser sends the SDP Offer.
5. Native receiver validates the session and returns an SDP Answer with the
   video direction accepted as `recvonly`.
6. The native receiver explicitly starts local ICE gathering after creating the
   Answer because the initial configuration disables automatic gathering; both
   sides then exchange trickled ICE candidates.
7. Both sides report state changes and the selected codec for diagnostics.
8. Close, timeout, or network loss moves the session to disconnected and permits
   a fresh session without reusing stale SDP/ICE state.

Messages are versioned and bounded. Signaling carries no video bytes. The server
rejects a second active iPhone session in the initial MVP rather than silently
sharing one receiver.

## ICE and LAN security

- Private IPv4 host candidates are preferred.
- IPv6/link-local candidates are retained only if explicitly supported by the
  probe; they are not required for the first milestone.
- No external STUN or TURN server is configured.
- The receiver must report the selected candidate pair and fail clearly when a
  host-candidate direct path cannot be established.
- Windows Defender Firewall must permit the negotiated UDP range on the Private
  network profile only. The installer must not broadly open public-network
  rules; the exact rule and rollback path will be documented before adding it.
- WSS is same-origin and LAN-only. The server must not forward SDP or media to
  an internet endpoint.
- Reconnect creates a new PeerConnection and clears old ICE candidates; it does
  not reuse a failed transport.

## Codec policy

Safari runtime capability results are evidence, not a guarantee of WebRTC
negotiation. The browser will prefer H.264 using runtime codec capabilities and
the negotiated transceiver parameters. The native receiver will record the
actual codec from the negotiated SDP and inbound RTP/receiver stats.

H.265/HEVC capability remains a later experiment. Its presence in
`RTCRtpSender.getCapabilities("video")` is not treated as WebRTC sendability or
hardware-encode proof. VP8/VP9/AV1 are fallback investigation data only and are
not part of the first MVP acceptance target.

## Low-latency rules and measurements

- Request 1920x1080@60 from the existing Stage 1 Web Client; do not silently
  downgrade.
- Prefer `degradationPreference`/content hints only after runtime capability
  detection confirms the property; unsupported APIs are recorded as unavailable.
- Do not add a jitter buffer or application frame queue beyond WebRTC's own
  transport behavior.
- Decode directly toward NV12 where the selected Media Foundation decoder
  exposes that path; RGB conversion is not part of the target path.
- The frame handoff is latest-frame/sequence based, not FIFO.
- First-frame latency and steady-state latency are measured separately.

The native diagnostics model records, where available:

```text
capture FPS, outbound FPS, encoded frames, sent frames,
inbound FPS, received frames, decoded frames, dropped frames,
render/virtual-camera FPS, RTT, jitter, packet loss, decode time,
jitter-buffer delay, queue depth, selected codec, bitrate, frame size
```

Browser and native stats are timestamped independently. A “measured end-to-end
latency” value is only shown when a synchronized visual test is running; it is
not inferred from RTT.

The Web Client begins polling `RTCPeerConnection.getStats()` after the Answer
is applied. It reports only runtime fields exposed by the browser, including
the outbound video codec, sender FPS, encoded/dropped frames, bitrate,
packets, packet loss, RTT, and jitter. The Native receiver separately reports
the H.264 codec strings found in the remote Offer and local Answer. Its media
pipeline derives decoded and published FPS from decoded frame timestamps and
reports the selected Media Foundation transform, hardware-path flag, output
dimensions, and stride. These values are diagnostic evidence; absent fields
are not synthesized and browser/native clocks are not combined into a latency
claim.

## Windows receiver and decoder

The receiver is C++20 and uses libdatachannel first. The H.264 access unit
boundary is kept explicit between RTP depacketization and decoding. The first
decoder adapter targets Media Foundation H.264 hardware acceleration when the
runtime exposes a hardware transform, with a software-compatible diagnostic
path only for comparison. The selected MFT, hardware/software status, input
format, output format, and decoder errors are logged.

The decoded output must be validated as NV12 with width, height, stride, and
timestamps before publishing. The existing Media Source consumes the shared
latest-frame contract; its COM and Frame Server behavior are unchanged.

## Probe and acceptance gates

The implementation is split into independently testable gates:

1. Signaling loopback: Offer/Answer/candidate validation without media.
2. Safari interop probe: Safari Offer, native Answer, LAN ICE, DTLS/SRTP,
   H.264 selected, RTP/RTCP callbacks, timestamps, reconnect.
3. H.264 RTP depacketizer: packet fixtures including marker bits, FU-A, loss,
   out-of-order packets, and access-unit boundaries.
4. Decoder probe: known H.264 input to NV12 with selected MFT evidence.
5. Receiver-to-IPC synthetic NV12 test: 1080p60 latest-frame publication.
6. Integrated network preview path: Safari to native receiver, without Virtual
   Camera changes until the prior gates pass.
7. Existing capture probe: 120+ synthetic samples remains green after receiver
   additions.

Implementation status at this revision:

- Gate 1 signaling loopback: PASS.
- Gate 2 receiver state/SDP policy core: PASS.
- Gate 2 libdatachannel opt-in API/SDP wiring: PASS with v0.24.5 package;
  Answer callback and local host-candidate callback are observed in local tests.
- Gate 4 decoder boundary: the Media Foundation H.264-to-NV12 adapter builds,
  starts on this host, records the selected transform, and produces NV12 frames
  from a valid yuv420p Annex-B fixture. This is a decoder contract/fixture
  result, not proof of 60fps live WebRTC decode or hardware acceleration.
- Gate 5 latest-frame publisher contract: PASS in an isolated native test;
  decoded-NV12 handoff and invalid-frame rejection are covered. It is not yet
  connected to the WebRTC receiver.
- Receiver media pipeline fixture boundary: PASS. A valid H.264 fixture can
  traverse the decoder and publish NV12 into a separate existing IPC mapping;
  the native receiver CLI is wired to this boundary. A native two-peer RTP
  loopback now also drives the same valid Annex-B fixture through
  depacketization, Media Foundation decode, and the existing IPC mapping. No
  live access unit has yet been received through Safari.
- Gate 5 native network-to-IPC loopback: PASS with a generated 1920x1080@60
  H.264 fixture. The test covers single-NAL and FU-A RTP packetization,
  native libdatachannel receive, NV12 decode, and latest-frame IPC readback.
  It is not Safari interoperability or a sustained live 60fps result.
- Safari ICE/DTLS/SRTP and H.264 RTP receive: not yet verified on a live Safari
  device. The native receiver and same-origin WSS path are implemented.
- Receiver-to-IPC live network path and integrated network path: not yet
  verified; the fixture and native loopback boundaries are passing as noted
  above.

Acceptance for this Stage 2 design's implementation is not claimed until:

- Safari rear camera sends 1920x1080@60;
- selected candidate pair is private-LAN direct;
- actual negotiated codec is shown and is H.264 for the first MVP;
- native receive/decode sustains 59-60 fps;
- no cloud/TURN/external STUN path is used;
- reconnect works;
- a 10-minute receive run completes with packet loss, RTT, jitter, decoder
  errors, dropped frames, and queue depth recorded.

Visual latency measurement will use a high-refresh-rate camera filming an iPhone
screen containing a rapidly changing timestamp/counter and the Windows camera
preview at the same time. The captured recording is analyzed frame-by-frame to
measure the displayed-counter difference. This is an external measurement plan,
not a claim that browser timestamps alone provide end-to-end latency.

## Explicit non-goals

Stage 2 does not implement H.265, D3D11 preview UI, installer auto-start, QR,
multi-device sessions, Virtual Camera registration changes, kernel drivers,
OBS integration, or internet relay.
