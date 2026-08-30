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
- `NativeSignalingSession::Restart()` discards the prior receiver/PeerConnection
  state after the signaling socket is closed, creates a fresh session with a new
  session ID, and reapplies the Access Unit handler. This is an explicit bounded
  restart operation; it is not an infinite retry loop.
- `LibDataChannelReceiver` records peer-connection state, ICE state, state-change
  counters, and whether H.264 is present in the remote offer and local answer.
  These are diagnostic observations only; they do not prove a live Safari path.

## ICE gathering correction

The receiver uses `disableAutoGathering=true` so that the application controls
when local candidates are emitted. Before this correction, `AcceptOffer()` set
the remote description and local Answer but never started local ICE gathering;
the local candidate callback therefore did not fire in a native probe. The
smallest fix is an explicit `gatherLocalCandidates()` immediately after the
Answer is created. A test-first regression now observes a non-empty candidate
callback with the configured LAN bind path. This does not claim a completed
Safari connection; it only proves that the native side can produce candidates
for the existing WSS signaling boundary.

## Test evidence

With the repository `vcpkg.json` manifest and the vcpkg baseline recorded there,
the opt-in CMake Debug build compiled and all 16 registered CTest tests passed,
including:

- `cambridge_libdatachannel_receiver_tests`
- `cambridge_signaling_protocol_tests`
- `cambridge_native_signaling_session_tests`
- `cambridge_native_signaling_websocket_tests`
- `cambridge_h264_depacketizer_tests`
- `cambridge_libdatachannel_loopback_tests`
- `cambridge_libdatachannel_pipeline_loopback_tests`

The receiver test observes a generated SDP Answer containing H.264, verifies
the remote/local H.264 diagnostic flags, and verifies that an Offer causes a
non-empty local host-candidate callback. The WebSocket test verifies that
missing CA trust is rejected before any socket is opened. The depacketizer test checks
the current libdatachannel contract, where the reconstructed message is returned
through the message vector. The fixture pipeline test reads back published NV12
from a separate IPC mapping. The loopback test uses two native libdatachannel
peers on a detected private IPv4 interface, exchanges Offer/Answer and host
candidates without STUN/TURN, reaches the connected state, sends one synthetic
H.264 RTP single-NAL packet, and observes one non-empty access unit at the
receiver `onFrame` callback. These are local API/contract tests, not Safari
interoperation or a device result.

## Next gate

The restart unit test also verifies that a closed native session can emit a new
`hello` with a new session ID and return to `WaitingForOffer`. The next
implementation gate is a real same-LAN Safari H.264 interop probe. It
must use the Stage 1 Web Client, the existing HTTPS/WSS server, a private IPv4
host candidate, and a real selected codec/DTLS/SRTP/RTP evidence log. Until that
gate passes, no live decode rate or end-to-end Virtual Camera result is claimed.
