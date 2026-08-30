# Native WSS TLS waiting probe — 2026-08-31

This is a host-side transport check. It does not claim Safari WebRTC
interoperability or media reception.

## Setup

- Bind address: `192.168.11.2`
- HTTPS/WSS port: `8443`
- WSS endpoint: `wss://192.168.11.2:8443/signaling`
- Server certificate SAN: `DNS:cambridge.local`, `IP Address:192.168.11.2`
- Native receiver: libdatachannel Release build
- TLS verification: enabled; `--allow-insecure-tls` was not used

The distributed public root certificate is a DER `.cer`. For this probe only,
it was converted with Windows `certutil -encode` to a temporary PEM file for
the native receiver. The PEM file was removed after the run. No private key,
PFX, or password was copied to the receiver.

## Command boundary

The Stage 1 server was started with the live password-protected PFX and the
bound private IPv4 address. The receiver was run with:

```powershell
cambridge_native_receiver.exe `
  --url wss://192.168.11.2:8443/signaling `
  --ca <temporary CamBridge Local CA PEM> `
  --bind-address 192.168.11.2 `
  --duration-ms 3500 `
  --no-publish
```

## Result

- HTTPS server started and advertised the expected WSS endpoint.
- Native receiver completed the TLS handshake and entered
  `waiting for Safari Offer`.
- Receiver exited normally after the bounded 3.5-second duration with exit
  code `0`.
- `remoteOfferH264=no`, `localAnswerH264=no`, and `accessUnits=0` are expected
  because no browser peer was connected during this waiting-only probe.
- The server and temporary PEM were stopped/removed after the run.

This proves the normal local CA/WSS startup boundary. The next live gate is a
Safari Offer followed by private-LAN ICE/DTLS/SRTP, H.264 access units,
Media Foundation decode, NV12 IPC publication, and the existing Virtual
Camera capture probe.
