# Stage 2 Native WebRTC MVP implementation plan draft

This plan is design-only until the Stage 2 design draft is reviewed. It assumes
the Native Synthetic Virtual Camera gate remains green and does not alter that
baseline.

## Phase 1: signaling contract

- Define versioned JSON messages for `hello`, `offer`, `answer`, `ice`, `state`,
  `stats`, and `close`.
- Add a same-origin WSS endpoint to the existing local HTTPS server.
- Add loopback tests for ordering, malformed SDP, duplicate session, timeout,
  close, and reconnect.
- Verify no signaling message contains media payloads or external endpoints.

## Phase 2: native receiver skeleton

- Pin and document the libdatachannel dependency and Windows build method.
- Create a receiver process with a one-session state machine.
- Bind only the configured LAN/private IPv4 path for the initial probe.
- Add SDP validation for a single H.264 video m-line and recvonly answer.
- Add control-path logging for ICE, DTLS, SRTP, RTP, RTCP, and reconnect.

## Phase 3: Safari H.264 interop probe

- Add a Stage 1-controlled “connect” path that creates a sendonly video
  transceiver and applies the exact 1920x1080@60 constraints.
- Prefer H.264 only from runtime codec capabilities.
- Run the browser/native probe on the same LAN.
- Record the actual negotiated codec, candidate pair, connection state, RTP
  timestamps, RTCP reports, and reconnect behavior.
- Stop and reassess libdatachannel versus libwebrtc if this gate fails.

## Phase 4: RTP and decoder boundary

- Add H.264 RTP depacketizer fixtures for single NAL, STAP-A where applicable,
  FU-A fragmentation, marker bit, packet loss, and out-of-order packets.
- Add a decoder adapter with Media Foundation MFT enumeration and selected MFT
  logging.
- Verify output type and stride as NV12 1920x1080 before publishing.
- Keep decoder tests independent from WebRTC by using recorded/fixture RTP or
  H.264 access units.

## Phase 5: latest-frame publication

- Publish decoded NV12 frames through the existing shared-memory contract.
- Preserve sequence/timestamp semantics and latest-frame behavior.
- Add synthetic decoded-frame injection so 1080p60 publication is testable
  without Safari.
- Re-run the existing Virtual Camera capture probe and require 120+ samples.

## Phase 6: integrated MVP acceptance

- Run Safari rear-camera 1080p60 to native receiver on LAN.
- Confirm H.264 negotiation and native decode path.
- Confirm CamBridge Virtual Camera remains enumerated and receives the new
  frames; do not add a new Virtual Camera registration path.
- Validate Discord or another capture client only after the synthetic and
  receiver gates are green.
- Run the 10-minute receive test and collect the complete low-latency metrics.

## Rollback and isolation

Each phase is a separate commit. If a phase fails, retain its logs and revert
only that phase; keep Web Stage 1 and the Synthetic Virtual Camera baseline
unchanged. No automatic Frame Server restart, registry replacement, firewall
weakening, or forced process termination is part of this plan.
