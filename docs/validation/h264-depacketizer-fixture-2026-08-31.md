# H.264 RTP depacketizer fixture (2026-08-31)

The opt-in libdatachannel build now has a small packet-level fixture for the
H.264 receive boundary. It feeds RTP packets directly into
`rtc::H264RtpDepacketizer` and does not create a network connection.

Verified cases:

- one complete H.264 NAL unit with marker bit;
- a two-packet FU-A fragmented NAL unit;
- reconstructed Annex-B start code and NAL header;
- access-unit payload preservation;
- RTP timestamp propagation through `FrameInfo`.

The test target is `cambridge_h264_depacketizer_tests` and passes in the same
vcpkg/libdatachannel opt-in build as the receiver and signaling tests. This does
not prove Safari RTP delivery, packet-loss recovery, RTCP behavior, H.264 decode,
hardware acceleration, or NV12 output. Those remain later gates.
