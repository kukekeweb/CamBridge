# Native receiver signaling boundary (2026-08-31)

## Scope

This validation covers the native WebRTC control boundary and the subsequent
fixture-level decoder/publisher wiring after the synthetic Virtual Camera gate.
It does not claim Safari interoperation, ICE, DTLS, SRTP, live H.264 RTP
reception, sustained decoding, or Virtual Camera output from network frames.

## Implemented layers

- `LibDataChannelReceiver` creates a real libdatachannel `PeerConnection`.
- A recv-only H.264 media track is installed with
  `H264RtpDepacketizer`; no received access unit has been claimed yet.
- The existing dependency-free `ReceiverSession` validates one sending H.264
  video m-line and the private-IPv4 host-candidate policy before the external
  library is called.
- `NativeSignalingSession` serializes `hello`, accepts `offer`, queues ICE that
  arrives before the offer, and serializes the native `answer` and local `ice`.
- `NativeSignalingWebSocket` uses libdatachannel's WSS client with a local CA
  certificate by default. TLS verification can be bypassed only by an explicit
  local-probe option; it is not the default.
- `ReceiverMediaPipeline` accepts a depacketized H.264 access unit, invokes the
  Media Foundation H.264-to-NV12 decoder boundary, and publishes decoded frames
  through the existing latest-frame IPC contract. The native receiver CLI wires
  this path by default and retains `--no-publish` for control-only diagnostics.

## Test evidence

With the repository `vcpkg.json` manifest and the vcpkg baseline recorded there,
the opt-in CMake build compiled and passed:

- `cambridge_libdatachannel_receiver_tests`
- `cambridge_signaling_protocol_tests`
- `cambridge_native_signaling_session_tests`
- `cambridge_native_signaling_websocket_tests`

The session test observes a generated SDP Answer containing a video m-line and
the WebSocket test verifies that missing CA trust is rejected before any socket
is opened. The fixture pipeline test reads back published NV12 from a separate
IPC mapping. These are local API/contract tests, not a network or device result.

## Next gate

The next implementation gate is a real same-LAN Safari H.264 interop probe. It
must use the Stage 1 Web Client, the existing HTTPS/WSS server, a private IPv4
host candidate, and a real selected codec/DTLS/SRTP/RTP evidence log. Until that
gate passes, no live decode rate or end-to-end Virtual Camera result is claimed.
