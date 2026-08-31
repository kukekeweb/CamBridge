# Native WebRTC Track Dispatch Validation (2026-08-31)

## Scope

This validation isolates the first native WebRTC media boundary. It does not
claim iPhone Safari acceptance, Windows hardware decoding, 1080p60 sustained
operation, or Discord/Virtual Camera acceptance.

The live probe used the local Stage 1 HTTPS/WSS server and a separate Chromium
process with a synthetic Y4M camera. The production Media Source, IPC contract,
and Virtual Camera registration were not modified by this investigation.

## Evidence before the fix

The native receiver reached `connected` and the global media audit observed
RTP/RTCP, but the Track audit remained at zero and no H.264 access units were
delivered. The relevant pattern was:

```text
rawRtpPackets > 0
rawRtcpPackets > 0
trackRtpPackets = 0
accessUnits = 0
```

The receiver had added a local recv-only Track with MID `video` before applying
the browser offer. The browser offer used its own media MID. In libdatachannel's
dispatch path, multiple track lines use the SSRC-to-Track map; this run did not
route the incoming browser packets to the configured local placeholder Track.

## Change

The receiver now:

1. Does not add a local placeholder Track for the browser offer.
2. Installs an `onTrack` callback before applying the offer.
3. Stores the remote Track created from the offer.
4. Installs the H.264 depacketizer, RTCP receiving session, and bounded input /
   output audits on that remote Track.

The existing local WSS, ICE host-candidate policy, H.264 codec preference, and
access-unit callback are unchanged.

## Live result after the change

The new Debug receiver, using the same local fake-camera page, produced:

```text
State: connected
rawRtpPackets=1840
rawRtcpPackets=38
trackRtpPackets=1840
depacketizerFrames=1682
accessUnits=1682
remoteOfferH264=yes
localAnswerH264=yes
remoteOfferCodec=H264/90000
localAnswerCodec=H264/90000
selectedLocalCandidate=... 192.168.11.2 ... typ host
selectedRemoteCandidate=... 192.168.11.2 ... typ host
```

This proves that the local browser-shaped sender reached the native remote
Track and that H.264 RTP was reassembled into access units. It is a synthetic
Chromium interoperability probe, not an iPhone Safari result.

## H.264 to NV12 / IPC bounded probe

With the normal publishing path enabled in a separate run:

```text
remoteOfferH264=yes
localAnswerH264=yes
accessUnits=978
decoded=941
published=941
decodeErrors=0
publishErrors=0
transform=Microsoft H264 Video Decoder MFT
```

The decoder reported changing output dimensions during this synthetic run as
the browser encoder adapted under test load, so this is evidence of successful
H.264-to-NV12-to-shared-IPC delivery only. It is not evidence of a stable
1920x1080@60 stream. The iPhone Safari 1080p60 and sustained 60fps gates remain
open.

## Automated verification

Both Debug and Release builds completed, and both configurations passed all 16
CTest tests, including the native RTP loopback and H.264 pipeline loopback.

The native receiver still reports RTP, Track, depacketizer, access-unit,
decoder, and publisher counters so a future Safari run can distinguish
transport, Track dispatch, depacketization, decoding, and IPC failures.

## Signaling reconnect regression

The native signaling session test now verifies that an explicit signaling
`close` recycles the receiver and accepts a second Offer on the same signaling
socket. The browser-side sender test likewise verifies that an unexpected WSS
close closes the old PeerConnection and leaves the sender reusable. These are
unit-level lifecycle results; live Safari reconnect and WSS socket reconnect
remain open acceptance gates.
