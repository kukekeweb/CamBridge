# Native receiver pipeline loopback (2026-08-31)

This validation is an opt-in, device-free boundary test. It does not claim
Safari interoperability, live camera capture, sustained 60fps, or Virtual
Camera acceptance.

## Covered path

```text
valid H.264 Annex-B fixture
  -> RTP single-NAL / FU-A packetization
  -> libdatachannel native two-peer LAN loopback
  -> H264RtpDepacketizer
  -> Media Foundation H.264 decoder
  -> NV12 frame
  -> existing latest-frame shared-memory IPC
  -> SharedFrameReader readback
```

The test uses no STUN, TURN, external signaling service, or cloud relay. Both
native peers bind a detected private IPv4 address. The production Media Source,
Frame Server, Virtual Camera registration, and Web Client are not modified by
the test.

## Evidence

Fixture generated outside the repository with FFmpeg:

```text
C:\Users\kukeke\AppData\Local\Temp\CamBridge-pipeline-loopback-1080p60.h264
```

The fixture used for the bounded run contains 120 frames at 1920x1080 and is
passed only through the `CAMBRIDGE_H264_FIXTURE` environment variable. The
Annex-B stream includes Access Unit Delimiters because the loopback fixture
splitter uses them to separate access units. The test is skipped when the
variable is unset, so CI does not depend on a repository or downloaded media
artifact.

Commands:

```powershell
ffmpeg -hide_banner -loglevel error -y `
  -f lavfi -i 'testsrc2=size=1920x1080:rate=60' -frames:v 120 `
  -c:v libx264 -preset ultrafast -tune zerolatency -pix_fmt yuv420p `
  -bsf:v h264_metadata=aud=insert -f h264 `
  'C:\Users\kukeke\AppData\Local\Temp\CamBridge-pipeline-loopback-1080p60.h264'
$env:CAMBRIDGE_H264_FIXTURE = 'C:\Users\kukeke\AppData\Local\Temp\CamBridge-pipeline-loopback-1080p60.h264'
ctest --test-dir build/native-mvp-libdatachannel -C Debug --output-on-failure
ctest --test-dir build/native-mvp-libdatachannel -C Release --output-on-failure
Remove-Item -LiteralPath 'C:\Users\kukeke\AppData\Local\Temp\CamBridge-pipeline-loopback-1080p60.h264' -Force
Remove-Item Env:CAMBRIDGE_H264_FIXTURE
```

Result on this Windows host:

- Debug: 16/16 tests passed in the documented validation run
- Release: 16/16 tests passed in the documented validation run
- Current Release rerun: 16/16 tests passed
- Pipeline loopback: passed
- Published frame readback: NV12, 1920x1080, valid stride and byte count
- Receiver selected local/remote candidate pair: both private-LAN address
- Receiver `accessUnitBytes`: incremented from the native track callback
- Existing Media Source/Synthetic tests: passed in the same CTest runs

The libdatachannel `PeerConnection::bytesReceived()` value remained zero in
this media-only loopback even though the track callback and decoded frames
advanced. It is therefore retained as an optional transport statistic but is
not used as proof of media reception; `accessUnitBytes` is the explicit
application-level counter for this boundary.

This proves the user-mode network-to-IPC fixture boundary only. It does not
prove the browser's negotiated codec, hardware encoder/decoder selection,
network loss behavior, or an end-to-end CamBridge Virtual Camera capture.
