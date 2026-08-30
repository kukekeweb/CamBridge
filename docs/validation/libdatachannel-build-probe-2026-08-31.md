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

## Reproducible vcpkg package probe

The same Windows host was then tested with the vcpkg tool repository at commit
`32017522e1065c6b4547d649711faa01ab351dc9`:

```powershell
vcpkg install libdatachannel[ws,srtp]:x64-windows --clean-after-build
cmake -S windows/native-mvp -B build/native-mvp-libdatachannel `
  -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE=<vcpkg>\scripts\buildsystems\vcpkg.cmake `
  -DCAMBRIDGE_ENABLE_LIBDATACHANNEL=ON
cmake --build build/native-mvp-libdatachannel --config Release `
  --target cambridge_libdatachannel_receiver_tests
ctest --test-dir build/native-mvp-libdatachannel -C Release `
  -R cambridge_libdatachannel_receiver_tests --output-on-failure
```

This opt-in build resolved libdatachannel `0.24.5`, OpenSSL `3.6.4`, and the
media-enabled CMake target. The adapter compiled and its two tests passed. The
default build remains dependency-free because `CAMBRIDGE_ENABLE_LIBDATACHANNEL`
defaults to `OFF`. The package binaries remain outside the repository; the
repository contains only the manifest, source adapter, and test.

The v0.9.4 CMake project uses OpenSSL by default, libjuice for ICE, usrsctp, and
optionally LibSRTP for media transport. The current opt-in package setup pins
the vcpkg registry baseline and records libdatachannel's MPL-2.0 license in the
package metadata. A production bundle still needs a separate third-party
notices/artifact packaging review.

## Current CamBridge state

The native receiver core remains dependency-free and is tested independently.
The opt-in adapter now constructs a real libdatachannel PeerConnection, installs
a recv-only H.264 track and depacketizer, accepts a validated Offer, and observes
the generated Answer callback. The receiver CLI is wired to the independent
Media Foundation H.264-to-NV12 decoder and latest-frame IPC boundary. This is
still an API/linkage, fixture, and startup wiring result: DTLS/SRTP, H.264 RTP
callback, live decoder rate, and Safari interoperability are not claimed.

## Next dependency gate

For a reproducible Windows x64 build, install the manifest using the documented
vcpkg baseline, then configure the separate opt-in CMake build. The default
CamBridge build remains green when that optional dependency is absent.
