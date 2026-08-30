# libdatachannel build probe (2026-08-31)

## Probe target

- Repository: `https://github.com/paullouisageneau/libdatachannel.git`
- Release tag: `v0.9.4`
- Resolved source commit: `8ea828f1b081bda9cf7cf87bf059e3090cfe4bd1`
- Configuration: Visual Studio 17 2022, x64, `NO_EXAMPLES=ON`, `NO_TESTS=ON`,
  WebSocket enabled, warnings-as-errors disabled

## Result

The source and its declared submodules were fetched into a temporary directory
outside the CamBridge repository. CMake configuration did not complete because
this Windows development environment has neither an OpenSSL development root
(headers and import libraries) nor a configured LibSRTP package. The immediate
configure errors were missing submodule directories before initialization, then
`Could NOT find OpenSSL` after the submodules were initialized.

This is a dependency/toolchain gap, not evidence of Safari incompatibility. No
libdatachannel source, generated build tree, OpenSSL binary, or copied third-party
artifact was added to CamBridge.

The v0.9.4 CMake project uses OpenSSL by default, libjuice for ICE, usrsctp, and
optionally LibSRTP for media transport. The future CamBridge dependency setup
must pin these inputs and record their licenses before enabling the adapter in
the production build.

## Current CamBridge state

The native receiver core remains dependency-free and is tested independently.
It validates the state/SDP/candidate boundary that the future libdatachannel
adapter will call. The actual PeerConnection, DTLS/SRTP, H.264 RTP callback, and
Safari interoperability probe are not claimed as implemented.

## Next dependency gate

Provide a reproducible Windows x64 development dependency root containing
OpenSSL headers/import libraries and, for media-enabled builds, LibSRTP. Then
configure a separate opt-in CMake build for the pinned libdatachannel commit.
The default CamBridge build must remain green when that optional dependency is
absent.
